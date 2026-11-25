# Legibility audit of a host HUD capture (tools/ui/host/run.sh): finds every text run (font glyph quads) and every digit
# run (n30 / n26 / nc glyph images) drawn, measures its size on the Vita screen and checks it against the brief's
# Legibility rule (labels >= 24 px font, ~17 px cap height; fine print >= 20 px; big numbers: cap >= 17 px).
# usage: legib.py capdir_or_cap texdir pack.ghud [pack.ghud ...]   (packs in load order; prints one line per run)
import sys, os, struct, glob, numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import ghud_read as G
def load_packs(paths, texdir):
    packs = []
    for p in paths:
        P = G.read(p); d = open(p, 'rb').read(); (w0, h0), (w1, h1) = P['tex']
        a0 = np.frombuffer(d, np.uint8, w0 * h0 * 4, len(d) - (w0 * h0 + w1 * h1) * 4).reshape(h0, w0, 4)
        a1 = np.frombuffer(d, np.uint8, w1 * h1 * 4, len(d) - w1 * h1 * 4).reshape(h1, w1, 4)
        P['atlas'] = (a0, a1); P['path'] = p; packs.append(P)
    # texture ids of the capture -> (pack, tex) by matching the dumped pixels
    tmap = {}
    for f in glob.glob(os.path.join(texdir, 'tex*.bin')):
        tid = int(os.path.basename(f)[3:-4]); d = open(f, 'rb').read(); w, h, fmt, size = struct.unpack_from('<4i', d, 0)
        if fmt != 4: continue
        px = np.frombuffer(d, np.uint8, size, 16).reshape(h, w, 4)
        for P in packs:
            for t in (0, 1):
                A = P['atlas'][t]
                if A.shape == px.shape and np.array_equal(A[::37, ::41], px[::37, ::41]): tmap[tid] = (P, t)
    return packs, tmap
def img_at(P, t, u0, v0, u1, v1):
    A = P['atlas'][t]; H, W = A.shape[:2]; x, y = u0 * W, v0 * H; w, h = (u1 - u0) * W, (v1 - v0) * H
    key = (t, int(round(x)), int(round(y)))
    if 'byxy' not in P: P['byxy'] = {(im['tex'], im['x'], im['y']): nm for nm, im in P['imgs'].items()}
    return P['byxy'].get(key)
def ink(P, nm, cache={}):
    """(top, bottom) of the ink (alpha > 0.5) as fractions of the image height"""
    k = (P['path'], nm)
    if k not in cache:
        im = P['imgs'][nm]; a = P['atlas'][im['tex']][im['y']:im['y'] + im['h'], im['x']:im['x'] + im['w'], 3]
        rows = np.nonzero(a.max(1) > 127)[0]; cache[k] = (rows[0] / im['h'], (rows[-1] + 1) / im['h']) if len(rows) else (0, 0)
    return cache[k]
LABEL_IMGS = {'hp.label': 'HP', 'hp.labelW': 'HP', 'ap.label': 'AP'}
def quads(cap):
    d = open(cap, 'rb').read(); o = 0
    while o < len(d):
        tid, prem, n = struct.unpack_from('<3i', d, o); o += 12
        if tid == -1: o += (n + 3) // 4 * 4; continue   # ui b42: an element tag (tools/ui/host/hudtag.h)
        V = np.frombuffer(d, np.float32, n * 17, o).reshape(n, 17); o += n * 68
        for q in range(0, n - 5, 6):
            v = V[q:q + 6]; c = v[[0, 1, 2, 5]]; w = c[:, 3]
            if np.any(w <= 0): continue
            sx = (c[:, 0] / w + 1) * 480; sy = (1 - c[:, 1] / w) * 272
            yield tid, sx, sy, c[:, 4:8], c[:, 8:12]
def audit(cap, texdir, packs, tmap, quiet=False):
    runs = []; cur = None
    for tid, sx, sy, uv, mul in quads(cap):
        if tid not in tmap or mul[0, 3] < 0.05: cur = None; continue
        P, t = tmap[tid]; u0, v0 = uv[:, 0].min(), uv[:, 1].min(); u1, v1 = uv[:, 0].max(), uv[:, 1].max()
        nm = img_at(P, t, u0, v0, u1, v1)
        if nm is None: cur = None; continue
        hgt = (np.hypot(sx[3] - sx[0], sy[3] - sy[0]) + np.hypot(sx[2] - sx[1], sy[2] - sy[1])) / 2   # screen height of the quad
        kind = None
        if nm.startswith('g0') and len(nm) == 5:
            loc = P['imgs'][nm]['loc']; em = hgt / (loc[3] - loc[1]); ch = chr(int(nm[1:], 16)); kind = 'text'
        elif nm.split('.')[0] in ('n30', 'n26', 'nc', 'num', 'n40', 'n20', 'rn') and nm.split('.')[-1].isdigit():
            a, b = ink(P, nm); em = hgt * (b - a); ch = nm.split('.')[-1]; kind = 'num'
        elif nm in LABEL_IMGS:                 # text baked into an image (the movie's static labels): ink height = cap height
            a, b = ink(P, nm); runs.append(dict(kind='label', em=hgt * (b - a), s=LABEL_IMGS[nm], x0=sx.mean(), x1=sx.mean(), y=sy.mean(), pack=os.path.basename(P['path']))); cur = None; continue
        else: cur = None; continue
        x, y = sx.mean(), sy.mean()
        if cur and cur['kind'] == kind and abs(cur['em'] - em) < 0.08 * em and abs(cur['y'] - y) < em * 0.6 and abs(x - cur['x1']) < em * 1.6:
            cur['s'] += ch; cur['x1'] = x
        else:
            cur = dict(kind=kind, em=em, s=ch, x0=x, x1=x, y=y, pack=os.path.basename(P['path'])); runs.append(cur)
    return runs
if __name__ == '__main__':
    src, texroot = sys.argv[1], sys.argv[2]; packs_p = sys.argv[3:]
    caps = sorted(glob.glob(os.path.join(src, '*.cap'))) if os.path.isdir(src) else [src]
    bad = 0
    for cap in caps:
        n = os.path.basename(cap)[:-4]; td = os.path.join(os.path.dirname(cap), n + '.tex')
        packs, tmap = load_packs(packs_p, td)
        cap_h = None
        for P, t in tmap.values():
            if 'g0048' in P['imgs'] and cap_h is None: a, b = ink(P, 'g0048'); loc = P['imgs']['g0048']['loc']; cap_h = (b - a) * (loc[3] - loc[1])
        cap_h = cap_h or 0.7
        print('== %s (font cap height %.2f em)' % (n, cap_h))
        for r in audit(cap, td, packs, tmap):
            if r['kind'] == 'text':
                capx = r['em'] * cap_h; ok = 'ok' if r['em'] >= 23.5 else 'fine' if r['em'] >= 19.5 else 'SMALL'
                print('  %-5s %-34s font %5.1f px cap %4.1f  at (%3.0f,%3.0f)  %s' % (ok, repr(r['s'][:32]), r['em'], capx, r['x0'], r['y'], r['pack']))
            elif r['kind'] == 'label':
                ok = 'ok' if r['em'] >= 15.5 else 'fine' if r['em'] >= 13 else 'SMALL'
                print('  %-5s %-34s label      cap %4.1f  at (%3.0f,%3.0f)  %s' % (ok, repr(r['s']), r['em'], r['x0'], r['y'], r['pack']))
            else:
                ok = 'ok' if r['em'] >= 16.5 else 'fine' if r['em'] >= 13.5 else 'SMALL'
                print('  %-5s %-34s digits     cap %4.1f  at (%3.0f,%3.0f)  %s' % (ok, repr(r['s'][::-1] if False else r['s']), r['em'], r['x0'], r['y'], r['pack']))
            bad += ok == 'SMALL'
    print('runs below the fine-print minimum:', bad)
