# databuilder: make_scope_overlay.py's scoped view drawn on the CPU (the same scene, camera, shaders, depth test and blending
# as its moderngl version), so that every PC gets the same texture: the GPU version's output depended on the GPU and driver.
# Uses tools/gfit_view.py's small deterministic rasterizer (numpy elementwise IEEE arithmetic only).
#   build(size, ss) -> PIL RGBA image (size x size), as make_scope_overlay.build_gpu()
import sys, os, numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import gfit_view as GV
import make_scope_overlay as SO
from gvmlib import static_meshes

class ArrTex(GV.Tex):
    """an RGBA image with a mip chain like glGenerateMipmap's (2 x 2 box averages, 8 bits per level)"""
    def __init__(s, img):
        lv = [np.asarray(img, np.uint8)]
        while lv[-1].shape[0] > 1 or lv[-1].shape[1] > 1:
            a = lv[-1].astype(np.float64); h, w = a.shape[:2]
            if h > 1: a = (a[0:h - h % 2:2] + a[1:h:2]) / 2
            if w > 1: a = (a[:, 0:w - w % 2:2] + a[:, 1:w:2]) / 2
            lv.append(np.floor(a + 0.5).astype(np.uint8))
        s.lv = {k: v.astype(np.float64) / 255.0 for k, v in enumerate(lv)}; s.n = len(lv)
        s.t = dict(w=lv[0].shape[1], h=lv[0].shape[0])

def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3.0 - 2.0 * t)

def build(size=1024, ss=2):
    N = size * ss; img = np.zeros((N * N, 4)); depth = np.full(N * N, 2.0)
    ms = static_meshes(SO.SD + 'RIFLESCOPE.nif')
    ex = SO.EYECUP_X - SO.EYE_D
    V = [[0.0, -1.0, 0.0, 0.0], [0.0, 0.0, 1.0, -SO.AXIS_Z], [-1.0, 0.0, 0.0, ex], [0.0, 0.0, 0.0, 1.0]]
    t = SO.HALF * 720 / SO.F720; n_, f_ = 1.0, 100.0
    P = [[1 / t, 0.0, 0.0, 0.0], [0.0, 1 / t, 0.0, 0.0], [0.0, 0.0, (f_ + n_) / (n_ - f_), 2 * f_ * n_ / (n_ - f_)], [0.0, 0.0, -1.0, 0.0]]
    MVP = GV.mmul(P, V); MVP = [[float(np.float32(v)) for v in row] for row in MVP]   # (uploaded as float32)
    ldir = (-0.35, 0.75, 0.56); rin = 1.72 * SO.F720 / SO.EYE_D / (SO.HALF * 720); gain = SO.BODY_GAIN

    def corners(m, keep=None):
        """per triangle corner: 0-3 clip, 4-5 uv, 6-8 object position, 9-10 clip xy / w, 11 shade"""
        I = m['I'].reshape(-1, 3)
        if keep is not None: I = I[keep]
        Pm = m['P'].astype(np.float64); Nm = m['N'].astype(np.float64); UV = m['UV'].astype(np.float64)
        cx, cy, cz, cw = GV.xform(MVP, Pm[:, 0], Pm[:, 1], Pm[:, 2])
        # sh = 0.40 + 0.75 clamp(dot(normalize(nm nrm + (0, 0, 1e-4)), ldir)); nm = the view's rotation
        nx = V[0][0] * Nm[:, 0] + V[0][1] * Nm[:, 1] + V[0][2] * Nm[:, 2]; ny = V[1][0] * Nm[:, 0] + V[1][1] * Nm[:, 1] + V[1][2] * Nm[:, 2]
        nz = V[2][0] * Nm[:, 0] + V[2][1] * Nm[:, 1] + V[2][2] * Nm[:, 2] + 0.0001; ln = np.sqrt(nx * nx + ny * ny + nz * nz)
        sh = 0.40 + 0.75 * np.clip((nx * ldir[0] + ny * ldir[1] + nz * ldir[2]) / ln, 0.0, 1.0)
        Vx = np.stack([cx, cy, cz, cw, UV[:, 0], UV[:, 1], Pm[:, 0], Pm[:, 1], Pm[:, 2], cx / cw, cy / cw, sh], 1)
        return GV.clip_near(Vx[I])

    def frags(A):
        idx, x, y, z, area = GV.setup(A, None, W=N, H=N)
        for T, pix, l1, l2, d in GV.raster(idx, x, y, z, area, W=N, H=N):
            yield x, y, T, pix, l1, l2, d

    body = ArrTex(Image.open(SO.SD + 'RifeScope_D.dds').convert('RGBA'))
    # opaque body (lit 1): depth test LESS (the first of equal depths stays), the eyecup-opening discard before it
    for k, m in enumerate(ms):
        if (m['tex'] or '') != 'RifeScope_D.dds' or k == 1: continue
        P_ = m['P']; I = m['I'].reshape(-1, 3); C = P_[I].mean(1); rc = np.hypot(C[:, 1], C[:, 2] - SO.AXIS_Z)
        A = corners(m, keep=~(rc < 1.6))
        for x, y, T, pix, l1, l2, d in frags(A):
            sp = GV.interp(A, x, y, T, l1, l2, [9, 10]); lp = GV.interp(A, x, y, T, l1, l2, [6])[:, 0]
            r = np.sqrt(sp[:, 0] * sp[:, 0] + sp[:, 1] * sp[:, 1])
            ok = ~((r < rin) & (lp > -13.35)); T, pix, l1, l2, d, r = T[ok], pix[ok], l1[ok], l2[ok], d[ok], r[ok]
            if not len(T): continue
            o = np.lexsort((T, d, pix)); T, pix, l1, l2, d, r = T[o], pix[o], l1[o], l2[o], d[o], r[o]
            first = np.r_[True, pix[1:] != pix[:-1]]; T, pix, l1, l2, d, r = T[first], pix[first], l1[first], l2[first], d[first], r[first]
            up = d < depth[pix]; T, pix, l1, l2, d, r = T[up], pix[up], l1[up], l2[up], d[up], r[up]
            if not len(T): continue
            u, v, r2 = GV.tex_coords(A, x, y, T, l1, l2, 4, body); c = body.sample(u, v, r2)
            sh = GV.interp(A, x, y, T, l1, l2, [11])[:, 0]; g = 0.7 + (gain - 0.7) * smoothstep(0.76, 0.82, r)
            rgb = np.minimum(c[:, :3] * (sh * g)[:, None], 1.0)
            img[pix] = GV.q8(np.c_[rgb, np.ones(len(pix))]); depth[pix] = d
    # blended layers in order (depth test LESS, no depth writes): colour SRC_ALPHA / ONE_MINUS_SRC_ALPHA, alpha ONE / ONE_MINUS_SRC_ALPHA
    for name, path, lit, am in (('R_SC_CROSS.dds', SO.SD + 'R_SC_CROSS.dds', 0, 1.0), ('LENS_02_D.dds', SO.TEXD + 'LENS_02_D.dds', 2, 0.75), ('LENS_01_D.dds', SO.TEXD + 'LENS_01_D.dds', 2, 1.0)):
        for m in ms:
            if (m['tex'] or '').lower() != name.lower(): continue
            tx = ArrTex(Image.open(path).convert('RGBA')); A = corners(m); F = []
            for x, y, T, pix, l1, l2, d in frags(A):
                ok = d < depth[pix]; T, pix, l1, l2 = T[ok], pix[ok], l1[ok], l2[ok]
                if not len(T): continue
                u, v, r2 = GV.tex_coords(A, x, y, T, l1, l2, 4, tx); c = tx.sample(u, v, r2)
                rgb = c[:, :3] if lit == 0 else np.zeros((len(T), 3)); F.append((pix, T, rgb, c[:, 3] * am))
            if not F: continue
            pix, T, rgb, a = (np.concatenate([f[i] for f in F]) for i in range(4))
            o = np.lexsort((T, pix)); pix, rgb, a = pix[o], np.clip(rgb[o], 0, 1), np.clip(a[o], 0, 1)
            first = np.r_[True, pix[1:] != pix[:-1]]; grp = np.cumsum(first) - 1; rank = np.arange(len(pix)) - np.nonzero(first)[0][grp]
            for rk in range(int(rank.max()) + 1):
                sel = rank == rk; p = pix[sel]; al = a[sel][:, None]; dst = img[p]
                img[p] = GV.q8(np.c_[rgb[sel] * al + dst[:, :3] * (1 - al), al + dst[:, 3:4] * (1 - al)])
    a = np.floor(img * 255.0 + 0.5).astype(np.uint8).reshape(N, N, 4)[::-1]
    return Image.fromarray(np.ascontiguousarray(a), 'RGBA').resize((size, size), Image.LANCZOS)
