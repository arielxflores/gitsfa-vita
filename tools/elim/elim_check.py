# Draws a .gelim over the map's EM radar image (world -> image affine from minimap.txt): zones (red), barriers (yellow),
# team spawns (cyan list 0 / orange list 1), safe points (green). usage: elim_check.py data_dir out.png
import sys, struct, numpy as np
from PIL import Image, ImageDraw
D = sys.argv[1]; OUT = sys.argv[2]
EM = {'urban': 'EM_urbancity', 'port': 'EM_Port', 'ghost_storage': 'EM_01'}
aff = {l.split()[0]: list(map(float, l.split()[2:8])) for l in open(D + '/minimap.txt') if not l.startswith('#') and len(l.split()) >= 8}
def load(p):
    d = open(p, 'rb').read(); ver = struct.unpack_from('<I', d, 4)[0]; n0, n1, nz, nb = struct.unpack_from('<4I', d, 8); o = 24
    sp = []
    for n in (n0, n1): sp.append(np.frombuffer(d, '<f4', n * 4, o).reshape(-1, 4)); o += n * 16
    zones = []
    for _ in range(nz):
        o += 24; nv, nt = struct.unpack_from('<II', d, o); o += 8
        V = np.frombuffer(d, '<f4', nv * 3, o).reshape(-1, 3); o += nv * 12; T = np.frombuffer(d, '<u2', nt * 3, o).reshape(-1, 3); o += nt * 6 + (2 if nt * 3 % 2 else 0)
        zones.append((V, T))
    bars = np.frombuffer(d, '<f4', nb * 12, o).reshape(-1, 4, 3); o += nb * 48
    safe = np.zeros((0, 3))
    if ver >= 2: ns = struct.unpack_from('<I', d, o)[0]; o += 4; safe = np.frombuffer(d, '<f4', ns * 3, o).reshape(-1, 3)
    return sp, zones, bars, safe
tiles = []
for st, em in EM.items():
    a, b, c, d_, e, f = aff[st]; k = 2.0
    def P(x, y): return ((a * x + b * y + c) * k, (d_ * x + e * y + f) * k)
    im = Image.open('/root/gits/ui/ui/map/%s.dds' % em).convert('RGBA'); bg = Image.new('RGBA', im.size, (25, 25, 30, 255)); bg.alpha_composite(im)
    im = bg.convert('RGB').resize((1200, 1200)); dr = ImageDraw.Draw(im, 'RGBA')
    sp, zones, bars, safe = load('%s/%s.gelim' % (D, st))
    for V, T in zones:
        for t in T: dr.polygon([P(*V[i][:2]) for i in t], fill=(255, 0, 0, 70), outline=(255, 60, 60, 160))
    for q in bars: dr.line([P(*q[0][:2]), P(*q[1][:2])], fill=(255, 230, 0, 255), width=5)
    for k_, L in enumerate(sp):
        for s in L:
            x, y = P(s[0], s[1]); dr.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(0, 220, 255) if k_ == 0 else (255, 150, 0))
            dr.line([(x, y), P(s[0] + 160 * np.cos(s[3]), s[1] + 160 * np.sin(s[3]))], fill=(255, 255, 255), width=2)
    for s in safe: x, y = P(s[0], s[1]); dr.rectangle([x - 4, y - 4, x + 4, y + 4], fill=(40, 255, 80))
    dr.text((10, 10), st, fill=(255, 255, 0)); tiles.append(im.resize((600, 600)))
S = Image.new('RGB', (600 * len(tiles), 600))
for i, t in enumerate(tiles): S.paste(t, (600 * i, 0))
S.save(OUT); print('ok', OUT)
