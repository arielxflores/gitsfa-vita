# b76: the lobby's TACHIKOMA tile picture (menu_fe.h LB_TANK, 'lb.tank' in menu.ghud): the game's own blue Think Tank
# (tk_blue.gvm from tools/tank/make_tank.py) rendered from the front, a little from above, textured and lit, cut out
# (RGBA) - the tile shows it over its own gradient. Software rasteriser (numpy), supersampled; nothing of the game is
# kept in the tools: the model is read from the data folder at build time.
# usage (test): python3 tank_icon.py <tk_blue.gvm> <out.png> [yaw degrees] [elevation degrees]
import os, sys, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'vm'))
from preview import read, palette_mats, decode_dxt

def _frame(G):
    """a still pose: the first frame of the idle clip (the first clip if there is none called idle)"""
    if not G['clips']: return None
    name = next((k for k in G['clips'] if 'idle' in k.lower()), next(iter(G['clips'])))
    return G['clips'][name][2][0]

def render(gvm, W=320, H=200, yaw=0.0, elev=14.0, ss=3, fov=24.0, light=(-0.45, -0.75, 0.6)):
    """RGBA uint8 (H, W, 4): the posed model framed to fill the picture (95 % of the width or height)"""
    G = read(gvm); fr = _frame(G)
    M = palette_mats(G, fr) if fr is not None else None
    # world-space vertices of every mesh (skinned with the pose)
    meshes = []
    for m in G['meshes']:
        if (m['blend'] & 15) == 2: continue   # (additive glows: not in a still picture)
        v = m['v']; P = np.c_[v['p'], np.ones(len(v))]; N = v['n'][:, :3].astype(np.float64) / 127.0
        if M is not None:
            bw = v['bw'] / 255.0; acc = np.zeros((len(v), 4)); nacc = np.zeros((len(v), 3))
            for j in range(4):
                Mj = M[v['bi'][:, j] + m.get('base', 0)]
                acc += bw[:, j:j + 1] * np.einsum('nij,nj->ni', Mj, P); nacc += bw[:, j:j + 1] * np.einsum('nij,nj->ni', Mj[:, :3, :3], N)
            X = acc[:, :3]; N = nacc
        else: X = P[:, :3]
        N = N / (np.linalg.norm(N, axis=1, keepdims=True) + 1e-9)
        meshes.append((m, X, N))
    allX = np.concatenate([x for _, x, _ in meshes])
    lo, hi = np.percentile(allX, 0.2, axis=0), np.percentile(allX, 99.8, axis=0); ctr = (lo + hi) / 2
    # camera: in front of the model (it faces -y), raised by elev, turned by yaw; looking at the centre
    ya, el = np.radians(yaw), np.radians(elev)
    fwd = np.array([-np.sin(ya) * np.cos(el), np.cos(ya) * np.cos(el), -np.sin(el)])   # camera -> model
    up0 = np.array([0, 0, 1.0]); rt = np.cross(fwd, up0); rt /= np.linalg.norm(rt); up = np.cross(rt, fwd)
    rad = np.linalg.norm(hi - lo) / 2; dist = rad / np.tan(np.radians(fov) / 2) * 1.05; eye = ctr - fwd * dist
    f = 1 / np.tan(np.radians(fov) / 2)
    def proj(X):
        d = X - eye; z = d @ fwd; return d @ rt / z * f, d @ up / z * f, z
    # framing: fit the projected model into 95 % of the picture
    px, py, _ = proj(allX); sx = (px.max() - px.min()) / 2; sy = (py.max() - py.min()) / 2; cx = (px.max() + px.min()) / 2; cy = (py.max() + py.min()) / 2
    asp = W / H; k = 0.95 / max(sx / asp, sy)
    WW, HH = W * ss, H * ss
    img = np.zeros((HH, WW, 3)); cov = np.zeros((HH, WW)); zb = np.full((HH, WW), 1e18)
    L = np.array(light, float); L /= np.linalg.norm(L)
    texc = {}
    for m, X, N in meshes:
        if m['tex'] not in texc:
            w, h, fmt, mm = G['texs'][m['tex']]; texc[m['tex']] = decode_dxt(w, h, fmt, mm[0]) / 255.0
        T = texc[m['tex']]; th, tw = T.shape[:2]
        px, py, z = proj(X)
        sxp = ((px - cx) * k / asp * 0.5 + 0.5) * WW; syp = (0.5 - (py - cy) * k * 0.5) * HH
        # light per vertex: wrapped key + a cool rim toward the viewer's edges + ambient
        ndl = N @ L; key = np.clip(ndl * 0.75 + 0.25, 0, 1); view = -(X - eye); view /= np.linalg.norm(view, axis=1, keepdims=True)
        rim = np.clip(1 - np.abs(np.sum(N * view, 1)), 0, 1) ** 3
        shade = 0.32 + 0.85 * key; rimc = rim[:, None] * np.array([0.25, 0.45, 0.55])
        for a, b, c in m['I'].reshape(-1, 3):
            if z[a] < 1 or z[b] < 1 or z[c] < 1: continue
            x0 = int(max(0, np.floor(min(sxp[a], sxp[b], sxp[c])))); x1 = int(min(WW - 1, np.ceil(max(sxp[a], sxp[b], sxp[c]))))
            y0 = int(max(0, np.floor(min(syp[a], syp[b], syp[c])))); y1 = int(min(HH - 1, np.ceil(max(syp[a], syp[b], syp[c]))))
            if x0 > x1 or y0 > y1: continue
            d = (syp[b] - syp[c]) * (sxp[a] - sxp[c]) + (sxp[c] - sxp[b]) * (syp[a] - syp[c])
            if abs(d) < 1e-9: continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            l0 = ((syp[b] - syp[c]) * (xs - sxp[c]) + (sxp[c] - sxp[b]) * (ys - syp[c])) / d
            l1 = ((syp[c] - syp[a]) * (xs - sxp[c]) + (sxp[a] - sxp[c]) * (ys - syp[c])) / d
            l2 = 1 - l0 - l1; ins = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not ins.any(): continue
            iz = l0 / z[a] + l1 / z[b] + l2 / z[c]; zz = 1 / iz
            yy, xx = np.nonzero(ins); zz = zz[ins]; yy = yy + y0; xx = xx + x0
            ok = zz < zb[yy, xx]
            if not ok.any(): continue
            yy, xx, zz = yy[ok], xx[ok], zz[ok]
            w0 = (l0[ins][ok] / z[a]) * zz; w1 = (l1[ins][ok] / z[b]) * zz; w2 = (l2[ins][ok] / z[c]) * zz
            uv = w0[:, None] * m['v']['uv'][a] + w1[:, None] * m['v']['uv'][b] + w2[:, None] * m['v']['uv'][c]
            tu = (np.mod(uv[:, 0], 1) * tw).astype(int) % tw; tv = (np.mod(uv[:, 1], 1) * th).astype(int) % th
            col = T[tv, tu, :3]
            sh = w0 * shade[a] + w1 * shade[b] + w2 * shade[c]; rc = w0[:, None] * rimc[a] + w1[:, None] * rimc[b] + w2[:, None] * rimc[c]
            img[yy, xx] = col * sh[:, None] + rc; zb[yy, xx] = zz; cov[yy, xx] = 1
    # down to the picture size (box filter over the supersamples; colour un-premultiplied by coverage)
    img = img.reshape(H, ss, W, ss, 3).mean((1, 3)); a = cov.reshape(H, ss, W, ss).mean((1, 3))
    col = np.where(a[..., None] > 1e-4, img / np.maximum(a[..., None], 1e-4), 0)
    return (np.dstack([np.clip(col, 0, 1), a]) * 255 + 0.5).astype(np.uint8)

if __name__ == '__main__':
    from PIL import Image
    yaw = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0; el = float(sys.argv[4]) if len(sys.argv) > 4 else 14.0
    Image.fromarray(render(sys.argv[1], yaw=yaw, elev=el)).save(sys.argv[2]); print(sys.argv[2])
