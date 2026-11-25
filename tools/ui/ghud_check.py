# Integrity check of a .ghud pack (hud.h format): every image rectangle inside its atlas page, no two images with different
# pixels sharing atlas texels (identical images may share one rectangle: hudbuild's de-duplication), premultiplied texels
# (rgb <= alpha), ramps in range, glyph table pointing at font-page images, nodes / fields finite and in range. With a
# reference pack (e.g. b28's hud.ghud), the images both packs have are compared after scaling to the same size (a wrong
# atlas region shows up as a low correlation). usage: ghud_check.py pack.ghud [reference.ghud] [--sheet out.png]
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ghud_read as G
from PIL import Image
def atlases(path, P):
    d = open(path, 'rb').read(); (w0, h0), (w1, h1) = P['tex']
    a0 = np.frombuffer(d, np.uint8, w0 * h0 * 4, len(d) - (w0 * h0 + w1 * h1) * 4).reshape(h0, w0, 4)
    a1 = np.frombuffer(d, np.uint8, w1 * h1 * 4, len(d) - w1 * h1 * 4).reshape(h1, w1, 4)
    return a0, a1
def pix(A, im): return A[im['tex']][im['y']:im['y'] + im['h'], im['x']:im['x'] + im['w']]
def check(path):
    P = G.read(path); A = atlases(path, P); bad = []
    rects = {}
    for nm, im in P['imgs'].items():
        H, W = A[im['tex']].shape[:2]
        if im['w'] <= 0 or im['h'] <= 0 or im['x'] + im['w'] > W or im['y'] + im['h'] > H: bad.append('%s: rect %d,%d %dx%d outside the %dx%d page' % (nm, im['x'], im['y'], im['w'], im['h'], W, H)); continue
        if im['x'] < 4 and im['y'] < 4 and im['tex'] == 0 and not nm.endswith('#r'): bad.append('%s: overlaps the reserved no-ramp texel block' % nm)
        if not all(np.isfinite(im['loc'])) or im['loc'][2] <= im['loc'][0] or im['loc'][3] <= im['loc'][1]: bad.append('%s: bad local bounds %s' % (nm, im['loc']))
        p = pix(A, im).astype(np.int32)
        if not nm.endswith('#r') and (p[..., :3].max(2) > p[..., 3] + 1).any(): bad.append('%s: %d texels not premultiplied (rgb > alpha)' % (nm, int((p[..., :3].max(2) > p[..., 3] + 1).sum())))
        rects.setdefault((im['tex'], im['x'], im['y'], im['w'], im['h']), []).append(nm)
    # overlaps between distinct rectangles (a shared identical rectangle is a de-duplicated image: check the pixels agree)
    items = sorted(rects.items())
    for i, ((t, x, y, w, h), names) in enumerate(items):
        for (t2, x2, y2, w2, h2), n2 in items[i + 1:]:
            if t2 != t: continue
            if x2 < x + w and x < x2 + w2 and y2 < y + h and y < y2 + h2: bad.append('overlap: %s (%d,%d %dx%d) and %s (%d,%d %dx%d)' % (names, x, y, w, h, n2, x2, y2, w2, h2))
    for g in [k for k in P['imgs'] if k.startswith('g0') and len(k) == 5]:
        if P['imgs'][g]['tex'] != 1: bad.append('glyph %s not on the font page' % g)
    DATA_NODES = ('kr.style', 'vin.poly')     # nodes that carry numbers, not a plane (make_hud.py)
    for nm, nd in P['nodes'].items():
        if nm.startswith(DATA_NODES): continue
        if not np.all(np.isfinite(nd['H'])) or abs(nd['H'][2, 2]) < 1e-9 and abs(nd['H'][2, :2]).max() < 1e-12: bad.append('node %s: bad homography' % nm)
    nn = len(P['nodes'])
    for nm, f in P['fields'].items():
        if not (0 <= f['node'] < nn) or not (f['size'] > 0) or not all(np.isfinite(f['b'])): bad.append('field %s: bad node / size / bounds' % nm)
    shared = sum(len(v) - 1 for v in rects.values() if len(v) > 1)
    return P, A, bad, shared
def compare(P, A, R, RA, sheet=None):
    rows = []
    for nm in sorted(set(P['imgs']) & set(R['imgs'])):
        a = pix(A, P['imgs'][nm]).astype(np.float32) / 255; b = pix(RA, R['imgs'][nm]).astype(np.float32) / 255
        if nm.endswith('#r'): continue
        h, w = 24, max(8, int(24 * a.shape[1] / max(1, a.shape[0])))
        ar = np.asarray(Image.fromarray((a * 255).astype(np.uint8)).resize((w, h), Image.BILINEAR), np.float32) / 255
        br = np.asarray(Image.fromarray((b * 255).astype(np.uint8)).resize((w, h), Image.BILINEAR), np.float32) / 255
        x, y = ar[..., 3].ravel(), br[..., 3].ravel()
        if x.std() < 1e-3 or y.std() < 1e-3: c = 1.0 - abs(x.mean() - y.mean())
        else: c = float(np.corrcoef(x, y)[0, 1])
        col = float(np.abs(ar[..., :3] - br[..., :3]).mean())
        rows.append((c, col, nm, ar, br))
    rows.sort(key=lambda r: r[0])
    if sheet:
        worst = rows[:40]; W = 2 * max(r[3].shape[1] for r in worst) + 260; S = Image.new('RGB', (W, 30 * len(worst)), (40, 40, 40))
        from PIL import ImageDraw
        d = ImageDraw.Draw(S)
        for i, (c, col, nm, ar, br) in enumerate(worst):
            for k, im in enumerate((ar, br)):
                rgb = im[..., :3] + (1 - im[..., 3:4]) * 0.25; S.paste(Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)), (250 + k * (ar.shape[1] + 6), i * 30 + 3))
            d.text((4, i * 30 + 8), '%-16s %.2f %.2f' % (nm, c, col), fill=(255, 255, 255))
        S.save(sheet)
    return rows
if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]; sheet = sys.argv[sys.argv.index('--sheet') + 1] if '--sheet' in sys.argv else None
    if sheet in args: args.remove(sheet)
    P, A, bad, shared = check(args[0])
    print('%s: %d images (%d share an identical rectangle), %d nodes, %d fields, pages %s' % (args[0], len(P['imgs']), shared, len(P['nodes']), len(P['fields']), P['tex']))
    print('problems: %d' % len(bad)); [print('  ' + b) for b in bad[:60]]
    if len(args) > 1:
        R, RA, rbad, _ = check(args[1]); rows = compare(P, A, R, RA, sheet)
        low = [r for r in rows if r[0] < 0.85]
        print('compared with %s: %d common images, %d with alpha correlation < 0.85:' % (args[1], len(rows), len(low)))
        for c, col, nm, _, _ in low[:40]: print('  %-18s corr %.2f  colour diff %.2f' % (nm, c, col))
