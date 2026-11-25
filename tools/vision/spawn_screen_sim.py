# Host check of vision.h vis_spawn_screen (the respawn screen edge): the same quads / uvs / colours, additive, over a frame.
# usage: python3 spawn_screen_sim.py IN.jpg OUT.jpg [t]
import sys, os, numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import host_vis
_, _, _, hx = host_vis.vis_textures()
im = np.asarray(Image.open(sys.argv[1]).convert('RGB').resize((960, 544))).astype('f4') / 255; t = float(sys.argv[3]) if len(sys.argv) > 3 else 0.2
f = (1 - t / 1.8) ** 2; H, W = 544, 960
ys, xs = np.mgrid[0:H, 0:W]; nx = (xs + 0.5) / W * 2 - 1; ny = 1 - (ys + 0.5) / H * 2
out = im.copy()
# haze: quad x -1..1, y 0.25..1, uv 0..1 (v down), texture vy^2 * (0.55 + 0.45 ax^2)
m = ny >= 0.25; vv = (1 - ny) / 0.75; ax = np.abs(nx); hz = (1 - vv) ** 2 * (0.55 + 0.45 * ax * ax)
out += np.where(m, hz * 0.75 * f, 0)[..., None] * np.array([0.20, 0.70, 0.80])
# corner texture (vision.h vis_tex_corner), one quad per corner, 0.62 x 0.62 NDC, texel 0,0 at the corner
def corner_tex():
    T = np.zeros((64, 128))
    for y in range(64):
        for x in range(128):
            u = (x + 0.5) / 128; v = (y + 0.5) / 64; r = np.sqrt(u * u + v * v * 1.2); fo = min(max(1 - r / 0.95, 0), 1) ** 2
            d1 = d2 = 1e9
            for j in range(-1, 5):
                for i in range(-1, 8):
                    cx = i * 22 + (11 if j & 1 else 0); cy = j * 19; d = (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2
                    if d < d1: d2 = d1; d1 = d
                    elif d < d2: d2 = d
            e = np.sqrt(d2) - np.sqrt(d1); edge = 1.0 if e < 1.3 else 0.45 if e < 2.6 else 0
            T[y, x] = fo * (0.30 + 0.70 * edge)
    return T
CT = corner_tex(); add = np.zeros((H, W))
cu = (1 - np.abs(nx)) / 0.62; cv = (1 - np.abs(ny)) / 0.62; m = (cu < 1) & (cv < 1)
add = np.where(m, CT[np.clip((cv * 64).astype(int), 0, 63), np.clip((cu * 128).astype(int), 0, 127)], 0) * 0.75 * f
out += add[..., None] * np.array([0.25, 0.85, 0.95])
Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8)).save(sys.argv[2], quality=90)
