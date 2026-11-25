# Colour grading without texture reads: fits each map's 16^3 grading LUT (<stem>.glut, raw 256x16 RGB) with
#   POLY:   g(x) = k9 + k0 x.r + k1 x.g + k2 x.b + k3 x.r^2 + k4 x.g^2 + k5 x.b^2 + k6 x.r^3 + k7 x.g^3 + k8 x.b^3   (x = saturate(colour))
#   AFFINE: g(x) = k9 + k0 x.r + k1 x.g + k2 x.b
# (k* are RGB vectors), weighted towards the colours the map really produces before grading (host renders of the
# map with grading off, from its spawn points), plus a light weight on the whole cube so nothing extrapolates wildly.
# Appends a trailer to the LUT: 'GFIT', u32 1, 10 x float4 POLY, 10 x float4 AFFINE (grade.h reads it; older readers
# only read the first 12288 bytes and ignore it).
# usage: grade_fit.py LUT_IN_DIR OUT_DIR [stem ...]      (needs tools/mapvis/render.py: moderngl + EGL)
import sys, os, math, struct, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(HERE, 'mapvis'))
sys.path.insert(0, HERE); import gitspaths
MAPS = gitspaths.root('BMAPS')
ALL = ["downtown", "berutalube", "ecenter", "ghost_storage", "headquarter", "port", "pss9", "ruinednight", "ruinedtokyo",
       "trainstation", "underground", "urban", "zioplant", "shooting", "tutorial", "tutorial2", "tutorial3"]

def lut_eval(L, c):   # the engine's LUT lookup (bilinear in r,g inside a blue slice, lerp between slices)
    q = np.clip(c, 0, 1) * 15; b = np.minimum(np.floor(q[:, 2]), 14); f = q[:, 2] - b
    def samp(x, y):
        x = x - 0.5; y = y - 0.5; x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int); fx = (x - x0)[:, None]; fy = (y - y0)[:, None]
        x0c, x1c = np.clip(x0, 0, 255), np.clip(x0 + 1, 0, 255); y0c, y1c = np.clip(y0, 0, 15), np.clip(y0 + 1, 0, 15)
        return (L[y0c, x0c] * (1 - fx) + L[y0c, x1c] * fx) * (1 - fy) + (L[y1c, x0c] * (1 - fx) + L[y1c, x1c] * fx) * fy
    x = q[:, 0] + 0.5 + b * 16; y = q[:, 1] + 0.5
    return (samp(x, y) * (1 - f)[:, None] + samp(x + 16, y) * f[:, None]) / 255.

def feats(c, poly):
    r, g, b = c[:, 0], c[:, 1], c[:, 2]; one = np.ones_like(r)
    F = [r, g, b] + ([r * r, g * g, b * b, r ** 3, g ** 3, b ** 3] if poly else []) + [one]
    return np.stack(F, 1)

def samples(stem, n_views=8):
    from render import Renderer
    from gcol import spawns
    R = Renderer(W=480, H=272, ss=1); gs = '%s/%s.gsky' % (MAPS, stem)
    R.load(stem, lut=False, gsky=gs if os.path.exists(gs) else None)
    sp = spawns('%s/%s.gcol' % (MAPS, stem)); rng = np.random.default_rng(0)
    px = []
    for i in rng.choice(len(sp), min(n_views, len(sp)), replace=False):
        g, t, x, y, z, yw = sp[i]
        for dy in (0, 180):
            for p in (-5, 15):
                px.append(np.asarray(R.shot((x, y, z + 152), yw + math.radians(dy), math.radians(p))).reshape(-1, 3)[::5])
    R.ctx.release()
    return np.concatenate(px).astype(np.float64) / 255

def fit(L, S, poly, grid_share=0.15):
    g = np.arange(16) / 15.; G = np.stack(np.meshgrid(g, g, g, indexing='ij'), -1).reshape(-1, 3)
    X = np.concatenate([S, G]); w = np.concatenate([np.ones(len(S)), np.full(len(G), grid_share * len(S) / len(G))])
    Y = lut_eval(L, X); F = feats(X, poly); sw = np.sqrt(w)[:, None]
    W = np.linalg.lstsq(F * sw, Y * sw, rcond=None)[0]
    k = np.zeros((10, 4), np.float32)
    if poly: k[:9, :3] = W[:9]; k[9, :3] = W[9]
    else: k[:3, :3] = W[:3]; k[9, :3] = W[3]
    e = np.abs(np.clip(F[:len(S)] @ W, 0, 1) - Y[:len(S)]) * 255; eg = np.abs(np.clip(F[len(S):] @ W, 0, 1) - Y[len(S):]) * 255
    return k, e.mean(), np.percentile(e, 99), eg.mean()

if __name__ == '__main__':
    src, out = sys.argv[1], sys.argv[2]; stems = sys.argv[3:] or ALL; os.makedirs(out, exist_ok=True)
    for stem in stems:
        raw = open(os.path.join(src, stem + '.glut'), 'rb').read()[:12288]
        L = np.frombuffer(raw, np.uint8).reshape(16, 256, 3).astype(np.float64)
        S = samples(stem)
        kp, ep, pp, gp = fit(L, S, True); ka, ea, pa, ga = fit(L, S, False)
        open(os.path.join(out, stem + '.glut'), 'wb').write(raw + b'GFIT' + struct.pack('<I', 1) + kp.tobytes() + ka.tobytes())
        print('%-13s poly: mean %.2f p99 %.1f (cube %.1f)   affine: mean %.2f p99 %.1f (cube %.1f)   [/255]' % (stem, ep, pp, gp, ea, pa, ga), flush=True)
