# Per-map colour calibration against the footage: our renders at the located cameras (locate_mm.py json) vs the
# footage frames, compared on blurred low-res images (HUD / weapon masked, edges ignored so small misalignments don't
# count). Fits a per-channel tone curve ours -> footage (quantile matching of the paired pixels, smoothed, monotone)
# and writes a corrected LUT (the curve applied to the map's own LUT entries) -- the original's HDR exposure / tone
# mapping that our renderer doesn't run, folded into the colour grade.
# usage: calib.py STEM GLUT_IN GLUT_OUT LOC.json [LOC.json ...]     (prints before/after errors, writes sheets with SHEET=path)
import sys, os, math, json, numpy as np, cv2
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render import Renderer
from PIL import Image, ImageDraw
MAPS = '/root/gits/build/maps'; W, H = 192, 108
def mask(w, h):
    m = np.ones((h, w), bool); y, x = np.mgrid[0:h, 0:w] / np.array([h, w])[:, None, None]
    m[y > 0.78] = 0; m[(x > 0.55) & (y > 0.45)] = 0; m[(x < 0.20) & (y < 0.34)] = 0; m[y < 0.08] = 0; m[(x > 0.78) & (y < 0.5)] = 0
    m[(x < 0.25) & (y > 0.4) & (y < 0.75)] = 0   # kill feed / chat text
    return m
def prep(a):
    a = cv2.GaussianBlur(a.astype(np.float32), (0, 0), 2.0); return cv2.resize(a, (W // 2, H // 2), interpolation=cv2.INTER_AREA)
def pairs(R, stem, locs, minscore):
    F, O = [], []
    for loc in locs:
        for f, v in loc.items():
            if v['score'] < minscore: continue
            fr = np.asarray(Image.open(f).convert('RGB').resize((W, H), Image.BOX)).astype(np.float32) / 255
            ou = np.asarray(R.shot(v['pos'], math.radians(v['yaw']), math.radians(v['pitch']), fov=59).resize((W, H), Image.BOX)).astype(np.float32) / 255
            F.append(fr); O.append(ou)
    return F, O
def sel(F, O):
    m = mask(W // 2, H // 2); xs, ys = [], []
    for fr, ou in zip(F, O):
        a, b = prep(fr), prep(ou)
        g = np.abs(cv2.Laplacian(cv2.cvtColor(b, cv2.COLOR_RGB2GRAY), cv2.CV_32F)) + np.abs(cv2.Laplacian(cv2.cvtColor(a, cv2.COLOR_RGB2GRAY), cv2.CV_32F))
        k = m & (g < np.percentile(g[m], 70)); xs.append(b[k]); ys.append(a[k])
    return np.concatenate(xs), np.concatenate(ys)
def fit_curves(x, y, n=24):
    """per channel: monotone curve ours -> footage from paired quantiles, extended linearly to 0 and 1"""
    qs = np.linspace(2, 98, n); C = []
    for c in range(3):
        qx = np.percentile(x[:, c], qs); qy = np.percentile(y[:, c], qs)
        qx = qx + np.arange(n) * 1e-4; qy = np.maximum.accumulate(qy)
        s0 = (qy[2] - qy[0]) / max(1e-3, qx[2] - qx[0]); s1 = (qy[-1] - qy[-3]) / max(1e-3, qx[-1] - qx[-3])
        y0 = np.clip(qy[0] - qx[0] * s0, 0, qy[0]); y1 = np.clip(qy[-1] + (1 - qx[-1]) * s1, qy[-1], 1)
        C.append((np.r_[0.0, qx, 1.0], np.r_[y0, qy, y1]))
    return C
def apply_curves(C, v):
    return np.stack([np.clip(np.interp(v[..., c], C[c][0], C[c][1]), 0, 1) for c in range(3)], -1)
def stats(x, y):
    e = np.abs(x - y) * 255; lx = x.mean(1); ly = y.mean(1)
    return 'mean abs %.1f  mean ours %s footage %s  luma p10/50/90 ours %s footage %s' % (e.mean(), (x.mean(0) * 255).astype(int), (y.mean(0) * 255).astype(int),
        (np.percentile(lx, [10, 50, 90]) * 255).astype(int), (np.percentile(ly, [10, 50, 90]) * 255).astype(int))
if __name__ == '__main__':
    stem, gin, gout = sys.argv[1:4]; locs = [json.load(open(p)) for p in sys.argv[4:]]
    minscore = float(os.environ.get('MINSCORE', 0.45))
    R = Renderer(W=W * 2, H=H * 2, ss=1); gs = '%s/%s.gsky' % (MAPS, stem)
    R.load(stem, glut=gin, gmode='lut', gsky=gs if os.path.exists(gs) else None)
    F, O = pairs(R, stem, locs, minscore); print(stem, len(F), 'frames')
    x, y = sel(F, O); print('before:', stats(x, y))
    C = fit_curves(x, y); print('after: ', stats(apply_curves(C, x), y))
    raw = open(gin, 'rb').read(); L = np.frombuffer(raw[:12288], np.uint8).reshape(16, 256, 3).astype(np.float32) / 255
    L2 = (apply_curves(C, L) * 255 + 0.5).astype(np.uint8)
    open(gout, 'wb').write(L2.tobytes())   # (the GFIT trailer is redone by grade_fit.py)
    json.dump([[list(map(float, a)), list(map(float, b))] for a, b in C], open(gout + '.curves.json', 'w'))
    if os.environ.get('SHEET'):
        R.load(stem, glut=gout, gmode='lut', gsky=gs if os.path.exists(gs) else None); F2, O2 = pairs(R, stem, locs, minscore)
        rows = []
        for fr, ou, o2 in zip(F, O, O2): rows.append(np.hstack([fr, ou, o2]))
        S = (np.vstack(rows[:8]) * 255).astype(np.uint8); im = Image.fromarray(S); d = ImageDraw.Draw(im)
        d.text((4, 2), 'footage', fill=(255, 255, 0)); d.text((W + 4, 2), 'ours (map LUT)', fill=(255, 255, 0)); d.text((2 * W + 4, 2), 'ours (calibrated LUT)', fill=(255, 255, 0))
        im.save(os.environ['SHEET'], quality=90)
