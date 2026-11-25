#!/usr/bin/env python3
"""Before / after host pictures of the b38 half-precision grading (render b38): a map view graded as b37 drew it (float
LUT, or float polynomial on the polynomial maps) and as b38 draws it (the half tails), plus the difference x20.
usage: halfsheet.py <out.jpg> <stem>:<poly 0|1> ...   (BUILD /root/gits/build)"""
import sys, os, math
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mapvis'))
from gradeprec import lut_eval, lut_eval_half, poly_eval
from render import Renderer
from gcol import spawns
B = '/root/gits/build'
rows = []
for arg in sys.argv[2:]:
    stem, poly = arg.split(':'); poly = int(poly)
    raw = open('%s/data/%s.glut' % (B, stem), 'rb').read(); lut = np.frombuffer(raw[:12288], np.uint8).reshape(16, 256, 3)
    k = np.frombuffer(raw, '<f4', 80, 12296).reshape(2, 10, 4)[0]
    R = Renderer(W=480, H=272, ss=1); gs = '%s/maps/%s.gsky' % (B, stem); R.load(stem, lut=False, gsky=gs if os.path.exists(gs) else None)
    sp = spawns('%s/maps/%s.gcol' % (B, stem)); g, t, x, y, z, yw = sp[len(sp) // 3]
    fc = np.asarray(R.shot((x, y, z + 152), yw, math.radians(5))).astype(np.float64) / 255.0
    a = poly_eval(k, fc, np.float32) if poly else lut_eval(lut, fc)
    b = poly_eval(k, fc, np.float16) if poly else lut_eval_half(lut, fc)
    A = np.round(np.clip(a, 0, 1) * 255).astype(np.uint8); Bm = np.round(np.clip(b, 0, 1) * 255).astype(np.uint8)
    D = np.clip(np.abs(A.astype(int) - Bm.astype(int)) * 20, 0, 255).astype(np.uint8)
    e = np.abs(A.astype(int) - Bm.astype(int)).max(2)
    row = Image.new('RGB', (480 * 3, 272 + 18), (0, 0, 0)); row.paste(Image.fromarray(A), (0, 18)); row.paste(Image.fromarray(Bm), (480, 18)); row.paste(Image.fromarray(D), (960, 18))
    d = ImageDraw.Draw(row); d.text((4, 3), '%s: b37 float %s' % (stem, 'polynomial' if poly else 'LUT'), fill=(255, 255, 255)); d.text((484, 3), 'b38 half', fill=(255, 255, 255))
    d.text((964, 3), 'difference x20: mean %.3f, max %d steps of 255' % (e.mean(), e.max()), fill=(255, 255, 255)); rows.append(row)
    print(stem, 'mean %.3f max %d' % (e.mean(), e.max()), flush=True)
S = Image.new('RGB', (480 * 3, len(rows) * 290)); [S.paste(r, (0, i * 290)) for i, r in enumerate(rows)]; S.save(sys.argv[1], quality=90)
