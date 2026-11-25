#!/usr/bin/env python3
# Side-by-side sheets: footage frame (scaled to the Vita's 960 x 544) | our host render (tools/tank/host/run.sh output), one
# pair per row with labels. usage: compare.py <footage dir> <render dir> <out dir>
# footage dir: the frames named below (TTM videos a/m/n/t<sec>.jpg, early-access tutorial full_<sec>.jpg)
import sys, os
from PIL import Image, ImageDraw
F, R, O = sys.argv[1:4]; os.makedirs(O, exist_ok=True)
PAIRS = [
    # later footage (2016-17 build): Berutalube (Adman314 a<sec>), Ruined Tokyo (Machina Media m<sec>, nopproblem n<sec>)
    ('ttm_top_hud', 'a744.jpg', 't_live_hud.png', (0, 0, 960, 150), 'TTM top: panels (owner / tank name / Hacker Detected / HP), score "n 5 n", bar with the sides\' end rings, "X 2", "<name> capturing"'),
    ('ttm_hack_code', 'a796.jpg', 'u_code_hud.png', None, 'Hack running (later build): "HACK DETECTED", "Bypassing encryption...", 10-digit code, "Complete hacking to shut down defensive protocols."'),
    ('ttm_ready', 'a672.jpg', 't_ready_hud.png', (0, 0, 960, 272), '"TACHIKOMA READY / PRESS [E] KEY" -> PRESS (SELECT); the call-in gauge full (cyan) at the bottom'),
    ('ttm_gauge', 'n79.jpg', 't_ready_hud.png', (560, 440, 960, 544), 'Call-in gauge: "[E] <hexagon> 100" -> (SELECT) <hexagon> 100'),
    ('ttm_secured', 'm188.jpg', 't_secured_hud.png', (0, 0, 960, 272), '"Terminal secured! / Deploying Tachikoma." (a call-in right after the capture)'),
    ('capture_point', 'm676.jpg', 't_live_hud.png', None, 'Dashed "CAPTURE POINT" ring above the live terminal + its tags; our tank\'s tag "<owner>\'s / <tank>" with HP bar'),
    ('timer_warning', 'a1100.jpg', 't_timewarn_hud.png', (0, 0, 960, 272), '"Timer Warning / Time is running out!" at 0:20 (+ Aramaki on the radio, drawn by menu.h)'),
    # older footage (Terminal Conquest Underground Base - Sitara, t<sec>) and the early-access tutorial (full_<sec>)
    ('ttm_hack_prompt', 't408.jpg', 'u_prompt_hud.png', None, 'Hack prompt next to an enemy tank: "Hack Think Tank / PRESS [R]" (ours: Square)'),
    ('ea_hack_code', 'full_585.5.jpg', 'u_code_ea_hud.png', None, 'Early Access tutorial look: code mid-screen + "Complete hacking to shut down defensive protocols."'),
    ('defences_down', 'full_597.5.jpg', 'u_open.png', None, 'Defences down: the tank glows red'),
    ('placement', 't392.jpg', 't_place_hud.png', None, 'Call-in placement: hologram of the tank where the player looks, "Click to deploy" -> (R) to deploy, (SELECT) cancel'),
    ('targeted', 't360.jpg', 'ai_fire_hud.png', None, 'An enemy tank aims at the player: red laser, lock sign, "TARGETED BY THINK TANK"'),
    ('ttm_banner', 't440.jpg', 't_shutdown_hud.png', (0, 0, 960, 272), '"Think Tank defensive protocols shutting down!" banner (+ the notice line)'),
]
for name, f, r, crop, label in PAIRS:
    fp = os.path.join(F, f); rp = os.path.join(R, r)
    if not (os.path.exists(fp) and os.path.exists(rp)): print('skip', name); continue
    a = Image.open(fp).convert('RGB').resize((960, 544), Image.LANCZOS); b = Image.open(rp).convert('RGB').resize((960, 544))
    if crop: a = a.crop(crop); b = b.crop(crop)
    w, h = a.size; S = Image.new('RGB', (w * 2 + 8, h + 26), (24, 24, 24)); S.paste(a, (0, 26)); S.paste(b, (w + 8, 26))
    d = ImageDraw.Draw(S); d.text((4, 6), 'FOOTAGE ' + f + ' (scaled to 960x544)', fill=(255, 220, 80)); d.text((w + 12, 6), 'OURS (host render) - ' + label, fill=(120, 230, 255))
    S.save(os.path.join(O, name + '.jpg'), quality=88); print(name)
