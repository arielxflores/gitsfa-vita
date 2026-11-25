# A software look at a menu model (chr_<..>_hi.gvm, or an in-game chr_<..>.gvm) in a pose of the out-of-game bank
# (chr_og_m.gab / chr_og_f.gab: '<operator>.choice' = the OPERATIVE page's pose, 'result.res_lose_idle' ...) with the
# ENGINE'S shading: sh = 0.40 + 0.75 sat(n.l), light (0.30, -0.70, 0.65), tint 1.1 (model.h MD_VS, main.c chr_preview).
# b103: this is what showed Saito's dark face off the device - vm/preview_chr.py's softer shading (0.5 + 0.5 n.l, the idle
# pose) had hidden it. Head and shoulders, one tile per moment of the clip.
# usage: python3 preview_og.py <model.gvm> <textures' model.gvm> <bank.gab> <clip> <out.png> [0..1 moments of the clip ...]
import sys, os, numpy as np
H_ = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, H_); sys.path.insert(0, os.path.join(H_, '..', 'tools', 'anim'))
import preview, preview_chr, gabview as g
from PIL import Image
def skinned_pose(H, G, clip, fr):
    d, bones, par, clips = G; P, R = g.pose(G, clip, fr); out = []
    M = np.zeros((len(H['pal']), 3, 4))
    for e, (s, S) in enumerate(H['pal']):
        b = bones.index(H['bones'][s]); W = np.c_[R[b], P[b]]; S4 = np.r_[S, [[0, 0, 0, 1]]]; M[e] = W @ S4
    for m in H['meshes']:
        v = m['v']; Pv = np.c_[v['p'], np.ones(len(v))]; bw = v['bw'] / 255.0; acc = np.zeros((len(v), 3)); nacc = np.zeros((len(v), 3))
        for j in range(4):
            Mj = M[v['bi'][:, j].astype(int) + m.get('base', 0)]; acc += bw[:, j:j + 1] * np.einsum('nij,nj->ni', Mj, Pv); nacc += bw[:, j:j + 1] * np.einsum('nij,nj->ni', Mj[:, :, :3], v['n'][:, :3] / 127.0)
        out.append((acc, nacc / (np.linalg.norm(nacc, axis=1, keepdims=True) + 1e-9)))
    return out
def view(H, SK, texs, box, W, Hh):
    img = np.full((Hh, W, 3), 0.16); zb = np.full((Hh, W), 1e9); L = np.array([0.30, -0.70, 0.65]); L /= np.linalg.norm(L); texc = {}
    for m, (X, N) in zip(H['meshes'], SK):
        if m['tex'] not in texc: w, h, fmt, mm = texs[m['tex']]; texc[m['tex']] = preview.decode_dxt(w, h, fmt, mm[0]) / 255.0
        T = texc[m['tex']]; th, tw = T.shape[:2]; sx = (-X[:, 0] - box[0]) / (box[1] - box[0]) * W; sy = (box[3] - X[:, 2]) / (box[3] - box[2]) * Hh; dep = X[:, 1]
        sh = 0.40 + 0.75 * np.clip(N @ L, 0, 1); UV = m['v']['uv']
        for a, b, c in m['I'].reshape(-1, 3):
            x0 = int(max(0, np.floor(min(sx[a], sx[b], sx[c])))); x1 = int(min(W - 1, np.ceil(max(sx[a], sx[b], sx[c])))); y0 = int(max(0, np.floor(min(sy[a], sy[b], sy[c])))); y1 = int(min(Hh - 1, np.ceil(max(sy[a], sy[b], sy[c]))))
            if x0 > x1 or y0 > y1: continue
            d = (sy[b] - sy[c]) * (sx[a] - sx[c]) + (sx[c] - sx[b]) * (sy[a] - sy[c])
            if abs(d) < 1e-9: continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            l0 = ((sy[b] - sy[c]) * (xs - sx[c]) + (sx[c] - sx[b]) * (ys - sy[c])) / d; l1 = ((sy[c] - sy[a]) * (xs - sx[c]) + (sx[a] - sx[c]) * (ys - sy[c])) / d; l2 = 1 - l0 - l1
            ins = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not ins.any(): continue
            z = l0 * dep[a] + l1 * dep[b] + l2 * dep[c]; uv = l0[..., None] * UV[a] + l1[..., None] * UV[b] + l2[..., None] * UV[c]
            px = T[np.clip((uv[..., 1] % 1 * th).astype(int), 0, th - 1), np.clip((uv[..., 0] % 1 * tw).astype(int), 0, tw - 1)]
            ok = ins & (z < zb[y0:y1 + 1, x0:x1 + 1]); s = l0 * sh[a] + l1 * sh[b] + l2 * sh[c]
            zb[y0:y1 + 1, x0:x1 + 1][ok] = z[ok]; img[y0:y1 + 1, x0:x1 + 1][ok] = (px[..., :3] * s[..., None] * 1.1)[ok]
    return img
if __name__ == '__main__':
    hi, ig, bank, clip, out = sys.argv[1:6]; frs = [float(x) for x in sys.argv[6:]] or [0.5]
    H = preview.read(hi); Gm = preview.read(ig); G = g.load(bank); tiles = []
    for f in frs:
        nf = G[3][clip][0]; SK = skinned_pose(H, G, clip, int(f * (nf - 1))); allp = np.concatenate([x for x, _ in SK]); top = allp[:, 2].max(); cx = -allp[allp[:, 2] > top - 20][:, 0].mean()
        tiles.append(view(H, SK, Gm['texs'], (cx - 28, cx + 28, top - 50, top + 6), 392, 392))
    Image.fromarray((np.clip(np.concatenate(tiles, 1), 0, 1) * 255).astype(np.uint8)).save(out); print('ok', out)
