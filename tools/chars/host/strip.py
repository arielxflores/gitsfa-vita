# motion strip of chr_test renders: python3 strip.py out.png gvm bank set scenario t0 dt n yaw dist h [cols] (WEAPON env: gun model)
import sys, subprocess, os
from PIL import Image, ImageDraw
out, gvm, bank, aset, sc = sys.argv[1:6]; t0, dt = float(sys.argv[6]), float(sys.argv[7]); n = int(sys.argv[8]); yaw, dist, h = sys.argv[9:12]
cols = int(sys.argv[12]) if len(sys.argv) > 12 else 6
H = os.path.dirname(os.path.abspath(__file__)); tmp = out + '.tmp.ppm'; W = os.environ.get('WEAPON', '')
tiles = []
for k in range(n):
    t = t0 + k * dt
    subprocess.run([H + '/chr_test', gvm, bank, aset, sc, '%.4f' % t, yaw, dist, h, tmp, '0'] + ([W] if W else []), capture_output=True, timeout=60)
    im = Image.open(tmp).convert('RGB').crop((120, 40, 520, 600)).resize((200, 280), Image.LANCZOS)
    ImageDraw.Draw(im).text((4, 4), '%.2f' % t, fill=(255, 255, 0)); tiles.append(im)
rows = (n + cols - 1) // cols; S = Image.new('RGB', (200 * cols, 280 * rows))
for i, im in enumerate(tiles): S.paste(im, (200 * (i % cols), 280 * (i // cols)))
S.save(out); os.remove(tmp)
