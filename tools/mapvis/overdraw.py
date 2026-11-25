#!/usr/bin/env python3
"""Fragment counts per frame, by what the Vita's world_draw shades (render, b35): measure before optimizing the GPU side.

The SGX is a tile-based deferred renderer with hidden-surface removal: an opaque pixel is shaded ONCE (the visible
layer), but every alpha-tested, blended and sky-layer fragment that passes the depth test is shaded and blended.
So the fill cost of a view is roughly
    visible opaque pixels x opaque shader + test fragments x test shader + blended fragments x blend shader
    + sum over sky layers (pixels where no opaque surface is) x sky shader
This tool renders each class from the map's spawn points (eye height 152 cm, 4 yaws, 59 deg vertical fov, 960x544, as
the game) and prints the mean fragments per frame of each class, and of each sky layer.

usage: overdraw.py <stem> [<stem> ...]      (maps from /root/gits/build/maps)
"""
import sys, os, math, struct, json
import numpy as np, moderngl
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gmapio
from gcol import spawns
from render import persp, view

MAPS = os.environ.get('MAPS', '/root/gits/build/maps')
W, H = 960, 544
VS = '''#version 330
in vec3 pos; uniform mat4 mvp; uniform float far;
void main(){ vec4 p = mvp*vec4(pos,1.0); if (far > 0.5) p.z = p.w*0.99999; gl_Position = p; }'''
FS = '''#version 330
out vec4 o; void main(){ o = vec4(1.0, 0.0, 0.0, 1.0); }'''


def read_sky(path):
    if not os.path.exists(path):
        return []
    d = open(path, 'rb').read()
    mg, ver, nl, nt = struct.unpack_from('<4sIII', d, 0); o = 16
    for i in range(nt):
        w, h, fmt, n = struct.unpack_from('<HHBB', d, o); o += 6
        for k in range(n):
            s = struct.unpack_from('<I', d, o)[0]; o += 4 + s
    L = []
    for i in range(nl):
        tex, flags = struct.unpack_from('<ii', d, o); o += 8
        tint = struct.unpack_from('<4f', d, o); o += 16
        o += 8; rotz, fog = struct.unpack_from('<2f', d, o); o += 8
        nv, ni = struct.unpack_from('<II', d, o); o += 8
        V = np.frombuffer(d, '<f4', nv * 5, o).reshape(nv, 5); o += nv * 20
        I = np.frombuffer(d, '<u2', ni, o); o += ni * 2 + (2 if ni % 2 else 0)
        L.append(dict(tex=tex, flags=flags, tint=tint, P=V[:, :3].copy(), I=I.copy()))
    return L


class Counter:
    def __init__(s):
        s.ctx = moderngl.create_standalone_context(backend='egl')
        s.prog = s.ctx.program(vertex_shader=VS, fragment_shader=FS)
        s.col = s.ctx.texture((W, H), 1, dtype='f4')
        s.dep = s.ctx.depth_texture((W, H))
        s.fbo = s.ctx.framebuffer([s.col], s.dep)

    def vao(s, P, I):
        vb = s.ctx.buffer(np.ascontiguousarray(P, dtype='f4').tobytes())
        ib = s.ctx.buffer(np.ascontiguousarray(I, dtype='u4').tobytes())
        return s.ctx.vertex_array(s.prog, [(vb, '3f', 'pos')], ib)

    def count(s, items, mvp, far=0.0):
        """fragments of `items` (vao, two_sided) passing the depth test, no depth writes"""
        ctx = s.ctx
        s.col.write(np.zeros((H, W), 'f4').tobytes())
        ctx.enable(moderngl.BLEND); ctx.blend_func = (moderngl.ONE, moderngl.ONE)
        ctx.depth_mask = False
        s.prog['far'].value = far
        for va, two in items:
            if two: ctx.disable(moderngl.CULL_FACE)
            else: ctx.enable(moderngl.CULL_FACE)
            va.render(moderngl.TRIANGLES)
        ctx.depth_mask = True; ctx.disable(moderngl.BLEND)
        return float(np.frombuffer(s.col.read(), 'f4').sum())


def analyse(stem, C):
    g = gmapio.read(os.path.join(MAPS, stem + '.gmap'), decode=False)
    sky = read_sky(os.path.join(MAPS, stem + '.gsky'))
    cls = {'opaque': [], 'test': [], 'blend': [], 'blend_ref': [], 'blend_add': []}
    for m in g['mesh']:
        P = m['V'][:, :3]; va = C.vao(P, m['I'])
        if m['mode'] == 0: cls['opaque'].append((va, m['two']))
        elif m['mode'] == 1: cls['test'].append((va, m['two']))
        else:
            cls['blend'].append((va, m['two']))
            if m['ref'] > 0: cls['blend_ref'].append((va, m['two']))
            if m['flags'] & 8: cls['blend_add'].append((va, m['two']))
    skyv = [(C.vao(L['P'], L['I']), L) for L in sky]
    sp = spawns(os.path.join(MAPS, stem + '.gcol')); sp = sp[::max(1, len(sp) // 10)]
    acc = {k: 0.0 for k in ['visible_opaque', 'test', 'blend', 'blend_ref', 'blend_add', 'sky_pixels']}
    accs = [0.0] * len(sky); nview = 0
    ctx = C.ctx; C.fbo.use(); ctx.viewport = (0, 0, W, H); ctx.enable(moderngl.DEPTH_TEST); ctx.depth_func = '<='
    ctx.front_face = 'ccw'; ctx.cull_face = 'back'
    for gi, t, x, y, z, yw in sp:
        for yaw in (0, 90, 180, 270):
            P = persp(math.radians(59), W / H, 10, 12000); V = view(np.array((x, y, z + 152.0)), math.radians(yaw), 0.0)
            mvp = (P @ V).astype('f4')
            C.prog['mvp'].write(mvp.T.tobytes())
            # depth of the opaque + alpha-tested world (their visible pixels are shaded once)
            C.fbo.clear(0, 0, 0, 0, depth=1.0); ctx.depth_mask = True
            C.prog['far'].value = 0.0
            for va, two in cls['opaque'] + cls['test']:
                if two: ctx.disable(moderngl.CULL_FACE)
                else: ctx.enable(moderngl.CULL_FACE)
                va.render(moderngl.TRIANGLES)
            d = np.frombuffer(C.dep.read(), 'f4')
            covered = float((d < 1.0).sum())
            acc['visible_opaque'] += covered; acc['sky_pixels'] += W * H - covered
            acc['test'] += C.count(cls['test'], mvp)
            for k in ('blend', 'blend_ref', 'blend_add'):
                acc[k] += C.count(cls[k], mvp)
            for i, (va, L) in enumerate(skyv):
                if L['flags'] & 4:
                    # centred on the camera
                    Vc = V.copy(); Vc[:3, 3] = 0.0
                    m2 = (P @ Vc).astype('f4'); C.prog['mvp'].write(m2.T.tobytes())
                    accs[i] += C.count([(va, 1)], m2, far=1.0)
                    C.prog['mvp'].write(mvp.T.tobytes())
                else:
                    accs[i] += C.count([(va, 1)], mvp)
            nview += 1
    n = max(1, nview); px = W * H
    print('%-13s %d views | visible opaque %.0f%% of the screen, sky pixels %.0f%% | alpha-test fragments %.2f screens | blended %.2f screens (with discard ref>0: %.2f, additive %.2f) | sky layers %.2f screens: %s' % (
        stem, nview, 100 * acc['visible_opaque'] / n / px, 100 * acc['sky_pixels'] / n / px, acc['test'] / n / px, acc['blend'] / n / px, acc['blend_ref'] / n / px, acc['blend_add'] / n / px,
        sum(accs) / n / px, ' '.join('L%d%s=%.2f' % (i, '(far)' if L['flags'] & 4 else '', a / n / px) for i, ((va, L), a) in enumerate(zip(skyv, accs)))), flush=True)
    return dict(stem=stem, views=nview, **{k: v / n / px for k, v in acc.items()}, sky=[a / n / px for a in accs])


if __name__ == '__main__':
    C = Counter(); out = []
    for stem in sys.argv[1:]:
        out.append(analyse(stem, C))
    if os.environ.get('JSON'):
        json.dump(out, open(os.environ['JSON'], 'w'), indent=1)
