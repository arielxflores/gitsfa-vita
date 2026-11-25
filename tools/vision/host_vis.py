# Host renders of vision.h on the real maps: the map through tools/mapvis/render.py (moderngl, llvmpipe), the characters
# CPU-skinned from build/data/chr_*.gvm (gvm.py), and GLSL ports of vision.h's VIS_VS / VIS_FS and model.h's MD shader,
# with the same depth / stencil / blend sequence as vision.h (vis_xray_line, vis_xray_fill, glass looks, glints).
# usage: python3 host_vis.py OUTDIR      (renders the scenes listed at the bottom; see make_sheets.py for the comparisons)
import sys, os, math, struct, ctypes, numpy as np, moderngl
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..', 'mapvis')); sys.path.insert(0, os.path.join(HERE, '..'))
import gvm
import render as mapr
DATA = '/root/gits/build/data'; MAPS = '/root/gits/build/maps'
import re
def vision_consts(path=os.path.join(HERE, '..', '..', 'engine', 'src', 'vision.h')):
    """the colour / size constants of vision.h (so the host renders use the engine's numbers)"""
    out = {}
    for m in re.finditer(r'(?:static const float |, )(VIS_\w+)\[[34]\] = \{([^}]*)\}', open(path).read()):
        out[m.group(1)] = tuple(float(x.strip().rstrip('f')) for x in m.group(2).split(','))
    for m in re.finditer(r'(VIS_LINE_PX|VIS_GLOW_PX|VIS_PULL) = ([0-9.]+)f', open(path).read()): out[m.group(1)] = float(m.group(2))
    for m in re.finditer(r'static const float (VIS_\w+)\[3\]\[[234]\] = \{(.*?)\};', open(path).read(), re.S):   # 2D tables
        out[m.group(1)] = [tuple(float(x.strip().rstrip('f')) for x in r.split(',')) for r in re.findall(r'\{([^{}]*)\}', m.group(2))]
    return out
VC = vision_consts()

# ---- raw GL for the stencil (moderngl has no stencil API); the moderngl context is current
_egl = ctypes.CDLL('libEGL.so.1'); _egl.eglGetProcAddress.restype = ctypes.c_void_p; _egl.eglGetProcAddress.argtypes = [ctypes.c_char_p]
def _gl(name, res, *args): return ctypes.CFUNCTYPE(res, *args)(_egl.eglGetProcAddress(name.encode()))
U, I = ctypes.c_uint, ctypes.c_int
glEnable = _gl('glEnable', None, U); glDisable = _gl('glDisable', None, U)
glStencilFunc = _gl('glStencilFunc', None, U, I, U); glStencilOp = _gl('glStencilOp', None, U, U, U); glStencilMask = _gl('glStencilMask', None, U)
glClearStencil = _gl('glClearStencil', None, I); glClear = _gl('glClear', None, U); glClearDepth = _gl('glClearDepthf', None, ctypes.c_float); glDepthMask = _gl('glDepthMask', None, ctypes.c_ubyte)
glGenRenderbuffers = _gl('glGenRenderbuffers', None, I, ctypes.POINTER(U)); glBindRenderbuffer = _gl('glBindRenderbuffer', None, U, U)
glRenderbufferStorage = _gl('glRenderbufferStorage', None, U, U, I, I); glFramebufferRenderbuffer = _gl('glFramebufferRenderbuffer', None, U, U, U, U)
GL_STENCIL_TEST = 0x0B90; GL_ALWAYS = 0x0207; GL_NOTEQUAL = 0x0205; GL_KEEP = 0x1E00; GL_REPLACE = 0x1E01; GL_STENCIL_BUFFER_BIT = 0x400

# ---- shaders (GLSL ports; same math as the Cg in vision.h / model.h)
MD_VS = '''#version 330
in vec3 pos; in vec3 nrm; in vec2 uv;
uniform mat4 mvp; uniform vec4 ldir; uniform vec4 fogp;
out vec2 t0; out float sh; out float fog;
void main(){ vec4 vp = mvp*vec4(pos,1.0); gl_Position = vp; t0 = uv;
 sh = 0.40 + 0.75*clamp(dot(normalize(nrm+vec3(0,0,0.0001)), ldir.xyz),0.0,1.0); fog = fogp.z*(1.0-exp(-max(vp.w-fogp.x,0.0)*fogp.y)); }'''
MD_FS = '''#version 330
in vec2 t0; in float sh; in float fog; uniform sampler2D dif; uniform vec4 tint; uniform vec4 fogc; out vec4 o;
void main(){ vec4 c = texture(dif,t0); if (c.a < 0.3) discard; o = vec4(mix(c.rgb*sh*tint.rgb, fogc.rgb, fog*fogc.a), tint.a); }'''
VIS_VS = '''#version 330
in vec3 pos; in vec3 nrm; in vec2 uv;
uniform mat4 mvp; uniform vec4 eyep; uniform vec4 infl; uniform vec4 pscl; uniform vec4 rdir; uniform vec2 half_;
out vec2 t0; out vec2 t1; out float fr;
void main(){
 vec3 s = pos; vec3 nn = normalize(nrm + vec3(0,0,0.0001)); vec3 ve = normalize(eyep.xyz - s);
 vec3 q = s + ve*infl.y;
 vec4 cp = mvp*vec4(q,1.0); vec4 ce = mvp*vec4(q + nn*8.0, 1.0);
 vec2 d = (ce.xy/max(ce.w,1.0) - cp.xy/max(cp.w,1.0))*half_;
 vec2 dn = d/(length(d)+0.0001);
 cp.xy = cp.xy + dn*vec2(infl.x/half_.x, infl.x/half_.y)*cp.w;
 gl_Position = cp; t0 = uv; t1 = vec2(dot(s, rdir.xyz), s.z)*pscl.x + pscl.zw; fr = 1.0 - abs(dot(nn, ve)); }'''
VIS_FS = '''#version 330
in vec2 t0; in vec2 t1; in float fr; uniform sampler2D dif; uniform sampler2D pat; uniform vec4 col; uniform vec4 rim; uniform vec4 prm; out vec4 o;
void main(){ float f2 = fr*fr; float r = mix(f2, f2*f2, prm.w); float p = mix(1.0, texture(pat,t1).r, prm.z); vec4 d = texture(dif,t0);
 vec3 c = (col.rgb + rim.rgb*r)*p + d.rgb*prm.y; float a = clamp((col.a + rim.a*r)*p + d.a*prm.x, 0.0, 1.0); o = vec4(c, a); }'''
R_VS = '''#version 330
in vec3 pos; in vec3 nrm; in vec2 uv;
uniform mat4 mvp; uniform vec4 eyep; uniform vec4 infl; uniform vec4 pscl; uniform vec4 rdir; uniform vec4 wob;
out vec2 t0; out vec4 t1; out float fr; out vec2 t3;
void main(){
 vec3 s = pos; vec3 nn = normalize(nrm + vec3(0,0,0.0001)); vec3 ve = normalize(eyep.xyz - s);
 vec3 q = s + ve*infl.y;
 vec4 cp = mvp*vec4(q,1.0); vec4 ce = mvp*vec4(q + nn*8.0, 1.0);
 float w = max(cp.w, 1.0);
 vec2 o = (ce.xy/max(ce.w,1.0) - cp.xy/w)*infl.z;
 float ol = length(o); o = o*(min(ol, infl.w)/(ol + 0.00001));
 o = o + vec2(sin(s.z*0.13 + wob.y*7.0), cos((s.x + s.y)*0.11 + wob.y*6.0))*wob.x;
 gl_Position = cp; t0 = uv;
 t1 = vec4((cp.w + cp.x + o.x*cp.w)*0.5*wob.z, (cp.w - (cp.y + o.y*cp.w)*wob.w)*0.5, 0.0, cp.w);
 t3 = vec2(dot(s, rdir.xyz), s.z)*pscl.x + pscl.zw; fr = 1.0 - abs(dot(nn, ve)); }'''
R_FS = '''#version 330
in vec2 t0; in vec4 t1; in float fr; in vec2 t3; uniform sampler2D dif; uniform sampler2D pat; uniform sampler2D scr;
uniform vec4 rfr; uniform vec4 col; uniform vec4 rim; uniform vec4 prm; uniform vec4 shd; out vec4 o;
void main(){ vec2 su = t1.xy/t1.w; vec4 b4 = texture(scr, vec2(su.x, 1.0 - su.y)); vec4 d = texture(dif, t0); vec4 pt = texture(pat, t3);
 float l = dot(d.rgb, vec3(0.30,0.59,0.11)); float f2 = fr*fr; float r = f2 + (f2*f2 - f2)*prm.w; float p = 1.0 + (pt.r - 1.0)*prm.z;
 vec3 c = b4.rgb*rfr.rgb*(1.0 - shd.x*(1.0 - l)) + col.rgb*(l*prm.x + prm.y) + rim.rgb*(r*p); o = vec4(c, clamp(rfr.a + col.a + rim.a*r, 0.0, 1.0)); }'''
SP_VS = '''#version 330
in vec3 pos; in vec2 uv; in vec4 col; uniform mat4 mvp; out vec2 t0; out vec4 c0;
void main(){ gl_Position = mvp*vec4(pos,1.0); t0 = uv; c0 = col; }'''
SP_FS = '''#version 330
in vec2 t0; in vec4 c0; uniform sampler2D tex; out vec4 o; void main(){ o = texture(tex,t0)*c0; }'''

# ---- the procedural textures of vision.h (vis_textures), same generator
def xorshift(sd):
    sd ^= (sd << 13) & 0xffffffff; sd ^= sd >> 17; sd ^= (sd << 5) & 0xffffffff; return sd & 0xffffffff, (sd >> 8) / 16777216.0
def vis_textures():
    sd = 0x2545F491
    blocks = np.zeros((64, 64), 'f4')
    def R():
        nonlocal sd; sd, v = xorshift(sd); return v
    for cy in range(8):
        for cx in range(8):
            v = 0.45 + 0.55 * R(); v2 = (0.45 + 0.55 * R()) if R() < 0.35 else v
            for y in range(8):
                for x in range(8):
                    blocks[cy * 8 + y, cx * 8 + x] = (v2 if x >= 4 else v) * (0.55 if (x == 0 or y == 0) else 1)
    lines = np.zeros((64, 64), 'f4')
    for y in range(64):
        v = 0.35 + 0.4 * R()
        if R() < 0.12: v = 1
        if R() < 0.15: v = 0.1
        for x in range(64): lines[y, x] = min(max(v * (0.85 + 0.15 * R()), 0), 1)
    gl = np.zeros((64, 128), 'f4')
    for y in range(64):
        for x in range(128):
            u = (x + 0.5) / 64 - 1; v = (y + 0.5) / 32 - 1; r = math.sqrt(u * u * 4 + v * v) * 0.5
            core = math.exp(-(r / 0.035) ** 2); streak = 0.55 * math.exp(-abs(u) / 0.16) * math.exp(-(v / 0.07) ** 2); halo = 0.22 * math.exp(-r / 0.09)
            l = min(max(core + streak + halo, 0), 1)
            if x in (0, 127) or y in (0, 63): l = 0
            gl[y, x] = l
    cv = [[0.45 + 0.4 * R() for i in range(4)] for j in range(4)]   # hexagons (vis_tex_hex)
    hx = np.zeros((64, 64), 'f4')
    for y in range(64):
        for x in range(64):
            d1 = d2 = 1e9; ci = cj = 0
            for j in range(-1, 5):
                for i in range(-1, 5):
                    cx = i * 16 + (8 if j & 1 else 0) + 8; cy = j * 16 + 8; dx = x + 0.5 - cx; dy = (y + 0.5 - cy) * 1.15; d = dx * dx + dy * dy
                    if d < d1: d2 = d1; d1 = d; ci, cj = i, j
                    elif d < d2: d2 = d
            e = math.sqrt(d2) - math.sqrt(d1); hx[y, x] = 1.0 if e < 1.2 else 0.8 if e < 2.4 else cv[(cj + 4) & 3][(ci + 4) & 3]
    return blocks, lines, gl, hx

# ---- collision rays (for composing scenes)
class Col:
    def __init__(s, stem):
        d = open('%s/%s.gcol' % (MAPS, stem), 'rb').read(); mg, ver, nv, nt = struct.unpack_from('<4sIII', d, 0)
        V = np.frombuffer(d, '<f4', nv * 3, 16).reshape(nv, 3); T = np.frombuffer(d, '<u4', nt * 3, 16 + nv * 12).reshape(nt, 3)
        o = 16 + nv * 12 + nt * 12; ns = struct.unpack_from('<i', d, o)[0]
        s.spawns = [struct.unpack_from('<ii4f', d, o + 4 + 24 * i) for i in range(ns)]
        mats = np.frombuffer(d, np.uint8, nt, o + 4 + 24 * ns) if ver >= 2 else np.zeros(nt, np.uint8)
        keep = (mats >> 6) & 2 if ver >= 3 else np.ones(nt, np.uint8) * 2   # TF_BULLET (flags in the high bits)
        T = T[keep > 0] if keep.any() else T
        s.A = V[T[:, 0]].astype('f8'); s.E1 = V[T[:, 1]] - s.A; s.E2 = V[T[:, 2]] - s.A
    def ray(s, o, d, maxd):
        o = np.asarray(o, 'f8'); d = np.asarray(d, 'f8'); d = d / np.linalg.norm(d)
        P = np.cross(d, s.E2); det = (s.E1 * P).sum(1); ok = np.abs(det) > 1e-9; inv = np.where(ok, 1 / np.where(ok, det, 1), 0)
        Tv = o - s.A; u = (Tv * P).sum(1) * inv; Q = np.cross(Tv, s.E1); v = (Q * d).sum(1) * inv; t = (s.E2 * Q).sum(1) * inv
        hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 0.5) & (t < maxd)
        return float(t[hit].min()) if hit.any() else -1.0
    def clear(s, a, b):
        a = np.asarray(a, 'f8'); b = np.asarray(b, 'f8'); d = b - a; l = np.linalg.norm(d); h = s.ray(a, d, l - 25); return h < 0

def persp(fovy, asp, n, f): return mapr.persp(fovy, asp, n, f)
def mview(p, yaw, pitch): return mapr.view(np.array(p, float), yaw, pitch)

class Host:
    def __init__(s, stem, W=960, H=544, ss=2):
        s.R = mapr.Renderer(W, H, ss); s.R.load(stem, gsky=None, lut=False); s.ctx = s.R.ctx; s.W, s.H, s.ss = W, H, ss; ctx = s.ctx   # lut=False: the Vita's graded shaders fail to compile (b29)
        col = ctx.renderbuffer((W * ss, H * ss)); s.fbo = ctx.framebuffer([col]); s.fbo.use()
        rb = U(0); glGenRenderbuffers(1, ctypes.byref(rb)); glBindRenderbuffer(0x8D41, rb.value); glRenderbufferStorage(0x8D41, 0x88F0, W * ss, H * ss)
        glFramebufferRenderbuffer(0x8D40, 0x821A, 0x8D41, rb.value); s.R.fbo = s.fbo
        s.md = ctx.program(vertex_shader=MD_VS, fragment_shader=MD_FS); s.vis = ctx.program(vertex_shader=VIS_VS, fragment_shader=VIS_FS)
        s.sp = ctx.program(vertex_shader=SP_VS, fragment_shader=SP_FS); s.vr = ctx.program(vertex_shader=R_VS, fragment_shader=R_FS); s.prev = None
        b, l, g, hx = vis_textures()
        def tex(a, rep=True):
            t = ctx.texture((a.shape[1], a.shape[0]), 4, np.dstack([a, a, a, np.ones_like(a)]).__mul__(255).astype(np.uint8).tobytes()); t.repeat_x = t.repeat_y = rep; t.filter = (moderngl.LINEAR, moderngl.LINEAR); return t
        s.t_blocks, s.t_lines, s.t_glint, s.t_hex = tex(b), tex(l), tex(g, False), tex(hx)
        s.models = {}; s.col = Col(stem); s.now = 3.0
    def model(s, name):
        if name not in s.models:
            g = gvm.GVM(name if name.endswith('.gvm') else '%s/chr_%s.gvm' % (DATA, name)); texs = []
            for t in g.tex:
                tx = s.ctx.texture((t.shape[1], t.shape[0]), 4, t.tobytes()); tx.build_mipmaps(); tx.filter = (moderngl.LINEAR_MIPMAP_LINEAR, moderngl.LINEAR); texs.append(tx)
            s.models[name] = (g, texs)
        return s.models[name]
    def actor(s, name, pos, yaw, clip='idle', t=0.3):
        g, texs = s.model(name); ci = g.find_clip(clip); q, p = g.sample(ci if ci >= 0 else 0, t); pal = g.palette(q, p); sk = g.skin(pal)
        meshes = []
        for m, (P, N) in zip(g.mesh, sk):
            if m['blend'] == 2 or (m.get('lods', 0) and not (m['lods'] & 1)): continue   # additive / not LOD 0
            buf = np.c_[P, N, m['uv']].astype('f4'); vbo = s.ctx.buffer(buf.tobytes()); ibo = s.ctx.buffer(m['I'].astype('u4').tobytes())
            meshes.append(dict(tex=texs[m['tex']], vbo=vbo, ibo=ibo, n=m['ni']))
        th = yaw + math.pi / 2; c, sn = math.cos(th), math.sin(th)
        M = np.array([[c, -sn, 0, pos[0]], [sn, c, 0, pos[1]], [0, 0, 1, pos[2]], [0, 0, 0, 1]])
        # head / chest points for glints (model.h: bone positions; here: from the skinned mesh top)
        allp = np.concatenate([P for P, N in sk]); top = allp[:, 2].max()
        head = M @ np.array([0, -3, top - 12, 1]); chest = M @ np.array([0, 0, top * 0.68, 1])
        return dict(meshes=meshes, M=M, pos=np.array(pos), yaw=yaw, head=head[:3], chest=chest[:3], top=top)
    def begin(s, cam, yaw, pitch, fov=59):
        ctx = s.ctx; s.fbo.use(); ctx.viewport = (0, 0, s.W * s.ss, s.H * s.ss)
        ctx.depth_mask = True; s.fbo.clear(*s.R.fog[:3], 1.0); glDepthMask(1); glClearDepth(1.0); glClearStencil(0); glClear(GL_STENCIL_BUFFER_BIT | 0x100)
        ctx.enable(moderngl.DEPTH_TEST); ctx.depth_func = '<='; ctx.front_face = 'ccw'; ctx.cull_face = 'back'
        s.P = persp(math.radians(fov), 960 / 544, 10, 12000); s.V = mview(cam, yaw, pitch); s.VP = s.P @ s.V; s.cam = np.array(cam, float)
        s.R.draw(s.VP, True, cam=s.cam, P=s.P, Vm=s.V)
    def draw_body(s, A, tint=(0.9, 0.9, 0.95, 1), blend=False):
        ctx = s.ctx; ctx.disable(moderngl.CULL_FACE); ctx.depth_mask = True; ctx.depth_func = '<='
        if not blend: ctx.disable(moderngl.BLEND)
        else: ctx.depth_mask = tint[3] > 0.5
        MVP = s.VP @ A['M']; pr = s.md; pr['mvp'].write(MVP.T.astype('f4').tobytes())
        th = A['yaw'] + math.pi / 2; c, sn = math.cos(th), math.sin(th); L = (0.33, 0.42, 0.84)
        pr['ldir'].value = (c * L[0] + sn * L[1], -sn * L[0] + c * L[1], L[2], 0); pr['fogp'].value = s.R.fogp; pr['fogc'].value = s.R.fog; pr['tint'].value = tint; pr['dif'].value = 0
        for m in A['meshes']:
            m['tex'].use(0); vao = ctx.vertex_array(pr, [(m['vbo'], '3f 3f 2f', 'pos', 'nrm', 'uv')], m['ibo']); vao.render()
        ctx.disable(moderngl.BLEND); ctx.depth_mask = True
    def look(s, A, L):
        """vis_draw_look: one draw with the vision program (state set by the caller)"""
        ctx = s.ctx; MVP = s.VP @ A['M']; pr = s.vis
        Minv = np.linalg.inv(A['M']); eyep = Minv @ np.r_[s.cam, 1]
        rd = MVP[0, :3] / np.linalg.norm(MVP[0, :3])
        pr['mvp'].write(MVP.T.astype('f4').tobytes()); pr['eyep'].value = tuple(eyep[:3]) + (1,); pr['rdir'].value = tuple(rd) + (0,)
        pr['half_'].value = (s.W * s.ss / 2, s.H * s.ss / 2)
        pr['infl'].value = (L.get('infl', (0, 0, 0, 0))[0] * s.ss,) + tuple(L.get('infl', (0, 0, 0, 0))[1:])
        pr['pscl'].value = tuple(L.get('pscl', (1, 0, 0, 0))); pr['col'].value = tuple(L.get('col', (0, 0, 0, 0))); pr['rim'].value = tuple(L.get('rim', (0, 0, 0, 0)))
        pr['prm'].value = tuple(L.get('prm', (0, 0, 0, 0))); pr['dif'].value = 0; pr['pat'].value = 1
        L.get('pat', s.t_blocks).use(1)
        for m in A['meshes']:
            m['tex'].use(0); vao = ctx.vertex_array(pr, [(m['vbo'], '3f 3f 2f', 'pos', 'nrm', 'uv')], m['ibo']); vao.render()
    # ---- vision.h passes
    def premul(s): s.ctx.enable(moderngl.BLEND); s.ctx.blend_func = (moderngl.ONE, moderngl.ONE_MINUS_SRC_ALPHA)
    def xray_line(s, A, line, inner, a, ref, px=None, pull=None):   # = vision.h vis_xray_line
        px = VC['VIS_LINE_PX'] if px is None else px; pull = VC['VIS_PULL'] if pull is None else pull
        ctx = s.ctx; glEnable(GL_STENCIL_TEST); ctx.depth_mask = False; ctx.disable(moderngl.CULL_FACE); s.premul(); ctx.depth_func = '>'
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); glStencilFunc(GL_ALWAYS, ref, 0xFF)
        s.look(A, dict(rim=tuple(c * a for c in inner) + (0,), prm=(0, 0, 0, 1), infl=(0, pull, 0, 0)))
        glStencilFunc(GL_NOTEQUAL, ref | 1, 0xFE)
        s.look(A, dict(col=tuple(c * a for c in line) + (0,), infl=(px, pull, 0, 0)))
        ctx.depth_func = '<='; glDisable(GL_STENCIL_TEST); ctx.disable(moderngl.BLEND); ctx.depth_mask = True
    def xray_fill(s, A, fill, edge, a, ref, px=None):   # = vision.h vis_xray_fill
        px = VC['VIS_GLOW_PX'] if px is None else px
        ctx = s.ctx; glEnable(GL_STENCIL_TEST); ctx.depth_mask = False; ctx.disable(moderngl.CULL_FACE); s.premul(); ctx.depth_func = '1'
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); glStencilFunc(GL_ALWAYS, ref, 0xFF)
        fa = 0.78 * a
        s.look(A, dict(col=tuple(c * fa for c in fill) + (fa,), rim=tuple(c * 0.15 * a for c in fill) + (0.1 * a,), prm=(0, 0, 0.2, 0), pscl=(1 / 14.0, 0, 0, s.now * 0.35), pat=s.t_blocks))
        glStencilFunc(GL_NOTEQUAL, ref | 1, 0xFE)
        s.look(A, dict(col=tuple(c * a for c in edge) + (0,), infl=(px * 0.5, 0, 0, 0)))
        s.look(A, dict(col=tuple(c * 0.45 * a for c in edge) + (0,), infl=(px * 1.3, 0, 0, 0)))
        ctx.depth_func = '<='; glDisable(GL_STENCIL_TEST); ctx.disable(moderngl.BLEND); ctx.depth_mask = True
    def glass(s, A, kind, v, flash=0.0, hit=0.0, fire=0.0, body=False):   # = vision.h vis_glass_look + the bot hook draw
        K = {'enemy': 'ENEMY', 'ally': 'ALLY', 'self': 'SELF'}[kind]; c, r = VC['VIS_GLASS_%s_COL' % K], VC['VIS_GLASS_%s_RIM' % K]
        tx = VC['VIS_GLASS_TEX'][['enemy', 'ally', 'self'].index(kind)]
        L = dict(pscl=(1 / 9.0, 0, 0, s.now * 0.9), pat=s.t_lines)
        prm = [0, tx * v, 0.25 if kind == 'self' else 0.45, 0]
        col = [x * v for x in c]; rim = [x * v for x in r]
        if fire > 0: col[3] += 0.12 * fire; prm[1] += 0.18 * fire; rim = [rim[k] + (0.2 * fire if k < 3 else 0) for k in range(4)]
        if hit > 0: rim = [rim[k] + (0.5 * hit if k < 3 else 0) for k in range(4)]; prm[2] = 0.9
        if flash > 0:
            col = [col[k] + (0.6 * flash if k < 3 else 0.45 * flash) for k in range(4)]; rim = [rim[k] + (0.4 * flash if k < 3 else 0) for k in range(4)]; prm[1] += 0.35 * flash; prm[2] *= 1 - flash
        L.update(col=tuple(col), rim=tuple(rim), prm=tuple(prm), infl=(0, 1.5 if body else 0, 0, 0))
        return L
    def glass_draw(s, A, kind, v, **kw):
        L = s.glass(A, kind, v, **kw); ctx = s.ctx; ctx.depth_mask = False; ctx.disable(moderngl.CULL_FACE); s.premul(); ctx.depth_func = '<='
        s.look(A, L); ctx.disable(moderngl.BLEND); ctx.depth_mask = True
    def glint(s, A, ally, aim=True, pulse=0.0):
        to = s.cam - A['head']; d = np.linalg.norm(to); f = np.array([math.cos(A['yaw']), math.sin(A['yaw']), 0]); face = (to @ f) / d
        k = min(max((face - 0.25) / 0.5, 0), 1)
        if k <= 0: return
        inten = k * (0.85 if aim else 0.45) + pulse * 0.8; sz = max(1, d / 1500) * (1 + 0.8 * pulse)
        col = (0.25, 0.85, 0.95) if ally else (1.0, 0.18, 0.12)
        R = s.VP[0, :3] / np.linalg.norm(s.VP[0, :3]); Uv = s.VP[1, :3] / np.linalg.norm(s.VP[1, :3])
        c = A['head'] + np.array([f[0] * 9, f[1] * 9, -4]) + to / d * 16; hw, hh = 24 * sz, 6 * sz
        p = [c - R * hw + Uv * hh, c + R * hw + Uv * hh, c + R * hw - Uv * hh, c - R * hw - Uv * hh]; uv = [(0, 0), (1, 0), (1, 1), (0, 1)]
        v = []
        for i in (0, 1, 2, 0, 2, 3): v.append(list(p[i]) + list(uv[i]) + list(col) + [min(max(inten, 0), 1)])
        ctx = s.ctx; buf = ctx.buffer(np.array(v, 'f4').tobytes()); pr = s.sp; pr['mvp'].write(s.VP.T.astype('f4').tobytes()); pr['tex'].value = 0; s.t_glint.use(0)
        ctx.enable(moderngl.BLEND); ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE); ctx.depth_mask = False; ctx.disable(moderngl.CULL_FACE)
        ctx.vertex_array(pr, [(buf, '3f 2f 4f', 'pos', 'uv', 'col')]).render(); ctx.disable(moderngl.BLEND); ctx.depth_mask = True
    def end(s, out=None, crop=None):
        im = Image.frombytes('RGBA', (s.W * s.ss, s.H * s.ss), s.fbo.read(components=4)).transpose(Image.FLIP_TOP_BOTTOM).convert('RGB')
        if s.ss > 1: im = im.resize((s.W, s.H), Image.BOX)
        if out: im.save(out)
        return im
    def floor_at(s, x, y, z):
        h = s.col.ray((x, y, z + 150), (0, 0, -1), 600)
        return z + 150 - h if h > 0 else None

    # ---- first person: the viewmodel (camera-space pose, K = model -> camera, PV = its own projection), as main.c
    def viewmodel(s, path, clip='idle', t=0.5, off=(-2.37, 0.08, 0.0), vfov=49.75, mode='normal', glass=1.0, flash=0.0):
        g, texs = s.model(path); ci = g.find_clip(clip); q, p = g.sample(ci if ci >= 0 else 0, t); pal = g.palette(q, p); sk = g.skin(pal)
        meshes = []
        for m, (P, N) in zip(g.mesh, sk):
            if m['blend'] == 2 or (m.get('lods', 0) and not (m['lods'] & 1)): continue   # additive / not LOD 0
            buf = np.c_[P, N, m['uv']].astype('f4'); meshes.append(dict(tex=texs[m['tex']], vbo=s.ctx.buffer(buf.tobytes()), ibo=s.ctx.buffer(m['I'].astype('u4').tobytes()), n=m['ni']))
        K = np.eye(4); K[:3, 3] = off; A = dict(meshes=meshes, M=K, yaw=0.0)
        glDepthMask(1); glClearDepth(1.0); glClear(0x100)
        VP0, cam0 = s.VP, s.cam; s.VP = persp(math.radians(vfov), 960 / 544, 2, 400); s.cam = np.zeros(3)
        if mode == 'normal' or flash > 0 and glass < 0.5:
            s.draw_body(A, tint=(1, 1, 1, 1))
        if mode == 'glass':
            L = s.glass(A, 'self', glass, flash=flash, body=(flash > 0 and glass < 0.5))
            ctx = s.ctx; ctx.depth_mask = True; ctx.disable(moderngl.CULL_FACE); s.premul(); ctx.depth_func = '<='; s.look(A, L); ctx.disable(moderngl.BLEND)
        s.VP, s.cam = VP0, cam0
    def overlay(s, cell_uv, alpha, gtx=DATA + '/sk_ovl.gtx'):
        """a skills.h screen overlay cell, additive over the whole screen (sk_ovl_quad)"""
        if not hasattr(s, 'ovl'):
            d = open(gtx, 'rb').read(); w = d[4] | d[5] << 8; h = d[6] | d[7] << 8; sz = struct.unpack_from('<I', d, 12)[0]
            a = gvm.dds_rgba(w, h, d[16:16 + sz], 1); s.ovl = s.ctx.texture((w, h), 4, a.tobytes()); s.ovl.filter = (moderngl.LINEAR, moderngl.LINEAR)
        u0, v0, u1, v1 = cell_uv; c = (1, 1, 1, alpha)
        p = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]; uv = [(u0, v1), (u1, v1), (u1, v0), (u0, v0)]
        v = [list(p[i]) + list(uv[i]) + list(c) for i in (0, 1, 2, 0, 2, 3)]
        ctx = s.ctx; buf = ctx.buffer(np.array(v, 'f4').tobytes()); pr = s.sp; pr['mvp'].write(np.eye(4, dtype='f4').tobytes()); pr['tex'].value = 0; s.ovl.use(0)
        ctx.disable(moderngl.DEPTH_TEST); ctx.enable(moderngl.BLEND); ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE)
        ctx.vertex_array(pr, [(buf, '3f 2f 4f', 'pos', 'uv', 'col')]).render(); ctx.disable(moderngl.BLEND); ctx.enable(moderngl.DEPTH_TEST)
    def holo(s, A, col, a=1.0):   # = vision.h vis_model(VIS_HOLO)
        L = dict(col=tuple(c * 0.22 * a for c in col) + (0.18 * a,), rim=tuple(c * 0.95 * a for c in col) + (0.55 * a,), prm=(0, 0.10 * a, 0.3, 0), pscl=(1 / 10.0, 0, 0, s.now * 0.5), pat=s.t_lines)
        ctx = s.ctx; ctx.depth_mask = False; ctx.disable(moderngl.CULL_FACE); s.premul(); ctx.depth_func = '<='; s.look(A, L); ctx.disable(moderngl.BLEND); ctx.depth_mask = True

# ---- camo refraction (vision.h vis_camo_rl / vis_rdraw) with the previous frame as the screen texture
def _camo_rl(kind, c, e, h, dist, clock):   # = vision.h vis_camo_rl
    K = ['enemy', 'ally', 'self'].index(kind); L = VC['VIS_CAMO_LENS'][K]; O = VC['VIS_CAMO_OFS'][K]; M = VC['VIS_CAMO_RIM'][K]; HO = VC['VIS_CAMO_HOLO']; PT = VC['VIS_CAMO_PAT'][K]
    tm = clock % 1000.0; a = L[0] * c; k = c * (min(max(1 - dist / 4000, 0.3), 1) if kind == 'enemy' else 1)
    R = dict(pscl=[1 / 9.0, 0, 0, tm * 0.9], prm=[0, 0, PT[0], PT[1]], rfr=[L[1] * a, L[2] * a, L[3] * a, a], infl=[0, 0, O[0], O[1]], wob=[O[2] * c, tm, 1, 1],
             rim=[x * k for x in M], col=[0, 0, 0, 0], shd=[VC['VIS_CAMO_SHADE'][K] * c, 0, 0, 0])
    if kind == 'ally': R['col'] = [HO[j] * HO[3] * c for j in range(3)] + [HO[3] * c]; R['prm'][0], R['prm'][1] = 1.5, 0.22
    if e > 0:
        if R['prm'][0] == 0 and R['prm'][1] == 0: R['prm'][0], R['prm'][1] = 0.9, 0.25
        sv = VC['VIS_CAMO_SILVER'][K]
        FL = VC['VIS_CAMO_FLASH'][K]
        for j in range(3): R['col'][j] += (0.55 + 0.02 * j) * e * sv; R['rim'][j] += FL[0] * e
        R['col'][3] += 0.7 * e * sv; R['rim'][3] += 0.15 * e; R['prm'][2] = max(R['prm'][2], 0.8); R['prm'][3] = FL[1]; R['pscl'][3] = tm * 3.0
    if h > 0:
        j = h * 0.8; R['rim'][0] += 0.55 * j; R['rim'][1] += 0.75 * j; R['rim'][2] += 1.0 * j; R['prm'][2] = 0.9; R['infl'][2] *= 1 + j
    return R
def _grab_prev(s):
    """the frame so far, resolved to 960x544, as the 'previous frame' texture (the Vita's front display buffer)"""
    im = Image.frombytes('RGBA', (s.W * s.ss, s.H * s.ss), s.fbo.read(components=4)).transpose(Image.FLIP_TOP_BOTTOM).convert('RGB').resize((s.W, s.H), Image.BOX)
    a = np.asarray(im)[::-1].copy()   # GL rows bottom-up
    if s.prev is None: s.prev = s.ctx.texture((s.W, s.H), 3, a.tobytes()); s.prev.filter = (moderngl.LINEAR, moderngl.LINEAR); s.prev.repeat_x = s.prev.repeat_y = False
    else: s.prev.write(a.tobytes())
    return im
def _rdraw(s, A, R, pull=0.0):
    ctx = s.ctx; MVP = s.VP @ A['M']; pr = s.vr
    Minv = np.linalg.inv(A['M']); eyep = Minv @ np.r_[s.cam, 1]; rd = MVP[0, :3] / np.linalg.norm(MVP[0, :3])
    pr['mvp'].write(MVP.T.astype('f4').tobytes()); pr['eyep'].value = tuple(eyep[:3]) + (1,); pr['rdir'].value = tuple(rd) + (0,)
    infl = list(R['infl']); infl[1] = pull
    for k, v in (('infl', infl), ('shd', R['shd']), ('pscl', R['pscl']), ('wob', R['wob']), ('rfr', R['rfr']), ('col', R['col']), ('rim', R['rim']), ('prm', R['prm'])):
        if k in pr: pr[k].value = tuple(v)
    for k, u in (('dif', 0), ('pat', 1), ('scr', 5)):
        if k in pr: pr[k].value = u
    s.t_lines.use(1); s.prev.use(5)
    ctx.disable(moderngl.CULL_FACE); s.premul(); ctx.depth_func = '<='
    for m in A['meshes']:
        m['tex'].use(0); ctx.vertex_array(pr, [(m['vbo'], '3f 3f 2f', 'pos', 'nrm', 'uv')], m['ibo']).render()
    ctx.disable(moderngl.BLEND); ctx.depth_mask = True
def camo_shot(s, scene_fn, A, kind, c=1.0, e=0.0, h=0.0, frames=4, clock=3.0, body_tint=(0.9, 0.9, 0.95)):
    """renders `frames` frames of a static scene with actor A in the camo look (vision.h vis_bot_body): each frame's camo
    samples the frame before it, as on the Vita. scene_fn() draws the background (after begin)."""
    dist = float(np.linalg.norm(A['chest'] - s.cam)); im = None
    scene_fn(); _grab_prev(s)   # frame 0: the scene without her (the frame before the cloaked body appears)
    for f in range(frames):
        scene_fn(); ba = 1 - c
        if ba > 0.01:
            s.ctx.enable(moderngl.BLEND); s.ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE_MINUS_SRC_ALPHA)
            s.draw_body(A, tint=tuple(body_tint) + (ba,), blend=True)
        if c > 0.01 or e > 0.01 or h > 0.01:
            s.ctx.depth_mask = False; _rdraw(s, A, _camo_rl(kind, c, e, h, dist, clock + f / 60.0), pull=1.5 if ba > 0.01 else 0.0)
        im = _grab_prev(s)
    return im
Host.camo_shot = camo_shot

# ---- round-start hologram (vision.h vis_spawn_tint / vis_spawn_overlay)
def spawn_draw(s, A, sp=1.0, scale=1 / 40.0, pull=1.5, tint=(0.9, 0.9, 0.95)):
    T = VC['VIS_SPAWN_TINT']; t2 = tuple(tint[k] * (1 - sp + sp * T[k]) for k in range(3)) + (1,)
    s.draw_body(A, tint=t2)
    L = dict(col=tuple(T[k] * 0.20 * sp for k in range(3)) + (0,), rim=tuple(T[k] * 0.35 * sp for k in range(3)) + (0,), prm=(0, 0, 0.85, 0), pscl=(scale, 0, 0, 0), pat=s.t_hex, infl=(0, pull, 0, 0))
    ctx = s.ctx; ctx.depth_mask = False; ctx.disable(moderngl.CULL_FACE); s.premul(); ctx.depth_func = '<='; s.look(A, L); ctx.disable(moderngl.BLEND); ctx.depth_mask = True
Host.spawn_draw = spawn_draw
