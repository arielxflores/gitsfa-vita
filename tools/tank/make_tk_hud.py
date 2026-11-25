#!/usr/bin/env python3
# tk_hud.gtx: the pictures of the Think Tank HUD panels (ttm.h ttm_panel; footage: a picture of the tank in each panel, the blue
# one on a grey-blue ground, the black / red-eyed one on a dark red ground). GTX1 DXT1 256 x 256: cell 0 (blue model) at
# (0, 0)-(248, 112), cell 1 (red model) at (0, 128)-(248, 240). The pictures are our own models rendered by the host test
# (tools/tank/host/run.sh with MODE=portraits writes port<k>_b.ppm / port<k>_w.ppm: on black and on white -> alpha).
# usage: make_tk_hud.py <dir with the port*.ppm> [out dir]
import sys, os, struct, numpy as np, etcpak
from PIL import Image
src = sys.argv[1]; out = sys.argv[2]
CW, CH = 248, 112
GROUND = [((0.10, 0.17, 0.24), (0.27, 0.38, 0.47)), ((0.20, 0.04, 0.05), (0.50, 0.12, 0.13))]   # bottom-left -> top-right
atlas = np.zeros((256, 256, 3), np.float32)
for k in (0, 1):
    b = np.asarray(Image.open(os.path.join(src, 'port%d_b.ppm' % k)).convert('RGB'), np.float32) / 255
    w = np.asarray(Image.open(os.path.join(src, 'port%d_w.ppm' % k)).convert('RGB'), np.float32) / 255
    a = np.clip(1 - (w - b).mean(2), 0, 1); col = np.where(a[..., None] > 1e-3, b / np.maximum(a[..., None], 1e-3), 0)
    ys, xs = np.nonzero(a > 0.5); x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    # fill the cell's width with 95 % of the tank, the picture's centre on the upper body (40 % down the tank)
    s = CW / ((x1 - x0) * 0.95); cy = y0 + (y1 - y0) * 0.42; cx = (x0 + x1) / 2
    big = Image.fromarray((np.dstack([np.clip(col, 0, 1), a]) * 255).astype(np.uint8), 'RGBA')
    big = big.resize((int(big.width * s + 0.5), int(big.height * s + 0.5)), Image.LANCZOS)
    ox, oy = int(cx * s - CW / 2), int(cy * s - CH / 2)
    cell = np.asarray(big.crop((ox, oy, ox + CW, oy + CH)), np.float32) / 255
    g0, g1 = np.array(GROUND[k][0]), np.array(GROUND[k][1]); u = np.linspace(0, 1, CW)[None, :, None]; v = np.linspace(1, 0, CH)[:, None, None]
    ground = g0 + (g1 - g0) * (0.6 * u + 0.4 * v)
    pic = cell[..., :3] * cell[..., 3:4] + ground * (1 - cell[..., 3:4])
    atlas[128 * k:128 * k + CH, :CW] = pic
img = (np.clip(atlas, 0, 1) * 255).astype(np.uint8)
Image.fromarray(img).save(os.path.join(src, 'tk_hud.png'))
dxt = etcpak.compress_to_dxt1(np.ascontiguousarray(np.dstack([img, np.full((256, 256), 255, np.uint8)])).tobytes(), 256, 256)
p = os.path.join(out, 'tk_hud.gtx')
with open(p, 'wb') as f: f.write(b'GTX1' + struct.pack('<HHBBHI', 256, 256, 1, 0, 0, len(dxt))); f.write(dxt)
print(p, os.path.getsize(p), 'bytes')
