# Composites the sk_ovl.gtx cells over a game frame the way skills.h is meant to draw them (add / mul2x) -> out/overlays_preview.png
# usage: python3 preview_overlays.py background.png   (atlas PNG written by make_skill_overlays.py: /tmp/claude-0/skills/sk_ovl_atlas.png)
import sys, numpy as np
from PIL import Image, ImageDraw
BG = sys.argv[1] if len(sys.argv) > 1 else '/mnt/user-data/uploads/vita-agent-use/gitsfa/skills-study/tdm_377.png'
base = np.asarray(Image.open(BG).convert('RGB').resize((480, 272)), np.float32) / 255
at = np.asarray(Image.open('/tmp/claude-0/skills/sk_ovl_atlas.png').convert('RGB'), np.float32) / 255
T = {}
for l in open('/root/gits/tools/skills/out/sk_ovl.txt'):
    if l.startswith('#'): continue
    p = l.split(); T[p[0]] = (list(map(float, p[1:5])), p[5])
def cell(n, w=480, h=272):
    (u0, v0, u1, v1), b = T[n]; x0, y0, x1, y1 = int(u0 * 1024), int(v0 * 1024), int(u1 * 1024 + 1), int(v1 * 1024 + 1)
    return np.asarray(Image.fromarray((at[y0:y1, x0:x1] * 255).astype(np.uint8)).resize((w, h), Image.BILINEAR), np.float32) / 255, b
def comp(stack):
    img = base.copy()
    for n, tint in stack:
        c, b = cell(n)
        if n == 'laugh': cc = np.zeros_like(img); lc, _ = cell(n, 110, 110); cc[81:191, 185:295] = lc; c = cc
        img = img + c * np.array(tint) if b == 'add' else img * c * 2
    return np.clip(img, 0, 1)
cfg = [('camo', [('camo', (1, 1, 1))]), ('meta heal', [('meta', (1, 1, 1)), ('vig', (0.1, 0.5, 0.6))]), ('shield 1P', [('shield', (1, 1, 1))]), ('rush', [('rush', (0.8, 0.8, 0.8))]),
       ('cyber dive', [('dive_mul', (1, 1, 1)), ('dive', (1, 1, 1))]), ('emp hit', [('glitch', (0.4, 0.6, 1.0))]), ('wall pass', [('glitch', (0.2, 0.8, 0.3)), ('hlines', (1, 1, 1))]),
       ('heat pulse', [('heat_mul', (1, 1, 1)), ('vig', (0.9, 0.35, 0.15))]), ('laughing man', [('laugh', (1, 1, 1)), ('glitch', (0.3, 0.3, 0.3))]), ('dmd laser', [('vig', (0.9, 0.1, 0.05))]),
       ('shield hit', [('shield', (1, 1, 1)), ('shield_hit', (1, 1, 1))]), ('recon warn', [('vig', (1.0, 0.25, 0.1))])]
S = Image.new('RGB', (480 * 4, 286 * 3)); d = ImageDraw.Draw(S)
for i, (n, st) in enumerate(cfg):
    S.paste(Image.fromarray((comp(st) * 255).astype(np.uint8)), ((i % 4) * 480, (i // 4) * 286 + 14)); d.text(((i % 4) * 480 + 4, (i // 4) * 286), n, fill=(255, 255, 0))
S.save('/root/gits/tools/skills/out/overlays_preview.png')
