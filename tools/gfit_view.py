# databuilder: the colour samples grade_fit.py took from GPU renders of a map (tools/mapvis/render.py: 8 spawn points x 2
# yaws x 2 pitches, 480 x 272, ungraded, fog and sky on, every 5th pixel), drawn on the CPU by a small software rasterizer
# instead, so that every PC gets the same samples (the GPU renders depended on the GPU and its driver).
# It follows render.py / sky.py: opaque meshes (depth test <=, back faces culled unless two-sided, alpha test), then the sky
# layers (camera-centred ones only where no geometry is), then the blended meshes far to near; texture x vertex colour x
# tint, x lightmap x 2, + glow, exponential fog per vertex; trilinear mipmapped texture filtering; an 8-bit RGBA target.
# Deterministic: numpy elementwise IEEE arithmetic only (no BLAS, no libm transcendentals: sin / cos / exp below use + - * /
# only), sorting with fixed tie-breaks and no floating-point reductions.
#   views(gmap, gcol, gsky or None, fog) -> (n, 3) float64 colours in 0..1 (the GPU path's R.shot(...)[::5] / 255, 32 views)
import os, sys, math, struct, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(HERE, 'mapvis'))
import gmapio

W, H = 480, 272; FOV = 59.0; ASPECT = 960 / 544; NEAR, FAR = 10.0, 12000.0
CHUNK = 3000000                      # (triangle, pixel) candidates per batch

# ------------------------------------------------------------------ deterministic sin / cos / exp
_PIO2 = 1.5707963267948966; _LN2 = 0.6931471805599453
def dsincos(x):
    """sin(x), cos(x) of a Python float with + - * / only (Taylor series after a reduction to [-pi/4, pi/4])"""
    k = math.floor(x / _PIO2 + 0.5); r = x - k * _PIO2; r2 = r * r
    s = 1.0; c = 1.0
    for n in range(24, 0, -2):   # sin r = r (1 - r2/(2.3) (1 - r2/(4.5) (...)));  cos r = 1 - r2/(1.2) (1 - r2/(3.4) (...))
        s = 1.0 - r2 / ((n) * (n + 1)) * s
        c = 1.0 - r2 / ((n - 1) * (n)) * c
    s = r * s; q = int(k) % 4
    return ((s, c), (c, -s), (-s, -c), (-c, s))[q]
def dexp(x):
    """exp of a float64 array (x <= ~0 here) with + - * / and ldexp only"""
    k = np.floor(x / _LN2 + 0.5); r = x - k * _LN2; p = np.ones_like(r)
    for n in range(16, 0, -1): p = 1.0 + r * p / n
    return np.ldexp(p, k.astype(np.int64))

# ------------------------------------------------------------------ camera (render.py's persp / view, MVP = P V)
def persp(fovy, asp, n, f):
    s, c = dsincos(fovy / 2); t = c / s; m = [[0.0] * 4 for _ in range(4)]
    m[0][0] = t / asp; m[1][1] = t; m[2][2] = (f + n) / (n - f); m[2][3] = 2 * f * n / (n - f); m[3][2] = -1.0; return m
def view(p, yaw, pitch):
    sp, cp = dsincos(pitch); sy, cy = dsincos(yaw)
    F = (cy * cp, sy * cp, sp); R = (sy, -cy, 0.0)
    U = (R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0])
    dot = lambda a, b: a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
    return [[R[0], R[1], R[2], -dot(R, p)], [U[0], U[1], U[2], -dot(U, p)], [-F[0], -F[1], -F[2], dot(F, p)], [0.0, 0.0, 0.0, 1.0]]
def mmul(A, B): return [[A[i][0] * B[0][j] + A[i][1] * B[1][j] + A[i][2] * B[2][j] + A[i][3] * B[3][j] for j in range(4)] for i in range(4)]
def xform(M, X, Y, Z):
    return [M[i][0] * X + M[i][1] * Y + M[i][2] * Z + M[i][3] for i in range(4)]

# ------------------------------------------------------------------ textures (GL_LINEAR_MIPMAP_LINEAR, repeat)
class Tex:
    def __init__(s, t): s.t = t; s.lv = {}; s.n = len(t['levels'])
    def level(s, k):
        if k not in s.lv:
            w, h = max(1, s.t['w'] >> k), max(1, s.t['h'] >> k)
            s.lv[k] = gmapio.dds_decode(w, h, s.t['levels'][k], s.t['fmt']).astype(np.float64) / 255.0
        return s.lv[k]
    def bilinear(s, k, u, v):
        img = s.level(k); h, w = img.shape[:2]
        x = u * w - 0.5; y = v * h - 0.5; x0 = np.floor(x); y0 = np.floor(y); fx = (x - x0)[:, None]; fy = (y - y0)[:, None]
        x0 = x0.astype(np.int64) % w; y0 = y0.astype(np.int64) % h; x1 = (x0 + 1) % w; y1 = (y0 + 1) % h
        return (img[y0, x0] * (1 - fx) + img[y0, x1] * fx) * (1 - fy) + (img[y1, x0] * (1 - fx) + img[y1, x1] * fx) * fy
    def sample(s, u, v, rho2):
        """rho2: squared texels per pixel (level 0); the level is log2(rho) with a linear fraction between powers of 2"""
        if s.n == 1: return s.bilinear(0, u, v)
        out = np.empty((len(u), 4))
        k = np.zeros(len(u), np.int64)
        for j in range(1, s.n): k += rho2 >= 4.0 ** j
        rho = np.sqrt(np.maximum(rho2, 1.0)); f = np.clip(rho / np.ldexp(1.0, k) - 1.0, 0.0, 1.0)
        f = np.where(rho2 <= 1.0, 0.0, np.where(k >= s.n - 1, 0.0, f))
        for j in range(s.n):
            m = k == j
            if not m.any(): continue
            a = s.bilinear(j, u[m], v[m])
            if j + 1 < s.n:
                ff = f[m][:, None]; nz = ff[:, 0] > 0
                if nz.any(): a[nz] = a[nz] * (1 - ff[nz]) + s.bilinear(j + 1, u[m][nz], v[m][nz]) * ff[nz]
            out[m] = a
        return out

# ------------------------------------------------------------------ geometry: per triangle corner arrays, clipping, rasterizing
# corner attributes: 0-3 clip x y z w, 4-5 uv0, 6-7 uv1, 8-11 vertex colour, 12 fog
NA = 13
def clip_near(A, w_min=None):
    """clips (n, 3, NA) clip-space triangles against the near plane (z >= -w), or against w >= w_min (the camera-centred
    sky, whose z is pinned to the far end); keeps the winding"""
    d = A[:, :, 2] + A[:, :, 3] if w_min is None else A[:, :, 3] - w_min; ins = d >= 0; c = ins.sum(1)
    out = [A[c == 3]]
    for want, odd_inside in ((1, True), (2, False)):
        sel = np.nonzero(c == want)[0]
        if not len(sel): continue
        T = A[sel]; dd = d[sel]; ii = ins[sel]
        odd = np.argmax(ii if odd_inside else ~ii, axis=1)   # the vertex alone on its side goes first (cyclic: winding kept)
        r = np.stack([odd, (odd + 1) % 3, (odd + 2) % 3], 1)
        T = np.take_along_axis(T, r[:, :, None], 1); dd = np.take_along_axis(dd, r, 1)
        a, b, cc = T[:, 0], T[:, 1], T[:, 2]; da, db, dc = dd[:, 0:1], dd[:, 1:2], dd[:, 2:3]
        ab = a + (b - a) * (da / (da - db)); ac = a + (cc - a) * (da / (da - dc))
        if odd_inside: out.append(np.stack([a, ab, ac], 1))
        else:   # a outside, b c inside: (b, c, ca) (b, ca, ab)
            out.append(np.stack([b, cc, ac], 1)); out.append(np.stack([b, ac, ab], 1))
    return np.concatenate(out) if out else A[:0]

def setup(A, cull, W=W, H=H):
    """clip-space triangles -> screen data: x y (window, y up), z/w, 1/w, area; drops culled, degenerate, off-screen ones"""
    w = A[:, :, 3]; x = (A[:, :, 0] / w + 1.0) * (W / 2); y = (A[:, :, 1] / w + 1.0) * (H / 2); z = A[:, :, 2] / w
    area = (x[:, 1] - x[:, 0]) * (y[:, 2] - y[:, 0]) - (x[:, 2] - x[:, 0]) * (y[:, 1] - y[:, 0])
    keep = (area != 0) & np.isfinite(area) & np.isfinite(z).all(1)
    if cull is not None: keep &= np.where(cull, area > 0, True)   # front = counter-clockwise (GL default)
    keep &= ~((x.max(1) < 0) | (x.min(1) > W) | (y.max(1) < 0) | (y.min(1) > H) | (z.min(1) > 1))
    return np.nonzero(keep)[0], x, y, z, area

def raster(idx, x, y, z, area, W=W, H=H):
    """yields (tri, pixel, l1, l2, depth) for the pixel centres inside each triangle in idx, in batches"""
    xs, ys = x[idx], y[idx]
    i0 = np.clip(np.ceil(xs.min(1) - 0.5), 0, W).astype(np.int64); i1 = np.clip(np.floor(xs.max(1) - 0.5), -1, W - 1).astype(np.int64)
    j0 = np.clip(np.ceil(ys.min(1) - 0.5), 0, H).astype(np.int64); j1 = np.clip(np.floor(ys.max(1) - 0.5), -1, H - 1).astype(np.int64)
    nx = np.maximum(i1 - i0 + 1, 0); ny = np.maximum(j1 - j0 + 1, 0); n = nx * ny
    cum = np.cumsum(n); start = 0
    while start < len(idx):
        end = int(np.searchsorted(cum, (cum[start - 1] if start else 0) + CHUNK, 'right')); end = max(end, start + 1)
        sl = slice(start, end); nn = n[sl]; tot = int(nn.sum())
        if tot:
            t = np.repeat(np.arange(start, end), nn); k = np.arange(tot) - np.repeat(np.cumsum(nn) - nn, nn)
            px = i0[t] + k % nx[t]; py = j0[t] + k // nx[t]; T = idx[t]
            cx = px + 0.5; cy = py + 0.5
            x0, y0 = x[T, 0], y[T, 0]; ar = area[T]
            l1 = ((cx - x0) * (y[T, 2] - y0) - (x[T, 2] - x0) * (cy - y0)) / ar
            l2 = ((x[T, 1] - x0) * (cy - y0) - (cx - x0) * (y[T, 1] - y0)) / ar
            ins = (l1 >= 0) & (l2 >= 0) & (l1 + l2 <= 1)
            T, px, py, l1, l2 = T[ins], px[ins], py[ins], l1[ins], l2[ins]
            d = z[T, 0] + l1 * (z[T, 1] - z[T, 0]) + l2 * (z[T, 2] - z[T, 0])
            ok = (d >= -1) & (d <= 1)
            yield T[ok], (py * W + px)[ok], l1[ok], l2[ok], d[ok]
        start = end

def interp(A, x, y, T, l1, l2, cols, dx=0.0, dy=0.0):
    """perspective-correct corner attributes cols of triangles T at the pixel centres (+dx, dy) given by l1 l2 there"""
    if dx or dy:   # barycentrics are affine in window space: move them by the per-pixel gradient
        x0, y0 = x[T, 0], y[T, 0]; ar = (x[T, 1] - x0) * (y[T, 2] - y0) - (x[T, 2] - x0) * (y[T, 1] - y0)
        l1 = l1 + (dx * (y[T, 2] - y0) - (x[T, 2] - x0) * dy) / ar
        l2 = l2 + ((x[T, 1] - x0) * dy - dx * (y[T, 1] - y0)) / ar
    q = 1.0 / A[T, :, 3]; l0 = 1.0 - l1 - l2
    a0, a1, a2 = l0 * q[:, 0], l1 * q[:, 1], l2 * q[:, 2]; den = a0 + a1 + a2
    V = A[T][:, :, cols]
    return (V[:, 0] * a0[:, None] + V[:, 1] * a1[:, None] + V[:, 2] * a2[:, None]) / den[:, None]

def tex_coords(A, x, y, T, l1, l2, c0, tex):
    """uv at the pixels and the squared texels-per-pixel of the texture (for its mip level)"""
    uv = interp(A, x, y, T, l1, l2, [c0, c0 + 1]); ux = interp(A, x, y, T, l1, l2, [c0, c0 + 1], dx=1.0); uy = interp(A, x, y, T, l1, l2, [c0, c0 + 1], dy=1.0)
    w0, h0 = tex.t['w'], tex.t['h']
    ax = (ux[:, 0] - uv[:, 0]) * w0; bx = (ux[:, 1] - uv[:, 1]) * h0; ay = (uy[:, 0] - uv[:, 0]) * w0; by = (uy[:, 1] - uv[:, 1]) * h0
    return uv[:, 0], uv[:, 1], np.maximum(ax * ax + bx * bx, ay * ay + by * by)

def q8(c): return np.floor(np.clip(c, 0.0, 1.0) * 255.0 + 0.5) / 255.0

# ------------------------------------------------------------------ the scene
def flat_uv(V, I):
    """uvrebase.rebase's one change to the picture: triangles that repeat their texture 64+ times get one texel"""
    tri = I.reshape(-1, 3); uv = V[:, 3:5].astype(np.float64)
    if not len(tri) or np.abs(V[:, 3:5]).max() <= 8.0: return None
    tu = uv[tri]; span = (tu.max(1) - tu.min(1)).max(1); fl = span > 64
    if not fl.any(): return None
    m = np.mod(tu[fl], 1.0); return fl, (m[:, 0] + m[:, 1] + m[:, 2]) / 3.0

class Scene:
    def __init__(s, gmap, gsky, fog):
        g = gmapio.read(gmap); s.ver = g['ver']; s.tex = [Tex(t) for t in g['tex']]; s.meshes = []
        for m in g['mesh']:
            V = m['V']; I = m['I'].astype(np.int64)
            if m.get('vcol'): col = V[:, 7].copy().view(np.uint8).reshape(-1, 4).astype(np.float64) / 255.0   # (b67: not the stride - an env mesh's 8th slot is its normal)
            else: col = np.ones((len(V), 4))
            C = np.zeros((len(I) // 3, 3, NA)); tri = I.reshape(-1, 3)
            for k in range(3):
                C[:, k, 0:3] = V[tri[:, k], 0:3]; C[:, k, 4:8] = V[tri[:, k], 3:7]; C[:, k, 8:12] = col[tri[:, k]]
            if not (m['flags'] & 16):
                f = flat_uv(V, I)
                if f is not None: C[f[0], :, 4:6] = f[1][:, None, :]
            cen = [(m['mn'][i] + m['mx'][i]) / 2 for i in range(3)]
            s.meshes.append(dict(m, C=C, cen=cen))
        fc, fp = fog; s.fogc = np.array(fc, np.float64); s.fogp = fp
        s.sky = read_gsky(gsky) if gsky and os.path.exists(gsky) else None

    def corners(s, C, MVP):
        A = C.copy(); X, Y, Z = C[:, :, 0], C[:, :, 1], C[:, :, 2]
        cx, cy, cz, cw = xform(MVP, X, Y, Z); A[:, :, 0] = cx; A[:, :, 1] = cy; A[:, :, 2] = cz; A[:, :, 3] = cw
        A[:, :, 12] = s.fogp[2] * (1.0 - dexp(-np.maximum(cw - s.fogp[0], 0.0) * s.fogp[1]))
        return A

    def shade(s, m, A, x, y, T, l1, l2):
        """render.py's fragment shader before fog / grading: (rgb, alpha) of mesh m at the pixels"""
        if m['tex'] >= 0:
            tx = s.tex[m['tex']]; u, v, r2 = tex_coords(A, x, y, T, l1, l2, 4, tx); c = tx.sample(u, v, r2)
        else: c = np.tile([1.0, 0.0, 1.0, 1.0], (len(T), 1))
        if m.get('vcol'): c = c * interp(A, x, y, T, l1, l2, [8, 9, 10, 11])
        c = c * np.array(m['tint'], np.float64)
        return c, (u, v, r2) if m['tex'] >= 0 else None

    def finish(s, m, A, x, y, T, l1, l2, c, uvr):
        rgb = c[:, :3]
        if m['lm'] >= 0:
            lt = s.tex[m['lm']]; u, v, r2 = tex_coords(A, x, y, T, l1, l2, 6, lt); rgb = rgb * (lt.sample(u, v, r2)[:, :3] * 2.0)
        if m.get('glow', -1) >= 0 and m['glowk'] > 0:
            gt = s.tex[m['glow']]; u, v, r2 = tex_coords(A, x, y, T, l1, l2, 4, gt); rgb = rgb + gt.sample(u, v, r2)[:, :3] * m['glowk']
        f = interp(A, x, y, T, l1, l2, [12])
        return rgb + (s.fogc - rgb) * f   # mix(fc, fogc, fog)

    def shot(s, pos, yaw, pitch):
        P = persp(math.radians(FOV), ASPECT, NEAR, FAR); Vm = view(pos, yaw, pitch); MVP = mmul(P, Vm)
        img = np.tile(q8(s.fogc), (W * H, 1)); depth = np.full(W * H, 2.0)
        best_mesh = np.full(W * H, -1, np.int64); best_tri = np.zeros(W * H, np.int64); best_l = np.zeros((W * H, 2))
        geo = {}
        for mi, m in enumerate(s.meshes):
            if m['mode'] == 2 or not len(m['C']): continue
            A = clip_near(s.corners(m['C'], MVP))
            idx, x, y, z, area = setup(A, None if m['two'] else np.ones(len(A), bool))
            if not len(idx): continue
            geo[mi] = (A, x, y)
            for T, pix, l1, l2, d in raster(idx, x, y, z, area):
                if not len(T): continue
                if m['mode'] == 1:   # alpha test (texture x vertex colour x tint against ref)
                    c, _ = s.shade(m, A, x, y, T, l1, l2); ok = c[:, 3] >= (m['ref'] if s.ver >= 2 else 0.5)
                    T, pix, l1, l2, d = T[ok], pix[ok], l1[ok], l2[ok], d[ok]
                if not len(T): continue
                # nearest per pixel within the batch (ties: the later triangle, as depth func <= draws over)
                o = np.lexsort((-T, d, pix)); pix, T, l1, l2, d = pix[o], T[o], l1[o], l2[o], d[o]
                first = np.r_[True, pix[1:] != pix[:-1]]; pix, T, l1, l2, d = pix[first], T[first], l1[first], l2[first], d[first]
                upd = d <= depth[pix]
                pix, T, l1, l2, d = pix[upd], T[upd], l1[upd], l2[upd], d[upd]
                depth[pix] = d; best_mesh[pix] = mi; best_tri[pix] = T; best_l[pix, 0] = l1; best_l[pix, 1] = l2
        for mi in np.unique(best_mesh[best_mesh >= 0]):
            m = s.meshes[mi]; A, x, y = geo[mi]; pix = np.nonzero(best_mesh == mi)[0]
            T, l1, l2 = best_tri[pix], best_l[pix, 0], best_l[pix, 1]
            c, uvr = s.shade(m, A, x, y, T, l1, l2); img[pix] = q8(s.finish(m, A, x, y, T, l1, l2, c, uvr))
        if s.sky is not None:
            s.draw_sky(img, depth, P, Vm, far=True); s.draw_sky(img, depth, P, Vm, far=False)
        cam = pos
        bl = [i for i, m in enumerate(s.meshes) if m['mode'] == 2 and len(m['C'])]
        bl.sort(key=lambda i: -((s.meshes[i]['cen'][0] - cam[0]) ** 2 + (s.meshes[i]['cen'][1] - cam[1]) ** 2 + (s.meshes[i]['cen'][2] - cam[2]) ** 2))
        for mi in bl: s.draw_blended(img, depth, MVP, s.meshes[mi])
        out = img.reshape(H, W, 3)[::-1].reshape(-1, 3)   # rows top to bottom, as the GPU image read back and flipped
        return out

    def draw_blended(s, img, depth, MVP, m):
        A = clip_near(s.corners(m['C'], MVP))
        idx, x, y, z, area = setup(A, None if m['two'] else np.ones(len(A), bool))
        frags = []
        for T, pix, l1, l2, d in raster(idx, x, y, z, area):
            ok = d <= depth[pix]; T, pix, l1, l2, d = T[ok], pix[ok], l1[ok], l2[ok], d[ok]
            if not len(T): continue
            c, uvr = s.shade(m, A, x, y, T, l1, l2); keep = c[:, 3] >= m['ref']
            T, pix, l1, l2, d, c = T[keep], pix[keep], l1[keep], l2[keep], d[keep], c[keep]
            if uvr is not None: uvr = tuple(a[keep] for a in uvr)
            rgb = s.finish(m, A, x, y, T, l1, l2, c, uvr)
            frags.append((pix, T, rgb, c[:, 3], d))
        if not frags: return
        pix, T, rgb, a, d = (np.concatenate([f[i] for f in frags]) for i in range(5))
        if not len(pix): return
        blend(img, pix, T, rgb, a, additive=bool(m['flags'] & 8))
        if m['zwrite']:
            o = np.lexsort((d, pix)); p2, d2 = pix[o], d[o]; first = np.r_[True, p2[1:] != p2[:-1]]
            depth[p2[first]] = np.minimum(depth[p2[first]], d2[first])

    def draw_sky(s, img, depth, P, Vm, far):
        texs, layers = s.sky
        for L in layers:
            if bool(L['flags'] & 4) != far: continue
            M = [row[:] for row in Vm]
            if far: M[0][3] = M[1][3] = M[2][3] = 0.0
            MVP = mmul(P, M)   # (t = 0: no rotation, no scroll)
            C = L['C']; A = C.copy(); cx, cy, cz, cw = xform(MVP, C[:, :, 0], C[:, :, 1], C[:, :, 2])
            A[:, :, 0] = cx; A[:, :, 1] = cy; A[:, :, 2] = cw * 0.99999 if far else cz; A[:, :, 3] = cw
            A = clip_near(A, 1e-3 if far else None); idx, x, y, z, area = setup(A, None)
            tx = texs[L['tex']]; pixs = []; Ts = []; rgbs = []; als = []
            for T, pix, l1, l2, d in raster(idx, x, y, z, area):
                ok = d <= depth[pix]; T, pix, l1, l2 = T[ok], pix[ok], l1[ok], l2[ok]
                if not len(T): continue
                u, v, r2 = tex_coords(A, x, y, T, l1, l2, 4, tx); c = tx.sample(u, v, r2) * np.array(L['tint'], np.float64)
                rgb = c[:, :3] + (s.fogc - c[:, :3]) * L['fog']
                pixs.append(pix); Ts.append(T); rgbs.append(rgb); als.append(c[:, 3])
            if not pixs: continue
            pix, T, rgb, a = np.concatenate(pixs), np.concatenate(Ts), np.concatenate(rgbs), np.concatenate(als)
            if not len(pix): continue
            if L['flags'] & 1: blend(img, pix, T, rgb, a, additive=True)
            elif L['flags'] & 2: blend(img, pix, T, rgb, a, additive=False)
            else:
                o = np.lexsort((T, pix)); pix, rgb = pix[o], rgb[o]; last = np.r_[pix[1:] != pix[:-1], True]
                img[pix[last]] = q8(rgb[last])

def blend(img, pix, order, rgb, a, additive):
    """SRC_ALPHA, ONE (additive) or SRC_ALPHA, ONE_MINUS_SRC_ALPHA, fragment by fragment in draw order, 8-bit target"""
    if not len(pix): return
    o = np.lexsort((order, pix)); pix, rgb, a = pix[o], rgb[o], a[o][:, None]
    first = np.r_[True, pix[1:] != pix[:-1]]; grp = np.cumsum(first) - 1; rank = np.arange(len(pix)) - np.nonzero(first)[0][grp]
    for r in range(int(rank.max()) + 1 if len(rank) else 0):
        sel = rank == r; p = pix[sel]; c = np.clip(rgb[sel], 0, 1); al = np.clip(a[sel], 0, 1)
        img[p] = q8(img[p] + c * al if additive else c * al + img[p] * (1 - al))

def read_gsky(path):
    d = open(path, 'rb').read(); mg, ver, nl, nt = struct.unpack_from('<4sIII', d, 0); o = 16; texs = []
    for i in range(nt):
        w, h, fmt, n = struct.unpack_from('<HHBB', d, o); o += 6; lv = []
        for k in range(n): sz = struct.unpack_from('<I', d, o)[0]; o += 4; lv.append(d[o:o + sz]); o += sz
        texs.append(Tex(dict(w=w, h=h, fmt=fmt, levels=lv)))
    layers = []
    for i in range(nl):
        tex, flags = struct.unpack_from('<iI', d, o); o += 8; tint = struct.unpack_from('<4f', d, o); o += 16; su, sv, rot, fog = struct.unpack_from('<4f', d, o); o += 16
        nv, ni = struct.unpack_from('<II', d, o); o += 8
        V = np.frombuffer(d, '<f4', nv * 5, o).reshape(nv, 5).astype(np.float64); o += nv * 20
        I = np.frombuffer(d, '<u2', ni, o).astype(np.int64); o += ni * 2 + (2 if ni % 2 else 0)
        tri = I.reshape(-1, 3); C = np.zeros((len(tri), 3, NA))
        for k in range(3): C[:, k, 0:3] = V[tri[:, k], 0:3]; C[:, k, 4:6] = V[tri[:, k], 3:5]
        layers.append(dict(tex=tex, flags=flags, tint=tint, fog=fog, C=C))
    return texs, layers

def spawns(path):
    d = open(path, 'rb').read(); mg, ver, nv, ni = struct.unpack_from('<4sIII', d, 0); o = 16 + nv * 12 + ni * 12
    n = struct.unpack_from('<I', d, o)[0]; o += 4; out = []
    for i in range(n):
        out.append(struct.unpack_from('<ii4f', d, o)); o += 24
    return out

def views(gmap, gcol, gsky, fog, n_views=8, every=5):
    """grade_fit.py's samples(): the same spawn points (numpy's default_rng(0)), yaws, pitches, eye height and pixels"""
    S = Scene(gmap, gsky, fog); sp = spawns(gcol); rng = np.random.default_rng(0); px = []
    for i in rng.choice(len(sp), min(n_views, len(sp)), replace=False):
        g, t, x, y, z, yw = sp[i]
        for dy in (0, 180):
            for p in (-5, 15):
                px.append(S.shot((x, y, z + 152), yw + math.radians(dy), math.radians(p))[::every])
    return np.concatenate(px)
