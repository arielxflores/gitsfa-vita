# databuilder: the GFIT trailer of a map's colour grading (<stem>.glut), computed on the CPU, deterministically, from the
# LUT and the map itself (replaces tools/grade_fit.py's fit to host GPU renders, which no other machine reproduces).
#   POLY:   g(x) = k9 + k0 x.r + k1 x.g + k2 x.b + k3 x.r^2 + k4 x.g^2 + k5 x.b^2 + k6 x.r^3 + k7 x.g^3 + k8 x.b^3
#   AFFINE: g(x) = k9 + k0 x.r + k1 x.g + k2 x.b                                       (k* RGB; grade.h, unchanged)
# Weighting: as grade_fit.py, the colours the map shows before grading in 32 views from its spawn points (8 spawns x 2 yaws
# x 2 pitches, every 5th pixel of 480 x 272), drawn by gfit_view.py's CPU rasterizer instead of the GPU, plus a light weight
# on the whole cube. (Without a .gcol: the surfaces' own colours, texture x lightmap x 2 x tint, sampled by area.)
# Deterministic on every machine: elementwise IEEE arithmetic, the normal equations summed with math.fsum (correctly
# rounded) and solved by Gaussian elimination in Python floats - no BLAS, no GPU.
# usage: gfit_cpu.py LUT_IN_DIR MAPS_DIR OUT_DIR [stem ...]   (LUT_IN: <stem>.glut, the first 12288 bytes are used)
#        gfit_cpu.py --compare OLD_GLUT_DIR NEW_GLUT_DIR MAPS_DIR [stem ...]   (colour shift of the new fits vs old, /255)
import sys, os, struct, math, numpy as np
from PIL import Image
ALL = ["downtown", "berutalube", "ecenter", "ghost_storage", "headquarter", "port", "pss9", "ruinednight", "ruinedtokyo",
       "trainstation", "underground", "urban", "zioplant", "shooting", "tutorial", "tutorial2", "tutorial3"]
NSAMP = 120000            # surface samples per map
GRID_SHARE = 0.15         # weight of the 16^3 cube against the samples (grade_fit.py's)
# fixed barycentric sample points inside a triangle (a 2D Halton set folded into the triangle), up to 1024 per triangle
def _halton(i, b):
    f, r = 1.0, 0.0
    while i > 0: f /= b; r += f * (i % b); i //= b
    return r
BARY = np.array([[_halton(i + 1, 2), _halton(i + 1, 3)] for i in range(1024)])
BARY = np.where((BARY.sum(1) > 1)[:, None], 1 - BARY[:, ::-1], BARY)

def lut_eval(L, c):   # the engine's LUT lookup (as grade_fit.py)
    q = np.clip(c, 0, 1) * 15; b = np.minimum(np.floor(q[:, 2]), 14); f = q[:, 2] - b
    def samp(x, y):
        x = x - 0.5; y = y - 0.5; x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int); fx = (x - x0)[:, None]; fy = (y - y0)[:, None]
        x0c, x1c = np.clip(x0, 0, 255), np.clip(x0 + 1, 0, 255); y0c, y1c = np.clip(y0, 0, 15), np.clip(y0 + 1, 0, 15)
        return (L[y0c, x0c] * (1 - fx) + L[y0c, x1c] * fx) * (1 - fy) + (L[y1c, x0c] * (1 - fx) + L[y1c, x1c] * fx) * fy
    x = q[:, 0] + 0.5 + b * 16; y = q[:, 1] + 0.5
    return (samp(x, y) * (1 - f)[:, None] + samp(x + 16, y) * f[:, None]) / 255.

def feats(c, poly):
    r, g, b = c[:, 0], c[:, 1], c[:, 2]; one = np.ones_like(r)
    F = [r, g, b] + ([r * r, g * g, b * b, r * r * r, g * g * g, b * b * b] if poly else []) + [one]
    return np.stack(F, 1)

def read_gmap(path):
    d = open(path, 'rb').read(); assert d[:4] == b'GMAP'; ver, nt, nm = struct.unpack_from('<III', d, 4); o = 16; texs = []
    for _ in range(nt):
        w, h, fmt, nmip = struct.unpack_from('<HHBB', d, o); o += 6; lv = []
        for _ in range(nmip): n, = struct.unpack_from('<I', d, o); o += 4; lv.append(d[o:o + n]); o += n
        texs.append((w, h, fmt, lv[0]))
    meshes = []
    for _ in range(nm):
        tex, lm, glow, mode, flags, nv, ni = struct.unpack_from('<iiiIIII', d, o); o += 28
        ref, glowk, *tint = struct.unpack_from('<6f', d, o); o += 24
        if flags & 64: o += 8   # b67: env, envk (map_visual.py; the fit draws without the reflection)
        o += 24
        st = (8 if flags & 4 else 7) + (1 if flags & 64 else 0); V = np.frombuffer(d, '<f4', nv * st, o).reshape(nv, st)[:, :8 if flags & 4 else 7]; o += nv * st * 4
        I = np.frombuffer(d, '<u2', ni, o).astype(np.int64); o += ni * 2 + (2 if ni % 2 else 0)
        meshes.append(dict(tex=tex, lm=lm, mode=mode, flags=flags, ref=ref, tint=tint, V=V, I=I))
    return texs, meshes

_tc = {}
def texel_img(texs, k):
    if k not in _tc:
        w, h, fmt, data = texs[k]
        if fmt == 0: im = Image.frombytes('RGBA', (w, h), data)
        else: im = Image.frombytes('RGBA', (w, h), data, 'bcn', 1 if fmt == 1 else 3)
        _tc[k] = np.asarray(im, np.float64) / 255
    return _tc[k]

def samples(gmap):
    """surface colours before grading, area-weighted, deterministic -> (n, 3) float64"""
    _tc.clear(); texs, meshes = read_gmap(gmap)
    tris = []
    for mi, m in enumerate(meshes):
        if m['mode'] == 2 or m['tex'] < 0 or not len(m['I']): continue   # blended / additive layers left out (as the opaque look)
        P = m['V'][:, :3].astype(np.float64); T = m['I'].reshape(-1, 3)
        e1 = P[T[:, 1]] - P[T[:, 0]]; e2 = P[T[:, 2]] - P[T[:, 0]]; cr = np.cross(e1, e2)
        area = 0.5 * np.sqrt(cr[:, 0] * cr[:, 0] + cr[:, 1] * cr[:, 1] + cr[:, 2] * cr[:, 2])
        tris.append((mi, T, area))
    tot = math.fsum(math.fsum(a.tolist()) for _, _, a in tris)
    out = []; carry = 0.0
    for mi, T, area in tris:
        m = meshes[mi]; V = m['V']
        want = area * (NSAMP / tot)
        # per triangle: floor of the running total (deterministic: a sequential carry)
        k = np.zeros(len(T), np.int64)
        for i, w in enumerate(want.tolist()):
            carry += w; n = int(carry); carry -= n; k[i] = min(n, 1024)
        sel = np.nonzero(k)[0]
        if not len(sel): continue
        rep = np.repeat(sel, k[sel]); j = np.concatenate([np.arange(c) for c in k[sel]])
        u, v = BARY[j, 0], BARY[j, 1]; a = T[rep, 0]; b = T[rep, 1]; c = T[rep, 2]
        def at(col):
            return V[a, col] * (1 - u - v) + V[b, col] * u + V[c, col] * v
        tu, tv = at(3), at(4); lu, lv_ = at(5), at(6)
        img = texel_img(texs, m['tex']); h, w = img.shape[:2]
        x = np.floor((tu - np.floor(tu)) * w).astype(np.int64) % w; y = np.floor((tv - np.floor(tv)) * h).astype(np.int64) % h
        base = img[y, x]
        if m['mode'] == 1: keep = base[:, 3] >= m['ref']
        else: keep = np.ones(len(base), bool)
        if m['lm'] >= 0:
            L = texel_img(texs, m['lm']); lh, lw = L.shape[:2]
            lx = np.clip(np.floor(lu * lw).astype(np.int64), 0, lw - 1); ly = np.clip(np.floor(lv_ * lh).astype(np.int64), 0, lh - 1)
            lmc = L[ly, lx, :3] * 2.0
        else: lmc = np.ones((len(base), 3))
        col = base[:, :3] * lmc * np.array(m['tint'][:3], np.float64)
        out.append(np.clip(col[keep], 0, 1))
    return np.concatenate(out) if out else np.zeros((0, 3))

def solve(A, B):
    """A x = B (A: n x n, B: n x k) by Gaussian elimination with partial pivoting, in Python floats"""
    n = len(A); M = [list(A[i]) + list(B[i]) for i in range(n)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c])); M[c], M[p] = M[p], M[c]
        for r in range(c + 1, n):
            f = M[r][c] / M[c][c]
            if f: M[r] = [x - f * y for x, y in zip(M[r], M[c])]
    X = [[0.0] * len(B[0]) for _ in range(n)]
    for r in range(n - 1, -1, -1):
        for k in range(len(B[0])):
            s = M[r][n + k] - math.fsum(M[r][c] * X[c][k] for c in range(r + 1, n)); X[r][k] = s / M[r][r]
    return np.array(X)

def fit(L, S, poly):
    g = np.arange(16) / 15.; G = np.stack(np.meshgrid(g, g, g, indexing='ij'), -1).reshape(-1, 3)
    X = np.concatenate([S, G]); w = np.concatenate([np.ones(len(S)), np.full(len(G), GRID_SHARE * len(S) / len(G))])
    Y = lut_eval(L, X); F = feats(X, poly); nf = F.shape[1]
    Fw = F * w[:, None]
    A = [[math.fsum((Fw[:, i] * F[:, j]).tolist()) for j in range(nf)] for i in range(nf)]
    B = [[math.fsum((Fw[:, i] * Y[:, k]).tolist()) for k in range(3)] for i in range(nf)]
    W = solve(A, B)
    k = np.zeros((10, 4), np.float32)
    if poly: k[:9, :3] = W[:9]; k[9, :3] = W[9]
    else: k[:3, :3] = W[:3]; k[9, :3] = W[3]
    e = np.abs(np.clip(F[:len(S)] @ W, 0, 1) - Y[:len(S)]) * 255
    return k, float(e.mean()) if len(S) else 0.0

def apply_k(k, X, poly):
    F = feats(X, poly); W = np.zeros((F.shape[1], 3))
    if poly: W[:9] = k[:9, :3]; W[9] = k[9, :3]
    else: W[:3] = k[:3, :3]; W[3] = k[9, :3]
    return np.clip(F @ W, 0, 1)

def view_samples(gmap, gcol, gsky, fog):
    """the colours grade_fit.py weighted by (its 32 spawn views), drawn by gfit_view.py on the CPU"""
    import gfit_view
    return gfit_view.views(gmap, gcol, gsky, fog)

def gfit(raw12288, gmap, gcol=None, gsky=None, fog=None):
    """the LUT + its GFIT trailer; weighted by the spawn views when the map's .gcol and fog are given, else by area"""
    L = np.frombuffer(raw12288, np.uint8).reshape(16, 256, 3).astype(np.float64)
    S = view_samples(gmap, gcol, gsky, fog) if gcol and fog is not None and os.path.exists(gcol) else samples(gmap)
    kp, ep = fit(L, S, True); ka, ea = fit(L, S, False)
    return raw12288 + b'GFIT' + struct.pack('<I', 1) + kp.tobytes() + ka.tobytes(), (len(S), ep, ea)

def trailer(path):
    d = open(path, 'rb').read(); assert d[12288:12292] == b'GFIT'
    return (np.frombuffer(d, np.float32, 40, 12296).reshape(10, 4), np.frombuffer(d, np.float32, 40, 12296 + 160).reshape(10, 4))

if __name__ == '__main__':
    if sys.argv[1] == '--compare':
        old, new, maps = sys.argv[2:5]; stems = sys.argv[5:] or ALL
        g = np.arange(16) / 15.; G = np.stack(np.meshgrid(g, g, g, indexing='ij'), -1).reshape(-1, 3)
        for st in stems:
            (po, ao), (pn, an) = trailer(os.path.join(old, st + '.glut')), trailer(os.path.join(new, st + '.glut'))
            S = samples(os.path.join(maps, st + '.gmap'))
            for nm, X in (('surfaces', S), ('cube', G)):
                dp = np.abs(apply_k(po, X, True) - apply_k(pn, X, True)) * 255; da = np.abs(apply_k(ao, X, False) - apply_k(an, X, False)) * 255
                print('%-13s %-8s poly shift mean %.2f p99 %.1f max %.1f | affine mean %.2f p99 %.1f  [/255]' % (st, nm, dp.mean(), np.percentile(dp, 99), dp.max(), da.mean(), np.percentile(da, 99)))
        sys.exit(0)
    src, maps, out = sys.argv[1:4]; stems = sys.argv[4:] or ALL; os.makedirs(out, exist_ok=True)
    for st in stems:
        raw = open(os.path.join(src, st + '.glut'), 'rb').read()[:12288]
        data, (n, ep, ea) = gfit(raw, os.path.join(maps, st + '.gmap'))
        open(os.path.join(out, st + '.glut'), 'wb').write(data)
        print('%-13s %6d surface samples   poly fit mean %.2f   affine fit mean %.2f   [/255]' % (st, n, ep, ea), flush=True)
