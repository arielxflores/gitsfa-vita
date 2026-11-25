# ui b42: HUD overlap checker. Reads host HUD captures made with the element tags (hudtag.h: the engine's HUD_TAG("name")
# marks), rasterises each element's coverage like the HUD shader (tools/menu/raster.py's sampling), and reports every pair of
# elements whose visible pixels overlap or come closer than the minimum gap (default 6 px; rows of one list, e.g. feed0..feed4,
# 2 px). Names: "~x" not checked (world markers, vignette, crosshair), "#x" a full-screen layer (pause, scoreboard: not
# checked), "?" drawn with no tag (reported). A name and its own sub-parts ("skill" / "skill.cost") are one widget.
# usage: overlap.py [--gap N] [--alpha A] [--png outdir [--bg picture]] [--json out.json] [--only regex] capture_dir_or_files...
#   (each X.cap needs its X.tex texture folder beside it; 'A.cap+B.cap@feed,commander' draws B's listed elements over A;
#   --png writes X.png and X_boxes.png with every element's box, the clashing ones in red)
import sys, os, struct, json, glob, re, argparse, numpy as np
from scipy import ndimage
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import raster_hud as RH   # patches raster.tex for the DXT5 radar images
R = RH.R
W, H = 960, 544

def walk(cap, texdir, cov, keep=None, cv=None):
    """adds each tagged element's coverage (cov: {name: float image}); keep: only elements whose name starts with one of these;
    cv: an RGB image to composite the drawn triangles onto (as tools/menu/raster.py)"""
    R._texcache.clear()
    d = open(cap, 'rb').read(); o = 0; vtot = 0; cur = '?'; sw = []   # pending switches: (vertex index, name)
    while o < len(d):
        tid, prem, n = struct.unpack_from('<3i', d, o); o += 12
        if tid == -1:   # tag: the next `prem` vertices still belong to the element before
            name = d[o:o + n].decode(); o += (n + 3) // 4 * 4; sw.append((vtot + prem, name)); continue
        V = np.frombuffer(d, np.float32, n * 17, o).reshape(n, 17); o += n * 68
        T = R.tex(texdir, tid)
        for t in range(0, n, 3):
            gi = vtot + t
            while sw and sw[0][0] <= gi: cur = sw.pop(0)[1]
            if keep and not cur.lstrip('~#').startswith(keep): continue
            v = V[t:t + 3]; w = v[:, 3]
            if np.any(w <= 0): continue
            sx = (v[:, 0] / w + 1) * 0.5 * W; sy = (1 - v[:, 1] / w) * 0.5 * H
            x0, x1 = int(max(np.floor(sx.min()), 0)), int(min(np.ceil(sx.max()), W)); y0, y1 = int(max(np.floor(sy.min()), 0)), int(min(np.ceil(sy.max()), H))
            if x1 <= x0 or y1 <= y0: continue
            ys, xs = np.mgrid[y0:y1, x0:x1] + 0.5
            den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
            if abs(den) < 1e-9: continue
            l0 = ((sy[1] - sy[2]) * (xs - sx[2]) + (sx[2] - sx[1]) * (ys - sy[2])) / den
            l1 = ((sy[2] - sy[0]) * (xs - sx[2]) + (sx[0] - sx[2]) * (ys - sy[2])) / den
            l2 = 1 - l0 - l1
            ins = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
            if not ins.any(): continue
            iw = l0 / w[0] + l1 / w[1] + l2 / w[2]
            def at(k): return (l0[..., None] * v[0, k] / w[0] + l1[..., None] * v[1, k] / w[1] + l2[..., None] * v[2, k] / w[2]) / iw[..., None]
            uv = at(slice(4, 8)); mul = at(slice(8, 12)); thr = at(slice(16, 17))[..., 0]
            if T is None: c = np.ones(ys.shape + (4,), np.float32)
            else: c = R.sample(T, uv[..., 0], uv[..., 1]); r = R.sample(T, uv[..., 2], uv[..., 3])[..., 0]; c = c * np.clip((thr - r) * 200 + 0.5, 0, 1)[..., None]
            a = c[..., 3] * mul[..., 3] * ins
            C = cov.setdefault(cur, np.zeros((H, W), np.float32)); reg = C[y0:y1, x0:x1]; reg[:] = 1 - (1 - reg) * (1 - a)
            if cv is not None:
                add = at(slice(12, 16))
                if not prem: c = np.concatenate([c[..., :3] * c[..., 3:4], c[..., 3:4]], -1)
                o_ = np.empty_like(c); o_[..., :3] = (c[..., :3] * mul[..., :3] + add[..., :3] * c[..., 3:4]) * mul[..., 3:4]; o_[..., 3] = c[..., 3] * mul[..., 3]
                o_[..., :3] = np.minimum(o_[..., :3], o_[..., 3:4]); o_ = o_ * ins[..., None]
                rg = cv[y0:y1, x0:x1]; rg[:] = o_[..., :3] + rg * (1 - o_[..., 3:4])
        vtot += n
    return cov

def elements(spec, alpha_min, cv=None):
    """spec: 'X.cap' or 'A.cap+B.cap@name1,name2' (B's listed elements drawn over A: an overlay from another harness)"""
    cov = {}
    for k, part in enumerate(spec.split('+')):
        keep = None
        if '@' in part: part, f = part.split('@'); keep = tuple(f.split(','))
        walk(part, part[:-4] + '.tex', cov, keep, cv)
    return {k: v >= alpha_min for k, v in cov.items() if (v >= alpha_min).any()}

def family(n): return re.sub(r'\d+$', '', n)
def same_widget(a, b): return a.split('.')[0] == b or b.split('.')[0] == a
def checked(n): return not n.startswith(('~', '#', '?'))

def pairs(M, gap, list_gap=2):
    names = sorted(k for k in M if checked(k)); out = []
    box = {k: (lambda ys, xs: (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))(*np.nonzero(M[k])) for k in names}
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            if same_widget(a, b): continue
            g = list_gap if family(a) == family(b) and family(a) != a else gap
            A, B = box[a], box[b]
            if A[0] > B[2] + g or B[0] > A[2] + g or A[1] > B[3] + g or B[1] > A[3] + g: continue
            x0, y0, x1, y1 = max(0, min(A[0], B[0]) - g - 1), max(0, min(A[1], B[1]) - g - 1), min(W, max(A[2], B[2]) + g + 1), min(H, max(A[3], B[3]) + g + 1)
            ma, mb = M[a][y0:y1, x0:x1], M[b][y0:y1, x0:x1]
            ov = int((ma & mb).sum())
            if ov: out.append((a, b, ov, 0.0)); continue
            dist = ndimage.distance_transform_edt(~ma)[mb].min()
            gp = float(dist) - 1   # pixels strictly between the two
            if gp < g: out.append((a, b, 0, gp))
    return out, box

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--gap', type=float, default=6); ap.add_argument('--alpha', type=float, default=0.1)
    ap.add_argument('--png'); ap.add_argument('--bg'); ap.add_argument('--json'); ap.add_argument('--only'); ap.add_argument('src', nargs='+'); a = ap.parse_args()
    caps = []
    for s in a.src: caps += sorted(glob.glob(os.path.join(s, '*.cap'))) if os.path.isdir(s) else [s]
    def label(c):
        parts = c.split('+'); nm = os.path.basename(parts[0].split('@')[0])[:-4]
        return nm + ''.join('+' + os.path.basename(p.split('@')[0])[:-4] for p in parts[1:])
    if a.only: caps = [c for c in caps if re.search(a.only, label(c))]
    bg = None
    if a.bg and os.path.exists(a.bg):
        from PIL import Image; bg = np.asarray(Image.open(a.bg).convert('RGB').resize((W, H)), np.float32) / 255
    res = {}
    for cap in caps:
        nm = label(cap); cv = None
        if a.png: cv = bg.copy() if bg is not None else np.full((H, W, 3), 0.35, np.float32)
        M = elements(cap, a.alpha, cv); cl, box = pairs(M, a.gap)
        res[nm] = { 'clashes': [{ 'a': x, 'b': y, 'overlap_px': ov, 'gap_px': round(gp, 1) } for x, y, ov, gp in cl],
                    'boxes': { k: [int(v) for v in b] for k, b in box.items() }, 'untagged': '?' in M }
        print('%-34s %s' % (nm, '; '.join('%s x %s %s' % (x, y, ('OVERLAP %dpx' % ov) if ov else ('gap %.1f' % gp)) for x, y, ov, gp in cl) or 'clear') + ('  [untagged draws]' if '?' in M else ''), flush=True)
        if a.png:
            from PIL import Image, ImageDraw
            im = Image.fromarray((np.clip(cv, 0, 1) * 255).astype(np.uint8)); im.save(os.path.join(a.png, nm + '.png'))
            dr = ImageDraw.Draw(im); bad = set([x for x, y, _, _ in cl] + [y for x, y, _, _ in cl])
            for k, (x0, y0, x1, y1) in box.items():
                c = (255, 60, 60) if k in bad else (90, 220, 255); dr.rectangle([x0, y0, x1 - 1, y1 - 1], outline=c); dr.text((x0 + 2, y0 + 1), k, fill=c)
            im.save(os.path.join(a.png, nm + '_boxes.png'))
    if a.json: json.dump(res, open(a.json, 'w'), indent=1)
if __name__ == '__main__': main()
