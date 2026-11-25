# A software look at an operator model (chr_<op>[_<skin>].gvm, vm/make_chr.py) in its idle pose: front and side, the whole
# figure and the head - with the menu's high-detail model (chr_<...>_hi.gvm: mesh only, the in-game model lends its textures,
# as main.c chr_preview does) when one is given. b103: the looks of single skins on the OPERATIVE page (Saito STANDARD,
# Motoko PHOENIX) and the head parts the mask toggle hides.
# usage: python3 preview_chr.py <chr.gvm> <out.png> [hi.gvm|-] [hide=<flag bits hex>] [alpha=<cut 0..255, default 0: none>] [skip=<mesh,mesh: not drawn>]
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preview
from PIL import Image
def skinned(G, frame):
    M = preview.palette_mats(G, frame); out = []
    for m in G['meshes']:
        v = m['v']; P = np.c_[v['p'], np.ones(len(v))]; bw = v['bw'] / 255.0; acc = np.zeros((len(v), 4)); nacc = np.zeros((len(v), 3))
        for j in range(4):
            Mj = M[v['bi'][:, j] + m.get('base', 0)]; acc += bw[:, j:j + 1] * np.einsum('nij,nj->ni', Mj, P)
            nacc += bw[:, j:j + 1] * np.einsum('nij,nj->ni', Mj[:, :3, :3], v['n'][:, :3] / 127.0)
        out.append((acc[:, :3], nacc / (np.linalg.norm(nacc, axis=1, keepdims=True) + 1e-9)))
    return out
def view(G, SK, texs, box, W, H, side=0, hide=0, cut=0, lod=1, skip=()):
    """orthographic: front (the figure faces -y: the eye on the -y side) or side (the eye on the -x side); box = (x0, x1, z0, z1) cm"""
    img = np.full((H, W, 3), 0.16); zb = np.full((H, W), 1e9); L = np.array([0.3, -0.6, 0.6]); L /= np.linalg.norm(L); texc = {}
    for mi, (m, (X, N)) in enumerate(zip(G['meshes'], SK)):
        fl = m['flags']; lodbits = (m['blend'] >> 4) & 7
        if mi in skip: continue
        if (fl << 16) & hide: continue
        if lodbits and not (lodbits & lod): continue
        if m['tex'] not in texc: w, h, fmt, mm = texs[m['tex']]; texc[m['tex']] = preview.decode_dxt(w, h, fmt, mm[0]) / 255.0
        T = texc[m['tex']]; th, tw = T.shape[:2]; hx = (X[:, 1] if side else -X[:, 0]); dep = (X[:, 0] if side else X[:, 1])
        sx = (hx - box[0]) / (box[1] - box[0]) * W; sy = (box[3] - X[:, 2]) / (box[3] - box[2]) * H; sh = 0.5 + 0.5 * np.clip(N @ L, 0, 1); UV = m['v']['uv']
        for a, b, c in m['I'].reshape(-1, 3):
            x0 = int(max(0, np.floor(min(sx[a], sx[b], sx[c])))); x1 = int(min(W - 1, np.ceil(max(sx[a], sx[b], sx[c]))))
            y0 = int(max(0, np.floor(min(sy[a], sy[b], sy[c])))); y1 = int(min(H - 1, np.ceil(max(sy[a], sy[b], sy[c]))))
            if x0 > x1 or y0 > y1: continue
            d = (sy[b] - sy[c]) * (sx[a] - sx[c]) + (sx[c] - sx[b]) * (sy[a] - sy[c])
            if abs(d) < 1e-9: continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            l0 = ((sy[b] - sy[c]) * (xs - sx[c]) + (sx[c] - sx[b]) * (ys - sy[c])) / d; l1 = ((sy[c] - sy[a]) * (xs - sx[c]) + (sx[a] - sx[c]) * (ys - sy[c])) / d; l2 = 1 - l0 - l1
            ins = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not ins.any(): continue
            z = l0 * dep[a] + l1 * dep[b] + l2 * dep[c]; uv = l0[..., None] * UV[a] + l1[..., None] * UV[b] + l2[..., None] * UV[c]
            px = T[np.clip((uv[..., 1] % 1 * th).astype(int), 0, th - 1), np.clip((uv[..., 0] % 1 * tw).astype(int), 0, tw - 1)]
            ok = ins & (z < zb[y0:y1 + 1, x0:x1 + 1])
            if cut and px.shape[-1] == 4: ok &= px[..., 3] * 255 > cut
            s = l0 * sh[a] + l1 * sh[b] + l2 * sh[c]
            zb[y0:y1 + 1, x0:x1 + 1][ok] = z[ok]; img[y0:y1 + 1, x0:x1 + 1][ok] = (px[..., :3] * s[..., None])[ok]
    return img
def sheet(chr_path, hi_path=None, hide=0, cut=0, skip=()):
    G = preview.read(chr_path); fr = G['clips']['idle'][2][0]; texs = G['texs']; D = G
    if hi_path: D = preview.read(hi_path); D['pal'] = D['pal']   # (its own palette; the pose comes from the in-game model's idle: same skeleton)
    SK = skinned(D, fr); allp = np.concatenate([x for x, _ in SK]); top = allp[:, 2].max()
    hb = (-16, 16, top - 30, top + 2)
    tiles = [view(D, SK, texs, (-55, 55, -5, top + 8), 300, int(300 * (top + 13) / 110), 0, hide, cut, 1, skip), view(D, SK, texs, hb, 384, 384, 0, hide, cut, 1, skip), view(D, SK, texs, hb, 384, 384, 1, hide, cut, 1, skip)]
    Hm = max(t.shape[0] for t in tiles); tiles = [np.pad(t, ((0, Hm - t.shape[0]), (0, 0), (0, 0)), constant_values=0.1) for t in tiles]
    return np.concatenate(tiles, 1)
if __name__ == '__main__':
    hi = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] != '-' and '=' not in sys.argv[3] else None
    kv = dict(a.split('=') for a in sys.argv[3:] if '=' in a)
    im = sheet(sys.argv[1], hi, int(kv.get('hide', '0'), 16), int(kv.get('alpha', '0')), tuple(int(x) for x in kv.get('skip', '').split(',') if x))
    Image.fromarray((np.clip(im, 0, 1) * 255).astype(np.uint8)).save(sys.argv[2]); print('ok', sys.argv[2])
