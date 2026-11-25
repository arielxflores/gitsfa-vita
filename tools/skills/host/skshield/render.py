# Rasterises the sprite-batch draws captured by the skills host test (SKSH_CAP=<dir>, tools/skills/host/sktest) over a
# background picture: 960 x 544, the same matrices and blending as on the Vita (additive SRC_ALPHA/ONE, alpha
# SRC_ALPHA/ONE_MINUS_SRC_ALPHA), texture = sk_shield.gtx (DXT5). No depth test (there is no world here).
# usage: python3 render.py cap_file sk_shield.gtx out.png [background.jpg]
import sys, struct, numpy as np, etcpak
from PIL import Image
W, H = 960, 544
def load_gtx(p):   # a .gtx (DXT5 decoded block by block) or the tool's uncompressed .png
    if p.endswith('.png'): return np.asarray(Image.open(p).convert('RGBA'), np.float32) / 255
    if p.endswith('.npy'): return np.load(p)
    d = open(p, 'rb').read(); w, h, fmt = struct.unpack_from('<HHB', d, 4); n = struct.unpack_from('<I', d, 12)[0]
    out = np.zeros((h, w, 4), np.float32); blk = d[16:16 + n]; bs = 16 if fmt == 2 else 8; k = 0
    for by in range(h // 4):
        for bx in range(w // 4):
            b = blk[k * bs:(k + 1) * bs]; k += 1; c = b[8:] if fmt == 2 else b
            c0, c1 = struct.unpack_from('<HH', c, 0); bits = struct.unpack_from('<I', c, 4)[0]
            def rgb(v): return np.array([(v >> 11) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31])
            p0, p1 = rgb(c0), rgb(c1); pal = [p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3] if (c0 > c1 or fmt == 2) else [p0, p1, (p0 + p1) / 2, np.zeros(3)]
            if fmt == 2:
                a0, a1 = b[0], b[1]; ab = int.from_bytes(b[2:8], 'little')
                ap = [a0, a1] + ([((6 - i) * a0 + (1 + i) * a1) / 7 for i in range(6)] if a0 > a1 else [((4 - i) * a0 + (1 + i) * a1) / 5 for i in range(4)] + [0, 255])
            for i in range(16):
                y, x = by * 4 + i // 4, bx * 4 + i % 4; out[y, x, :3] = pal[(bits >> (2 * i)) & 3] / 255
                out[y, x, 3] = (ap[(ab >> (3 * i)) & 7] / 255) if fmt == 2 else 1
    return out
def main():
    cap, gtx, out = sys.argv[1:4]; bg = sys.argv[4] if len(sys.argv) > 4 else None
    tex = load_gtx(gtx); th, tw = tex.shape[:2]
    img = np.asarray(Image.open(bg).convert('RGB').resize((W, H), Image.LANCZOS), np.float32) / 255 if bg else np.full((H, W, 3), 0.18, np.float32)
    d = open(cap, 'rb').read(); o = 0; nd = 0; nfrag = 0; byreg = {}
    yy, xx = np.mgrid[0:H, 0:W] + 0.5
    while o < len(d):
        texid, bs, bd, n = struct.unpack_from('<4i', d, o); o += 16
        M = np.frombuffer(d, '<f4', 16, o).reshape(4, 4); o += 64
        V = np.frombuffer(d, '<f4', n * 9, o).reshape(n, 9).astype(np.float64); o += n * 36; nd += 1
        P = np.c_[V[:, :3], np.ones(n)]; C = P @ M.T   # rows m0..m3 (glUniform4fv rows)
        add = bd == 1
        for i in range(0, n - 2, 3):
            c = C[i:i + 3]
            if (c[:, 3] <= 1e-3).any(): continue   # (no near clipping here: skip triangles crossing the eye plane)
            ndc = c[:, :3] / c[:, 3:4]; sx = (ndc[:, 0] * 0.5 + 0.5) * W; sy = (1 - (ndc[:, 1] * 0.5 + 0.5)) * H
            x0, x1 = int(max(0, np.floor(sx.min()))), int(min(W, np.ceil(sx.max()))); y0, y1 = int(max(0, np.floor(sy.min()))), int(min(H, np.ceil(sy.max())))
            if x0 >= x1 or y0 >= y1: continue
            px, py = xx[y0:y1, x0:x1], yy[y0:y1, x0:x1]
            den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
            if abs(den) < 1e-9: continue
            l0 = ((sy[1] - sy[2]) * (px - sx[2]) + (sx[2] - sx[1]) * (py - sy[2])) / den
            l1 = ((sy[2] - sy[0]) * (px - sx[2]) + (sx[0] - sx[2]) * (py - sy[2])) / den
            l2 = 1 - l0 - l1; m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not m.any(): continue
            nfrag += int(m.sum())   # every covered pixel is shaded and blended (the cost of the draw: fragments)
            tu, tv = V[i, 3] * tw, V[i, 4] * th   # which texture region (make_shield.py) -> per-layer fragments
            reg = ('H lines' if tu < 128 else 'B2 hit lines') if tv >= 256 else 'rim' if 192 <= tu < 256 and 128 <= tv < 144 else 'A hex' if tu < 128 else 'B noise' if tv < 128 else 'C glow' if tu < 192 else 'F white' if tv < 160 else 'E fill'
            byreg[reg] = byreg.get(reg, 0) + int(m.sum())
            w = 1 / c[:, 3]; q = l0 * w[0] + l1 * w[1] + l2 * w[2]   # perspective-correct
            def itp(k): return (l0 * V[i, k] * w[0] + l1 * V[i + 1, k] * w[1] + l2 * V[i + 2, k] * w[2]) / q
            u, v = itp(3), itp(4); col = np.stack([itp(5 + k) for k in range(4)], -1)
            ti = np.clip((u * tw - 0.5).astype(int), 0, tw - 1); tj = np.clip((v * th - 0.5).astype(int), 0, th - 1)
            s = tex[tj, ti] * col; a = s[..., 3:4] * m[..., None]
            region = img[y0:y1, x0:x1]
            if add: region += s[..., :3] * a
            else: region[:] = region * (1 - a) + s[..., :3] * a
    Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).save(out)
    print('%s %d draws, %d fragments = %.3f screens (%s)' % (out, nd, nfrag, nfrag / (W * H), ', '.join('%s %.2f' % (k, v / (W * H)) for k, v in sorted(byreg.items()))))
main()
