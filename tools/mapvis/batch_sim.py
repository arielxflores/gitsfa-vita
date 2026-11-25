#!/usr/bin/env python3
"""Simulates world.h's load-time batching (b35): opaque / alpha-tested meshes with the same material in the same
grid cell are merged (<= 65535 vertices per batch); blended meshes stay single (sorted back to front).
Prints draws and triangles per view (frustum culled by AABB, as w_visible) before / after, per cell size.
usage: batch_sim.py stem [stem ...]"""
import sys, os, math, numpy as np, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gmapio
from gcol import spawns
from render import persp, view
def vis(mvp, mn, mx):
    for p in range(6):
        r = p >> 1; s = -1 if p & 1 else 1; pl = mvp[3, :] + s * mvp[r, :]
        x = mx[0] if pl[0] >= 0 else mn[0]; y = mx[1] if pl[1] >= 0 else mn[1]; z = mx[2] if pl[2] >= 0 else mn[2]
        if pl[0] * x + pl[1] * y + pl[2] * z + pl[3] < 0: return False
    return True
def batches(M, cell):
    out = []; groups = collections.OrderedDict()
    for i, m in enumerate(M):
        if m['mode'] == 2 or cell <= 0: out.append(dict(mn=m['mn'], mx=m['mx'], tri=m['ni'] // 3, nv=m['nv'])); continue
        c = ((np.array(m['mn']) + np.array(m['mx'])) / 2)[:2] // cell
        key = (m['tex'], m['lm'], m['glow'], m['mode'], m['flags'], round(m['ref'], 4), round(m['glowk'], 4), tuple(round(x, 4) for x in m['tint']), int(c[0]), int(c[1]))
        groups.setdefault(key, []).append(m)
    for key, ms in groups.items():
        cur = None
        for m in ms:
            if cur is None or cur['nv'] + m['nv'] > 65535:
                cur = dict(mn=list(m['mn']), mx=list(m['mx']), tri=0, nv=0); out.append(cur)
            cur['mn'] = [min(a, b) for a, b in zip(cur['mn'], m['mn'])]; cur['mx'] = [max(a, b) for a, b in zip(cur['mx'], m['mx'])]; cur['tri'] += m['ni'] // 3; cur['nv'] += m['nv']
    return out
for stem in sys.argv[1:]:
    g = gmapio.read('/root/gits/build/maps/%s.gmap' % stem, decode=False); M = g['mesh']
    sp = spawns('/root/gits/build/maps/%s.gcol' % stem); sp = sp[::max(1, len(sp) // 10)]
    views = []
    for gi, t, x, y, z, yw in sp:
        for yaw in (0, 90, 180, 270):
            views.append(persp(math.radians(59), 960 / 544, 10, 12000) @ view(np.array((x, y, z + 152.)), math.radians(yaw), 0.))
    res = []
    for cell in (0, 1000, 2000, 4000, 1e9):
        B = batches(M, cell); D = []; T = []
        for mvp in views:
            d = [b for b in B if vis(mvp, b['mn'], b['mx'])]; D.append(len(d)); T.append(sum(b['tri'] for b in d))
        res.append('cell %s: %d batches, draws %.0f (max %d), tris %.0fk' % ('none' if cell == 0 else 'whole map' if cell > 1e8 else '%d' % cell, len(B), np.mean(D), max(D), np.mean(T) / 1e3))
    print('%-12s ' % stem + ' | '.join(res), flush=True)
