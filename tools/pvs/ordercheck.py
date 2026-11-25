#!/usr/bin/env python3
"""render b45: does world.h's chunk split change any pixel? Per spawn view (eye height, 4 yaws) the depth-writing world
(opaque + alpha-tested meshes, LEQUAL as the game) is drawn twice into a triangle-ID buffer: as b44 (whole meshes, the
file's triangle order) and as b45 (each mesh's triangles sorted by chunk, only the chunks whose box is in the frustum).
A pixel whose front triangle differs = a change (a culled chunk that was visible, or two coplanar triangles that now
draw in the other order). usage: ordercheck.py <stem>"""
import sys, os, math
import numpy as np, moderngl
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import pvslib as L
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mapvis')); from gcol import spawns
W, H = 960, 544
stem = sys.argv[1]; meshes, chunks = L.load(stem); S = L.Scene(meshes, chunks, W, H); ctx = S.ctx
# b44 order: one vertex array per mesh, ids = global original triangle index
off = np.cumsum([0] + [len(m['I']) for m in meshes]); vam = []
for i, m in enumerate(meshes):
    vb = ctx.buffer(np.ascontiguousarray(m['V'][m['I'].ravel()], dtype='f4').tobytes()); vam.append(ctx.vertex_array(S.pt, [(vb, '3f', 'pos')]))
orig = np.zeros(S.ntri, np.int64)   # chunk-ordered triangle -> original global index
for c in chunks: orig[c['base']:c['base'] + len(c['tris'])] = off[c['mesh']] + c['tris']
dw = [m['mode'] != 2 and m['occ'] or m['mode'] == 1 for m in meshes]
cnt = dict(i44=0, i45=0, d44=0, d45=0)
def draw(mvp, split):
    S.fbo.use(); ctx.viewport = (0, 0, W, H); S.fbo.clear(depth=1.0); S.ids.write(np.full((H, W), -1, 'i4').tobytes())
    ctx.enable(moderngl.DEPTH_TEST); ctx.depth_func = '<='; ctx.front_face = 'ccw'; ctx.cull_face = 'back'; ctx.depth_mask = True
    S.pt['mvp'].write(mvp.astype('f4').T.tobytes()); inb = S.boxes_in(mvp) if split else None
    for i, m in enumerate(meshes):
        if not dw[i] or not L.box_in(mvp, m['mn'], m['mx']): continue
        ctx.disable(moderngl.CULL_FACE) if m['two'] else ctx.enable(moderngl.CULL_FACE)
        if not split: S.pt['base'].value = int(off[i]); vam[i].render(moderngl.TRIANGLES); cnt['i44'] += 3 * len(m['I']); cnt['d44'] += 1; continue
        prev = False
        for ci in range(m['c0'], m['c1']):
            if os.environ.get("NOCULL") is None and m["c1"] - m["c0"] > 1 and not inb[ci]: prev = False; continue
            S.pt['base'].value = int(chunks[ci]['base']); S.vat[ci].render(moderngl.TRIANGLES); cnt['i45'] += 3 * len(chunks[ci]['tris']); cnt['d45'] += not prev; prev = True
    idv = np.frombuffer(S.ids.read(), 'i4').reshape(H, W).astype(np.int64)
    if split: idv = np.where(idv >= 0, orig[np.maximum(idv, 0)], -1)
    return idv
nv = 0; nd = 0; worst = 0
for gi, t, x, y, z, yw in spawns(os.path.join(L.MAPS, stem + '.gcol')):
    for k in range(4):
        yaw = yw + k * math.pi / 2; e = np.array((x, y, z + 152.0)); f = (math.cos(yaw), math.sin(yaw), 0)
        mvp = L.persp(math.radians(59), W / H, 10, 12000) @ L.look(e, f, (0, 0, 1))
        a = draw(mvp, False); b = draw(mvp, True); d = int((a != b).sum()); nv += 1; nd += d; worst = max(worst, d)
print('%-13s %d views: %d pixels differ in all (worst view %d) | depth-writing world indices/view %.0fk -> %.0fk (%.0f%%), draws %.0f -> %.0f' % (stem, nv, nd, worst,
      cnt['i44'] / nv / 1e3, cnt['i45'] / nv / 1e3, 100 * (cnt['i45'] / max(1, cnt['i44']) - 1), cnt['d44'] / nv, cnt['d45'] / nv), flush=True)
if os.environ.get('WHY'):
    tri_pos = np.concatenate([np.sort(m['V'][m['I']].reshape(-1, 3, 3).round(2).view([('x', 'f4'), ('y', 'f4'), ('z', 'f4')]).reshape(-1, 3), axis=1).view('f4').reshape(-1, 9) for m in meshes])
    same = other = 0; kinds = {}
    for gi, t, x, y, z, yw in spawns(os.path.join(L.MAPS, stem + '.gcol')):
        for k in range(4):
            yaw = yw + k * math.pi / 2; e = np.array((x, y, z + 152.0)); f = (math.cos(yaw), math.sin(yaw), 0)
            mvp = L.persp(math.radians(59), W / H, 10, 12000) @ L.look(e, f, (0, 0, 1))
            a = draw(mvp, False); b = draw(mvp, True); dd = a != b
            for ta, tb in set(zip(a[dd].tolist(), b[dd].tolist())):
                if ta < 0 or tb < 0: other += 1; continue
                if np.allclose(tri_pos[ta], tri_pos[tb]): same += 1
                else:
                    ma = np.searchsorted(off, ta, 'right') - 1; mb = np.searchsorted(off, tb, 'right') - 1; kinds[(ma == mb)] = kinds.get(ma == mb, 0) + 1
    print('differing pairs: identical positions %d, other %d, different triangles (same mesh: count) %s' % (same, other, kinds))
