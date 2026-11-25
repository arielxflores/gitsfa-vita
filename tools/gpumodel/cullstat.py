#!/usr/bin/env python3
"""What the world's frustum culling leaves for the GPU's vertex and tiling work (render b45; host, spawn views).

The SGX543 shades every vertex of a draw and culls triangles only after that (and the tiler bins every triangle that
survives clipping), so the vertex / tiling work is set by what the CPU submits. world.h world_draw submits every mesh
whose box is in the view frustum. Per view (the map's spawn points at eye height, N yaws, level, as fragcount.py) this
counts the indices that reach the GPU and what finer culling would leave:
  mesh     world_draw today: whole meshes whose box is in the frustum
  tri      triangles in the frustum (no finer CPU test can do better without occlusion)
  chunk    meshes split into CHUNK x CHUNK cm cells (each cell's triangles one draw): cells whose box is in the frustum
  chunkvis cells with at least one visible pixel (perfect occlusion culling per cell: a PVS / portal system's best case)
  trivis   triangles with at least one visible pixel (the floor; blended meshes count their in-frustum triangles)
and the draws each needs (a split mesh draws once per cell). usage: cullstat.py <stem> [yaws] [chunk cm] -> one JSON line
"""
import sys, os, math, json
import numpy as np, moderngl
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mapvis'))
import gmapio
from gcol import spawns
from render import persp, view

MAPS = os.environ.get('MAPS', '/root/gits/build/maps')
W, H = 960, 544
VS = '''#version 330
in vec3 pos; uniform mat4 mvp; void main(){ gl_Position = mvp*vec4(pos,1.0); }'''
FS_ID = '''#version 330
uniform int base; out int o; void main(){ o = base + gl_PrimitiveID; }'''


def box_in(mvp, mn, mx):
    c = np.array([[x, y, z, 1.0] for x in (mn[0], mx[0]) for y in (mn[1], mx[1]) for z in (mn[2], mx[2])]) @ mvp.T
    for ax in range(3):
        if (c[:, ax] > c[:, 3]).all() or (c[:, ax] < -c[:, 3]).all(): return False
    return not (c[:, 3] < 0).all()


def main():
    stem = sys.argv[1]; nyaw = int(sys.argv[2]) if len(sys.argv) > 2 else 4; CH = float(sys.argv[3]) if len(sys.argv) > 3 else 1000.0
    g = gmapio.read(os.path.join(MAPS, stem + '.gmap'), decode=False)
    ctx = moderngl.create_standalone_context(backend='egl')
    pid = ctx.program(vertex_shader=VS, fragment_shader=FS_ID)
    ids = ctx.texture((W, H), 1, dtype='i4'); dep = ctx.depth_texture((W, H)); fbo = ctx.framebuffer([ids], dep)
    M = []; base = 0; chunks = []   # chunks: (mesh, tri index array, mn, mx)
    for i, m in enumerate(g['mesh']):
        V = np.asarray(m['V'][:, :3], dtype='f8'); I = np.asarray(m['I'], dtype='i8').reshape(-1, 3)
        vb = ctx.buffer(np.ascontiguousarray(V, dtype='f4').tobytes()); ib = ctx.buffer(np.ascontiguousarray(I, dtype='u4').tobytes())
        M.append(dict(V=V, I=I, base=base, va=ctx.vertex_array(pid, [(vb, '3f', 'pos')], ib), two=bool(m['two']), mode=m['mode'], mn=m['mn'], mx=m['mx']))
        cen = V[I].mean(axis=1); cell = np.floor(cen[:, :2] / CH).astype('i8'); key = cell[:, 0] * 100003 + cell[:, 1]
        for k in np.unique(key):
            t = np.nonzero(key == k)[0]; P = V[I[t].ravel()]; chunks.append((i, t, P.min(0), P.max(0)))
        base += len(I)
    ntri = base; sp = spawns(os.path.join(MAPS, stem + '.gcol'))
    ctx.enable(moderngl.DEPTH_TEST); ctx.depth_func = '<='; ctx.front_face = 'ccw'; ctx.cull_face = 'back'; ctx.viewport = (0, 0, W, H)
    acc = {k: [] for k in ('mesh', 'tri', 'chunk', 'chunkvis', 'trivis', 'd_mesh', 'd_chunk', 'd_chunkvis')}
    for gi, t, x, y, z, yw in sp:
        for yi in range(nyaw):
            yaw = yw + 2 * math.pi * yi / nyaw
            P = persp(math.radians(59), W / H, 10, 12000); Vw = view(np.array((x, y, z + 152.0)), yaw, 0.0); mvp = (P @ Vw)
            vis = [box_in(mvp, m['mn'], m['mx']) for m in M]
            # triangles in the frustum (all three corners outside one plane = out; behind the eye = out)
            trin = np.zeros(ntri, bool)
            for i, m in enumerate(M):
                if not vis[i]: continue
                c = np.c_[m['V'], np.ones(len(m['V']))] @ mvp.T; T = c[m['I']]
                out = np.zeros(len(T), bool)
                for ax in range(3): out |= (T[:, :, ax] > T[:, :, 3]).all(1) | (T[:, :, ax] < -T[:, :, 3]).all(1)
                out |= (T[:, :, 3] < 1e-3).all(1)
                trin[m['base']:m['base'] + len(T)] = ~out
            # visible triangles (opaque + tested, depth-tested ID buffer); blended: their in-frustum triangles
            fbo.use(); fbo.clear(depth=1.0); ids.write(np.full((H, W), -1, 'i4').tobytes()); pid['mvp'].write(mvp.astype('f4').T.tobytes())
            for i, m in enumerate(M):
                if m['mode'] == 2 or not vis[i]: continue
                ctx.disable(moderngl.CULL_FACE) if m['two'] else ctx.enable(moderngl.CULL_FACE)
                pid['base'].value = m['base']; m['va'].render(moderngl.TRIANGLES)
            idv = np.frombuffer(ids.read(), 'i4'); trv = np.zeros(ntri, bool); trv[np.unique(idv[idv >= 0])] = True
            for i, m in enumerate(M):
                if m['mode'] == 2: trv[m['base']:m['base'] + len(m['I'])] = trin[m['base']:m['base'] + len(m['I'])]
            acc['mesh'].append(3 * sum(len(m['I']) for i, m in enumerate(M) if vis[i])); acc['d_mesh'].append(sum(vis))
            acc['tri'].append(3 * int(trin.sum())); acc['trivis'].append(3 * int(trv.sum()))
            cin = cv = dc = dv = 0
            for (i, tt, mn, mx) in chunks:
                if not vis[i] or not box_in(mvp, mn, mx): continue
                cin += 3 * len(tt); dc += 1
                if trv[M[i]['base'] + tt].any(): cv += 3 * len(tt); dv += 1
            acc['chunk'].append(cin); acc['chunkvis'].append(cv); acc['d_chunk'].append(dc); acc['d_chunkvis'].append(dv)
    r = dict(stem=stem, views=len(acc['mesh']), meshes=len(M), chunks=len(chunks), chunk_cm=CH, tris=ntri)
    for k, v in acc.items(): r[k] = float(np.mean(v)); r[k + '_p90'] = float(np.percentile(v, 90))
    print(json.dumps(r), flush=True)


if __name__ == '__main__':
    main()
