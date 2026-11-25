# ovl.ghud: loading screen (GameLoading.swf) + in-match overlays (InGameUI.swf / GameExitPopup.swf) for menu.h
import os as _os; HERE = _os.path.dirname(_os.path.abspath(__file__)); WORK = _os.environ.get('MENU_WORK') or _os.path.join(_os.environ.get('GITSFA_TMP') or _os.environ.get('TMPDIR') or '.', 'menu-ui'); _os.makedirs(WORK, exist_ok=True)
import sys; sys.path.insert(0, HERE)
from menulib import *
import gitspaths
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.path('DATA', 'ovl.ghud')
B = MB2()
B.screen_node(); B.stage_node()
B.add_image('white', np.ones((4, 4, 4), np.float32), (0, 0, 1, 1)); B.rects.add('white')
# ------------------------------------------------------------------ loading screen
print('loading')
gl = movie('GameLoading'); LC = 'loadingContainer'
dyn = [LC + '.' + k for k in ('imageContainer', 'bgFx', 'd1', 'modeIconMc', 'tipInfoMc', 'loadTXT', 'd39', 'mapInfoMc', 'gameModeMc', 'progressBar', 'tf_wait', 'gmInfo')]
B.auto('ld.bg', gl, LC, hide=hide_prefix(*dyn))
MODE_FR = {'tdm': 0, 'ttm': 4, 'dm': 5, 'gm': 8, 'em': 10}
for k, f in MODE_FR.items():
    B.mpart('ld.mode.' + k, gl, LC + '.modeIconMc', frames={LC + '.modeIconMc': f}, node='ld.modeicon')
for nm, p in (('ld.modename', 'gameModeMc.gameModeMc.tf_text'), ('ld.map', 'mapInfoMc.mapNameMc.tf_text'), ('ld.date', 'mapInfoMc.dateMc.tf_text'),
              ('ld.tip', 'mapInfoMc.tipMc.tf_text'), ('ld.status', 'loadTXT.txt'), ('ld.pct', 'progressBar.tf_percent'), ('ld.ptitle', 'progressBar.tf_title'),
              ('ld.wait', 'tf_wait'), ('ld.sktitle', 'tipInfoMc.skillTitleMc.tf_text'), ('ld.skmsg', 'tipInfoMc.skillMsg.tf_text'),
              ('ld.wpname', 'tipInfoMc.specialWeaponName.tf_text'), ('ld.wpmsg', 'tipInfoMc.specialWeaponMsg.tf_text')):
    B.mfield(nm, gl, LC + '.' + p)
B.mpart('ld.tiplabel', gl, LC + '.tipInfoMc.d2')
for f in range(9): B.mpart('ld.skico.%d' % f, gl, LC + '.tipInfoMc.skillIconMc', frames={LC + '.tipInfoMc.skillIconMc': f}, node='ld.skico')
for f in range(7): B.mpart('ld.wpico.%d' % f, gl, LC + '.tipInfoMc.weaponIconMc', frames={LC + '.tipInfoMc.weaponIconMc': f}, node='ld.wpico')
B.mpart('ld.pbg', gl, LC + '.progressBar.d1', scale=0.3); B.mpart('ld.bar', gl, LC + '.progressBar.bar')
B.mpart('ld.dia', gl, LC + '.d39.d1')
for i, d in enumerate((1, 3, 5, 7, 9, 11, 13, 15)): B.mnode('ld.dia%d' % i, gl, LC + '.d39.d%d' % d)
EXTRA = []
exec(open(HERE + '/ovl_ingame.py').read())
# Elimination's emblem as the footage shows it (round splash): the Western InGameUI atlas's silver emblem, in place of the
# GameLoading frame (a skull) - fitted into the box of the TDM emblem of the same part
from PIL import Image
_em = Image.open(gitspaths.path('UI', 'ui', 'GFx_DA', 'Data', 'InGameUI_I31.dds')).convert('RGBA').crop((699, 731, 829, 878))
for nm in ('ld.mode.em', 'sp.mode.em'):
    ref = B.images[nm.replace('.em', '.tdm')]; lx0, ly0, lx1, ly1 = ref['loc']; ph = ref['img'].shape[0]
    sc = min((lx1 - lx0) / _em.size[0], (ly1 - ly0) / _em.size[1]) * 0.92; w, h = _em.size[0] * sc, _em.size[1] * sc; cx, cy = (lx0 + lx1) / 2, (ly0 + ly1) / 2
    px = max(8, int(round(ph * h / (ly1 - ly0)))); im = _em.resize((max(8, int(round(px * _em.size[0] / _em.size[1]))), px), Image.LANCZOS)
    a = np.asarray(im, np.float32) / 255.0; a[..., :3] *= a[..., 3:4]
    B.images[nm]['img'] = a; B.images[nm]['loc'] = (cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
B.font(44)
a0, a1 = B.write(OUT)
print('wrote', OUT, 'atlas', a0.shape, 'font', a1.shape, 'images', len(B.images), 'nodes', len(B.nodes), 'fields', len(B.fields))
from PIL import Image
Image.fromarray((np.clip(a0, 0, 1) * 255).astype(np.uint8)).save(WORK + '/ovl_atlas0.png')
