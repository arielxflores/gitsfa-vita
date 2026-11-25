#!/usr/bin/env python3
"""Host check of world.h's merged sky bands (b35): the two additive band layers drawn separately (each turned by its
rotz * t, blend SRC_ALPHA/ONE, as w_draw_sky did) vs. in one pass (mesh not turned, the turns as u offsets, blend
ONE/ONE, world.h program 17). Prints the max / mean difference per test time and writes a side-by-side image.
usage: sky_pair_check.py <stem> <layer a> <layer b> [out.png]"""
import sys, os, struct, math, numpy as np, moderngl
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gmapio
from render import persp, view
MAPS = '/root/gits/build/maps'; W, H = 960, 544
def read(stem):
    d = open('%s/%s.gsky' % (MAPS, stem), 'rb').read(); mg, ver, nl, nt = struct.unpack_from('<4sIII', d, 0); o = 16; T = []
    for i in range(nt):
        w, h, fmt, n = struct.unpack_from('<HHBB', d, o); o += 6; lv = []
        for k in range(n): s = struct.unpack_from('<I', d, o)[0]; o += 4; lv.append(d[o:o + s]); o += s
        T.append((w, h, fmt, lv))
    L = []
    for i in range(nl):
        tex, flags = struct.unpack_from('<ii', d, o); o += 8; tint = struct.unpack_from('<4f', d, o); o += 16; o += 8; rotz, fog = struct.unpack_from('<2f', d, o); o += 8
        nv, ni = struct.unpack_from('<II', d, o); o += 8; V = np.frombuffer(d, '<f4', nv * 5, o).reshape(nv, 5).copy(); o += nv * 20
        I = np.frombuffer(d, '<u2', ni, o).copy(); o += ni * 2 + (2 if ni % 2 else 0); L.append(dict(tex=tex, tint=tint, rotz=rotz, V=V, I=I))
    return T, L
def fit(V):   # as w_sky_cyl
    n = round(float(V[:, 3].max() - V[:, 3].min()))
    for sg in (-1, 1):
        k = sg * n / (2 * math.pi); z = np.exp(2j * math.pi * (V[:, 3] - k * np.arctan2(V[:, 1], V[:, 0]))).mean()
        if abs(z) > 0.999: return k, math.atan2(z.imag, z.real) / (2 * math.pi)
VS = '''#version 330
in vec3 pos; in vec2 uv0; uniform mat4 mvp; out vec2 t0; void main(){ vec4 p = mvp*vec4(pos,1.0); p.z = p.w*0.99999; gl_Position = p; t0 = uv0; }'''
FS1 = '''#version 330
in vec2 t0; uniform sampler2D dif; uniform vec4 tint; out vec4 o; void main(){ vec4 c = texture(dif, t0)*tint; o = vec4(c.rgb, c.a); }'''
FS2 = '''#version 330
in vec2 t0; uniform sampler2D dif; uniform vec4 tint; uniform vec4 po; out vec4 o;
void main(){ vec4 a = texture(dif, vec2(t0.x + po.x, t0.y))*tint; vec4 b = texture(dif, vec2(t0.x + po.y, t0.y))*tint; o = vec4(a.rgb*a.a + b.rgb*b.a, 1.0); }'''
def rotz(a):
    c, s = math.cos(a), math.sin(a); R = np.eye(4); R[0, 0], R[0, 1], R[1, 0], R[1, 1] = c, -s, s, c; return R
if __name__ == '__main__':
    stem, ia, ib = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); out = sys.argv[4] if len(sys.argv) > 4 else None
    T, L = read(stem); A, B = L[ia], L[ib]; kA, bA = fit(A['V']); kB, bB = fit(B['V'])
    ctx = moderngl.create_standalone_context(backend='egl'); p1 = ctx.program(vertex_shader=VS, fragment_shader=FS1); p2 = ctx.program(vertex_shader=VS, fragment_shader=FS2)
    w, h, fmt, lv = T[A['tex']]; img = gmapio.dds_decode(w, h, lv[0], fmt)
    tex = ctx.texture((w, h), 4, np.ascontiguousarray(img).tobytes()); tex.repeat_x = tex.repeat_y = True; tex.filter = (moderngl.LINEAR, moderngl.LINEAR); tex.use(0)
    col = ctx.texture((W, H), 4); fbo = ctx.framebuffer([col]); fbo.use(); ctx.viewport = (0, 0, W, H)
    def va(prog, Lr): return ctx.vertex_array(prog, [(ctx.buffer(np.ascontiguousarray(Lr['V'], 'f4').tobytes()), '3f 2f', 'pos', 'uv0')], ctx.buffer(np.ascontiguousarray(Lr['I'], 'u4').tobytes()))
    vA1, vB1, vA2 = va(p1, A), va(p1, B), va(p2, A)
    worst = 0; rows = []
    for t in (0.0, 7.3, 141.9, 1777.0):
        for yaw in (0, 100, 220):
            P = persp(math.radians(59), W / H, 10, 12000); V = view(np.zeros(3), math.radians(yaw), math.radians(8)); V[:3, 3] = 0
            ctx.enable(moderngl.BLEND); ctx.disable(moderngl.CULL_FACE)
            fbo.clear(0.05, 0.05, 0.1, 1); ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE)
            for vv, Lr in ((vA1, A), (vB1, B)):
                p1['mvp'].write((P @ V @ rotz(Lr['rotz'] * t)).astype('f4').T.tobytes()); p1['tint'].value = Lr['tint']; vv.render()
            sep = np.frombuffer(fbo.read(components=3), np.uint8).reshape(H, W, 3).astype(int)
            fbo.clear(0.05, 0.05, 0.1, 1); ctx.blend_func = (moderngl.ONE, moderngl.ONE)
            oa = -kA * A['rotz'] * t; ob = bB - bA - kB * B['rotz'] * t
            p2['mvp'].write((P @ V).astype('f4').T.tobytes()); p2['tint'].value = A['tint']; p2['po'].value = (oa - math.floor(oa), ob - math.floor(ob), 0, 0); vA2.render()
            mer = np.frombuffer(fbo.read(components=3), np.uint8).reshape(H, W, 3).astype(int)
            d = np.abs(sep - mer); worst = max(worst, d.max())
            print('t %7.1f yaw %3d: max diff %d/255, mean %.3f, pixels > 2: %.4f%%' % (t, yaw, d.max(), d.mean(), 100 * (d.max(-1) > 2).mean()))
            if len(rows) < 3: rows.append(np.concatenate([sep, mer, np.clip(d * 16, 0, 255)], 1)[::-1])
    if out: Image.fromarray(np.concatenate(rows, 0).astype(np.uint8)).resize((1440, 272 * len(rows))).save(out)
    print('worst', worst)
