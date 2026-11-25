#!/usr/bin/env python3
# check_clips.py (b122): every first-person model's everyday clips keep the gun where the hands are - the Grip bone's farthest
# distance from its place in the idle clip. b122 found seven guns (C-30, C-26A, F2000, UMP-45, MG36, SEBURO MG, KSG) whose jump
# clip threw it 98 cm out of the picture (the M4A1 set's jump decoded with the wrong rig, make_vm_weapon.py): the gun and the
# arms swung away and back with every jump.   check_clips.py [data folder]   -> exit 1 when a clip is more than 30 cm off
import sys, glob, os, numpy as np
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, H)
import preview as pv
D = sys.argv[1] if len(sys.argv) > 1 else os.path.join(H, '..', 'build', 'data')
CLIPS = ('walk', 'run', 'sprint', 'sprint_in', 'sprint_out', 'jump', 'fall', 'land', 'slide', 'ads_idle', 'ads_in')
bad = 0; n = 0
for p in sorted(glob.glob(os.path.join(D, 'vm_*.gvm'))):
    G = pv.read(p)
    if 'Grip' not in G['bones'] or 'idle' not in G['clips']: continue
    g = G['bones'].index('Grip'); idle = np.array([f[g, 4:] for f in G['clips']['idle'][2]]).mean(0); n += 1
    for c in CLIPS:
        if c not in G['clips']: continue
        d = np.linalg.norm(np.array([f[g, 4:] for f in G['clips'][c][2]]) - idle, axis=1).max()
        if d > 30: bad += 1; print('%s: clip %s takes the Grip %.0f cm from its idle place' % (os.path.basename(p), c, d))
print('check_clips: %d first-person models, %d clips more than 30 cm off' % (n, bad))
sys.exit(1 if bad else 0)
