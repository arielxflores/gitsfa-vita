# Lays the menus' 3D operator (menufig.c: engine/src/main.c chr_preview on the host rasterizer) into host menu captures
# (tools/menu/run_host.sh), between the backdrop and the UI as the game draws it - Vita3K draws no menu figures, so this is
# the check for them. Needs the captures twice: as usual and with MENU_BGONLY=1 (the backdrop alone).
# usage: python3 menufig.py <menufig binary> <data dir> <backdrop png dir> <full png dir> <out dir> <old|new> <chr> <gun> [screens]
#   chr: the model stem (motoko, bato, ...); gun: weapon.h short name (m4a1, l118, m249, ...); screens: default all below
import os, subprocess, sys, tempfile
import numpy as np
from PIL import Image

# rect (x, y, w, h) and cam (eye height, distance, look-at height, fov, yaw) of menu_fe_preview / menu_results_preview
LOBBY = ((626, 108, 326, 336), (105, 380, 95, 32, -12))
QUICK = ((660, 112, 292, 392), (105, 380, 95, 32, -12))
SCREENS = {'fe_lobby': LOBBY, 'fe_quick': QUICK, 'fe_dive_scan': QUICK, 'fe_match_balance': QUICK, 'fe_match_vote': QUICK,
           'fe_operatives': ((300, 112, 330, 310), (110, 360, 95, 34, -15)),
           'res_results': ((640, 112, 300, 336), (105, 330, 100, 36, -10)), 'res_progress': ((640, 112, 300, 336), (105, 330, 100, 36, -10)),
           'res_stats': ((640, 112, 300, 336), (105, 330, 100, 36, -10)), 'res_topop': ((0, 0, 620, 544), (150, 170, 140, 34, 15))}
FEMALE = {'motoko', 'maven', 'reiko', 'sitara'}

binp, data, bgdir, fulldir, out, mode, chr_, gun = sys.argv[1:9]
names = sys.argv[9:] or [n for n in SCREENS if os.path.exists(os.path.join(fulldir, n + '.png'))]
os.makedirs(out, exist_ok=True)
for n in names:
    (x, y, w, h), cam = SCREENS[n]
    with tempfile.TemporaryDirectory() as td:
        ppm = os.path.join(td, 'f.ppm')
        bank = os.path.join(data, 'chr_anim_%s.gab' % ('f' if chr_ in FEMALE else 'm'))
        w3 = os.path.join(data, 'w3_%s.gvm' % gun) if mode == 'new' else '-'
        r = subprocess.run([binp, os.path.join(data, 'chr_%s.gvm' % chr_), bank, gun, w3, mode, '2.0', str(x), str(y), str(w), str(h)] + [str(c) for c in cam] + [ppm],
                           capture_output=True, text=True)
        if r.returncode: sys.exit('menufig: ' + r.stderr)
        fig = np.asarray(Image.open(ppm).convert('RGB')).astype(np.int16); cov = np.asarray(Image.open(ppm + '.a')) > 0
    bg = np.asarray(Image.open(os.path.join(bgdir, n + '.png')).convert('RGB')).astype(np.int16)
    full = np.asarray(Image.open(os.path.join(fulldir, n + '.png')).convert('RGB')).astype(np.int16)
    ui = np.abs(full - bg).sum(axis=2) > 24          # what the screen draws over the backdrop (and so over the figure)
    o = bg.copy(); sub = o[y:y + h, x:x + w]; sub[cov] = fig[cov]
    o[ui] = full[ui]
    Image.fromarray(o.astype(np.uint8)).save(os.path.join(out, n + '.png'))
    print('%s: %s %s %s, %d figure pixels' % (n, chr_, gun if mode == 'new' else '(no gun)', mode, int(cov.sum())))
