# Thermoptic camo: footage of the original (tools/vision/ref) next to host renders of vision.h's camo looks (vis_bot_body /
# vis_viewmodel with VIS_VS_R / VIS_FS_R), each frame's refraction sampling the frame before it as on the Vita.
# usage: python3 camo_sheet.py [MAP] [OUTDIR]     (default ruinednight = Neon Ruins, tools/vision/out)
import sys, os, math, pickle, numpy as np, moderngl
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import host_vis, scenes
from make_sheets_lib import row, stack, ref, crop_around
MAP = sys.argv[1] if len(sys.argv) > 1 else 'ruinednight'
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'out'); os.makedirs(OUT, exist_ok=True)
H = host_vis.Host(MAP)
cache = os.path.join(OUT, 'scenes_camo_%s.pkl' % MAP)
if os.path.exists(cache): S = pickle.load(open(cache, 'rb'))
else: S = scenes.find(H, 'open', 4, dists=(350, 500, 800, 1400), seed=11); pickle.dump(S, open(cache, 'wb'))

def tp(sc, kind, c=1.0, e=0.0, h=0.0, chr='motoko', clip='idle', t=0.3):
    A = H.actor(chr, sc['foot'], sc['tyaw'], clip, t)
    im = H.camo_shot(lambda: H.begin(sc['eye'], sc['yaw'], sc['pitch']), A, kind, c, e, h, frames=4)
    return crop_around(im, A, H, 380, 330)
def normal(sc, chr='motoko', clip='idle', t=0.3):
    A = H.actor(chr, sc['foot'], sc['tyaw'], clip, t); H.begin(sc['eye'], sc['yaw'], sc['pitch']); H.draw_body(A); return crop_around(H.end(), A, H, 380, 330)

# ---- first person: the viewmodel drawn the vis_viewmodel way (normal body at 1 - c, then the camo layer)
VM = host_vis.DATA + '/vm_m4a1.gvm'
def vm_actor(clip='idle', t=0.5, off=(-2.37, 0.08, 0.0)):
    g, texs = H.model(VM); ci = g.find_clip(clip); q, p = g.sample(ci if ci >= 0 else 0, t); pal = g.palette(q, p); sk = g.skin(pal); meshes = []
    for m, (P, N) in zip(g.mesh, sk):
        if m['blend'] == 2 or (m.get('lods', 0) and not (m['lods'] & 1)): continue
        buf = np.c_[P, N, m['uv']].astype('f4'); meshes.append(dict(tex=texs[m['tex']], vbo=H.ctx.buffer(buf.tobytes()), ibo=H.ctx.buffer(m['I'].astype('u4').tobytes()), n=m['ni']))
    K = np.eye(4); K[:3, 3] = off; return dict(meshes=meshes, M=K, yaw=0.0, chest=np.zeros(3))
def fp(sc, c=1.0, e=0.0, frames=4):
    A = vm_actor(); im = None
    def scene(): H.begin(sc['eye'], sc['yaw'], sc['pitch'])
    scene(); host_vis._grab_prev(H)
    for f in range(frames):
        scene(); host_vis.glDepthMask(1); host_vis.glClearDepth(1.0); host_vis.glClear(0x100)
        VP0, cam0 = H.VP, H.cam; H.VP = host_vis.persp(math.radians(49.75), 960 / 544, 2, 400); H.cam = np.zeros(3)
        ba = 1 - c
        if ba > 0.01:
            H.ctx.enable(moderngl.BLEND); H.ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE_MINUS_SRC_ALPHA); H.draw_body(A, tint=(1, 1, 1, ba), blend=ba < 0.99)
        if c > 0.01 or e > 0.01:
            host_vis._rdraw(H, A, host_vis._camo_rl('self', c, e, 0, 0, 3.0 + f / 60), pull=0.5 if ba > 0.01 else 0.0)
        H.VP, H.cam = VP0, cam0
        im = host_vis._grab_prev(H)
    return im.crop((400, 180, 960, 544))

sc, sc2 = S[0], S[min(1, len(S) - 1)]
r1 = row([(ref('camo_tut_cloaked_t320.jpg'), 'original: tutorial camo video, cloaked (320.5 s)'), (ref('camo_enemy_3v3_killcam_t303.jpg'), 'original: cloaked Motoko up close (3v3 killcam)'),
          (normal(sc), 'Vita host: the same spot, no camo'), (tp(sc, 'enemy'), 'Vita host: cloaked (lens, %.1f m)' % (np.linalg.norm(sc['foot'] - sc['eye']) / 100)),
          (tp(sc2, 'enemy', clip='run_f'), 'Vita host: cloaked, running (%.1f m)' % (np.linalg.norm(sc2['foot'] - sc2['eye']) / 100))], H=300,
         title='Therm-optic camo seen by an enemy: the background through a lens (displaced, slightly lighter), white edge glints')
r2 = row([(ref('camo_tut_uncloak_t323.0.jpg'), 'original: camo off (323.0 s): silver body, white edges'), (ref('camo_tut_uncloak_t323.25.jpg'), 'original: 323.25 s'),
          (tp(sc, 'enemy', c=0.55, e=1.0), 'Vita host: uncloak at 0.1 s (e = 1)'), (tp(sc, 'enemy', c=0.0, e=0.45), 'Vita host: uncloak at 0.3 s'),
          (tp(sc, 'enemy', c=0.5, e=0.9, h=0.0), 'Vita host: re-cloak after firing')], H=300,
         title='Cloak / uncloak / re-cloak: white edge lines (camouflage_start 0.67 s / _end 0.5 s); firing shows her until ~0.9 s after the last shot')
r3 = row([(ref('camo_ally_synced_neon_t113.5.jpg'), 'original: teammate with the shared camo (Neon 113.5 s)'), (ref('camo_allies_cyan_video.jpg'), 'original: tutorial SkillSync'),
          (tp(sc, 'ally', chr='bato'), 'Vita host: cloaked teammate'), (tp(sc2, 'ally', chr='saito', clip='run_f'), 'Vita host: cloaked teammate running'),
          (tp(sc, 'enemy', h=1.0), 'Vita host: enemy hit while cloaked (flicker)')], H=300,
         title='Teammates see a cloaked ally as a light-cyan hologram (+ cyan visor streak, eye glints)')
sf = S[min(2, len(S) - 1)]
r4 = row([(ref('camo_self_gun_neon_t13.5.jpg'), 'original: own gun cloaked (Neon 13.5 s)'), (fp(sf), 'Vita host: own gun cloaked'),
          (ref('camo_self_on_neon_t12.2.jpg'), 'original: camo on, 0.2 s'), (fp(sf, c=0.3, e=1.0), 'Vita host: camo on, 0.15 s'),
          (ref('camo_self_recloak_neon_t99.2.jpg'), 'original: re-cloak after firing (99.2 s)'), (fp(sf, c=0.4, e=0.9), 'Vita host: re-cloak')], H=260,
         title="The player's own view: the gun almost vanishes (the reticle glow stays, drawn by model.h), white edge lines on cloak / re-cloak; skills.h draws the white haze")
stack([r1, r2, r3, r4]).save(os.path.join(OUT, 'sheet_5_camo_refraction.jpg'), quality=88)
print('ok', os.path.join(OUT, 'sheet_5_camo_refraction.jpg'))
