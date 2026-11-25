#!/usr/bin/env python3
"""databuilder: unpacks what the converters read from the install's archives into the work tree (work/x/...), with the
mechanical conversions the original extraction made. Our own archive reader (datarc.py); the list of work files is
extract_list.txt (made by check/make_extract_list.py: names, sizes and CRCs only).

  how:  copy      the entry as it is
        png       a DDS texture as Pillow decodes it, saved as PNG (effect / skill pictures)
        tex256    that, as RGBA, scaled with Lanczos so its larger side is at most 256 px
        tex512    the same at most 512 px (b61: the maps' base textures; tools/map_visual.py picks which ship at 512)
        tex1024   the same at most 1024 px (b69: the maps' base textures whose original is 1024 px; map_visual.py picks)
        lmap1024  a lightmap (.tga / .dds) scaled with Lanczos so that each side is at most 1024 px
        lmap2048  the same at most 2048 px: the original's size (b69: the lightmaps ship at it)
        usm       the video stream of a CRI Sofdec .usm (tutorial clips: the MPEG-1 elementary stream)
        usmadx    its first audio stream (b63: the operator profiles' CRI ADX sound)

Each row names its source entry: a path (any letter case), '*\\<name>' (anywhere) or '<stem>.*' (any picture type), with
the size and CRC-32 it must have where known. A path that does not fit is searched for by name, size and CRC in all
archives (the same file can sit in several), then by size and CRC alone (a file stored under another name). Where the
size is not known and several different entries fit, the one whose result has the hash in check/extract_ref.txt is
taken. work/x/extract_plan.txt records the entries used (names only). The largest entry is 74 MB (a video).

usage: extract.py --game "<install>/Game Files" --work <work folder> [--only map,ui,...] [--list extract_list.txt]
  Resumable: files already in place are skipped (work/x/.extract_journal remembers where each came from); files are
  written whole or not at all. Ends with work/x/.complete. Memory: one entry at a time.
"""
import argparse, collections, hashlib, io, os, struct, sys, time
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, H); sys.path.insert(0, os.path.join(H, '..'))
import datarc, gitspaths
ARCHIVES = ['map', 'ui', 'model', 'weapon', 'effect', 'world', 'worldEnv']
PICTURES = ('.dds', '.tga', '.png', '.bmp')

def log(s): print(time.strftime('[%H:%M:%S] ') + s, flush=True)

Row = collections.namedtuple('Row', 'how key rel src size crc')
def rows(path):
    out = []
    for ln in open(path, encoding='utf-8'):
        if ln.startswith('#') or not ln.strip(): continue
        how, key, rel, src, size, crc = ln.rstrip('\n').split('\t')
        out.append(Row(how, key, rel, src, None if size == '-' else int(size), None if crc == '-' else int(crc, 16)))
    return out

def refs(path):
    out = {}
    if os.path.exists(path):
        for ln in open(path, encoding='utf-8'):
            h, kr = ln.rstrip('\n').split('\t'); out[kr] = h
    return out

# ---------------------------------------------------------------- conversions
def fit(data, limit, rgba, per_side=False):
    """scaled with Lanczos so its larger side is at most limit; per_side: each side on its own (the lightmaps: a 2048 x
    1024 atlas becomes 1024 x 1024, as the original extraction made them; the UVs are 0..1 either way)"""
    from PIL import Image
    im = Image.open(io.BytesIO(data)); im.load()
    if rgba: im = im.convert('RGBA')
    w, h = im.size
    if per_side: size = (min(w, limit), min(h, limit))
    else: s = min(1.0, limit / max(w, h)); size = (max(1, int(w * s)), max(1, int(h * s)))
    if size != (w, h): im = im.resize(size, Image.LANCZOS)
    return im

def picture(data):
    from PIL import Image
    im = Image.open(io.BytesIO(data)); im.load(); return im

def usm_video(data):
    """the @SFV data chunks of a CRI .usm, in order"""
    i = 0; v = bytearray()
    while i + 8 <= len(data):
        sig = data[i:i + 4]; sz = struct.unpack('>I', data[i + 4:i + 8])[0]
        off = data[i + 9]; pad = struct.unpack('>H', data[i + 10:i + 12])[0]; typ = data[i + 15] & 3
        if typ == 0 and sig == b'@SFV': v += data[i + 8 + off:i + 8 + sz - pad]
        i += 8 + sz
    return bytes(v)

def usm_audio(data):
    """the @SFA data chunks of channel 0 of a CRI .usm, in order (CRI ADX in this game)"""
    i = 0; a = bytearray()
    while i + 8 <= len(data):
        sig = data[i:i + 4]; sz = struct.unpack('>I', data[i + 4:i + 8])[0]
        off = data[i + 9]; pad = struct.unpack('>H', data[i + 10:i + 12])[0]; ch = data[i + 12]; typ = data[i + 15] & 3
        if typ == 0 and sig == b'@SFA' and ch == 0: a += data[i + 8 + off:i + 8 + sz - pad]
        i += 8 + sz
    return bytes(a)

def convert(how, data):
    """-> (file bytes, PIL image or None)"""
    if how == 'copy': return data, None
    if how == 'usm': return usm_video(data), None
    if how == 'usmadx': return usm_audio(data), None
    im = {'png': lambda: picture(data), 'tex256': lambda: fit(data, 256, True), 'tex512': lambda: fit(data, 512, True), 'tex1024': lambda: fit(data, 1024, True),
          'lmap1024': lambda: fit(data, 1024, False, True), 'lmap2048': lambda: fit(data, 2048, False, True)}[how]()
    b = io.BytesIO(); im.save(b, 'PNG'); return b.getvalue(), im

def outhash(ref, out, im):
    if ref.startswith('px:'):
        if im is None:
            from PIL import Image
            im = Image.open(io.BytesIO(out)); im.load()
        im = im.convert('RGBA'); return 'px:' + hashlib.sha256(('%dx%d:' % im.size).encode() + im.tobytes()).hexdigest()
    return hashlib.sha256(out).hexdigest()

# ---------------------------------------------------------------- finding each row's entry
def norm(p): return p.lower().replace('\\', '/')

class Index:
    def __init__(s, archives):
        s.A = archives; s.path = {}; s.name = collections.defaultdict(list); s.stem = collections.defaultdict(list)
        s.content = collections.defaultdict(list)
        for arc, A in archives.items():
            for e in A.order:
                n = norm(e.name); c = (arc, e); s.path[n] = c; b = n.rsplit('/', 1)[-1]; s.name[b].append(c)
                s.content[(e.usize, e.crc)].append(c)
                st, ext = os.path.splitext(b)
                if ext in PICTURES: s.stem[st].append(c)

def score(row, c):
    """how well an entry's path fits the row's work path: shared folder names, then the shorter path"""
    want = set(norm(row.key + '/' + row.rel).split('/')[:-1]) | set(norm(row.src).split('/')[:-1])
    parts = norm(c[1].name).split('/')[:-1]
    return (sum(p in want for p in parts), -len(parts), c[1].name)

def resolve(row, I):
    """the entries that can be this row's source, best first: one per different content (size + CRC)"""
    fits = lambda c: row.size is None or (c[1].usize == row.size and c[1].crc == row.crc)
    folder, name = row.src.rsplit('\\', 1); anyext = name.endswith('.*'); base = name[:-2] if anyext else name
    if folder != '*':
        for p in ([folder + '\\' + base + x for x in PICTURES] if anyext else [row.src]):
            c = I.path.get(norm(p))
            if c and fits(c) and row.size is not None: return [c]
    cands = [c for c in (I.stem[base.lower()] if anyext else I.name[base.lower()]) if fits(c)]
    if not cands and row.size is not None: cands = I.content[(row.size, row.crc)]   # (stored under another name)
    if anyext and folder != '*':   # a lightmap: only from its own map folder
        cands = [c for c in cands if norm(c[1].name).rsplit('/', 1)[0].endswith(norm(folder))]
    groups = collections.OrderedDict()
    for c in sorted(cands, key=lambda c: score(row, c), reverse=True): groups.setdefault((c[1].usize, c[1].crc), c)
    return list(groups.values())

def write_plan(x, archives, plan, bad):
    """work/x/extract_plan.txt: work file, archive, entry, size, crc32, how (names only)"""
    with open(os.path.join(x, 'extract_plan.txt'), 'w', encoding='utf-8', newline='\n') as f:
        for arc in archives:
            for e, r, _ in sorted(plan[arc], key=lambda t: (t[1].key, t[1].rel)):
                f.write('%s:%s\t%s\t%s\t%d\t%08x\t%s\n' % (r.key, r.rel, arc, e.name, e.usize, e.crc, r.how))
        for r, why in bad: f.write('%s:%s\t-\t%s\t-\t-\t%s\t%s\n' % (r.key, r.rel, r.src, r.how, why))

# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--game', required=True, help='the install\'s "Game Files" folder')
    ap.add_argument('--work', required=True, help='the work folder (files go to WORK/x/...)')
    ap.add_argument('--list', default=os.path.join(H, 'extract_list.txt'))
    ap.add_argument('--ref', default=os.path.join(H, 'check', 'extract_ref.txt'))
    ap.add_argument('--only', help='only these archives (test runs: rows from other archives are left out)')
    a = ap.parse_args()
    for k in list(os.environ):   # the work layout only (not a dev container's GITSFA_* folders)
        if k.startswith('GITSFA_') and k[7:] in gitspaths.LAYOUT: del os.environ[k]
    os.environ['GITSFA_WORK'] = a.work
    data = os.path.join(a.game, 'Data'); x = os.path.join(a.work, 'x'); os.makedirs(x, exist_ok=True)
    R = rows(a.list); ref = refs(a.ref); want = a.only.split(',') if a.only else ARCHIVES
    t0 = time.time(); archives = collections.OrderedDict()
    for arc in want:
        p = os.path.join(data, arc + '.dat')
        if not os.path.exists(p): raise SystemExit('%s not found: is --game the install\'s "Game Files" folder?' % p)
        archives[arc] = datarc.Archive(p)
    I = Index(archives); log('%d archives, %d entries; finding %d files' % (len(archives), sum(len(A.order) for A in archives.values()), len(R)))
    plan = collections.defaultdict(list); bad = []
    for r in R:
        cs = resolve(r, I)
        if not cs: bad.append((r, 'not found')); continue
        plan[cs[0][0]].append((cs[0][1], r, cs[1:]))
    log('%d files found, %d of them to be told apart by their result' % (sum(map(len, plan.values())),
        sum(1 for v in plan.values() for _, r, alt in v if r.size is None)))
    if bad:
        log('%d files %s' % (len(bad), 'not in these archives (left out)' if a.only else 'NOT FOUND in the archives, e.g.:'))
        if not a.only:
            for r, why in bad[:20]: log('  %s:%s  (%s)' % (r.key, r.rel, r.src))
            write_plan(x, archives, plan, bad)
            raise SystemExit('extraction stopped: the install does not have all the files this builder expects '
                             '(a different game version?). The list is in ' + os.path.join(x, 'extract_plan.txt'))
    used = {}; odd = []; before = {}; oddbefore = set()   # (a resumed run: the entries the files came from, from the journal)
    jp = os.path.join(x, '.extract_journal')
    if os.path.exists(jp):
        for ln in open(jp, encoding='utf-8'):
            f = ln.rstrip('\n').split('\t')
            if len(f) == 4 and f[1] in archives:
                before[f[0]] = (f[1], archives[f[1]].find(f[2]))
                if f[3] == 'odd': oddbefore.add(f[0])
                else: oddbefore.discard(f[0])   # (made again later and right)
    journal = open(jp, 'a', encoding='utf-8', newline='\n')
    def resumed(r, arc, e):
        kr = r.key + ':' + r.rel; used[r] = before.get(kr) or (arc, e)
        if kr in oddbefore: odd.append(r)
    for arc, A in archives.items():
        mine = sorted(plan[arc], key=lambda t: t[0].offset)
        if not mine: continue
        t1 = time.time(); n = 0; size = 0; last = None; buf = None; k = 0
        log('%s.dat: %d files ...' % (arc, len(mine)))
        for e, r, alt in mine:
            dst = gitspaths.path(r.key, *r.rel.split('/')); n += 1; used[r] = (arc, e)
            if os.path.exists(dst) and (r.how != 'copy' or os.path.getsize(dst) == e.usize):   # (resumed)
                h0 = ref.get(r.key + ':' + r.rel) if r.how in ('tex512', 'tex1024', 'lmap2048') else None   # b61 / b69: a smaller picture from an older run is made again
                if not h0 or outhash(h0, open(dst, 'rb').read(), None) == h0: resumed(r, arc, e); k += 1; continue
            if last is not e: buf = A.read(e); last = e   # (the same entry can go to several places)
            out, im = convert(r.how, buf); h = ref.get(r.key + ':' + r.rel)
            if r.size is None and h and outhash(h, out, im) != h:   # not this one: the other entries of that name
                for arc2, e2 in alt:
                    out2, im2 = convert(r.how, archives[arc2].read(e2))
                    if outhash(h, out2, im2) == h: out = out2; used[r] = (arc2, e2); break
                else:
                    odd.append(r)
                    if a.only: del used[r]; continue   # (test run: leave it out)
            os.makedirs(os.path.dirname(dst), exist_ok=True); tmp = dst + '.part'
            with open(tmp, 'wb') as f: f.write(out)
            os.replace(tmp, dst); size += len(out)
            journal.write('%s:%s\t%s\t%s\t%s\n' % (r.key, r.rel, used[r][0], used[r][1].name, 'odd' if odd and odd[-1] is r else 'ok')); journal.flush()
            if n % 500 == 0: log('  %s.dat: %d of %d' % (arc, n, len(mine)))
        buf = None
        log('%s.dat: %d files%s, %.0f MB written, %.0f s' % (arc, n, ' (%d already there)' % k if k else '', size / 1e6, time.time() - t1))
    for A in archives.values(): A.f.close()
    journal.close()
    plan2 = collections.defaultdict(list)
    for r, (arc, e) in used.items(): plan2[arc].append((e, r, ()))
    write_plan(x, archives, plan2, bad + [(r, 'no entry of that name gives the expected picture') for r in odd])
    if odd:
        log('%d pictures %s the expected ones, e.g.: %s' % (len(odd), 'left out: none of these archives gives' if a.only else 'differ from',
                                                          ', '.join('%s:%s' % (r.key, r.rel) for r in odd[:10])))
    if not a.only: open(os.path.join(x, '.complete'), 'w').write('ok\n')
    log('extraction done in %.0f s' % (time.time() - t0))

if __name__ == '__main__':
    main()
