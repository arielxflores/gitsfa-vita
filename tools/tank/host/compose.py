#!/usr/bin/env python3
# tk_test.c output -> PNGs: each <name>.ppm (3D render) as PNG; each <name>.cap (HUD capture) rasterised with tools/menu/raster.py
# over <name>.ppm (or black); then the sheets (render | footage frame) listed in SHEETS.  usage: compose.py outdir raster.py
import sys, os, glob, importlib.util, numpy as np
from PIL import Image, ImageDraw
out, rp = sys.argv[1], sys.argv[2]
spec = importlib.util.spec_from_file_location('raster', rp); R = importlib.util.module_from_spec(spec); spec.loader.exec_module(R)
for p in sorted(glob.glob(os.path.join(out, '*.ppm'))):
    Image.open(p).save(p[:-4] + '.png')
for c in sorted(glob.glob(os.path.join(out, '*.cap'))):
    n = c[:-4]; bg = None
    if os.path.exists(n + '.ppm'): bg = np.asarray(Image.open(n + '.ppm').convert('RGB'), np.float32) / 255
    R._texcache.clear()
    img = R.raster(c, n + '.tex', bg=bg)
    Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).save(n + '_hud.png')
print('\n'.join(sorted(os.path.basename(p) for p in glob.glob(os.path.join(out, '*.png')))))
