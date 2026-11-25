# Rasterises a capture from the host build (tools/menu/host/vitaGL.h) exactly like the HUD shader on the Vita.
import sys, os, struct, numpy as np
from PIL import Image
def dxt1(data, w, h):
    out = np.zeros((h, w, 4), np.uint8); o = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            c0, c1, bits = struct.unpack_from('<HHI', data, o); o += 8
            def rgb(c): return np.array([((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31, 255])
            a, b = rgb(c0), rgb(c1)
            pal = [a, b, (2 * a + b) // 3, (a + 2 * b) // 3] if c0 > c1 else [a, b, (a + b) // 2, np.array([0, 0, 0, 0])]
            for i in range(16): out[by + i // 4, bx + i % 4] = pal[(bits >> (2 * i)) & 3]
    return out
_texcache = {}
def tex(dirp, tid):
    if tid in _texcache: return _texcache[tid]
    p = os.path.join(dirp, 'tex%d.bin' % tid)
    if not os.path.exists(p): _texcache[tid] = None; return None
    d = open(p, 'rb').read(); w, h, fmt, size = struct.unpack_from('<4i', d, 0); px = d[16:16 + size]
    if fmt == 4: img = np.frombuffer(px, np.uint8).reshape(h, w, 4)
    elif fmt == 3: img = np.concatenate([np.frombuffer(px, np.uint8).reshape(h, w, 3), np.full((h, w, 1), 255, np.uint8)], 2)
    elif fmt in (0x83F0, 0x83F1): img = dxt1(px, w, h)
    else: raise ValueError(fmt)
    _texcache[tid] = img.astype(np.float32) / 255.0; return _texcache[tid]
def sample(T, u, v):
    h, w = T.shape[:2]; x = np.clip(u * w - 0.5, 0, w - 1); y = np.clip(v * h - 0.5, 0, h - 1)
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int); x1 = np.minimum(x0 + 1, w - 1); y1 = np.minimum(y0 + 1, h - 1)
    fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
    return T[y0, x0] * (1 - fx) * (1 - fy) + T[y0, x1] * fx * (1 - fy) + T[y1, x0] * (1 - fx) * fy + T[y1, x1] * fx * fy
def raster(cap, texdir, W=960, H=544, bg=None):
    cv = np.zeros((H, W, 3), np.float32) if bg is None else bg.copy()
    d = open(cap, 'rb').read(); o = 0
    while o < len(d):
        tid, prem, n = struct.unpack_from('<3i', d, o); o += 12
        if tid == -1: o += (n + 3) // 4 * 4; continue   # ui b42: an element tag (tools/ui/host/hudtag.h)
        V = np.frombuffer(d, np.float32, n * 17, o).reshape(n, 17); o += n * 68
        T = tex(texdir, tid)
        for t in range(0, n, 3):
            v = V[t:t + 3]; w = v[:, 3]
            if np.any(w <= 0): continue
            sx = (v[:, 0] / w + 1) * 0.5 * W; sy = (1 - v[:, 1] / w) * 0.5 * H
            x0, x1 = int(max(np.floor(sx.min()), 0)), int(min(np.ceil(sx.max()), W)); y0, y1 = int(max(np.floor(sy.min()), 0)), int(min(np.ceil(sy.max()), H))
            if x1 <= x0 or y1 <= y0: continue
            ys, xs = np.mgrid[y0:y1, x0:x1] + 0.5
            den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
            if abs(den) < 1e-9: continue
            l0 = ((sy[1] - sy[2]) * (xs - sx[2]) + (sx[2] - sx[1]) * (ys - sy[2])) / den
            l1 = ((sy[2] - sy[0]) * (xs - sx[2]) + (sx[0] - sx[2]) * (ys - sy[2])) / den
            l2 = 1 - l0 - l1
            ins = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
            if not ins.any(): continue
            iw = l0 / w[0] + l1 / w[1] + l2 / w[2]
            def at(k): return (l0[..., None] * v[0, k] / w[0] + l1[..., None] * v[1, k] / w[1] + l2[..., None] * v[2, k] / w[2]) / iw[..., None]
            uv = at(slice(4, 8)); mul = at(slice(8, 12)); add = at(slice(12, 16)); thr = at(slice(16, 17))[..., 0]
            if T is None: c = np.ones(ys.shape + (4,), np.float32)
            else: c = sample(T, uv[..., 0], uv[..., 1]); r = sample(T, uv[..., 2], uv[..., 3])[..., 0]; c = c * np.clip((thr - r) * 200 + 0.5, 0, 1)[..., None]
            if not prem: c = np.concatenate([c[..., :3] * c[..., 3:4], c[..., 3:4]], -1)  # non-premultiplied textures (backgrounds)
            o_ = np.empty_like(c); o_[..., :3] = (c[..., :3] * mul[..., :3] + add[..., :3] * c[..., 3:4]) * mul[..., 3:4]; o_[..., 3] = c[..., 3] * mul[..., 3]
            o_[..., :3] = np.minimum(o_[..., :3], o_[..., 3:4])
            o_ = o_ * ins[..., None]
            reg = cv[y0:y1, x0:x1]; reg[:] = o_[..., :3] + reg * (1 - o_[..., 3:4])
    return cv
if __name__ == '__main__':
    cap, texdir, out = sys.argv[1], sys.argv[2], sys.argv[3]
    bg = None
    if len(sys.argv) > 4 and os.path.exists(sys.argv[4]):
        bg = np.asarray(Image.open(sys.argv[4]).convert('RGB').resize((960, 544)), np.float32) / 255 * 0.85
    img = raster(cap, texdir, bg=bg)
    Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).save(out)
