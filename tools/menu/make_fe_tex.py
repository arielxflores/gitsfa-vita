# Front-end textures (GTX1 DXT1):
#   mload_lobby.gtx  1024x512  teal backdrop behind the lobby / operator / results screens (procedural: gradient, light band, bokeh)
#   mthumb.gtx       1024x1024 the 17 map thumbnails (240x135 each, 4 per row, in the engine's MAPS[] order) + 4 training programs (17..20)
import sys, numpy as np
from PIL import Image, ImageFilter
import os; HERE = os.path.dirname(os.path.abspath(__file__)); WORK = os.environ.get('MENU_WORK') or os.path.join(os.environ.get('GITSFA_TMP') or os.environ.get('TMPDIR') or '.', 'menu-ui'); os.makedirs(WORK, exist_ok=True)
sys.path.insert(0, HERE)
from make_bgs import gtx, MAP_BG, SRC as BGSRC
import gitspaths
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.dirp('DATA')
TH = gitspaths.dirp('MENURSC', 'menu', 'ui', 'GFx_DA', 'Data', 'rsc', 'map', 'thumbnail')
STEMS = ['downtown', 'berutalube', 'ecenter', 'ghost_storage', 'headquarter', 'port', 'pss9', 'ruinednight', 'ruinedtokyo', 'trainstation',
         'underground', 'urban', 'zioplant', 'shooting', 'tutorial', 'tutorial2', 'tutorial3']
THUMB = {'downtown': 'Downtown_Thumb.png', 'berutalube': 'Berutalube_Thumb.png', 'ecenter': 'Ecenter_Thumb.png', 'ghost_storage': 'GhostStorage_thumb.png',
         'headquarter': 'Headquarter_thumb.png', 'port': 'Port_thumb.png', 'pss9': 'PSS9_thumb.png', 'ruinednight': 'RuinedNight_thumb.png',
         'ruinedtokyo': 'Ruined_Tokyo__Thumb.png', 'trainstation': 'Trainstation_thumb.png', 'underground': 'Underground_thumb.png', 'urban': 'urban_thumb.png',
         'zioplant': 'Zioplant__Thumb.png'}
def lobby_bg(W=1024, H=512, seed=7):
    rng = np.random.default_rng(seed)
    y = np.linspace(0, 1, H)[:, None]; x = np.linspace(0, 1, W)[None, :]
    top = np.array([0.035, 0.11, 0.13]); mid = np.array([0.20, 0.44, 0.45]); bot = np.array([0.05, 0.16, 0.18])
    t = np.clip(1 - np.abs(y - 0.58) / 0.58, 0, 1) ** 1.6
    img = top * (1 - t[..., None]) + mid * t[..., None]
    img = img * (y[..., None] < 0.58) + (bot + (mid - bot) * np.clip(1 - (y - 0.58) / 0.42, 0, 1)[..., None] ** 2) * (y[..., None] >= 0.58)
    img = img * np.ones((H, W, 1))
    # bright horizontal light band (a lit floor line far away) and a glow from the upper right
    band = np.exp(-((y - 0.68) / 0.07) ** 2) * (0.5 + 0.5 * np.cos((x - 0.55) * 2.5))
    img += band[..., None] * np.array([0.10, 0.22, 0.21])
    glow = np.exp(-(((x - 0.72) / 0.35) ** 2 + ((y - 0.15) / 0.35) ** 2))
    img += glow[..., None] * np.array([0.10, 0.22, 0.22])
    # bokeh: soft discs along the light band
    bk = np.zeros((H, W)); yy, xx = np.mgrid[0:H, 0:W]
    for _ in range(46):
        cx = rng.uniform(0, W); cy = H * rng.normal(0.70, 0.05); r = rng.uniform(3, 14); a = rng.uniform(0.15, 0.6)
        d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2); bk += a * np.clip((r - d) / 2.0 + 0.5, 0, 1)
    bk = np.asarray(Image.fromarray((np.clip(bk, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2.5)), np.float32) / 255
    img += bk[..., None] * np.array([0.14, 0.28, 0.26])
    img = np.clip(img, 0, 1)
    im = Image.fromarray((img * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))
    return im
def thumbs():
    A = Image.new('RGB', (1024, 1024), (10, 16, 20))
    for i, st in enumerate(STEMS):
        if st in THUMB: im = Image.open(TH + THUMB[st]).convert('RGB')
        else:
            im = Image.open(BGSRC + MAP_BG[st]).convert('RGB'); w, h = im.size; ch = int(w * 135 / 240)
            im = im.crop((0, (h - ch) // 2, w, (h - ch) // 2 + ch))
        im = im.resize((240, 135), Image.LANCZOS)
        A.paste(im, ((i % 4) * 256 + 8, (i // 4) * 144 + 4))
    # slots 17..20: the training programs (menu_fe.h MENU_TUT[].thumb): the game's tutorial loading art + the Think Tank picture
    for i, fn in ((17, BGSRC + 'Tutorial1_big.png'), (18, BGSRC + 'Tutorial2_big.png'), (19, BGSRC + 'Tutorial3_big.png'), (20, TH + 'tach_big.png')):
        im = Image.open(fn).convert('RGB'); w, h = im.size; ch = int(w * 135 / 240)
        if ch <= h: im = im.crop((0, (h - ch) // 2, w, (h - ch) // 2 + ch))
        else: cw = int(h * 240 / 135); im = im.crop(((w - cw) // 2, 0, (w - cw) // 2 + cw, h))
        A.paste(im.resize((240, 135), Image.LANCZOS), ((i % 4) * 256 + 8, (i // 4) * 144 + 4))
    return A
if __name__ == '__main__':
    n1 = gtx(lobby_bg(), OUT + 'mload_lobby.gtx')
    A = thumbs(); n2 = gtx(A, OUT + 'mthumb.gtx', 1024, 1024)
    lobby_bg().save(WORK + '/lobby_bg.png'); A.save(WORK + '/thumbs_atlas.png')
    print('lobby bg', n1 // 1024, 'KB; thumbnails', n2 // 1024, 'KB')
