# Rasterises a capture from the host HUD test (tools/ui/host) like the HUD shader on the Vita (tools/menu/raster.py's
# method) and adds DXT5 textures (the per-map radar images). usage: raster_hud.py cap texdir out.png [background.jpg]
import sys, os, struct, numpy as np
from PIL import Image
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'menu'))
import raster as R
def dxt5(data, w, h):
    n = (w // 4) * (h // 4); blk = np.frombuffer(data, np.uint8, n * 16).reshape(n, 16)
    a0 = blk[:, 0].astype(np.float32); a1 = blk[:, 1].astype(np.float32)
    abits = np.zeros(n, np.uint64)
    for i in range(6): abits |= blk[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
    pal = np.zeros((n, 8), np.float32); pal[:, 0] = a0; pal[:, 1] = a1
    big = a0 > a1
    for i in range(1, 7): pal[:, 1 + i] = np.where(big, ((7 - i) * a0 + i * a1) / 7, pal[:, 1 + i])
    for i in range(1, 5): pal[:, 1 + i] = np.where(~big, ((5 - i) * a0 + i * a1) / 5, pal[:, 1 + i])
    pal[:, 6] = np.where(~big, 0, pal[:, 6]); pal[:, 7] = np.where(~big, 255, pal[:, 7])
    c0 = blk[:, 8].astype(np.uint32) | blk[:, 9].astype(np.uint32) << 8; c1 = blk[:, 10].astype(np.uint32) | blk[:, 11].astype(np.uint32) << 8
    cb = blk[:, 12].astype(np.uint32) | blk[:, 13].astype(np.uint32) << 8 | blk[:, 14].astype(np.uint32) << 16 | blk[:, 15].astype(np.uint32) << 24
    def rgb(c): return np.stack([((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31], 1).astype(np.float32)
    A, B = rgb(c0), rgb(c1); cp = np.stack([A, B, (2 * A + B) / 3, (A + 2 * B) / 3], 1)
    out = np.zeros((h, w, 4), np.float32); bx = np.arange(n) % (w // 4); by = np.arange(n) // (w // 4)
    for i in range(16):
        ai = ((abits >> np.uint64(3 * i)) & np.uint64(7)).astype(np.int64); ci = ((cb >> np.uint32(2 * i)) & 3).astype(np.int64)
        out[by * 4 + i // 4, bx * 4 + i % 4, :3] = cp[np.arange(n), ci]; out[by * 4 + i // 4, bx * 4 + i % 4, 3] = pal[np.arange(n), ai]
    return out.astype(np.uint8)
_old = R.tex
def tex(dirp, tid):
    if tid in R._texcache: return R._texcache[tid]
    p = os.path.join(dirp, 'tex%d.bin' % tid)
    if os.path.exists(p):
        d = open(p, 'rb').read(); w, h, fmt, size = struct.unpack_from('<4i', d, 0)
        if fmt == 0x83F3: R._texcache[tid] = dxt5(d[16:16 + size], w, h).astype(np.float32) / 255; return R._texcache[tid]
    return _old(dirp, tid)
R.tex = tex
if __name__ == '__main__':
    cap, texdir, out = sys.argv[1], sys.argv[2], sys.argv[3]; bg = None
    if len(sys.argv) > 4 and os.path.exists(sys.argv[4]): bg = np.asarray(Image.open(sys.argv[4]).convert('RGB').resize((960, 544)), np.float32) / 255 * 0.9
    img = R.raster(cap, texdir, bg=bg)
    Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).save(out)
