# Operator-skill particle effects for engine/src/fx.h, built from the game's own skill NIFs / textures (effect.dat:
# cyberbrain/, explosion/, missile/, character/ - extracted to /root/gits/raw/skills). Nothing in /root/fx is modified:
# this script IMPORTS /root/fx/build_fx.py at run time (so whatever is in its CELLS / EM / EFFECTS tables now - e.g. the
# grenade explosion another agent is adding - is included), appends the skill cells / emitters / effects below and writes
#   fx_sk.gfx   (default: /root/gits/build/data/fx_sk.gfx) = base + skills, FXG1 exactly as fx.h reads it; the atlas grows
#               to 1024 x 2048 (base cells first, skill cells after). Drop-in replacement for fx.gfx (all base effect ids
#               unchanged: skill effects are appended after the base ones).
#   fxs.gfx     (--separate) = the skill effects only (+ base decal table, so fx_init accepts it), for a second fx instance.
#   /root/gits/tools/skills/out/fx_sk_ids.txt   effect ids (FX_SK_*) and skill cell ids (FXC_SK_*) of the merged file.
# Effect names are <= 15 chars and start with "SK_": skills.h should resolve them BY NAME at init (fx_eff[i].name), so a
# later change of the base table (more effects) never breaks it. Cells used directly by skills.h (ground rings, beams,
# lock-on reticles) are exposed the same way: effects "C_DIR0".."C_DIR3" whose zero-count emitters point at them (see CELL_FX).
# usage: python3 build_skills_fx.py [out.gfx] [--separate [out_fxs.gfx]]
import sys, os, struct, math, importlib.util, numpy as np, etcpak
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..')); import gitspaths
SKPNG = gitspaths.dirp('SKRAW', 'png')
args = [a for a in sys.argv[1:] if not a.startswith('--')]
SEP = None
if '--separate' in sys.argv:
    i = sys.argv.index('--separate')
    SEP = sys.argv[i + 1] if i + 1 < len(sys.argv) and not sys.argv[i + 1].startswith('--') else gitspaths.path('DATA', 'fxs.gfx')
    if SEP in args: args.remove(SEP)
OUT = args[0] if args else gitspaths.path('DATA', 'fx_sk.gfx')

# ---------------------------------------------------------------- import the base tables without running build_fx.main()
# databuilder: the base is tools/fx/build_fx.py (now the /root/fx version with the grenade explosion, as shipped)
spec = importlib.util.spec_from_file_location('build_fx_base', os.path.join(HERE, '..', 'fx', 'build_fx.py'))
saved = sys.argv; sys.argv = [saved[0]]
BF = importlib.util.module_from_spec(spec); spec.loader.exec_module(BF)
sys.argv = saved
E, drag_k = BF.E, BF.drag_k

# ---------------------------------------------------------------- skill atlas cells: (name, png or generator, crop, size, pad)
def sk(name): return 'SK:' + name
def ele_grid():
    """the 7 EMP lightning sprites (ele_cen01..07) + 1 copy in a 4 x 2 grid of 128 x 128"""
    out = Image.new('RGBA', (512, 256))
    for k in range(8):
        im = Image.open(SKPNG + 'ele_cen%02d.png' % (k % 7 + 1)).convert('RGBA').resize((128, 128), Image.LANCZOS)
        out.paste(im, ((k % 4) * 128, (k // 4) * 128))
    return out
def sector90():
    """ground wedge of 90 degrees (apex at the bottom-left corner, radius = the cell side): radial fade + bright rim -
    skills.h draws 1 / 2 / 4 of them rotated for the sentry gun's 90 / 180 / 360 degree arcs"""
    n = 256; y, x = np.mgrid[0:n, 0:n] + 0.5; r = np.hypot(x, n - y) / n
    a = np.clip(0.25 * r ** 2, 0, 1) * (r < 1) + np.exp(-((r - 0.985) / 0.012) ** 2) * 0.9
    a += 0.6 * (np.exp(-(x / 1.5) ** 2) + np.exp(-((n - y) / 1.5) ** 2)) * (r < 1)
    a = np.clip(a, 0, 1); c = np.dstack([np.full_like(a, 255), np.full_like(a, 255), np.full_like(a, 255), a * 255]).astype(np.uint8)
    return Image.fromarray(c, 'RGBA')
def pillar():
    """vertical light pillar (placement beacon of the DMD drone / deploy marker): white core, soft edges, fades upwards"""
    w, h = 32, 256; x = (np.arange(w) + 0.5) / w * 2 - 1; y = (np.arange(h) + 0.5) / h
    a = np.exp(-(x / 0.25) ** 2)[None, :] * (1 - y[:, None]) ** 0.5 + 0.0 * y[:, None]
    c = np.dstack([np.full((h, w), 255.0)] * 3 + [np.clip(a, 0, 1) * 255]).astype(np.uint8)
    return Image.fromarray(c[::-1], 'RGBA')   # bright at the bottom (v1) -> top fades
def heat_band():
    """the heat-scan wave front: a horizontal band, bright leading edge, red-orange trail (drawn as a camera-facing ring
    segment strip by skills.h, or as an axis quad)"""
    w, h = 64, 64; y = (np.arange(h) + 0.5) / h
    a = np.exp(-((y - 0.8) / 0.05) ** 2) + 0.35 * np.clip((y - 0.2) / 0.6, 0, 1) * (y < 0.8)
    a = np.clip(a, 0, 1)[:, None].repeat(w, 1)
    c = np.dstack([np.full((h, w), 255.0), 120 + 135 * a, 60 + 100 * a, a * 255]).astype(np.uint8)
    return Image.fromarray(c, 'RGBA')
def hexflash():
    im = Image.open(SKPNG + 'as_dark_ally_tier2.png').convert('L').crop((0, 0, 512, 512)).resize((128, 128), Image.LANCZOS)
    a = np.asarray(im, np.float32) / 255
    yy, xx = np.mgrid[0:128, 0:128] + 0.5; r = np.hypot(xx - 64, yy - 64) / 64
    a = a * np.clip(1 - r, 0, 1) ** 0.7
    return Image.fromarray(np.dstack([np.full_like(a, 255)] * 3 + [a * 255]).astype(np.uint8), 'RGBA')
GEN = {'ele_grid': ele_grid, 'sector90': sector90, 'pillar': pillar, 'heat_band': heat_band, 'hexflash': hexflash}

SK_CELLS = [
 ('sk_expl',   sk('explosion_real3'), None, (256, 256), 'clear'),   # 4x4 fire flipbook (armlauncher_fire, missile hit)
 ('sk_smoke',  sk('smoke_bomb3'),     None, (256, 256), 'clear'),   # 8x8 puffs (armlauncher_missile_fire / _smoke)
 ('sk_flare',  sk('light'),           None, (64, 64),   'clear'),   # soft star flare (muzzle of the launcher, lasers)
 ('sk_lenz',   sk('camouflage_lenz_cen'), None, (64, 64), 'clear'), # blue-white lens dot (camo start/end, dive)
 ('sk_ring',   sk('ring_wh'),         None, (128, 128), 'clear'),   # thin ring (shield hit, EMP shock, pings)
 ('sk_dring',  sk('c_w_h_breakline'), None, (128, 128), 'clear'),   # dashed ring with 4 ticks (placement / ngon)
 ('sk_disk',   sk('c_w_linkable'),    None, (128, 128), 'clear'),   # filled disk + rim (placement footprint)
 ('sk_upline', sk('tachikoma_spawn_upline'), None, (32, 32), 'clear'),   # small bright square (rising ngon bits)
 ('sk_meta',   sk('metabust_start'),  None, (128, 128), 'clear'),   # columns of rising squares (metaboost / build)
 ('sk_ele',    'GEN:ele_grid',        None, (256, 128), 'clear'),   # 4x2 lightning sprites (EMP)
 ('sk_shard',  sk('b_exp_pt'),        None, (128, 128), 'clear'),   # 4x4 blue shards (EMP blast, shield break)
 ('sk_beam',   sk('drone_laser'),     None, (256, 32),  'clear'),   # laser beam strip (drone, turret laser sight)
 ('sk_box',    sk('box_line'),        None, (128, 128), 'clear'),   # cube outlines grid (dive hit, wall pass)
 ('sk_mosaic', sk('mosaic'),          None, (64, 64),   'clear'),   # checker blocks (dive / wall glitch)
 ('sk_scan',   sk('m_tachikoma_scan_base'), None, (128, 128), 'clear'),   # V scan cone (heat scan / recon / turret)
 ('sk_kal',    sk('kal'),             None, (32, 256),  'clear'),   # recon warning glyph column (icon above heads)
 ('sk_glow',   sk('explosion_sp'),    None, (128, 128), 'clear'),   # glow sphere (dive start, EMP core, meta start)
 ('sk_sprint', sk('screen_sprint'),   None, (64, 64),   'clear'),   # vertical streaks (rush trail)
 ('sk_sector', 'GEN:sector90',        None, (128, 128), 'clear'),   # 90-degree ground wedge (turret arc)
 ('sk_pillar', 'GEN:pillar',          None, (16, 128),  'clear'),   # vertical light pillar (deploy beacon)
 ('sk_heat',   'GEN:heat_band',       None, (32, 32),   'clear'),   # heat-scan wave front
 ('sk_hex',    'GEN:hexflash',        None, (128, 128), 'clear'),   # hex-grid flash (shield / wall hits)
 ('sk_spark',  sk('new_spark_r'),     None, (64, 64),   'clear'),   # spark star
]
base_load = BF.load_cell
def load_cell(png, crop, size):
    if png and png.startswith('SK:'):
        im = Image.open(SKPNG + png[3:].lower() + '.png').convert('RGBA')
        if crop: im = im.crop(crop)
        return im.resize(size, Image.LANCZOS)
    if png and png.startswith('GEN:'): return GEN[png[4:]]().resize(size, Image.LANCZOS)
    return base_load(png, crop, size)
BF.load_cell = load_cell

# ---------------------------------------------------------------- emitters
CY = lambda a0, a1=None: ((0, (120, 230, 255, a0)), (1, (120, 230, 255, a1 if a1 is not None else 0)))
WH = lambda a0: ((0, (255, 255, 255, a0)), (1, (255, 255, 255, 0)))
FADE = lambda c, a: ((0, c + (a,)), (0.6, c + (a,)), (1, c + (0,)))
SK_EM = {
 # --- markers for skills.h (count 0: spawning them does nothing; skills.h reads .cell to draw its own quads)
 **{'cm_' + c[0][3:]: E(c[0], count=(0, 0)) for c in SK_CELLS},
 # --- Motoko: therm-optic camo. camouflage_start / _end: a 0.67 s / 0.5 s shimmer of light-blue 'lens' dots and vertical
 # scan lines over the body (camouflage_line: illusion_line_base, 120 cm tall planes); drawn on the body volume
 'camo_dots':  E('sk_lenz', blend='add', count=(10, 12), life=(0.55, 0.15), size=(9, 4), size_end=0.2, speed=(25, 15), dir='sphere',
                 jitter=28, offset=95, colors=((0, (170, 220, 255, 255)), (0.5, (120, 200, 255, 200)), (1, (120, 200, 255, 0))), bright=1.3),
 'camo_lines': E('sk_beam', kind='axis', blend='add', count=(4, 5), life=(0.5, 0.15), size=(1.6, 0.4), aspect=38, anchor=1, dir='up',
                 jitter=22, offset=0, colors=((0, (160, 220, 255, 220)), (1, (160, 220, 255, 0)))),
 'camo_hit':   E('sk_ele', blend='add', grid=(4, 2), nfr=8, anim='random', count=(2, 3), life=(0.12, 0.05), size=(22, 8), rot=(-3.14, 3.14),
                 dir='sphere', jitter=25, offset=90, colors=((0, (150, 210, 255, 255)), (1, (150, 210, 255, 0))), bright=1.2),
 # --- Batou: arm launcher. armlauncher_fire: explosion_real3 4x4 additive flash, 0.133 s, stretched 2x10 along the shot;
 # 'light' flare 150 cm, alpha 1 -> 0 at 0.1 - 0.133 s. Missile trail (armlauncher_missile_fire / _smoke): smoke_bomb3 8x8,
 # fire puffs size 20 +-4, life 0.167, colour 255,102,0 -> 179,0,0; smoke puffs size 30, alpha 40 peak, rising (gravity -45)
 'al_flash':   E('sk_expl', kind='stretch', blend='add', grid=(4, 4), nfr=16, life=(0.133, 0), size=(14, 2), speed=(180, 0), dir='shot',
                 stretch=0.3, colors=((0, (255, 220, 170, 255)), (1, (255, 160, 80, 0))), bright=1.3),
 'al_light':   E('sk_flare', blend='add', life=(0.133, 0), size=(40, 0), colors=((0, (255, 200, 140, 255)), (0.75, (255, 200, 140, 255)), (1, (255, 200, 140, 0)))),
 'ms_fire':    E('sk_smoke', blend='add', grid=(8, 8), nfr=64, life=(0.167, 0), size=(10, 2), rot=(-1.24, 1.24),
                 colors=((0, (255, 102, 0, 255)), (0.5, (253, 71, 0, 133)), (1, (179, 0, 0, 0))), bright=1.4),
 'ms_smoke':   E('sk_smoke', grid=(8, 8), nfr=64, life=(0.9, 0.2), size=(15, 4), size_end=2.0, speed=(10, 8), spread=0.78, dir='sphere',
                 gravity=-45, rot=(-1.24, 1.24), colors=((0, (230, 230, 230, 0)), (0.3, (230, 230, 230, 90)), (0.5, (220, 220, 220, 45)), (1, (210, 210, 210, 0))), cull=6000),
 # missile impact (launcher rocket explosion, ~3 m blast): explosion_real3 fireball + smoke + sparks + flash
 'mx_fire':    E('sk_expl', blend='add', grid=(4, 4), nfr=16, count=(3, 4), life=(0.6, 0.15), size=(70, 20), size_end=1.4, speed=(260, 150),
                 dir='sphere', rot=(-3.14, 3.14), colors=((0, (255, 230, 190, 255)), (0.6, (255, 150, 70, 200)), (1, (200, 70, 20, 0))), bright=1.3),
 'mx_smoke':   E('sk_smoke', grid=(8, 8), nfr=64, count=(3, 4), delay=0.1, life=(1.6, 0.4), size=(80, 20), size_end=1.8, speed=(150, 80), dir='sphere',
                 drag=drag_k(0.5), gravity=-60, rot=(-3.14, 3.14), colors=((0, (90, 85, 80, 200)), (0.5, (100, 95, 90, 120)), (1, (110, 105, 100, 0))), cull=9000),
 'mx_spark':   E('spark', kind='stretch', blend='add', grid=(2, 2), nfr=4, anim='random', count=(10, 14), life=(0.5, 0.2), size=(2.2, 0.6), size_end=0.3,
                 speed=(1500, 700), dir='sphere', gravity=900, stretch=0.03, colors=((0, (255, 210, 150, 255)), (1, (255, 150, 60, 255))), bright=1.4, cull=7000),
 'mx_light':   E('sk_flare', blend='add', life=(0.2, 0), size=(320, 0), colors=((0, (255, 210, 160, 255)), (1, (255, 160, 90, 0)))),
 # --- deployables: the holographic "ngon" build (Turret / CyberWall / dmd / emp / SpecialRecon _ngon_complete: 2 s, cyan
 # outlines + c_w_linkable disk + rising squares tachikoma_spawn_upline) and the matching delete (dissolve)
 'ng_up':      E('sk_upline', kind='axis', blend='add', count=(10, 12), life=(0.9, 0.3), size=(3, 1.5), aspect=2.5, speed=(70, 40), dir='up',
                 jitter=45, colors=((0, (130, 235, 255, 255)), (0.7, (130, 235, 255, 180)), (1, (130, 235, 255, 0))), bright=1.2),
 'ng_flash':   E('sk_lenz', blend='add', life=(0.35, 0), size=(80, 0), size_end=1.6, offset=40, colors=((0, (140, 230, 255, 220)), (1, (140, 230, 255, 0)))),
 'ng_bits':    E('sk_upline', blend='add', count=(12, 14), life=(0.6, 0.25), size=(2.5, 1), size_end=0.3, speed=(160, 80), dir='sphere', jitter=30,
                 offset=40, gravity=-60, colors=((0, (150, 240, 255, 255)), (1, (150, 240, 255, 0))), bright=1.2),
 # destroyed deployable (turret / drone / emp / recon): small explosion, sparks, blue shards, smoke
 'dd_fire':    E('sk_expl', blend='add', grid=(4, 4), nfr=16, count=(2, 2), life=(0.45, 0.1), size=(40, 10), size_end=1.4, speed=(150, 80), dir='sphere',
                 rot=(-3.14, 3.14), colors=((0, (255, 235, 200, 255)), (1, (255, 120, 50, 0))), bright=1.2),
 'dd_shard':   E('sk_shard', blend='add', grid=(4, 4), nfr=16, anim='random', count=(8, 10), life=(0.5, 0.2), size=(5, 2), speed=(500, 250), dir='sphere',
                 gravity=600, rot=(-3.14, 3.14), spin=8, colors=((0, (180, 230, 255, 255)), (1, (180, 230, 255, 0))), bright=1.2),
 'dd_smoke':   E('sk_smoke', grid=(8, 8), nfr=64, count=(2, 2), delay=0.1, life=(1.4, 0.3), size=(45, 10), size_end=1.7, speed=(60, 40), dir='up',
                 gravity=-40, rot=(-3.14, 3.14), colors=((0, (60, 60, 60, 180)), (1, (90, 90, 90, 0))), cull=7000),
 # --- Ishikawa: sentry gun. turret_lazer_start / _loop (B_lenz_start, B_line_flare: cyan for allies, red for enemies) at
 # the laser node while it tracks; its gun uses the base MUZZLE_3P / TRACER effects
 'tu_lock':    E('sk_lenz', blend='add', life=(0.27, 0), size=(14, 0), size_end=0.6, colors=((0, (120, 230, 255, 255)), (1, (120, 230, 255, 0))), bright=1.5),
 # --- Borma: metaboost. meta_boost_start: rising columns of squares (metabust_start / metaboost.dds, 102 cm wide, z 37..148,
 # 0.27 s) + our_meta_boost_start rings; then a slow cyan sparkle while healing; endcorverfusion_hit_ally flash on each tick
 'mb_cols':    E('sk_meta', kind='axis', blend='add', count=(4, 5), life=(0.8, 0.2), size=(16, 4), aspect=3.5, anchor=1, speed=(60, 20), dir='up',
                 jitter=35, colors=((0, (120, 240, 255, 230)), (0.6, (120, 240, 255, 160)), (1, (120, 240, 255, 0))), bright=1.2),
 'mb_flash':   E('sk_lenz', blend='add', life=(0.4, 0), size=(70, 0), size_end=1.5, offset=100, colors=((0, (140, 240, 255, 200)), (1, (140, 240, 255, 0)))),
 'mb_tick':    E('sk_upline', blend='add', count=(5, 6), life=(0.7, 0.2), size=(4, 1.5), speed=(60, 25), dir='up', jitter=30, offset=60,
                 colors=((0, (140, 255, 230, 255)), (1, (140, 255, 230, 0))), bright=1.2),
 # --- Paz: rush. sprint_1..4 / screen_sprint: white speed streaks behind the runner; impact = base HIT_BLOOD + flash
 'ru_trail':   E('sk_sprint', kind='axis', blend='add', count=(2, 3), life=(0.25, 0.08), size=(5, 2), aspect=6, dir='shot', jitter=30, offset=90,
                 colors=((0, (220, 235, 255, 160)), (1, (220, 235, 255, 0)))),
 'ru_hit':     E('sk_flare', blend='add', life=(0.15, 0), size=(60, 0), colors=((0, (255, 255, 255, 255)), (1, (255, 230, 200, 0)))),
 # --- Azuma: plasma shield. shield_hit: ring_wh expanding (2 -> 100 cm in 0.2 s) + tut_inter_sp flash; break = shards + flash
 'sh_ring':    E('sk_ring', blend='add', life=(0.2, 0), size=(6, 0), size_end=6, colors=((0, (170, 240, 255, 255)), (1, (170, 240, 255, 0))), bright=1.3),
 'sh_hex':     E('sk_hex', blend='add', life=(0.25, 0), size=(22, 0), size_end=1.4, colors=((0, (120, 230, 255, 200)), (1, (120, 230, 255, 0)))),
 'sb_shard':   E('sk_shard', blend='add', grid=(4, 4), nfr=16, anim='random', count=(14, 16), life=(0.6, 0.2), size=(7, 3), speed=(450, 250), dir='sphere',
                 gravity=500, rot=(-3.14, 3.14), spin=10, colors=((0, (200, 245, 255, 255)), (1, (160, 230, 255, 0))), bright=1.3),
 'sb_flash':   E('sk_lenz', blend='add', life=(0.3, 0), size=(140, 0), size_end=1.6, colors=((0, (230, 250, 255, 255)), (1, (150, 230, 255, 0)))),
 # --- Maven: cyber wall. pass-through glitch on the intruder (cyber_dive_enemy_hit style: box_line / mosaic blocks)
 'wp_box':     E('sk_box', blend='add', count=(5, 6), life=(0.4, 0.15), size=(12, 5), dir='sphere', jitter=30, offset=90, rot=(0, 0),
                 colors=((0, (255, 90, 90, 230)), (1, (255, 90, 90, 0))), bright=1.2),
 'wp_mos':     E('sk_mosaic', blend='add', count=(3, 4), life=(0.3, 0.1), size=(10, 4), dir='sphere', jitter=25, offset=100,
                 colors=((0, (255, 140, 140, 200)), (1, (255, 140, 140, 0)))),
 # --- Sitara: DMD drone laser hit (dmd_enemy_damage: dive_sprite + glow) and arrival flash
 'dl_hit':     E('sk_flare', blend='add', life=(0.08, 0.02), size=(18, 6), rot=(-3.14, 3.14), colors=((0, (255, 120, 90, 255)), (1, (255, 80, 60, 0))), bright=1.5),
 'dl_spark':   E('spark', kind='stretch', blend='add', grid=(2, 2), nfr=4, anim='random', count=(1, 2), life=(0.15, 0.05), size=(1.2, 0.4), size_end=0.3,
                 speed=(600, 300), spread=1.2, dir='reflect', gravity=600, stretch=0.03, colors=((0, (255, 170, 120, 255)), (1, (255, 120, 80, 255))), bright=1.3),
 # --- Reiko: cyber dive. cyber_dive_ally_start: rising cyan bits (tachikoma_spawn_upline, 300 cm/s), glow sphere, rings;
 # cyber_dive_enemy_hit: box_line cubes + mosaic on the victim (1 s)
 'dv_up':      E('sk_upline', kind='axis', blend='add', count=(14, 16), life=(0.33, 0.1), size=(3.5, 1), aspect=3, speed=(300, 80), dir='up', jitter=35,
                 colors=((0, (140, 240, 255, 255)), (1, (140, 240, 255, 0))), bright=1.3),
 'dv_glow':    E('sk_glow', blend='add', life=(0.5, 0), size=(60, 0), size_end=0.3, offset=90, colors=((0, (180, 240, 255, 220)), (1, (120, 220, 255, 0)))),
 'dh_box':     E('sk_box', blend='add', count=(8, 10), life=(0.6, 0.3), size=(14, 6), dir='sphere', jitter=35, offset=90,
                 colors=((0, (255, 255, 255, 230)), (0.5, (255, 80, 80, 200)), (1, (255, 80, 80, 0))), bright=1.2),
 # --- Kurutsu: EMP. explosion_emp: B_exp_pt shards (size 12, life 0.13, 3000 cm/s), lightning bolts ele_cen01..07 in a
 # 600 x 600 x 880 cm box for 1 s, bg_small_lenz flare; emp_ch_ele: sparks on every character hit (3 s)
 'em_shard':   E('sk_shard', kind='stretch', blend='add', grid=(4, 4), nfr=16, anim='random', count=(16, 20), life=(0.2, 0.06), size=(8, 3), speed=(3000, 600),
                 dir='sphere', stretch=0.02, colors=((0, (150, 210, 255, 255)), (1, (150, 210, 255, 0))), bright=1.4),
 'em_bolt':    E('sk_ele', blend='add', grid=(4, 2), nfr=8, anim='random', count=(10, 12), life=(0.25, 0.12), delay=0.05, size=(120, 40), dir='sphere',
                 jitter=300, rot=(-3.14, 3.14), colors=((0, (160, 210, 255, 255)), (1, (160, 210, 255, 0))), bright=1.3, cull=9000),
 'em_flash':   E('sk_lenz', blend='add', life=(0.4, 0), size=(300, 0), size_end=1.6, offset=60, colors=((0, (200, 230, 255, 255)), (1, (120, 180, 255, 0)))),
 'es_ele':     E('sk_ele', blend='add', grid=(4, 2), nfr=8, anim='random', count=(2, 3), life=(0.1, 0.05), size=(28, 10), dir='sphere', jitter=25, offset=95,
                 rot=(-3.14, 3.14), colors=((0, (160, 210, 255, 255)), (1, (160, 210, 255, 0))), bright=1.3),
 'ec_spark':   E('sk_lenz', blend='add', count=(1, 2), life=(0.2, 0.05), size=(8, 3), dir='sphere', jitter=10, offset=50, rot=(-3.14, 3.14),
                 colors=((0, (150, 210, 255, 255)), (1, (150, 210, 255, 0))), bright=1.3),
 # --- Togusa: special recon. Mini-Tachikoma lands (dust) and pings: a lens flare at its eye
 're_ping':    E('sk_lenz', blend='add', life=(0.3, 0), size=(16, 0), size_end=2.0, offset=25, colors=((0, (150, 230, 255, 255)), (1, (150, 230, 255, 0)))),
 # --- Saito: heat scan start: red-orange flash on the scanner's eye level
 'hs_flash':   E('sk_flare', blend='add', life=(0.3, 0), size=(40, 0), size_end=1.5, offset=160, colors=((0, (255, 120, 60, 230)), (1, (255, 60, 30, 0)))),
}
SK_EFFECTS = [
 ('SK_CAMO_ON',  ['camo_dots', 'camo_lines'], 0),
 ('SK_CAMO_OFF', ['camo_dots', 'camo_lines'], 0),
 ('SK_CAMO_HIT', ['camo_hit'], 0),
 ('SK_AL_FIRE',  ['al_flash', 'al_light'], 0),
 ('SK_MS_TRAIL', ['ms_fire', 'ms_smoke'], 0),
 ('SK_MS_EXP',   ['mx_fire', 'mx_smoke', 'mx_spark', 'mx_light'], 0),
 ('SK_BUILD',    ['ng_up', 'ng_flash'], 0),
 ('SK_UNBUILD',  ['ng_bits', 'ng_flash'], 0),
 ('SK_DESTROY',  ['dd_fire', 'dd_shard', 'dd_smoke', 'mx_spark'], 0),
 ('SK_TU_LOCK',  ['tu_lock'], 0),
 ('SK_META_ON',  ['mb_cols', 'mb_flash'], 0),
 ('SK_META_TICK', ['mb_tick'], 0),
 ('SK_RUSH',     ['ru_trail'], 0),
 ('SK_RUSH_HIT', ['ru_hit'], 0),
 ('SK_SH_HIT',   ['sh_ring', 'sh_hex'], 0),
 ('SK_SH_BREAK', ['sb_shard', 'sb_flash'], 0),
 ('SK_WALL_PASS', ['wp_box', 'wp_mos'], 0),
 ('SK_DL_HIT',   ['dl_hit', 'dl_spark'], 0),
 ('SK_DIVE_ON',  ['dv_up', 'dv_glow'], 0),
 ('SK_DIVE_HIT', ['dh_box'], 0),
 ('SK_EMP_EXP',  ['em_shard', 'em_bolt', 'em_flash'], 0),
 ('SK_EMP_SHOCK', ['es_ele'], 0),
 ('SK_EMP_CHG',  ['ec_spark'], 0),
 ('SK_RE_PING',  ['re_ping'], 0),
 ('SK_HS_FLASH', ['hs_flash'], 0),
]
# cell directory for skills.h: effects C_DIR0, C_DIR1, ... each list up to 6 zero-count marker emitters, in SK_CELLS order
# (cell k of SK_CELLS = fx_em[fx_eff[C_DIR(k / 6)].em[k % 6]].cell) - 4 effect slots instead of one per cell
CELL_FX = [('C_DIR%d' % (k // 6), ['cm_' + c[0][3:] for c in SK_CELLS[k:k + 6]], 0) for k in range(0, len(SK_CELLS), 6)]

# ---------------------------------------------------------------- b80: the destruction deaths (engine/src/bparts.h)
# A headshot kill (or a heavy hit) blows the head (an arm, a leg) off: effect.dat character\body_arm_explosion.nif /
# body_all_explosion.nif (lens flash, new_spark sparks, tachikoma_m_p_* machine-part debris), body_arm_break_a/b.nif (the
# stump's cen_ele crackle, 30/s for ~2 s) and body_arm_break_blood.nif (BloodSplatter squirt); the Paz video EP19 (271.7,
# 336.5, 410.6 s) adds the ~2 m red cloud and shows the stump and the flying head glowing orange. Base effect.dat textures
# (FX png); no skills.h markers (effects found by name: DX_*).
DX_CELLS = [
 ('dx_ele',   'cen_ele',              None, (128, 128), 'clear'),   # 4x4 lightning sprites (the stump's crackle)
 ('dx_parts', 'tachikoma_m_p_b',      None, (192, 192), 'clear'),   # 6x6 turning machine parts (debris)
 ('dx_splat', 'bloodsplatter_frames', None, (192, 128), 'clear'),   # 6x4 blood squirt
]
OR = lambda a: ((0, (255, 205, 130, a)), (0.4, (255, 145, 55, a)), (1, (255, 95, 25, 0)))
DX_EM = {
 # PArray997: bg_small_lenz_cen, 387 cm, alpha 0 -> 1 (0.033 s) -> 0 (0.1 s), additive: the white flash (footage 271.70 s)
 'dx_flash':  E('white', blend='add', life=(0.12, 0), size=(150, 0), colors=((0, (255, 248, 240, 0)), (0.3, (255, 248, 240, 255)), (1, (255, 248, 240, 0))), bright=1.2),
 # PArray993: new_spark, 60 particles, 450 +- 270 cm/s, gravity 450 (orange streaks in the footage; 10-14 here)
 'dx_sparks': E('spark', kind='stretch', blend='add', grid=(2, 2), nfr=4, anim='random', count=(10, 14), life=(0.55, 0.25), size=(1.6, 0.5), size_end=0.4,
                speed=(450, 270), dir='sphere', gravity=450, stretch=0.035, colors=OR(255), bright=1.5, cull=5000),
 # PArray999 / 1000: tachikoma_m_p_b / _e 6x6 flipbooks, 15 / 10 cm, 450 +- 270 cm/s, gravity 450 (2.4 s in the NIF: 1.3 s here)
 'dx_debris': E('dx_parts', grid=(6, 6), nfr=36, anim='loop', fps=30, count=(5, 7), life=(1.3, 0.3), size=(7.5, 2.5), speed=(450, 270), dir='sphere',
                gravity=450, rot=(-3.14, 3.14), colors=((0, (205, 205, 205, 255)), (0.85, (205, 205, 205, 255)), (1, (205, 205, 205, 0))), cull=4000),
 # the red cloud (footage: ~2 m wide round the head): blood_smoke2 / cen_blood / blood_A at full size
 'dx_mist':   E('blood_smoke', blend='add', grid=(8, 8), nfr=64, count=(2, 3), life=(1.1, 0.2), size=(105, 20), size_end=1.4, rot=(-3.14, 3.14),
                speed=(70, 40), dir='sphere', colors=((0, (235, 235, 235, 235)), (0.5, (120, 120, 120, 120)), (1, (0, 0, 0, 0))), cull=7000),
 'dx_boom':   E('blood_boom', blend='add', grid=(2, 2), nfr=4, life=(0.16, 0), size=(95, 0), rot=(-3.14, 3.14), bright=1.6,
                colors=((0, (255, 255, 255, 255)), (0.5, (255, 255, 255, 128)), (1, (255, 255, 255, 0)))),
 'dx_spray':  E('blood', grid=(5, 5), nfr=25, count=(3, 4), life=(0.833, 0.1), size=(55, 12), speed=(320, 140), dir='sphere', gravity=675,
                rot=(-0.5, 0.5), colors=((0, (255, 255, 255, 255)), (0.5, (255, 255, 255, 128)), (1, (255, 255, 255, 0))), cull=6000),
 # body_arm_break ele: cen_ele 4x4, 40 +- 20 cm, 0.033 s, 30/s (bparts.h spawns it ~15/s for 1.7 s); orange as the footage's stump
 'dx_ele':    E('dx_ele', blend='add', grid=(4, 4), nfr=16, anim='random', life=(0.07, 0.02), size=(16, 7), rot=(-3.14, 3.14), dir='up',
                colors=((0, (255, 195, 95, 255)), (1, (255, 125, 40, 200))), bright=1.6, cull=4000),
 'dx_stsp':   E('spark', kind='stretch', blend='add', grid=(2, 2), nfr=4, anim='random', count=(0, 2), life=(0.3, 0.12), size=(1.0, 0.3), size_end=0.3,
                speed=(220, 120), spread=0.9, dir='up', gravity=700, stretch=0.03, colors=OR(255), bright=1.4, cull=3500),
 'dx_glow':   E('white', blend='add', life=(0.09, 0.02), size=(9, 3), colors=((0, (255, 140, 50, 170)), (1, (255, 100, 30, 0))), cull=4000),
 # body_arm_break_blood: BloodSplatter_Frames 6x4, 30 +- 15 cm, 240 +- 144 cm/s, gravity 675 (bparts.h: ~9/s for 1.2 s)
 'dx_drip':   E('dx_splat', grid=(6, 4), nfr=24, life=(0.8, 0.1), size=(13, 6), speed=(200, 110), spread=0.6, dir='up', gravity=675, rot=(-0.6, 0.6),
                colors=((0, (255, 255, 255, 255)), (0.5, (255, 255, 255, 128)), (1, (255, 255, 255, 0))), cull=4000),
 # off the flying part: embers + a soft orange glow
 'dx_ember':  E('spark', kind='stretch', blend='add', grid=(2, 2), nfr=4, anim='random', count=(1, 2), life=(0.35, 0.15), size=(0.8, 0.3), size_end=0.2,
                speed=(80, 50), dir='sphere', gravity=-40, stretch=0.04, colors=OR(220), bright=1.3, cull=3500),
 'dx_halo':   E('white', blend='add', life=(0.1, 0), size=(15, 3), colors=((0, (255, 130, 40, 110)), (1, (255, 90, 20, 0))), cull=4000),
}
DX_EFFECTS = [
 ('DX_BURST', ['dx_flash', 'dx_sparks', 'dx_debris', 'dx_mist', 'dx_boom', 'dx_spray'], 0),
 ('DX_STUMP', ['dx_ele', 'dx_stsp', 'dx_glow'], 0),
 ('DX_DRIP',  ['dx_drip'], 0),
 ('DX_EMBER', ['dx_ember'], 0),   # (dx_halo kept out: drawn through the head it washed the whole head orange - emu b80-2)
]

def build(cells, em, effects, decals, out, ah):
    BF.CELLS = cells; BF.AH = ah
    atlas, rects, used = BF.pack()
    names = list(rects.keys()); cid = {n: i for i, n in enumerate(names)}
    emn = list(em.keys()); eid = {n: i for i, n in enumerate(emn)}
    assert len(effects) <= 64, ('fx.h accepts at most 64 effects', len(effects))
    assert len(emn) <= 256 and len(names) <= 256
    levels = [atlas]; cur = Image.fromarray(atlas)
    for m in range(1, BF.NMIP): cur = cur.resize((BF.AW >> m, ah >> m), Image.BOX); levels.append(np.asarray(cur))
    with open(out, 'wb') as f:
        f.write(b'FXG1'); f.write(struct.pack('<HHI', BF.AW, ah, BF.NMIP))
        for lv in levels:
            h_, w_ = lv.shape[:2]; b = etcpak.compress_to_dxt5(np.ascontiguousarray(lv).tobytes(), w_, h_); f.write(struct.pack('<I', len(b))); f.write(b)
        f.write(struct.pack('<I', len(names)))
        for n in names: f.write(struct.pack('<4f', *rects[n]))
        f.write(struct.pack('<I', len(emn)))
        for n in emn:
            e = em[n]; ck = list(e['colors'])[:4]
            while len(ck) < 4: ck.append(ck[-1])
            f.write(struct.pack('<12i', cid[e['cell']], e['kind'], e['blend'], e['fx'], e['fy'], e['nfr'], e['anim'], e['nmin'], e['nmax'], e['dir'], len(e['colors'][:4]), e['anchor']))
            f.write(struct.pack('<24f', e['fps'], e['delay'], e['life'], e['life_var'], e['size'], e['size_var'], e['aspect'], e['grow'], e['size_end'],
                e['speed'], e['speed_var'], e['spread'], e['offset'], e['jitter'], e['gravity'], e['drag'], e['rot'], e['rot_var'], e['spin'], e['stretch'],
                e['bright'], e['cull'], 0., 0.))
            f.write(struct.pack('<4f', *[k[0] for k in ck]))
            for k in ck: f.write(bytes(k[1]))
        f.write(struct.pack('<I', len(effects)))
        for name, ems, pick in effects:
            assert len(ems) <= 6 and len(name) <= 15, name
            f.write(name.encode().ljust(16, b'\0')); f.write(struct.pack('<8i', len(ems), pick, *([eid[x] for x in ems] + [-1] * (6 - len(ems)))))
        f.write(struct.pack('<I', len(decals)))
        for c, g, s, l in decals: f.write(struct.pack('<2i2f', cid[c], g, s, l))
    return atlas, rects, used, names, emn

def main():
    base_cells, base_em, base_eff = list(BF.CELLS), dict(BF.EM), list(BF.EFFECTS)
    dup = [n for n in list(SK_EM) + list(DX_EM) if n in base_em] + [c[0] for c in SK_CELLS + DX_CELLS if c[0] in [b[0] for b in base_cells]]
    dup += [n for n in DX_EM if n in SK_EM]
    assert not dup, ('name clash with build_fx.py tables', dup)
    cells = base_cells + SK_CELLS + DX_CELLS; em = dict(base_em); em.update(SK_EM); em.update(DX_EM); effects = base_eff + SK_EFFECTS + CELL_FX + DX_EFFECTS   # (b80: DX after the cell markers)
    atlas, rects, used, names, emn = build(cells, em, effects, BF.DECALS, OUT, 2048)
    os.makedirs(gitspaths.path('TMP', 'skills_out'), exist_ok=True)
    with open(gitspaths.path('TMP', 'skills_out', 'fx_sk_ids.txt'), 'w') as f:
        f.write('# %s: %d effects (%d base + %d skill + %d cell markers), %d emitters, %d cells, atlas 1024x2048 rows used %d\n' % (
            OUT, len(effects), len(base_eff), len(SK_EFFECTS), len(CELL_FX), len(emn), len(names), used))
        for i, (n, _, _) in enumerate(effects): f.write('FX_%s=%d\n' % (n, i))
        for i, n in enumerate(names): f.write('FXC_%s=%d\n' % (n.upper(), i))
    Image.fromarray(atlas).save(gitspaths.tmp('skills', 'fx_sk_atlas.png'))
    print(OUT, os.path.getsize(OUT), 'bytes; effects', len(effects), '(base', len(base_eff), ') emitters', len(emn), 'cells', len(names), 'atlas rows used', used)
    if SEP:   # skills only (+ the base decal table so that fx_init's checks pass)
        need = {c for c, *_ in BF.DECALS} | {e['cell'] for e in SK_EM.values()}
        cells2 = [c for c in base_cells if c[0] in need] + SK_CELLS
        em2 = dict(SK_EM); eff2 = SK_EFFECTS + CELL_FX
        atlas2, rects2, used2, names2, emn2 = build(cells2, em2, eff2, BF.DECALS, SEP, 1024)
        print(SEP, os.path.getsize(SEP), 'bytes; effects', len(eff2), 'emitters', len(emn2), 'cells', len(names2), 'atlas rows used', used2)

if __name__ == '__main__': main()
