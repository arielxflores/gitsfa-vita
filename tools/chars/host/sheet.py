# contact sheet of chr_test renders: python3 sheet.py out.png gvm bank set "scenario:t:yaw:dist:h[:lod]" ...
import sys, subprocess, os
from PIL import Image, ImageDraw
out, gvm, bank, aset = sys.argv[1:5]; items = sys.argv[5:]
H = os.path.dirname(os.path.abspath(__file__)); tmp = out + '.tmp.ppm'; W = os.environ.get('WEAPON', '')
tiles = []
for it in items:
    p = it.split(':'); sc = p[0]; t = p[1] if len(p) > 1 else '1'; yaw = p[2] if len(p) > 2 else '30'; dist = p[3] if len(p) > 3 else '350'; h = p[4] if len(p) > 4 else '140'; lod = p[5] if len(p) > 5 else '0'
    if sc.startswith('slot'): sc = sc.replace('slot.', 'slot:')
    args = [H + '/chr_test', gvm, bank, aset, sc, t, yaw, dist, h, tmp, lod] + ([W] if W else [])
    r = subprocess.run(args, capture_output=True, text=True, timeout=60)
    im = Image.open(tmp).convert('RGB').resize((320, 320), Image.LANCZOS)
    ImageDraw.Draw(im).text((6, 4), '%s t=%s' % (sc, t), fill=(255, 255, 0)); tiles.append(im)
cols = min(6, len(tiles)); rows = (len(tiles) + cols - 1) // cols
S = Image.new('RGB', (320 * cols, 320 * rows), (0, 0, 0))
for i, im in enumerate(tiles): S.paste(im, (320 * (i % cols), 320 * (i // cols)))
S.save(out); os.remove(tmp)
