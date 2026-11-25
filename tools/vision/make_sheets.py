# Side-by-side sheets: footage of the original (tools/vision/ref) next to host renders of vision.h (host_vis.py) on the
# real Headquarters map. usage: python3 make_sheets.py [OUTDIR]  (default tools/vision/out)
import sys, os, math, numpy as np, pickle
from PIL import Image, ImageDraw
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import host_vis, scenes
from host_vis import VC
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'out'); os.makedirs(OUT, exist_ok=True)
REF = os.path.join(HERE, 'ref')

from make_sheets_lib import label, fit, row, stack, ref, crop_around

H = host_vis.Host('headquarter')
cache = os.path.join(OUT, 'scenes_hq.pkl')
if os.path.exists(cache): S = pickle.load(open(cache, 'rb'))
else:
    S = dict(low=scenes.find(H, 'low', 2), wall=scenes.find(H, 'wall', 2, seed=3), open=scenes.find(H, 'open', 3, dists=(400, 700, 1000), seed=5)); pickle.dump(S, open(cache, 'wb'))
LINE, INNER, ELINE, EINNER, HEAT, HEDGE = VC['VIS_ALLY_LINE'], VC['VIS_ALLY_INNER'], VC['VIS_ENEMY_LINE'], VC['VIS_ENEMY_INNER'], VC['VIS_HEAT'], VC['VIS_HEAT_EDGE']
def shot(sc, chr='bato', clip='idle', t=0.5, tyaw=None, fx=None, body=True):
    A = H.actor(chr, sc['foot'], sc['tyaw'] if tyaw is None else tyaw, clip, t); H.begin(sc['eye'], sc['yaw'], sc['pitch'])
    if body: H.draw_body(A)
    if fx: fx(A)
    return H.end(), A

# 1. teammate behind low cover (HQ footage: MAXMADD jumping behind the console)
sc = S['low'][1]; plain, A = shot(sc); vis, A = shot(sc, fx=lambda A: H.xray_line(A, LINE, INNER, 1.0, 0x80))
sc2 = S['wall'][0]; vis2, A2 = shot(sc2, 'saito', 'run_f', 0.3, fx=lambda A: H.xray_line(A, LINE, INNER, 1.0, 0x80))
r1 = row([(ref('ally_cover_hq_demolition_motoko_t16.jpg'), 'original: Demolition HQ (Motoko video) t=16 s'), (crop_around(plain, A, H), 'Vita host: no vision pass'),
          (crop_around(vis, A, H), 'Vita host: vision.h teammate outline'), (crop_around(vis2, A2, H), 'teammate fully behind a wall')], title='Teammates behind cover: teal outline where hidden, no fill (PostEdgePass look)')
r1.save(os.path.join(OUT, 'sheet_1_teammates.jpg'), quality=88)

# 2. heat scan + marked enemy
sc = S['wall'][1] if len(S['wall']) > 1 else S['wall'][0]
plain, A = shot(sc, 'motoko', 'run_f', 0.2); heat, A = shot(sc, 'motoko', 'run_f', 0.2, fx=lambda A: H.xray_fill(A, HEAT, HEDGE, 1.0, 0x80))
mark, A = shot(sc, 'motoko', 'run_f', 0.2, fx=lambda A: H.xray_line(A, ELINE, EINNER, 1.0, 0x80))
r2 = row([(ref('heat_tutorial_ingame_t408.jpg'), 'original: tutorial, heat sensor t=408 s'), (ref('heat_skillsync_video.jpg'), 'original: skillsync video (heat sensor)'),
          (crop_around(heat, A, H), 'Vita host: heat-scanned enemy'), (crop_around(mark, A, H), 'Vita host: recon / wall-marked enemy')], title='Enemies revealed through walls: heat = red silhouette over everything; marks = red outline where hidden (PostActorEnemyEdgePass)')
r2b = row([(ref('heat_tutorial_over_ally_t406.jpg'), 'original: heat silhouette drawn over a teammate in front'), (crop_around(plain, A, H), 'Vita host: enemy hidden, no pass')])
stack([r2, r2b]).save(os.path.join(OUT, 'sheet_2_revealed.jpg'), quality=88)

# 3. thermoptic camo: camo_sheet.py (sheet_5_camo_refraction.jpg)

# 4. eye glints + hologram look (for Think Tanks / pick-ups)
sc = S['open'][2] if len(S['open']) > 2 else S['open'][0]
def face_cam(sc): v = sc['eye'][:2] - sc['foot'][:2]; return math.atan2(v[1], v[0])
gl_e, A = shot(sc, 'bato', 'idle', 0.3, tyaw=face_cam(sc), fx=lambda A: H.glint(A, False, True))
gl_a, A = shot(sc, 'saito', 'idle', 0.3, tyaw=face_cam(sc), fx=lambda A: H.glint(A, True, True))
tur = dict(foot=sc['foot'], tyaw=face_cam(sc) + 0.7, eye=sc['eye'], yaw=sc['yaw'], pitch=sc['pitch'])
holo_r, T = shot(tur, host_vis.DATA + '/sk_turret.gvm', 'rest', 0, body=False, fx=lambda A: H.holo(A, (1.0, 0.2, 0.15)))
holo_c, T = shot(tur, host_vis.DATA + '/sk_turret.gvm', 'rest', 0, body=False, fx=lambda A: H.holo(A, (0.6, 0.95, 1.0)))
r4 = row([(ref('eye_glint_enemy_geofront_t544.jpg'), 'original: enemy visor streak (Geofront)'), (crop_around(gl_e, A, H, 300, 240), 'Vita host: enemy glint'), (crop_around(gl_a, A, H, 300, 240), 'Vita host: teammate glint'),
          (ref('thinktank_hacked_red_sitara_t411.jpg'), 'original: hacked Think Tank (red hologram)'), (crop_around(holo_r, T, H, 300, 240), 'vis_model VIS_HOLO red (turret stand-in)'), (crop_around(holo_c, T, H, 300, 240), 'VIS_HOLO cyan (pick-up look)')], H=240,
         title='Eye glints (effect\\character\\*_eye) and the hologram look other modules can call (vis_model)')
r4.save(os.path.join(OUT, 'sheet_4_glints_holo.jpg'), quality=88)
print('sheets in', OUT)
