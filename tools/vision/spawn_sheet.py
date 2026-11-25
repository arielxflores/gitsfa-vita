# Round-start hologram: footage (Demolition Geofront, 61 s) next to host renders of vision.h's spawn look.
# usage: python3 spawn_sheet.py [MAP] [OUTDIR]
import sys, os, math, pickle, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import host_vis, scenes
from make_sheets_lib import row, stack, ref, crop_around
MAP = sys.argv[1] if len(sys.argv) > 1 else 'underground'
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'out'); os.makedirs(OUT, exist_ok=True)
H = host_vis.Host(MAP)
cache = os.path.join(OUT, 'scenes_camo_%s.pkl' % MAP)
if os.path.exists(cache): S = pickle.load(open(cache, 'rb'))
else: S = scenes.find(H, 'open', 4, dists=(350, 500, 800, 1400), seed=11); pickle.dump(S, open(cache, 'wb'))
sc = S[0]
A = H.actor('reiko', sc['foot'], sc['tyaw'], 'idle', 0.3)
H.begin(sc['eye'], sc['yaw'], sc['pitch']); H.draw_body(A); n = crop_around(H.end(), A, H, 330, 360)
H.begin(sc['eye'], sc['yaw'], sc['pitch']); H.spawn_draw(A); h = crop_around(H.end(), A, H, 330, 360)
B = H.actor('bato', sc['foot'], sc['tyaw'], 'idle', 0.3)
H.begin(sc['eye'], sc['yaw'], sc['pitch']); H.spawn_draw(B); hb = crop_around(H.end(), B, H, 330, 360)
# own gun
import camo_sheet_vm as V
g = V.fp_spawn(H, sc)
r = row([(ref('spawn_holo_ally_geofront_t61.jpg'), 'original: teammate at round start (Geofront 61 s)'), (n, 'Vita host: normal'), (h, 'Vita host: round-start hologram'), (hb, 'Vita host: Batou'),
         (ref('spawn_holo_gun_geofront_t61.jpg'), 'original: own gun at round start (Geofront grade)'), (ref('spawn_holo_gun_pss9_respawn_t248.jpg'), 'original: own gun after a TDM respawn (PSS9 248 s)'), (g, 'Vita host: own gun (no grading)')], H=300,
        title='Spawn hologram (~3 s after a round start or a respawn): cyan holograms with hexagon cells (mint under the green map grades)')
r.save(os.path.join(OUT, 'sheet_6_round_start.jpg'), quality=88); print('ok')
