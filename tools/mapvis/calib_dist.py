# Per-map colour calibration against the footage WITHOUT located cameras: the colour distribution of many footage
# frames (HUD, weapon and kill-feed areas masked, death / menu frames dropped) against the distribution of our host
# renders from random player spots on the map's navigation mesh (eye height, random heading, slight pitch).
# Only low-saturation pixels count (walls, floors, sky: the shared content; HUD glows, holograms, muzzle flashes and
# effects we draw differently are left out), so the fitted per-channel curves (quantile matching, monotone, smoothed,
# applied at STRENGTH) set brightness, contrast and white balance. Written into a copy of the map's LUT like calib.py.
# usage: calib_dist.py STEM GLUT_IN GLUT_OUT FRAMES_DIR|LOC.json [...]  (env: N=views, STRENGTH=0..1, SHEET=path, SKIP=t,t)
import sys, os, math, json, glob, struct, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render import Renderer
from calib import mask
from PIL import Image, ImageDraw
MAPS = '/root/gits/build/maps'; W, H = 192, 108; EYE = 152.6
def nav_tris(stem):
    d = open('%s/%s.gnav' % (MAPS, stem), 'rb').read(); n = struct.unpack_from('<I', d, 4)[0]
    return np.frombuffer(d, '<f4', n * 12, 8).reshape(n, 12)[:, :9].reshape(n, 3, 3).astype(np.float64)
def _ray_dist(T, o, d, far=3000.0):
    """distance to the first collision triangle along o + t d (far if none)"""
    a = T[:, 0]; e1 = T[:, 1] - a; e2 = T[:, 2] - a; pv = np.cross(d, e2); det = (e1 * pv).sum(1); ok = np.abs(det) > 1e-9
    inv = np.where(ok, 1.0 / np.where(ok, det, 1), 0); tv = o - a; u = (tv * pv).sum(1) * inv; qv = np.cross(tv, e1); v = (qv * d).sum(1) * inv; t = (qv * e2).sum(1) * inv
    h = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1) & (t < far); return float(t[h].min()) if h.any() else far
def views(stem, n, seed=7):
    """player-like views: random nav spots at eye height, looking the most open way of 8 (no faces against a wall)"""
    import gcol
    T = nav_tris(stem); a = 0.5 * np.abs(np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])[:, 2]); p = a / a.sum(); rng = np.random.default_rng(seed); out = []
    P, I = gcol.tris('%s/%s.gcol' % (MAPS, stem)); CT = P[I].astype(np.float64); cmn = CT.min(1); cmx = CT.max(1)
    for _ in range(n * 6):
        if len(out) >= n: break
        k = rng.choice(len(T), p=p); r1, r2 = rng.random(2)
        if r1 + r2 > 1: r1, r2 = 1 - r1, 1 - r2
        q = T[k, 0] + r1 * (T[k, 1] - T[k, 0]) + r2 * (T[k, 2] - T[k, 0]); e = np.array([q[0], q[1], q[2] + EYE])
        near = CT[(cmn[:, 0] < e[0] + 3000) & (cmx[:, 0] > e[0] - 3000) & (cmn[:, 1] < e[1] + 3000) & (cmx[:, 1] > e[1] - 3000) & (cmn[:, 2] < e[2] + 600) & (cmx[:, 2] > e[2] - 600)]
        if _ray_dist(near, e, np.array([0, 0, 1.0]), 2000) < 40: continue   # inside something
        y0 = rng.random() * 2 * math.pi; best = None
        for j in range(8):
            y = y0 + j * math.pi / 4; dd = min(_ray_dist(near, e, np.array([math.cos(y + o), math.sin(y + o), 0.0])) for o in (-0.25, 0, 0.25))
            if best is None or dd > best[0]: best = (dd, y)
        if best[0] < 500: continue
        out.append(((e[0], e[1], e[2]), best[1] + rng.uniform(-0.3, 0.3), math.radians(rng.uniform(-10, 4))))
    return out
def frame_ok(a):
    """drop death screens (red), white flashes, black / loading frames and big menu panels"""
    m = a.reshape(-1, 3).mean(0); l = m.mean()
    if l < 0.04 or l > 0.8: return False
    if m[0] > 1.45 * m[1] and m[0] > 1.3 * m[2]: return False
    return True
def lowsat(a, m, smax):
    px = a[m]; s = px.max(1) - px.min(1); return px[s < smax]
def run(stem, gin, gout, dirs, n=60, strength=0.75, sheet=None, skip=(), smax=0.22, wbmax=0.04, minscore=0.44):
    # frame folders (unpaired: random views), or located-camera json files (locate.py / locate_mm.py: our views at
    # the footage cameras, so both distributions show the same places)
    locs = [json.load(open(d)) for d in dirs if d.endswith('.json')]; dirs = [d for d in dirs if not d.endswith('.json')]
    fs = sorted(sum([glob.glob(d + '/*.jpg') for d in dirs], []))
    fs = [f for f in fs if not any(('_%s.jpg' % s) in f for s in skip)]
    M = mask(W, H); F = []; used = []; V = []
    for f in fs:
        a = np.asarray(Image.open(f).convert('RGB').resize((W, H), Image.BOX)).astype(np.float32) / 255
        if frame_ok(a): F.append(a); used.append(f)
    for loc in locs:
        for f, v in loc.items():
            if v['score'] < minscore: continue
            F.append(np.asarray(Image.open(f).convert('RGB').resize((W, H), Image.BOX)).astype(np.float32) / 255); fs.append(f)
            V.append((tuple(v['pos']), math.radians(v['yaw']), math.radians(v['pitch'])))
    R = Renderer(W=W, H=H, ss=2); gs = '%s/%s.gsky' % (MAPS, stem)
    R.load(stem, glut=gin, gmode='lut', gsky=gs if os.path.exists(gs) else None)
    if not V: V = views(stem, n)
    O = [np.asarray(R.shot(*v, fov=59)).astype(np.float32) / 255 for v in V]
    y = np.concatenate([lowsat(a, M, smax) for a in F]); x = np.concatenate([lowsat(o, M, smax) for o in O])
    # brightness / contrast: one tone curve on luma (mean of r, g, b), applied as a gain so hues stay the map's own
    # (the footage spends more time in some rooms than our random views do: per-channel matching would move hues);
    # white balance: per-channel gains on the low-saturation pixels, held within +-WB
    qs = np.linspace(2, 98, 24); lx = np.percentile(x.mean(1), qs) + np.arange(24) * 1e-4; ly = np.maximum.accumulate(np.percentile(y.mean(1), qs))
    s0 = (ly[2] - ly[0]) / max(1e-3, lx[2] - lx[0]); s1 = (ly[-1] - ly[-3]) / max(1e-3, lx[-1] - lx[-3])
    cx = np.r_[0.0, lx, 1.0]; cy = np.r_[np.clip(ly[0] - lx[0] * s0, 0, ly[0]), ly, np.clip(ly[-1] + (1 - lx[-1]) * s1, ly[-1], 1)]
    cy = cx + (cy - cx) * strength
    wb = np.clip((y.mean(0) / y.mean()) / (x.mean(0) / x.mean()), 1 - wbmax, 1 + wbmax); wb = 1 + (wb - 1) * strength
    def apply(v):
        l = v.mean(-1, keepdims=True); g = np.interp(l, cx, cy) / np.maximum(l, 1e-4)
        o = v * g * wb; m = o.max(-1, keepdims=True)
        return np.clip(np.where(m > 1, o / np.maximum(m, 1e-6), o), 0, 1)   # over-range: scaled back, hue kept
    def st(v): return 'mean %s  luma p10/50/90 %s' % ((v.mean(0) * 255).astype(int), (np.percentile(v.mean(1), [10, 50, 90]) * 255).astype(int))
    print('%s: %d of %d frames, %d views | footage %s | ours %s | ours calibrated %s | wb %s' % (stem, len(F), len(fs), len(O), st(y), st(x), st(apply(x)), np.round(wb, 3)))
    raw = open(gin, 'rb').read(); L = np.frombuffer(raw[:12288], np.uint8).reshape(16, 256, 3).astype(np.float32) / 255
    open(gout, 'wb').write((apply(L) * 255 + 0.5).astype(np.uint8).tobytes())
    C = [(cx, cy)]
    json.dump(dict(luma=[list(map(float, cx)), list(map(float, cy))], wb=list(map(float, wb)), frames=len(F), views=len(O), strength=strength), open(gout + '.json', 'w'))
    if sheet:   # footage frames | ours before | ours after (different places: compare the overall look)
        R.load(stem, glut=gout, gmode='lut', gsky=gs if os.path.exists(gs) else None); O2 = [np.asarray(R.shot(*v, fov=59)).astype(np.float32) / 255 for v in V]
        k = min(8, len(F), len(O)); fi = np.linspace(0, len(F) - 1, k).astype(int); oi = np.linspace(0, len(O) - 1, k).astype(int)
        S = np.vstack([np.hstack([F[i], O[j], O2[j]]) for i, j in zip(fi, oi)]); im = Image.fromarray((S * 255).astype(np.uint8)); d = ImageDraw.Draw(im)
        for i, t in enumerate(('footage', 'ours (current LUT)', 'ours (calibrated LUT)')): d.text((i * W + 4, 2), t, fill=(255, 255, 0))
        im.save(sheet, quality=90)
    return C
if __name__ == '__main__':
    stem, gin, gout = sys.argv[1:4]
    run(stem, gin, gout, sys.argv[4:], n=int(os.environ.get('N', 60)), strength=float(os.environ.get('STRENGTH', 0.75)), sheet=os.environ.get('SHEET'),
        skip=[s for s in os.environ.get('SKIP', '').split(',') if s])
