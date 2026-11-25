#!/usr/bin/env python3
# databuilder check: are the extracted work files (extract.py) the same as the dev container's intermediates? Compares hashes
# only: extract_ref.txt holds, per work file, the sha256 of its bytes (or, for converted pictures, "px:" + the sha256 of
# its decoded RGBA pixels: PNG bytes depend on the encoder, the converters read only the pixels). Prints counts and the names
# of files that differ or are missing - no file contents.
#   usage: extract_check.py --work <work folder> [--ref extract_ref.txt] [--only KEY,...] [--present]
#   (--present: only the files that are there, for a test run of extract.py --only <some archives>)
import argparse, hashlib, os, sys
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(H, '..', '..'))
import gitspaths

def filehash(p, kind):
    if kind == 'px':
        from PIL import Image
        im = Image.open(p).convert('RGBA'); return 'px:' + hashlib.sha256(('%dx%d:' % im.size).encode() + im.tobytes()).hexdigest()
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--work', required=True); ap.add_argument('--ref', default=os.path.join(H, 'extract_ref.txt'))
    ap.add_argument('--only'); ap.add_argument('--present', action='store_true'); a = ap.parse_args()
    for k in list(os.environ):
        if k.startswith('GITSFA_') and k[7:] in gitspaths.LAYOUT: del os.environ[k]
    os.environ['GITSFA_WORK'] = a.work
    only = set(a.only.split(',')) if a.only else None
    ok = 0; diff = []; miss = []; per = {}
    for ln in open(a.ref, encoding='utf-8'):
        h, kr = ln.rstrip('\n').split('\t'); key, rel = kr.split(':', 1)
        if only and key not in only: continue
        p = gitspaths.path(key, *rel.split('/')); st = per.setdefault(key, [0, 0, 0])
        if not os.path.exists(p):
            if not a.present: miss.append(kr); st[2] += 1
            continue
        if filehash(p, 'px' if h.startswith('px:') else 'raw') == h: ok += 1; st[0] += 1
        else: diff.append(kr); st[1] += 1
    print('extract check: %d identical, %d different, %d missing (of %d)' % (ok, len(diff), len(miss), ok + len(diff) + len(miss)))
    for k, (o, d, m) in sorted(per.items()):
        if o + d + m: print('  %-8s %5d identical %4d different %4d missing' % (k, o, d, m))
    for kr in diff[:200]: print('  DIFFERENT ' + kr)
    for kr in miss[:200]: print('  MISSING ' + kr)
    if len(diff) + len(miss) > 400: print('  (lists cut at 200 each)')
    return 1 if diff or miss else 0

if __name__ == '__main__':
    sys.exit(main())
