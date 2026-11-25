# a software picture of a .gsc scene (tools/fx/make_scene.py; engine/src/fxscene.h draws the same): frames at given times,
# side by side. Screen scenes through their own camera; world scenes through their camera track (lens 59 deg).
# usage: scene_view.py <scene.gsc> <out.png> <t0,t1,...> [width (default 480)] [columns]
import sys, struct, numpy as np
from PIL import Image

def dxt_decode(w, h, fmt, data):   # through PIL's DDS reader
    import io
    hd = struct.pack('<4s7I44x13I', b'DDS ', 124, 0x81007, h, w, len(data), 0, 1, 32, 4, struct.unpack('<I', b'DXT1' if fmt == 1 else b'DXT5')[0], 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
    return np.asarray(Image.open(io.BytesIO(hd + data)).convert('RGBA')).astype(np.float32) / 255.0
def load(path):
    d = open(path, 'rb').read(); assert d[:4] == b'GSC2'
    nf, fps, nm, nt, nv, ni, flags, _, fov = struct.unpack_from('<HHHHIIHHf', d, 4); view = np.frombuffer(d, '<f4', 12, 28).reshape(3, 4); o = 76; texs = []
    for _ in range(nt):
        w, h, fmt, wrap, _, size = struct.unpack_from('<HHBBHI', d, o); o += 12; texs.append((dxt_decode(w, h, fmt, d[o:o + size]), wrap)); o += size
    ms = [struct.unpack_from('<IIIIhhBBH', d, o + 24 * i) for i in range(nm)]; o += 24 * nm
    V = np.frombuffer(d, '<f4', nv * 6, o).reshape(nv, 6); o += nv * 24; I = np.frombuffer(d, '<u2', ni, o); o += ni * 2 + (2 if ni & 1 else 0)
    col = np.frombuffer(d, np.uint8, nf * nm * 4, o).reshape(nf, nm, 4); o += nf * nm * 4; mat = []; uv0 = []; uv1 = []
    for m in ms: n = nf if m[7] & 1 else 1; mat.append(np.frombuffer(d, '<f4', n * 12, o).reshape(n, 3, 4)); o += n * 48
    for m in ms: n = nf if m[7] & 2 else 1; uv0.append(np.frombuffer(d, '<f4', n * 6, o).reshape(n, 6)); o += n * 24
    for m in ms:
        if m[5] >= 0: n = nf if m[7] & 4 else 1; uv1.append(np.frombuffer(d, '<f4', n * 6, o).reshape(n, 6)); o += n * 24
        else: uv1.append(None)
    cam = np.frombuffer(d, '<f4', nf * 9, o).reshape(nf, 9) if flags & 1 else None
    return dict(nf=nf, fps=fps, fov=fov, view=view, texs=texs, ms=ms, V=V, I=I, col=col, mat=mat, uv0=uv0, uv1=uv1, cam=cam)
def sample(tex, uv):
    img, wrap = tex; h, w = img.shape[:2]; u = uv[..., 0]; v = uv[..., 1]
    if wrap: u = u % 1.0; v = v % 1.0
    x = np.clip((u * w).astype(int), 0, w - 1); y = np.clip((v * h).astype(int), 0, h - 1); return img[y, x]
def clip_near(P, U, near):   # one triangle in clip space (3 x 4), uv (3 x 4: both sets): polygons with w > near
    out = [(P[i], U[i]) for i in range(3)]; res = []
    for i in range(len(out)):
        a, b = out[i], out[(i + 1) % len(out)]; ia, ib = a[0][3] > near, b[0][3] > near
        if ia: res.append(a)
        if ia != ib: k = (near - a[0][3]) / (b[0][3] - a[0][3]); res.append((a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k))
    return res
def render(S, t, W=480):
    H = int(round(W * 544 / 960)); f = min(S['nf'] - 1, int(t * S['fps'])); img = np.zeros((H, W, 3), np.float32)
    if S['cam'] is not None:
        e, d, u = S['cam'][f][:3], S['cam'][f][3:6], S['cam'][f][6:9]; r = np.cross(d, u); r /= np.linalg.norm(r); u = np.cross(r, d)
        view = np.zeros((3, 4)); view[0, :3] = r; view[1, :3] = u; view[2, :3] = -d; view[:, 3] = -view[:, :3] @ e; fov = 59.0
    elif S.get('manual'):
        e, at, u, fov = S['manual']; d = np.array(at, float) - e; d /= np.linalg.norm(d); r = np.cross(d, u); r /= np.linalg.norm(r); u = np.cross(r, d)
        view = np.zeros((3, 4)); view[0, :3] = r; view[1, :3] = u; view[2, :3] = -d; view[:, 3] = -view[:, :3] @ np.array(e, float)
    else: view = S['view']; fov = S['fov']
    th = 1 / np.tan(np.radians(fov) / 2); asp = 960 / 544; V4 = np.vstack([view, [0, 0, 0, 1]])
    for i, m in enumerate(S['ms']):
        v0, nv, i0, ni, t0, t1, add, anim, _ = m; c = S['col'][f, i].astype(np.float32) / 255
        if c[3] <= 0 or ni == 0: continue
        M = np.vstack([S['mat'][i][f if anim & 1 else 0], [0, 0, 0, 1]]); P = (V4 @ M @ np.c_[S['V'][v0:v0 + nv, :3], np.ones(nv)].T).T   # camera space
        q = np.c_[S['V'][v0:v0 + nv, 3:5], np.ones(nv)]; a = S['uv0'][i][f if anim & 2 else 0]; uva = np.c_[q @ a[:3], q @ a[3:]]
        uvb = uva
        if t1 >= 0: b = S['uv1'][i][f if anim & 4 else 0]; uvb = np.c_[q @ b[:3], q @ b[3:]]
        clip = np.c_[P[:, 0] * th / asp, P[:, 1] * th, P[:, 2], -P[:, 2]]; U = np.c_[uva, uvb, S['V'][v0:v0 + nv, 5]]; idx = S['I'][i0:i0 + ni].reshape(-1, 3)
        for tri in idx:
            poly = clip_near(clip[tri], U[tri], 2.0)
            for k in range(1, len(poly) - 1):
                A, B, C = poly[0], poly[k], poly[k + 1]; pa, pb, pc = [np.array([(p[0][0] / p[0][3] * 0.5 + 0.5) * W, (0.5 - p[0][1] / p[0][3] * 0.5) * H]) for p in (A, B, C)]
                x0 = int(max(0, np.floor(min(pa[0], pb[0], pc[0])))); x1 = int(min(W, np.ceil(max(pa[0], pb[0], pc[0])))); y0 = int(max(0, np.floor(min(pa[1], pb[1], pc[1])))); y1 = int(min(H, np.ceil(max(pa[1], pb[1], pc[1]))))
                if x1 <= x0 or y1 <= y0: continue
                den = (pb[1] - pc[1]) * (pa[0] - pc[0]) + (pc[0] - pb[0]) * (pa[1] - pc[1])
                if abs(den) < 1e-9: continue
                ys, xs = np.mgrid[y0:y1, x0:x1]; xs = xs + 0.5; ys = ys + 0.5
                l0 = ((pb[1] - pc[1]) * (xs - pc[0]) + (pc[0] - pb[0]) * (ys - pc[1])) / den; l1 = ((pc[1] - pa[1]) * (xs - pc[0]) + (pa[0] - pc[0]) * (ys - pc[1])) / den; l2 = 1 - l0 - l1
                ins = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
                if not ins.any(): continue
                iw = l0 / A[0][3] + l1 / B[0][3] + l2 / C[0][3]; uv = (l0[..., None] * A[1] / A[0][3] + l1[..., None] * B[1] / B[0][3] + l2[..., None] * C[1] / C[0][3]) / iw[..., None]
                px = sample(S['texs'][t0], uv[..., :2]) if t0 >= 0 else np.ones(uv.shape[:2] + (4,), np.float32)
                if t1 >= 0: px = px * sample(S['texs'][t1], uv[..., 2:4])
                px = px * c; px[..., 3] = px[..., 3] * uv[..., 4]; al = (px[..., 3] * ins)[..., None]; dst = img[y0:y1, x0:x1]
                img[y0:y1, x0:x1] = dst + px[..., :3] * al if add else dst * (1 - al) + px[..., :3] * al
    return np.clip(img, 0, 1)
if __name__ == '__main__':
    import os
    if os.environ.get('EYE'):   # a camera by hand for scenes without one: EYE=x,y,z AT=x,y,z [UP=x,y,z] [FOV=deg]
        S0 = None
    S = load(sys.argv[1])
    if os.environ.get('EYE'): S['manual'] = ([float(x) for x in os.environ['EYE'].split(',')], [float(x) for x in os.environ['AT'].split(',')], [float(x) for x in os.environ.get('UP', '0,0,1').split(',')], float(os.environ.get('FOV', '50')))
    ts = [float(x) for x in sys.argv[3].split(',')]; W = int(sys.argv[4]) if len(sys.argv) > 4 else 480; cols = int(sys.argv[5]) if len(sys.argv) > 5 else len(ts)
    tiles = [render(S, t, W) for t in ts]
    while len(tiles) % cols: tiles.append(np.zeros_like(tiles[0]))
    Image.fromarray((np.concatenate([np.concatenate(tiles[i:i + cols], 1) for i in range(0, len(tiles), cols)], 0) * 255).astype(np.uint8)).save(sys.argv[2]); print('ok', sys.argv[2])
