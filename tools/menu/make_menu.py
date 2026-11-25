# menu.ghud: art for the front-end screens (engine/src/menu_fe.h) and the post-match screens (menu.h), from the game's
# own SWFs (ui2 GFx_DA) and the Western build's texture atlases (ui/GFx_DA/Data/*.dds, the art of the footage's top bar and
# emblems). Panels, rows, tabs and buttons are drawn procedurally by menu_ui.h; this pack holds pictures and the font.
import os as _os; HERE = _os.path.dirname(_os.path.abspath(__file__)); WORK = _os.environ.get('MENU_WORK') or _os.path.join(_os.environ.get('GITSFA_TMP') or _os.environ.get('TMPDIR') or '.', 'menu-ui'); _os.makedirs(WORK, exist_ok=True)
import sys; sys.path.insert(0, HERE)
from menulib import *
from PIL import Image
OUT = sys.argv[1] if len(sys.argv) > 1 else _os.path.join(gitspaths.root('DATA'), 'menu.ghud')
B = MB2()
B.screen_node(); B.stage_node()
B.add_image('white', np.ones((4, 4, 4), np.float32), (0, 0, 1, 1)); B.rects.add('white')
STG = B.nodes['stage'][0]
def snode(name, x, y, s=1.0):
    """node in stage units (1600 x 906.7): origin at (x, y), uniform scale s"""
    B.nodes[name] = (STG @ np.array([[s, 0, x], [0, s, y], [0, 0, 1.0]]), np.ones(4, np.float32), np.zeros(4, np.float32))
def tnode(name, base, dx, dy, s=1.0):
    H, mul, add = B.nodes[base]
    B.nodes[name] = (H @ np.array([[s, 0, dx], [0, s, dy], [0, 0, 1.0]]), mul.copy(), add.copy())
def sfield(name, x, y, w, h, size, align=0, color=(255, 255, 255, 255)):
    snode(name, x, y); B.fields[name] = dict(bounds=[0, w, 0, h], size=size, align=align, color=list(color))
def rfield(name, mv, path, shape='d1'):
    B.mnode(name, mv, path); cid = mv.node_world(path + '.' + shape)[0]
    B.fields[name] = dict(bounds=list(mv.ch[cid]['bounds']), size=10, align=0, color=[255, 255, 255, 255])
ST = lambda p, *ks: hide_prefix(*[p + '.' + k for k in ks])
# ------------------------------------------------------------------ Western atlas crops (premultiplied by pil_image)
import gitspaths; DDS = gitspaths.dirp('UI', 'ui', 'GFx_DA', 'Data')
_dds = {}
def dds(name, file, box, h=None, trim=True):
    """a rectangle of a DDS atlas (trimmed to its alpha), optionally resized to h pixels tall; local units = pixels"""
    if file not in _dds: _dds[file] = Image.open(DDS + file).convert('RGBA')
    im = _dds[file].crop(box)
    if trim:
        a = np.asarray(im)[..., 3] > 4; ys, xs = np.nonzero(a)
        if len(xs): im = im.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
    if h: im = im.resize((max(1, round(im.size[0] * h / im.size[1])), h), Image.LANCZOS)
    B.pil_image(name, np.asarray(im), (0, 0, im.size[0], im.size[1])); return im.size
# top bar (MainMenu): the DIVE badge in its states, the slanted tabs (yellow = back / location, light = selected)
dds('tb.dive', 'MainMenu_I3.dds', (10, 0, 305, 53)); dds('tb.dive.on', 'MainMenu_I3.dds', (10, 107, 315, 160)); dds('tb.dive.off', 'MainMenu_I3.dds', (320, 107, 630, 160))
dds('tb.yellow', 'MainMenu_I3.dds', (367, 163, 590, 207)); dds('tb.light', 'MainMenu_I3.dds', (597, 163, 830, 207)); dds('tb.dark', 'MainMenu_I3.dds', (0, 213, 228, 256))
# mode emblems as the footage shows them: TDM / DEM / TTM from the loading screen atlas, Elimination from the round splash
dds('me.tdm', 'GameLoading_IC.dds', (129, 524, 383, 767), 128); dds('me.dem', 'GameLoading_IC.dds', (135, 3, 378, 245), 128)
dds('me.ttm', 'GameLoading_I27.dds', (100, 21, 413, 241), 112); dds('me.em', 'InGameUI_I31.dds', (699, 731, 829, 878), 128)
dds('me.gm', 'GameLoading_IC.dds', (114, 273, 397, 483), 112)
# lobby tile art: the operators group render of the Western intro screen (OPERATIVE tile)
dds('lb.team', 'Intro_I25.dds', (1, 1, 859, 420), 150)
# b76: the QUICK MATCH tile's Section 9 emblem (s9_emblem.py; b46-b75: quads at run time) and the TACHIKOMA tile's picture:
# the game's blue Think Tank from the front (tank_icon.py renders the data folder's tk_blue.gvm: the databuilder runs this
# step after 'tank'; without the model the tile keeps its drawn icon)
from s9_emblem import emblem as _s9
_im = _s9(80); B.pil_image('lb.s9', _im, (0, 0, _im.shape[1], _im.shape[0]))
_tkb = _os.path.join(_os.path.dirname(_os.path.abspath(OUT)), 'tk_blue.gvm')
if _os.path.exists(_tkb):
    from tank_icon import render as _tank
    from PIL import ImageFilter
    _im = _tank(_tkb, W=158, H=83, yaw=0.0, elev=10.0, ss=4)
    # a soft dark halo under the cut-out: the blue tank on the teal tile
    _a = Image.fromarray(_im[..., 3]).filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(3))
    _sh = np.asarray(_a, np.float32) / 255 * 0.55; _fa = _im[..., 3:4].astype(np.float32) / 255
    _rgb = _im[..., :3].astype(np.float32) / 255 * _fa + np.array([0.01, 0.04, 0.05]) * _sh[..., None] * (1 - _fa)
    _al = _fa[..., 0] + _sh * (1 - _fa[..., 0])
    _out = np.dstack([np.where(_al[..., None] > 1e-4, _rgb / np.maximum(_al[..., None], 1e-4), 0), _al])
    B.pil_image('lb.tank', (np.clip(_out, 0, 1) * 255 + 0.5).astype(np.uint8), (0, 0, 158, 83))
else: print('  lb.tank: no tk_blue.gvm next to', OUT, '- the TACHIKOMA tile keeps its drawn icon')
# ------------------------------------------------------------------ title: FIRST ASSAULT logo (LogIn.swf)
li = movie('LogIn')
B.mpart('tl.logo', li, 'containerSp.blMc', frames={'containerSp.blMc': 0})
# ------------------------------------------------------------------ play card art (Lobby.swf playMode modicons), larger for the Vita cards
lb = movie('Lobby'); PM = 'playMode'
for k in ('quick', 'custom', 'tuto', 'tachi'):
    B.mimage('pc.art.' + k, lb, PM + '.p0.modicons.%sIcons.icon1' % k, 1.4)
# ------------------------------------------------------------------ b105 (user: "the 'start in' and auto balance screen and accompanying
# icons look kind of rough please try and improve upon them with the game's data files"): the waiting room's START IN count
# and AUTO BALANCE flash were drawn at run time - a 12-sided "disc", a ring of quads (menu_fe.h fe_logo9). The game's own
# are in PopupManager.swf: v1.popup.readyRoom.GameStartCountMc (the Section 9 emblem 'gameStratLogoAni' in its layers of
# ring segments, a soft round glow, a wisp of smoke behind the number) and popupManager.shuffle.AutoMatchShuffleMc (a
# light streak across the lists under AUTO BALANCE). Their pictures, small (they are soft or drawn large from vectors):
pm = movie('PopupManager')
B.mimage('st.nine', pm, '#115', 0.45, frames={'#115': 29})   # the emblem as it stands when its layers have come together (the last frame)
B.mimage('st.smoke', pm, '#87', 0.25); B.mimage('st.flare', pm, '#148', 0.16)
# (the glow's picture is yellow in the file - the movie's colour transform makes it the cyan of the footage; here white with
# its brightness as alpha, tinted when drawn)
_g, _gl = pm.render_flat('#84', 0.2, cx=CX0); _ga = np.clip(_g[..., :3].max(2), 0, 1); B.add_image('st.glow', np.dstack([_ga, _ga, _ga, _ga]).astype(np.float32), _gl)
EXTRA_SECTIONS = ['menu_chr.py', 'menu_res.py']
for sec in EXTRA_SECTIONS:
    p = HERE + '/' + sec
    if os.path.exists(p): exec(open(p).read())
B.font(44)
a0, a1 = B.write(OUT)
print('wrote', OUT, 'atlas', a0.shape, 'font', a1.shape, 'images', len(B.images), 'nodes', len(B.nodes), 'fields', len(B.fields))
Image.fromarray((np.clip(a0, 0, 1) * 255).astype(np.uint8)).save(WORK + '/menu_atlas0.png')
