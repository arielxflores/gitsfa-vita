# Tutorial (engine/src/tutorial.h, tut_ui.h) textures:
#   tut_ui.gtx  - 512x512 RGBA, premultiplied alpha (drawn with the HUD shader, hud.h hud_poly): Vita button glyphs,
#                 the objective icon, the waypoint flag, SUCCESS disc, tier gauges, check mark, white texel
#   tut_fx.gtx  - 1024x1024 DXT5, straight alpha (world sprites, sprite.h): the gate panels (Fix_Tutorial *_gate_base),
#                 the hexagon / scan-line overlays, the INTER GATE terminal rings (effect\texture\tut_inter_*), the
#                 waypoint ring (intergate_ring), the chevron glow and the pin icon
#   engine/src/tut_atlas.h - the rectangles of both atlases (generated)
# GTX1 header as menu.h: 'GTX1' u16 w, u16 h, u8 fmt (0 RGBA8, 1 DXT1, 5 DXT5), u8 0, u16 0, u32 size, data
#
# Sources (the user's game files, extracted on the PC with tools/tutorial/extract_tut.py):
#   effect.dat  effect\texture\tut_inter_*.dds, intergate_ring.dds, last_gate_ngon.DDS, tut_arrow_glow.dds
#   map.dat     map\FixedMap\Fix_Tutorial\{large,medium,small}_gate_base.DDS, up_fx.dds; Fix_Tutorial3\tut_icon.DDS
# usage: python3 make_tut_assets.py <srcdir with the .dds files> <outdir (build/data)> <engine/src>
import sys, os, struct, math, numpy as np, etcpak
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
SRC = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('TUTSRC')
OUT = sys.argv[2] if len(sys.argv) > 2 else gitspaths.root('DATA')
HDR = sys.argv[3] if len(sys.argv) > 3 else gitspaths.root('TMP')   # tut_atlas.h (the engine's copy is committed; same text)

def load(name):
    for n in (name, name.replace('.dds', '.DDS'), name.replace('.DDS', '.dds')):
        p = os.path.join(SRC, n)
        if os.path.exists(p): return Image.open(p).convert('RGBA')
    raise FileNotFoundError(name)

def gtx(path, w, h, fmt, data):
    with open(path, 'wb') as f: f.write(b'GTX1' + struct.pack('<HHBBHI', w, h, fmt, 0, 0, len(data))); f.write(data)

# ------------------------------------------------------------------ UI atlas (premultiplied)
UW = UH = 512
ui = Image.new('RGBA', (UW, UH), (0, 0, 0, 0))
RECTS = {}
SS = 4   # supersampling for the drawn shapes

def put(name, im, x, y):
    ui.paste(im, (x, y)); RECTS[name] = (x, y, im.width, im.height)

def canvas(w, h): return Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))
def down(im, w, h): return im.resize((w, h), Image.LANCZOS)

def disc(d, cx, cy, r, fill=None, outline=None, width=1):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill, outline=outline, width=width)

def button(sym):
    """a Vita face button: dark disc, light rim, coloured symbol (as the Vita's own buttons)"""
    S = 40; c = canvas(S, S); d = ImageDraw.Draw(c); m = S * SS / 2; R = m - 2 * SS
    disc(d, m, m, R, fill=(28, 32, 38, 235), outline=(200, 210, 215, 255), width=int(1.6 * SS))
    w = int(2.6 * SS); r = R * 0.48
    if sym == 'x':
        col = (125, 178, 255, 255)
        d.line([m - r, m - r, m + r, m + r], fill=col, width=w); d.line([m - r, m + r, m + r, m - r], fill=col, width=w)
    elif sym == 'o':
        disc(d, m, m, r, outline=(255, 96, 110, 255), width=w)
    elif sym == 't':
        col = (70, 220, 160, 255); h = r * 1.05
        d.polygon([(m, m - h), (m + h * 0.95, m + h * 0.62), (m - h * 0.95, m + h * 0.62)], outline=col);
        d.line([(m, m - h), (m + h * 0.95, m + h * 0.62), (m - h * 0.95, m + h * 0.62), (m, m - h)], fill=col, width=w, joint='curve')
    elif sym == 's':
        col = (255, 130, 210, 255); q = r * 0.85
        d.rectangle([m - q, m - q, m + q, m + q], outline=col, width=w)
    return down(c, S, S)

def shoulder(side):
    """L / R shoulder button outline (the letter is drawn at run time with the HUD font)"""
    W, H = 48, 28; c = canvas(W, H); d = ImageDraw.Draw(c)
    x0, y0, x1, y1 = 2 * SS, 3 * SS, (W - 2) * SS, (H - 2) * SS
    d.rounded_rectangle([x0, y0, x1, y1], radius=9 * SS, fill=(28, 32, 38, 235), outline=(200, 210, 215, 255), width=int(1.6 * SS))
    return down(c, W, H)

def dpad(dirn):
    """the D-pad with one direction lit"""
    S = 40; c = canvas(S, S); d = ImageDraw.Draw(c); m = S * SS / 2; a = 6.5 * SS; L = 17 * SS
    arms = {'u': (m - a, m - L, m + a, m - a), 'd': (m - a, m + a, m + a, m + L), 'l': (m - L, m - a, m - a, m + a), 'r': (m + a, m - a, m + L, m + a)}
    d.rectangle([m - a, m - a, m + a, m + a], fill=(28, 32, 38, 235))
    for k, r in arms.items():
        lit = k == dirn
        d.rectangle(r, fill=(235, 240, 245, 255) if lit else (28, 32, 38, 235), outline=(200, 210, 215, 255), width=int(1.3 * SS))
    return down(c, S, S)

def stick(letter_side):
    S = 40; c = canvas(S, S); d = ImageDraw.Draw(c); m = S * SS / 2
    disc(d, m, m, m - 2 * SS, fill=(28, 32, 38, 235), outline=(200, 210, 215, 255), width=int(1.6 * SS))
    disc(d, m, m, (m - 2 * SS) * 0.55, fill=(70, 78, 88, 255), outline=(150, 160, 168, 255), width=int(1.2 * SS))
    return down(c, S, S)

def start_btn():
    W, H = 48, 20; c = canvas(W, H); d = ImageDraw.Draw(c)
    d.rounded_rectangle([2 * SS, 3 * SS, (W - 2) * SS, (H - 3) * SS], radius=7 * SS, fill=(28, 32, 38, 235), outline=(200, 210, 215, 255), width=int(1.4 * SS))
    return down(c, W, H)

def obj_icon():
    """the objective band's icon: a light grey ring open at the lower left, an arrow in a small disc (footage 1:42)"""
    S = 48; c = canvas(S, S); d = ImageDraw.Draw(c); m = S * SS / 2; R = m - 3 * SS
    d.arc([m - R, m - R, m + R, m + R], start=150, end=120 + 360 - 30, fill=(205, 210, 214, 255), width=int(3.2 * SS))
    r2 = R * 0.62; disc(d, m, m, r2, fill=(70, 74, 80, 255), outline=(150, 155, 160, 255), width=int(1.2 * SS))
    w = int(2.6 * SS); a = r2 * 0.55
    d.line([m - a, m, m + a, m], fill=(235, 238, 240, 255), width=w)
    d.line([m + a * 0.1, m - a * 0.75, m + a, m], fill=(235, 238, 240, 255), width=w); d.line([m + a * 0.1, m + a * 0.75, m + a, m], fill=(235, 238, 240, 255), width=w)
    return down(c, S, S)

def flag_icon():
    """the waypoint: a flag in a ring (footage 1:45, 2:01)"""
    S = 48; c = canvas(S, S); d = ImageDraw.Draw(c); m = S * SS / 2; R = m - 3 * SS
    disc(d, m, m, R, fill=(20, 24, 28, 170), outline=(240, 242, 244, 255), width=int(2.4 * SS))
    x0 = m - R * 0.38; d.line([x0, m - R * 0.52, x0, m + R * 0.55], fill=(240, 242, 244, 255), width=int(2.4 * SS))
    d.polygon([(x0, m - R * 0.52), (m + R * 0.48, m - R * 0.52), (m + R * 0.48, m + R * 0.08), (x0, m + R * 0.08)], fill=(240, 242, 244, 255))
    return down(c, S, S)

def success_disc():
    """SUCCESS: a dark disc in a cyan double ring (the word is drawn at run time)"""
    S = 112; c = canvas(S, S); d = ImageDraw.Draw(c); m = S * SS / 2
    disc(d, m, m, m - 4 * SS, fill=(14, 30, 38, 215))
    disc(d, m, m, m - 5 * SS, outline=(110, 230, 245, 255), width=int(2.4 * SS))
    disc(d, m, m, m - 11 * SS, outline=(70, 160, 175, 200), width=int(1.2 * SS))
    for k in range(4):   # small ticks on the outer ring
        a = math.radians(45 + 90 * k); r0, r1 = m - 2 * SS, m + 0
        d.line([m + math.cos(a) * (m - 9 * SS), m + math.sin(a) * (m - 9 * SS), m + math.cos(a) * (m - 1 * SS), m + math.sin(a) * (m - 1 * SS)], fill=(160, 240, 250, 255), width=int(1.6 * SS))
    im = down(c, S, S)
    return im.filter(ImageFilter.GaussianBlur(0.2))

def gauge(col):
    """the intel gauge of the skill explanation screens (TIER 1 cyan / TIER 2 orange): a 270-degree arc around a dark disc with
    the operator silhouette, a 'skill key' badge below (numbers and labels are drawn at run time)"""
    S = 104; c = canvas(S, S); d = ImageDraw.Draw(c); m = S * SS / 2; R = m - 6 * SS
    d.arc([m - R, m - R, m + R, m + R], start=95, end=95 + 300, fill=(60, 70, 78, 255), width=int(9 * SS))
    d.arc([m - R, m - R, m + R, m + R], start=95, end=95 + 220, fill=col + (255,), width=int(9 * SS))
    for k in range(13):   # tick marks inside the arc
        a = math.radians(95 + k * 25); r0, r1 = R - 12 * SS, R - 8 * SS
        d.line([m + math.cos(a) * r0, m + math.sin(a) * r0, m + math.cos(a) * r1, m + math.sin(a) * r1], fill=(120, 130, 136, 255), width=int(1.0 * SS))
    disc(d, m, m, R * 0.62, fill=(36, 42, 48, 255), outline=(150, 160, 166, 255), width=int(1.5 * SS))
    # person silhouette
    disc(d, m, m - R * 0.17, R * 0.16, fill=(230, 234, 236, 255))
    d.pieslice([m - R * 0.32, m + R * 0.02, m + R * 0.32, m + R * 0.62], start=180, end=360, fill=(230, 234, 236, 255))
    # key badge
    d.rounded_rectangle([m - 9 * SS, m + R - 4 * SS, m + 9 * SS, m + R + 13 * SS], radius=3 * SS, fill=(30, 36, 42, 255), outline=(220, 225, 228, 255), width=int(1.2 * SS))
    return down(c, S, S)

def portrait_ring():
    """the radio portrait's frame: a light grey ring with a dark inner rim and two ticks (footage 1:28)"""
    S = 128; c = canvas(S, S); d = ImageDraw.Draw(c); m = S * SS / 2; R = m - 4 * SS
    disc(d, m, m, R, outline=(205, 210, 214, 255), width=int(6 * SS))
    disc(d, m, m, R - 8 * SS, outline=(70, 82, 90, 255), width=int(3 * SS))
    for a in (200, 340):
        a = math.radians(a); d.line([m + math.cos(a) * (R + 1 * SS), m + math.sin(a) * (R + 1 * SS), m + math.cos(a) * (R + 9 * SS), m + math.sin(a) * (R + 9 * SS)], fill=(205, 210, 214, 255), width=int(2 * SS))
    return down(c, S, S)

def check_mark():
    S = 16; c = canvas(S, S); d = ImageDraw.Draw(c)
    d.line([3 * SS, 8.5 * SS, 6.5 * SS, 12 * SS, 13 * SS, 4 * SS], fill=(235, 238, 240, 255), width=int(1.8 * SS), joint='curve')
    return down(c, S, S)

def glow():
    S = 64; a = np.zeros((S, S), np.float32)
    y, x = np.mgrid[0:S, 0:S]; r = np.hypot(x + 0.5 - S / 2, y + 0.5 - S / 2) / (S / 2); a = np.clip(1 - r, 0, 1) ** 2
    im = np.zeros((S, S, 4), np.uint8); im[..., 0:3] = 255; im[..., 3] = (a * 255).astype(np.uint8)
    return Image.fromarray(im, 'RGBA')

def bracket():
    """explanation-screen video frame corner: a cyan L with a bevel (drawn mirrored for the four corners)"""
    W, H = 48, 48; c = canvas(W, H); d = ImageDraw.Draw(c); t = 4 * SS
    col = (130, 215, 230, 255)
    d.polygon([(0, 0), (W * SS, 0), (W * SS, t), (t + 6 * SS, t), (t, t + 6 * SS), (t, H * SS), (0, H * SS)], fill=col)
    return down(c, W, H)

def premul(im):
    a = np.asarray(im).astype(np.float32); a[..., :3] *= a[..., 3:4] / 255.0
    return Image.fromarray(np.clip(a + 0.5, 0, 255).astype(np.uint8), 'RGBA')

# layout
put('white', Image.new('RGBA', (8, 8), (255, 255, 255, 255)), 0, 0)
put('check', check_mark(), 8, 0)
put('glow', glow(), 0, 448)
x = 0
for i, s in enumerate('xots'): put('btn_' + s, button(s), 40 * i, 24)
for i, s in enumerate('udlr'): put('dpad_' + s, dpad(s), 160 + 40 * i, 24)
put('stick', stick('l'), 320, 24)
put('shoulder', shoulder('l'), 360, 24)
put('start', start_btn(), 408, 24)
put('obj', obj_icon(), 0, 64)
put('flag', flag_icon(), 48, 64)
put('bracket', bracket(), 96, 64)
put('success', success_disc(), 0, 112)
put('gauge1', gauge((48, 200, 220)), 112, 112)
put('gauge2', gauge((235, 165, 40)), 216, 112)
put('pring', portrait_ring(), 320, 112)
ui = premul(ui)
os.makedirs(OUT, exist_ok=True)
gtx(os.path.join(OUT, 'tut_ui.gtx'), UW, UH, 0, ui.tobytes())
ui.save(os.path.join(HDR, 'tut_ui_preview.png')) if os.environ.get('GITSFA_PREVIEW') else None

# ------------------------------------------------------------------ FX atlas (DXT5, straight alpha; additive or blended in the world)
FW = FH = 1024
fx = Image.new('RGBA', (FW, FH), (0, 0, 0, 0))
FR = {}
def fput(name, im, x, y, w, h):
    im = im.resize((w, h), Image.LANCZOS); fx.paste(im, (x, y)); FR[name] = (x, y, w, h)
def lum_alpha(im, keep_rgb=False):
    """white-on-black effect textures -> white with alpha = brightness"""
    a = np.asarray(im).astype(np.float32); l = a[..., :3].max(2) * (a[..., 3] / 255.0)
    o = np.zeros_like(a); o[..., :3] = 255 if not keep_rgb else a[..., :3]; o[..., 3] = l
    return Image.fromarray(o.astype(np.uint8), 'RGBA')
for i in range(6): fput('inter%d' % (i + 1), lum_alpha(load('tut_inter_%d.dds' % (i + 1))), (i % 4) * 256, (i // 4) * 256, 256, 256)
fput('interfont', lum_alpha(load('tut_inter_cen_font.dds')), 512, 256, 256, 256)
fput('interarc', lum_alpha(load('tut_inter_white.dds')), 768, 256, 256, 256)
fput('gate_m', load('medium_gate_base.DDS'), 0, 512, 256, 256)
fput('gate_s', load('small_gate_base.DDS'), 256, 512, 256, 256)
fput('gate_l', load('large_gate_base.DDS'), 512, 512, 512, 256)
fput('hex', lum_alpha(load('last_gate_ngon.DDS')), 0, 768, 256, 256)
fput('ring', lum_alpha(load('intergate_ring.dds')), 256, 768, 256, 256)
fput('scan', load('up_fx.dds'), 512, 768, 256, 128)
fput('chevron', lum_alpha(load('tut_arrow_glow.dds')), 512, 896, 256, 64)
pin = load('tut_icon3.DDS').crop((0, 256, 256, 512))   # the map pin of Fix_Tutorial3\tut_icon (lower left quarter)
fput('pin', lum_alpha(pin), 768, 768, 128, 128)
fput('glow', glow(), 896, 768, 128, 128)
fput('disc', lum_alpha(load('tut_inter_sp_g.DDS')), 768, 896, 128, 128)
data = etcpak.compress_to_dxt5(np.ascontiguousarray(np.asarray(fx)).tobytes(), FW, FH)
gtx(os.path.join(OUT, 'tut_fx.gtx'), FW, FH, 5, data)

# ------------------------------------------------------------------ header
with open(os.path.join(HDR, 'tut_atlas.h'), 'w') as f:
    f.write('// generated by tools/tutorial/make_tut_assets.py: atlas rectangles (x, y, w, h in texels)\n#pragma once\n')
    f.write('#define TUI_W %d\n#define TUI_H %d\n#define TFX_W %d\n#define TFX_H %d\n' % (UW, UH, FW, FH))
    f.write('enum { ' + ', '.join('TUI_%s' % n.upper() for n in RECTS) + ', TUI_COUNT };\n')
    f.write('static const unsigned short TUI_RECT[TUI_COUNT][4] = { ' + ', '.join('{ %d, %d, %d, %d }' % r for r in RECTS.values()) + ' };\n')
    f.write('enum { ' + ', '.join('TFX_%s' % n.upper() for n in FR) + ', TFX_COUNT };\n')
    f.write('static const unsigned short TFX_RECT[TFX_COUNT][4] = { ' + ', '.join('{ %d, %d, %d, %d }' % r for r in FR.values()) + ' };\n')
print('tut_ui.gtx %d KB, tut_fx.gtx %d KB, %d ui parts, %d fx parts' % (UW * UH * 4 // 1024, len(data) // 1024, len(RECTS), len(FR)))
