# Top-down check of the Terminal Conquest data of a map: navigation mesh (green), collision floor (grey), the terminals'
# capture areas (area_check.nif, outlined in the terminal's colour), terminal devices (big dots), side points (small), and
# the spawn lists of each active terminal (team 0 = cyan crosses, team 1 = red crosses).
# usage: python3 ttm_visual.py stem Fix_Name out.png
import sys, os, struct, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, '/root/work/tank/tools/skills'); sys.path.insert(0, '/root/work/tank/tools/minimap')
import ssf_ttm, sklib, gcol
from PIL import Image, ImageDraw
MAPS = '/root/gits/build/maps/'; FIX = '/root/gits/raw/dem/dem_x/map/FixedMap/'
def load_nav(path):
    d = open(path, 'rb').read(); n = struct.unpack_from('<I', d, 4)[0]
    return np.frombuffer(d, dtype=[('v', '<f4', (3, 3)), ('nb', '<i4', 3)], count=n, offset=8)['v'].copy()
def areas(fix):
    out = {}
    for g in sklib.static_parts(FIX + fix + '/area_check.nif', with_skinned=True):
        nm = g['node'] or g['name']
        if 'terminal' in (nm or '').lower(): out.setdefault(nm, []).append(g['P'][g['I'].reshape(-1, 3)])
    return {k: np.concatenate(v) for k, v in out.items()}
def main(stem, fix, outp):
    T = ssf_ttm.load(FIX + fix + '/SpawnSystem.ssf'); A = areas(fix)
    N = load_nav(MAPS + stem + '.gnav'); P = N.reshape(-1, 3)
    x0, x1 = P[:, 0].min() - 800, P[:, 0].max() + 800; y0, y1 = P[:, 1].min() - 800, P[:, 1].max() + 800
    res = max(x1 - x0, y1 - y0) / 900; W = int((x1 - x0) / res) + 1; H = int((y1 - y0) / res) + 1
    im = Image.new('RGB', (W, H), (20, 20, 24)); d = ImageDraw.Draw(im)
    uv = lambda x, y: ((x - x0) / res, (y1 - y) / res)
    for t in N: d.polygon([uv(p[0], p[1]) for p in t], fill=(30, 90, 40))
    cols = [(255, 220, 60), (60, 220, 255), (255, 120, 220), (180, 255, 120), (255, 160, 60)]
    for k, t in enumerate(T['term']):
        c = cols[k % len(cols)]; key = [a for a in A if a.lower() == t['name'].lower()]
        for tri in (A[key[0]] if key else []): d.polygon([uv(p[0], p[1]) for p in tri], outline=c)
        for s, sc in ((0, (80, 220, 255)), (1, (255, 80, 80))):
            for r in t['sp'][s]: u, v = uv(r[0], r[1]); d.line([u - 3, v - 3, u + 3, v + 3], fill=sc); d.line([u - 3, v + 3, u + 3, v - 3], fill=sc)
        for j, p in enumerate(t['pts']):
            u, v = uv(p[0], p[1]); r = 9 if j == 0 else 4; d.ellipse([u - r, v - r, u + r, v + r], fill=c if j == 0 else None, outline=c)
        u, v = uv(t['pts'][0][0], t['pts'][0][1]); d.text((u + 12, v - 8), '%s id%d -> %s' % (t['name'][-1], t['id'], t['link']), fill=c)
        if key: z = A[key[0]][:, :, 2]; print(t['name'], 'area z %.0f..%.0f, device z %.0f' % (z.min(), z.max(), t['pts'][0][2]))
    d.text((5, 5), '%s  start id %d  (team 0 spawns cyan, team 1 red; spawn lists drawn for every terminal)' % (stem, T['start']), fill=(255, 255, 255))
    im.save(outp); print(outp, im.size)
if __name__ == '__main__': main(*sys.argv[1:4])
