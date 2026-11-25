# Side-by-side check sheet: footage frame regions (left) next to the same regions of a host HUD render (right).
# usage: sidebyside.py out.png footage.jpg render.png [box ...]   box = x0,y0,x1,y1 in Vita px (960x544); zoom 2
import sys
from PIL import Image, ImageDraw
out, foot, rend = sys.argv[1:4]
boxes = [tuple(map(int, b.split(','))) for b in sys.argv[4:]] or [(0, 0, 200, 150), (300, 0, 660, 80), (0, 420, 200, 544), (760, 420, 960, 544), (720, 0, 960, 90), (840, 360, 960, 430)]
F = Image.open(foot).convert('RGB').resize((960, 544), Image.LANCZOS); R = Image.open(rend).convert('RGB')
z = 2; W = max(b[2] - b[0] for b in boxes) * z; H = sum((b[3] - b[1]) * z + 6 for b in boxes)
S = Image.new('RGB', (2 * W + 8, H), (255, 0, 255)); y = 0
for b in boxes:
    w, h = (b[2] - b[0]) * z, (b[3] - b[1]) * z
    S.paste(F.crop(b).resize((w, h), Image.LANCZOS), (0, y)); S.paste(R.crop(b).resize((w, h), Image.LANCZOS), (W + 8, y)); y += h + 6
S.save(out)
