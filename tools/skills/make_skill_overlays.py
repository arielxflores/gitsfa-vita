# Full-screen skill overlays for the Vita: one 1024 x 1024 DXT1 texture in the GTX1 format menu.h already loads
# (menu_tex_load: 'GTX1' u16 w, u16 h, u8 fmt 1 = DXT1, u8 0, u16 0, u32 size, data) + a UV table.
# Every cell is drawn as ONE screen quad (960 x 544, or a part of it) - 1 texture bind for all overlays, ~0.1 ms each.
# Two blend modes (all DXT1, no alpha needed):
#   add   : glBlendFunc(GL_ONE, GL_ONE)           black = no change; tint with the vertex colour (vig: cyan/red/white ...)
#   mul2x : glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR) grey 128 = no change (same as fx.h's decal pass), darker = darken/tint
# Sources: the game's own screen effect textures (effect.dat effect/screen/*, effect/kfm/screen_*: screen_camoflage,
# caustic_wave (screen_metaboost), screen_sprint, camouflage_out_inside_frist_start (screen_emp / screen_debuff_noise),
# hit_red_line, start_line_gra / base_out_dark (screen_dive), smileman_white (screen_smaileman = the Laughing Man debuff),
# ap_shield_white (screen_ap_shield_*)); the first-person shield frame is traced from the Shield skill video.
# usage: python3 make_skill_overlays.py [outdir]  -> sk_ovl.gtx + /root/gits/tools/skills/out/sk_ovl.txt (+ preview png)
import sys, os, struct, numpy as np, etcpak
from PIL import Image, ImageDraw, ImageFilter
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DATA')
PNG = gitspaths.dirp('SKRAW', 'png')
HERE = os.path.dirname(os.path.abspath(__file__))
A = 1024
atlas = np.zeros((A, A, 3), np.float32)
table = []

def L(name): return np.asarray(Image.open(PNG + name + '.png').convert('RGBA'), np.float32) / 255
def fit(img, w, h): return np.asarray(Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).resize((w, h), Image.LANCZOS), np.float32) / 255
def put(name, img, x, y, blend, note):
    h, w = img.shape[:2]
    if img.ndim == 2: img = np.repeat(img[..., None], 3, 2)
    atlas[y:y + h, x:x + w] = img[..., :3]
    # half-texel inset (bilinear never reaches the neighbour)
    table.append((name, (x + 0.5) / A, (y + 0.5) / A, (x + w - 0.5) / A, (y + h - 0.5) / A, blend, note))
def vgrid(w, h):
    y, x = np.mgrid[0:h, 0:w] + 0.5; return (x / w) * 2 - 1, (y / h) * 2 - 1

# ---- 1. generic soft vignette (add, white): tint at draw time - cyan = metaboost heal, red = DMD laser / recon warning /
#         low HP, orange = heat scan, white = camo start. Elliptical, ~35 % of the screen at the corners.
X, Y = vgrid(512, 256); r = np.sqrt((X / 1.0) ** 2 + (Y / 1.0) ** 2)
vig = np.clip((r - 0.55) / 0.85, 0, 1) ** 1.6
put('vig', vig, 0, 0, 'add', 'generic edge glow, tint with the vertex colour')
# ---- 2. therm-optic camo (screen_camoflage.dds: white haze at the edges, as in the footage), slightly blue
c = fit(L('screen_camoflage')[..., :3].mean(2), 512, 256)
camo = np.dstack([c * 0.80, c * 0.88, c * 1.0])
put('camo', camo, 512, 0, 'add', 'Motoko camo view (screen_op_camouflage); fade in 0.3 s, out 0.3 s')
# ---- 3. metaboost caustics (screen_metaboost: caustic_wave over the edges, cyan), animate by scrolling v
cw = fit(L('caustic_wave')[..., :3].max(2), 512, 256); X, Y = vgrid(512, 256); r = np.sqrt(X ** 2 + Y ** 2)
m = np.clip((r - 0.45) / 0.7, 0, 1)
meta = np.dstack([cw * m * 0.35, cw * m * 0.95, cw * m * 1.0]) + np.dstack([m * 0.0, m * 0.18, m * 0.22]) ** 1.5
put('meta', meta, 0, 256, 'add', 'Borma metaboost heal view; pulse brightness 0.6..1 at 2 Hz')
# ---- 4. first-person plasma shield frame (traced from the Shield video, 1280x720 frame): trapezoid outline + faint hex fill
W, H = 512, 256; im = Image.new('L', (W, H), 0); d = ImageDraw.Draw(im)
pts = [(0.015, 0.97), (0.255, 0.035), (0.745, 0.035), (0.985, 0.97)]
P = [(x * W, y * H) for x, y in pts]
for k in range(3): d.line([P[k], P[k + 1]], fill=255, width=4)
hexi = Image.open(PNG + 'as_dark_ally_tier2.png').convert('L').crop((0, 0, 512, 512)).resize((128, 128))
fill = Image.new('L', (W, H), 0)
for yy in range(0, H, 128):
    for xx in range(0, W, 128): fill.paste(hexi, (xx, yy))
mask = Image.new('L', (W, H), 0); ImageDraw.Draw(mask).polygon(P, fill=255)
fl = np.asarray(fill, np.float32) / 255 * (np.asarray(mask, np.float32) / 255) * 0.18
ln = np.asarray(im.filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
glow = np.asarray(im.filter(ImageFilter.GaussianBlur(6)), np.float32) / 255
s = np.clip(ln * 1.0 + glow * 0.6 + fl + (np.asarray(mask, np.float32) / 255) * 0.05, 0, 1)
put('shield', np.dstack([s * 0.45, s * 0.95, s * 0.90]), 512, 256, 'add', 'Azuma shield frame, 1st person (draw over the whole screen)')
# ---- 5. rush speed lines (screen_sprint streaks wrapped radially around the screen centre), additive white
sp = L('screen_sprint')[..., :3].max(2); X, Y = vgrid(512, 256); ang = (np.arctan2(Y * 0.56, X) / (2 * np.pi) + 0.5); rr = np.sqrt(X ** 2 + (Y * 0.56) ** 2 / 0.56 ** 2 * 0.56 ** 2 + 1e-6)
u = (ang * 4 % 1 * 255).astype(int); v = np.clip(255 - (np.sqrt(X ** 2 + Y ** 2) / 1.42 * 255), 0, 255).astype(int)
rush = sp[v, u] * np.clip((np.sqrt(X ** 2 + Y ** 2) - 0.35) / 0.6, 0, 1)
put('rush', np.dstack([rush * 0.9, rush * 0.95, rush]), 0, 512, 'add', 'Paz rush speed lines (Screen_sprint_circle); scale 1.0 -> 1.15 over the dash')
# ---- 6. cyber dive lines (screen_dive: start_line_gra scan bands + base_out_dark rim), cyan, additive
X, Y = vgrid(512, 256); band = (np.sin((Y + 1) * 3.1416 * 48) * 0.5 + 0.5) ** 8 * 0.10
rim = np.clip((np.sqrt(X ** 2 + Y ** 2) - 0.6) / 0.8, 0, 1) ** 1.3
dl = np.clip(band + rim * 0.7, 0, 1)
put('dive', np.dstack([dl * 0.25, dl * 0.85, dl * 1.0]), 512, 512, 'add', 'Reiko cyber-dive view: draw with dive_mul first')
# ---- 7. glitch blocks (camouflage_out_inside_frist_start: screen_emp / screen_debuff_noise / camo start), grey, additive
g = fit(L('camouflage_out_inside_frist_start')[..., :3].mean(2), 256, 128)
put('glitch', np.clip((g - 0.15) / 0.85, 0, 1) * 0.6, 0, 768, 'add', 'EMP hit / wall pass / heat-scan start noise; tint, jitter the UVs per frame')
# ---- 8. red horizontal lines (hit_red_line: the cyber-wall pass-through debuff), additive
h = L('hit_red_line'); hl = fit(h[..., :3].mean(2) * h[..., 3], 256, 128)
put('hlines', np.dstack([hl, hl * 0.25, hl * 0.2]), 256, 768, 'add', 'Maven wall pass-through debuff (red lines), scroll v')
# ---- 9. dive tint (mul2x): deep teal (darkens the world to the cyber-world look; 128 = unchanged)
put('dive_mul', np.dstack([np.full((128, 256), 14 / 255.), np.full((128, 256), 46 / 255.), np.full((128, 256), 58 / 255.)]), 512, 768, 'mul2x', 'Reiko cyber-dive tint (draw before dive)')
# ---- 10. heat tint (mul2x): slightly warm, desaturating darkening for Saito's scan pulses
put('heat_mul', np.dstack([np.full((128, 256), 140 / 255.), np.full((128, 256), 112 / 255.), np.full((128, 256), 104 / 255.)]), 768, 768, 'mul2x', 'Saito heat-scan tint during a pulse (0.4 s)')
# ---- 11. Laughing Man (smileman_white, screen_smaileman / screen_debuff: the cyber-dive victim's view), additive
sm = L('smileman_white'); a = sm[..., 3]
# b72 (user: "i thought it was a more circular symbol"): the game shows one 85 x 128 cell on a 2:1 quad (screen_smaileman.nif:
# 640 x 320, uv 0..1 over a 1/3 x 1/2 cell) - the logos are drawn 3x narrow in the texture; 256 x 128 here = the same 3.0x
# stretch (b71: 128 x 128 = 1.5x, an upright oval). The engine draws it 2:1 (skills.h).
logo = fit(np.clip((a[:128, :85] - 0.3) / 0.6, 0, 1), 256, 128)   # first of the 3 x 2 logos, background removed
put('laugh', np.dstack([logo * 0.9, logo * 0.95, logo]), 0, 896, 'add', 'cyber-dive victim: Laughing Man logo, centre of the screen, a 2:1 quad 80 % of its height')
# ---- 12. AP-shield white rim (ap_shield_white / screen_ap_shield_ing): hit flash on the shield holder's screen
aw = L('ap_shield_white'); rim2 = fit(aw[..., 3], 256, 128)
put('shield_hit', np.dstack([rim2 * 0.6, rim2, rim2]), 256, 896, 'add', 'Azuma shield hit flash (0.2 s)')

os.makedirs(OUT, exist_ok=True); os.makedirs(gitspaths.path('TMP', 'skills_out'), exist_ok=True)
img = (np.clip(atlas, 0, 1) * 255).astype(np.uint8)
rgba = np.dstack([img, np.full((A, A), 255, np.uint8)])
dxt = etcpak.compress_to_dxt1(np.ascontiguousarray(rgba).tobytes(), A, A)
path = os.path.join(OUT, 'sk_ovl.gtx')
with open(path, 'wb') as f:
    f.write(b'GTX1' + struct.pack('<HHBBHI', A, A, 1, 0, 0, len(dxt))); f.write(dxt)
with open(gitspaths.path('TMP', 'skills_out', 'sk_ovl.txt'), 'w') as f:
    f.write('# sk_ovl.gtx (GTX1 DXT1 %dx%d): name u0 v0 u1 v1 blend  note\n' % (A, A))
    for n, u0, v0, u1, v1, b, note in table: f.write('%-10s %.6f %.6f %.6f %.6f %-5s %s\n' % (n, u0, v0, u1, v1, b, note))
Image.fromarray(img).save(gitspaths.tmp('skills', 'sk_ovl_atlas.png'))
print(path, os.path.getsize(path), 'bytes;', len(table), 'overlays')
