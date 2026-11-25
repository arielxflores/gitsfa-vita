#!/usr/bin/env python3
"""Shaded fragments and processed vertices per world program, per viewpoint (render b38, GPU cost model).

The SGX543 is a tile-based deferred renderer with hidden-surface removal. Per frame the world costs
  - opaque surfaces: every VISIBLE pixel is shaded once by the program of the surface it shows;
  - alpha-tested surfaces (they discard): every fragment in front of the opaque depth is shaded;
  - blended surfaces: every fragment in front of the opaque + tested depth is shaded and blended;
  - sky layers: far layers only where no world surface is (they sit at the far plane); near layers depth-tested;
  - vertices: every vertex of every mesh whose box is in the view (world_draw culls whole meshes) runs the vertex program.
Fragments are counted at 960x544 (the game's resolution; 2x MSAA is folded into the fitted costs). Views: the map's
spawn points at eye height (152 cm), N yaws, level.

Each mesh / sky layer gets its program from assign.c's JSON (world.h's own world_draw on the host GL stand-in), for
both builds compared (e.g. b32 and b37) and all four program sets, so one pass serves every configuration.
usage: fragcount.py <stem> <assign_a.json> <assign_b.json> <out.json> [yaws]
"""
import sys, os, math, json
import numpy as np, moderngl
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mapvis'))
import gmapio
from gcol import spawns
from render import persp, view
from overdraw import read_sky

MAPS = os.environ.get('MAPS', '/root/gits/build/maps')
W, H = 960, 544
VS = '''#version 330
in vec3 pos; uniform mat4 mvp; uniform float far;
void main(){ vec4 p = mvp*vec4(pos,1.0); if (far > 0.5) p.z = p.w*0.99999; gl_Position = p; }'''
FS_CNT = '''#version 330
out vec4 o; void main(){ o = vec4(1.0, 0.0, 0.0, 1.0); }'''
FS_ID = '''#version 330
uniform int bid; out int o; void main(){ o = bid; }'''


def frustum_visible(mvp, mn, mx):
    c = np.array([[x, y, z, 1.0] for x in (mn[0], mx[0]) for y in (mn[1], mx[1]) for z in (mn[2], mx[2])]) @ mvp.T
    for ax in range(3):
        if (c[:, ax] > c[:, 3]).all() or (c[:, ax] < -c[:, 3]).all(): return False
    return not (c[:, 3] < 0).all()


def main():
    stem, fa, fb, out = sys.argv[1:5]; nyaw = int(sys.argv[5]) if len(sys.argv) > 5 else 4
    A = json.load(open(fa)); B = json.load(open(fb))
    g = gmapio.read(os.path.join(MAPS, stem + '.gmap'), decode=False)
    sky = read_sky(os.path.join(MAPS, stem + '.gsky'))
    ctx = moderngl.create_standalone_context(backend='egl')
    pc = ctx.program(vertex_shader=VS, fragment_shader=FS_CNT); pid = ctx.program(vertex_shader=VS, fragment_shader=FS_ID)
    col = ctx.texture((W, H), 1, dtype='f4'); ids = ctx.texture((W, H), 1, dtype='i4'); dep = ctx.depth_texture((W, H))
    fbo_c = ctx.framebuffer([col], dep); fbo_i = ctx.framebuffer([ids], dep)
    # buckets: meshes with the same class and the same programs in both builds and all sets
    bkey = []; bindex = {}; mb = []
    for i, m in enumerate(g['mesh']):
        cls = 'opaque' if m['mode'] == 0 else 'test' if m['mode'] == 1 else 'blend'
        k = (cls, tuple(A['meshes'][i]), tuple(B['meshes'][i]))
        if k not in bindex: bindex[k] = len(bkey); bkey.append(k)
        mb.append(bindex[k])
    vaos = []
    for i, m in enumerate(g['mesh']):
        vb = ctx.buffer(np.ascontiguousarray(m['V'][:, :3], dtype='f4').tobytes()); ib = ctx.buffer(np.ascontiguousarray(m['I'], dtype='u4').tobytes())
        vaos.append((ctx.vertex_array(pc, [(vb, '3f', 'pos')], ib), ctx.vertex_array(pid, [(vb, '3f', 'pos')], ib), bool(m['two']), len(m['V'])))
    svao = []
    for L in sky:
        vb = ctx.buffer(np.ascontiguousarray(L['P'], dtype='f4').tobytes()); ib = ctx.buffer(np.ascontiguousarray(L['I'], dtype='u4').tobytes())
        svao.append((ctx.vertex_array(pc, [(vb, '3f', 'pos')], ib), L, len(L['P'])))
    sp = spawns(os.path.join(MAPS, stem + '.gcol'))
    views = []
    ctx.enable(moderngl.DEPTH_TEST); ctx.depth_func = '<='; ctx.front_face = 'ccw'; ctx.cull_face = 'back'; ctx.viewport = (0, 0, W, H)
    nb = len(bkey)
    for gi, t, x, y, z, yw in sp:
        for yi in range(nyaw):
            yaw = yw + 2 * math.pi * yi / nyaw
            P = persp(math.radians(59), W / H, 10, 12000); V = view(np.array((x, y, z + 152.0)), yaw, 0.0); mvp = (P @ V).astype('f4')
            vis = [frustum_visible(mvp, m['mn'], m['mx']) for m in g['mesh']]
            verts = np.zeros(nb);
            for i, m in enumerate(g['mesh']):
                if vis[i]: verts[mb[i]] += vaos[i][3]
            # 1. opaque + tested: visible pixel per bucket (ID buffer), depth kept
            fbo_i.use(); fbo_i.clear(depth=1.0); ids.write(np.full((H, W), -1, 'i4').tobytes()); ctx.depth_mask = True
            pid['mvp'].write(mvp.T.tobytes()); pid['far'].value = 0.0
            for i, m in enumerate(g['mesh']):
                if m['mode'] == 2 or not vis[i]: continue
                ctx.disable(moderngl.CULL_FACE) if vaos[i][2] else ctx.enable(moderngl.CULL_FACE)
                pid['bid'].value = mb[i]; vaos[i][1].render(moderngl.TRIANGLES)
            idv = np.frombuffer(ids.read(), 'i4'); vispx = np.bincount(idv[idv >= 0], minlength=nb).astype(float); sky_px = float((idv < 0).sum())
            fbo_c.use(); ctx.depth_mask = False; ctx.enable(moderngl.BLEND); ctx.blend_func = (moderngl.ONE, moderngl.ONE)
            pc['mvp'].write(mvp.T.tobytes()); pc['far'].value = 0.0

            def count(items, far=0.0, m2=None):
                col.write(np.zeros((H, W), 'f4').tobytes())
                if m2 is not None: pc['mvp'].write(m2.T.tobytes())
                pc['far'].value = far
                for va, two in items:
                    ctx.disable(moderngl.CULL_FACE) if two else ctx.enable(moderngl.CULL_FACE); va.render(moderngl.TRIANGLES)
                if m2 is not None: pc['mvp'].write(mvp.T.tobytes())
                pc['far'].value = 0.0
                return float(np.frombuffer(col.read(), 'f4').sum())
            # 2. blended fragments per bucket (depth of opaque + tested)
            blend = np.zeros(nb); testx = np.zeros(nb)
            for b, k in enumerate(bkey):
                if k[0] == 'blend':
                    it = [(vaos[i][0], vaos[i][2]) for i in range(len(mb)) if mb[i] == b and vis[i]]
                    if it: blend[b] = count(it)
                elif k[0] == 'test':   # fragments of tested surfaces in front of the world depth (an upper bound of the discard work)
                    it = [(vaos[i][0], vaos[i][2]) for i in range(len(mb)) if mb[i] == b and vis[i]]
                    if it: testx[b] = count(it)
            # 3. sky layers
            skyf = []
            for va, L, nv in svao:
                if L['flags'] & 4:
                    Vc = V.copy(); Vc[:3, 3] = 0.0; skyf.append(count([(va, True)], 1.0, (P @ Vc).astype('f4')))
                else: skyf.append(count([(va, True)]))
            ctx.disable(moderngl.BLEND); ctx.depth_mask = True
            views.append(dict(pos=[x, y, z], yaw=yaw, vis=vispx.tolist(), test=testx.tolist(), blend=blend.tolist(), verts=verts.tolist(), sky=skyf, sky_px=sky_px))
    json.dump(dict(stem=stem, buckets=[[k[0], list(k[1]), list(k[2])] for k in bkey], sky_nv=[n for _, _, n in svao], views=views), open(out, 'w'))
    v = np.array([sum(w['vis']) for w in views]); bl = np.array([sum(w['blend']) for w in views]); sk = np.array([sum(w['sky']) for w in views])
    print('%-13s %3d views, %2d buckets | opaque+tested visible %.2f screens, blended %.2f, sky %.2f, vertices %.0fk' % (stem, len(views), nb, v.mean() / W / H, bl.mean() / W / H, sk.mean() / W / H,
          np.mean([sum(w['verts']) for w in views]) / 1e3), flush=True)


if __name__ == '__main__':
    main()
