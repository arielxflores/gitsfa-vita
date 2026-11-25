# .gsc: a keyed effect scene of the game (a Gamebryo 30.2 NIF of plain meshes under keyed nodes - no particles) baked for
# engine/src/fxscene.h (b102: the match start's dive, effect\cyber_world\start_screen.nif / start_bg_deco.nif /
# start_screen_end.nif with effect\camera\cam_cyber_world.nif's path). Every track is sampled at a fixed rate, so the engine
# only looks rows up:
#   node transforms (NiTransformController: translation / XYZ-Euler rotation (R = Rz Ry Rx) / scale keys), node visibility
#   (NiVisController), material alpha and emissive colour (NiAlphaController, NiMaterialColorController), the base and the
#   dark map's texture transforms (NiTextureTransformController: translate U / V, rotate, scale U / V about the middle).
#   Controller time: clamp / loop / reverse as the controller's flags say.
# File (little endian):
#   'GSC1' u16 frames, u16 fps, u16 meshes, u16 textures, u32 vertices, u32 indices, u16 flags (1 = world scene with a camera
#   track), u16 0, f32 fovy (degrees; screen scenes), f32 view[12] (world -> camera, rows; screen scenes)
#   textures: u16 w, u16 h, u8 format (1 DXT1, 2 DXT5), u8 wrap (1 = repeat), u16 0, u32 size, data
#   meshes: u32 v0, u32 nv, u32 i0, u32 ni, i16 tex, i16 tex2 (dark map, multiplied; -1 none), u8 blend (0 alpha, 1 add),
#           u8 anim (1 matrix per frame, 2 base uv per frame, 4 dark uv per frame), u16 0      - in drawing order (far first)
#   vertices: f32 x y z u v a (a: the vertex's own alpha, see below) ; indices: u16 (from the mesh's v0)
#   per frame, per mesh: u8 r g b a (a 0 = not drawn)
#   per mesh: f32 M[12] (model -> scene, rows) once, or per frame when anim & 1
#   per mesh: f32 uv[6] (u' = a u + b v + c, v' = d u + e v + f) once, or per frame when anim & 2; the same for the dark map
#   (only meshes with one) with anim & 4
#   camera track (flags & 1): per frame f32 eye[3], dir[3], up[3]
# One texture a mesh: where the game multiplies a base by a dark map (its light streaks: a plain white base whose alpha only
# fades the ends, times a scrolling pattern), the pattern becomes the mesh's texture with the dark map's transform and the
# base's alpha is sampled into the vertices (the streak meshes are fine grids). (b102: a second sampler in this program read
# the first one's texture on the test harness - one sampler leaves nothing to go wrong on the hardware either.)
# usage: make_scene.py <in.nif> <texture dir> <out.gsc> <seconds> [fps] [--camera cam.nif] [--fov deg] [--scale-tex name=WxH ...]
#        [--eye x,y,z (a screen scene without a camera node: the eye's place, looking along +y)] [--t0 s (the scene from this time on)]
import sys, os, struct, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import nifps
from PIL import Image
NONE = nifps.NONE

def hermite(a, b, u, it):
    if it == 2:
        u2 = u * u; u3 = u2 * u
        return (2 * u3 - 3 * u2 + 1) * a['v'] + (u3 - 2 * u2 + u) * a['fwd'][0] + (-2 * u3 + 3 * u2) * b['v'] + (u3 - u2) * b['bwd'][0]
    return a['v'] + (b['v'] - a['v']) * u
def keyval(g, t, vec=False, step=False):
    ks = g['keys']; it = g.get('it', 1)
    if not ks: return None
    if t <= ks[0]['t']: return np.array(ks[0]['v'], float) if vec else ks[0]['v']
    for a, b in zip(ks, ks[1:]):
        if t <= b['t']:
            if step: return a['v'] if t < b['t'] else b['v']
            u = (t - a['t']) / max(1e-9, b['t'] - a['t'])
            if vec:
                av, bv = np.array(a['v'], float), np.array(b['v'], float)
                if it == 2:
                    u2 = u * u; u3 = u2 * u
                    return (2 * u3 - 3 * u2 + 1) * av + (u3 - 2 * u2 + u) * np.array(a['fwd'], float) + (-2 * u3 + 3 * u2) * bv + (u3 - u2) * np.array(b['bwd'], float)
                return av + (bv - av) * u
            return hermite(a, b, u, it)
    return np.array(ks[-1]['v'], float) if vec else ks[-1]['v']
def ctime(c, t):
    t = t * c['freq'] + c['phase']; a, b = c['start'], c['stop']; cyc = (c['flags'] >> 1) & 3
    if b <= a: return a
    if cyc == 2: return min(b, max(a, t))
    if t < a: return a
    if cyc == 0: return a + (t - a) % (b - a)
    k = (t - a) / (b - a); f = k % 2.0; return a + (f if f <= 1 else 2 - f) * (b - a)
def rx(a): c, s = np.cos(a), np.sin(a); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
def ry(a): c, s = np.cos(a), np.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
def rz(a): c, s = np.cos(a), np.sin(a); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
def qmat(q):
    w, x, y, z = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])

class Scene:
    def __init__(s, path):
        s.h, s.B = nifps.parse(path); s.T = [t for t, o, z in s.h['blocks']]; s.par = {}
        for k, b in enumerate(s.B):
            for c in (b.get('children') or []): s.par[c] = k
    def chain(s, c):
        while c not in (None, NONE): yield c, s.B[c], s.T[c]; c = s.B[c]['next']
    def local(s, k, t):
        b = s.B[k]; T = np.array(b['t'], float); R = np.array(b['r'], float).reshape(3, 3); S = b['s']
        for c, cb, ct in s.chain(b.get('ctrl')):
            if ct != 'NiTransformController' or cb.get('interp') in (None, NONE): continue
            ib = s.B[cb['interp']]; tt = ctime(cb, t)
            if abs(ib['tr'][0]) < 1e30: T = np.array(ib['tr'], float)
            if abs(ib['q'][0]) < 1e30: R = qmat(ib['q'])
            if abs(ib['sc']) < 1e30: S = ib['sc']
            d = s.B[ib['data']] if ib.get('data') not in (None, NONE) else None
            if d is None: continue
            v = keyval(d['trans'], tt, vec=True) if d.get('trans') else None
            if v is not None: T = v
            if d.get('xyz') and any(g['keys'] for g in d['xyz']):
                e = [keyval(g, tt) if g['keys'] else 0.0 for g in d['xyz']]; R = rz(e[2]) @ ry(e[1]) @ rx(e[0])
            elif d.get('rot'):
                ks = d['rot']; q = ks[0][1]
                for (ta, qa), (tb, qb) in zip(ks, ks[1:]):
                    if tt >= ta: u = min(1, (tt - ta) / max(1e-9, tb - ta)); qa = np.array(qa); qb = np.array(qb); qb = -qb if qa @ qb < 0 else qb; q = qa + (qb - qa) * u
                q = np.array(q, float); R = qmat(q / np.linalg.norm(q))
            v = keyval(d['scale'], tt) if d.get('scale') else None
            if v is not None: S = v
        M = np.eye(4); M[:3, :3] = R * S; M[:3, 3] = T; return M
    def vis(s, k, t):
        for c, cb, ct in s.chain(s.B[k].get('ctrl')):
            if ct == 'NiVisController' and cb.get('interp') not in (None, NONE):
                ib = s.B[cb['interp']]; d = s.B[ib['data']] if ib.get('data') not in (None, NONE) else None
                if d is not None and d['keys']: return bool(keyval(d, ctime(cb, t), step=True))
                return bool(ib.get('value', 1))
        return not (s.B[k].get('flags', 0) & 1)   # NiAVObject flag bit 0: hidden
    def world(s, k, t):
        M = s.local(k, t); v = s.vis(k, t); p = s.par.get(k)
        while p is not None: M = s.local(p, t) @ M; v = v and s.vis(p, t); p = s.par.get(p)
        return M, v
    def texdesc(s, k):
        """NiTexturingProperty: (base, dark) = (file, wrap, static transform or None) each or None"""
        t, o, sz = s.h['blocks'][k]; d = s.h['d']; r = nifps.R(d, o); nifps.objnet(r); r.u('H'); r.u('I'); out = []
        for slot in range(2):
            if not r.u('B'): out.append(None); continue
            src = r.u('I'); fl = r.u('H'); r.u('H'); tt = None
            if r.u('B'): tr = r.arr('f', 2); sc = r.arr('f', 2); rot = r.u('f'); r.u('I'); cen = r.arr('f', 2); tt = (tr, sc, rot, cen)
            out.append((s.B[src].get('file') if src != NONE else None, ((fl >> 12) & 3) == 3, tt))
        return out
    def uvx(s, prop, slot, t, static):
        tr = [0.0, 0.0]; sc = [1.0, 1.0]; rot = 0.0; cen = [0.5, 0.5]; anim = False
        if static: tr, sc, rot, cen = list(static[0]), list(static[1]), static[2], list(static[3])
        for c, cb, ct in s.chain(s.B[prop].get('ctrl')):
            if ct != 'NiTextureTransformController' or cb['slot'] != slot or cb.get('interp') in (None, NONE): continue
            ib = s.B[cb['interp']]; d = s.B[ib['data']] if ib.get('data') not in (None, NONE) else None
            v = keyval(d, ctime(cb, t)) if d is not None and d['keys'] else ib.get('value')
            if v is None or abs(v) > 1e30: continue
            anim = True; op = cb['op']
            if op == 0: tr[0] = v
            elif op == 1: tr[1] = v
            elif op == 2: rot = v
            elif op == 3: sc[0] = v
            elif op == 4: sc[1] = v
        # uv' = R S (uv - centre) + centre + (-tu, +tv): a 3ds Max offset moves the picture along +U / up, and the file's V runs
        # down (start_screen's streaks have u growing outward and positive U keys, its ring v growing outward and negative V
        # keys: both rush out of the centre, as the footage)
        c, sn = np.cos(rot), np.sin(rot)
        A = np.array([[c * sc[0], -sn * sc[1]], [sn * sc[0], c * sc[1]]]); off = np.array(cen) - A @ np.array(cen) + np.array([-tr[0], tr[1]])
        return np.array([A[0, 0], A[0, 1], off[0], A[1, 0], A[1, 1], off[1]], np.float32), anim
    def material(s, mesh, t):
        col = np.ones(3); al = 1.0
        for p in s.B[mesh]['props']:
            if s.T[p] != 'NiMaterialProperty': continue
            pb = s.B[p]; col = np.array(pb['emis'], float); al = pb['alpha']
            for c, cb, ct in s.chain(pb.get('ctrl')):
                if cb.get('interp') in (None, NONE): continue
                ib = s.B[cb['interp']]; d = s.B[ib['data']] if ib.get('data') not in (None, NONE) else None
                if ct == 'NiAlphaController' and d is not None and d['keys']: al = keyval(d, ctime(cb, t))
                elif ct == 'NiMaterialColorController' and d is not None and d.get('keys') and cb.get('target_color') == 3: col = keyval(d, ctime(cb, t), vec=True)   # 3 = self illumination
        return np.clip(col, 0, 1), float(min(1, max(0, al)))

def covers(Pw, idx, view, fov, grid=(64, 36)):
    """does any triangle of the mesh (world points Pw, index triples idx) cover a point of a coarse grid over the picture?"""
    Pc = (view[:, :3] @ Pw.T).T + view[:, 3]; th = 1 / np.tan(np.radians(fov) / 2); w = -Pc[:, 2]
    if (w[idx] < 2.0).any(): return True   # reaches the near plane: left to the hardware's clipping
    x = Pc[:, 0] * th / (960 / 544) / w; y = Pc[:, 1] * th / w; a, b, c = idx[:, 0], idx[:, 1], idx[:, 2]
    keep = ~((np.maximum.reduce([x[a], x[b], x[c]]) < -1) | (np.minimum.reduce([x[a], x[b], x[c]]) > 1) | (np.maximum.reduce([y[a], y[b], y[c]]) < -1) | (np.minimum.reduce([y[a], y[b], y[c]]) > 1))
    if not keep.any(): return False
    a, b, c = a[keep], b[keep], c[keep]; gx, gy = np.meshgrid((np.arange(grid[0]) + 0.5) / grid[0] * 2 - 1, (np.arange(grid[1]) + 0.5) / grid[1] * 2 - 1); gx = gx.ravel()[None]; gy = gy.ravel()[None]
    def edge(i, j): return (x[j] - x[i])[:, None] * (gy - y[i][:, None]) - (y[j] - y[i])[:, None] * (gx - x[i][:, None])
    e0, e1, e2 = edge(a, b), edge(b, c), edge(c, a)
    return bool((((e0 >= 0) & (e1 >= 0) & (e2 >= 0)) | ((e0 <= 0) & (e1 <= 0) & (e2 <= 0))).any())

def dxt(img, alpha):
    import etcpak
    w, h = img.size; raw = np.ascontiguousarray(np.asarray(img.convert('RGBA'))).tobytes()
    return (2, etcpak.compress_to_dxt5(raw, w, h)) if alpha else (1, etcpak.compress_to_dxt1(raw, w, h))

def build(nif, texdir, out, seconds, fps=30, camera=None, fov=73.0, texscale=None, maxdim=512, eye=None, t0=0.0):
    S = Scene(nif); n = int(round(seconds * fps)) + 1; ts = [t0 + i / fps for i in range(n)]; texscale = texscale or {}
    meshes = [k for k, t in enumerate(S.T) if t == 'NiMesh']; cam = [k for k, b in enumerate(S.B) if S.T[k] == 'NiNode' and (b.get('name') or '').startswith('render_Camera')]
    texs = []; texid = {}; V = []; I = []; geo = {}; rows = []
    def tex(name, wrap):
        if name is None: return -1
        key = (name.lower(), wrap)
        if key in texid: return texid[key]
        p = [f for f in os.listdir(texdir) if f.lower() == name.lower()]
        if not p: print('  missing texture', name); return -1
        im = Image.open(os.path.join(texdir, p[0])).convert('RGBA'); w, h = texscale.get(name.lower(), im.size)
        while max(w, h) > maxdim: w //= 2; h //= 2
        if (w, h) != im.size: im = im.resize((max(4, w), max(4, h)), Image.LANCZOS)
        a = np.asarray(im)[..., 3]; fmt, data = dxt(im, a.min() < 250); texid[key] = len(texs); texs.append((im.size[0], im.size[1], fmt, 1 if wrap else 0, data)); return texid[key]
    def teximg(name):
        p = [f for f in os.listdir(texdir) if f.lower() == name.lower()]; return np.asarray(Image.open(os.path.join(texdir, p[0])).convert('RGBA')).astype(np.float32) / 255 if p else None
    nv = ni = 0
    for k in meshes:
        b = S.B[k]; P = UV = Ix = None
        for o in nifps.stream_pos(S.h, S.B, k):
            if 'P' in o: P = o['P']
            if 'UV' in o and UV is None: UV = o['UV']
            if 'I' in o: Ix = o['I']
        if P is None or Ix is None or len(P) > 65535: print('  skipped mesh', k, b.get('name')); continue
        if UV is None: UV = np.zeros((len(P), 2), np.float32)
        tp = [p for p in b['props'] if S.T[p] == 'NiTexturingProperty']; td = S.texdesc(tp[0]) if tp else [None, None]
        va = np.ones(len(P), np.float32); fold = False
        if td[0] and td[1] and td[0][0] and td[1][0]:
            bi = teximg(td[0][0]); u0, a0 = S.uvx(tp[0], 0, 0.0, td[0][2]); moves = any(np.abs(S.uvx(tp[0], 0, t, td[0][2])[0] - u0).max() > 1e-6 for t in ts[::5])
            if bi is not None and bi[..., :3].min() > 0.9 and not moves:   # a white base: only its alpha matters - into the vertices
                q = np.c_[UV, np.ones(len(UV))]; uu = q @ u0[:3]; vv = q @ u0[3:]; h, w = bi.shape[:2]
                if td[0][1]: uu = uu % 1.0; vv = vv % 1.0
                x = np.clip(uu * w - 0.5, 0, w - 1); y = np.clip(vv * h - 0.5, 0, h - 1); x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int); x1 = np.minimum(x0 + 1, w - 1); y1 = np.minimum(y0 + 1, h - 1); fx = x - x0; fy = y - y0
                A = bi[..., 3]; va = ((A[y0, x0] * (1 - fx) + A[y0, x1] * fx) * (1 - fy) + (A[y1, x0] * (1 - fx) + A[y1, x1] * fx) * fy).astype(np.float32); fold = True
            else: print('  mesh', k, b.get('name'), ': base', td[0][0], 'x dark', td[1][0], '- the dark map is left out (the base is not plain white, or it moves)')
        t0 = tex(td[1][0], td[1][1]) if fold else (tex(td[0][0], td[0][1]) if td[0] else -1); t1 = -1
        ap = [S.B[p] for p in b['props'] if S.T[p] == 'NiAlphaProperty']; add = 1 if ap and ap[0]['blend'][2] == 'ONE' else 0
        key = (P.tobytes(), UV.tobytes(), va.tobytes(), Ix.tobytes())
        if key not in geo: geo[key] = (len(V) and sum(len(x) for x in V), len(P), sum(len(x) for x in I), len(Ix)); V.append(np.hstack([P.astype(np.float32), UV.astype(np.float32), va[:, None]])); I.append(Ix.astype(np.uint16))
        g = geo[key]; mats = []; cols = []; uv0 = []; uv1 = []; an0 = an1 = False
        for t in ts:
            M, vis = S.world(k, t); col, al = S.material(k, t); mats.append(M[:3].astype(np.float32)); cols.append(np.r_[col, al if vis else 0.0])
            u, a = S.uvx(tp[0], 1 if fold else 0, t, td[1 if fold else 0][2] if td[1 if fold else 0] else None) if tp else (np.array([1, 0, 0, 0, 1, 0], np.float32), False); uv0.append(u); an0 |= a
            uv1.append(np.array([1, 0, 0, 0, 1, 0], np.float32))
        mats = np.array(mats); cols = np.array(cols); cols8 = np.clip(np.round(cols * 255), 0, 255).astype(np.uint8)
        if cols8[:, 3].max() == 0: continue   # never seen
        anim = (1 if np.abs(mats - mats[0]).max() > 1e-5 else 0) | (2 if an0 and np.abs(np.array(uv0) - uv0[0]).max() > 1e-6 else 0) | (4 if t1 >= 0 and an1 and np.abs(np.array(uv1) - uv1[0]).max() > 1e-6 else 0)
        rows.append(dict(k=k, name=b.get('name'), g=g, t0=t0, t1=t1, add=add, anim=anim, mats=mats, cols=cols8, uv0=np.array(uv0), uv1=np.array(uv1), depth=float(mats[0][1, 3]), P=P.astype(np.float64), I=Ix.astype(np.int64).reshape(-1, 3)))
    view = np.eye(4)[:3].astype(np.float32); flags = 0; camtrack = None
    if cam or eye is not None:   # a screen scene: its own camera - at the node, looking along the scene's +y, z up
        # (b121 --eye: a screen effect without a camera node - effect\screen\screen_dive*.nif, flat in the scene's xz plane: the place given)
        e = np.array(eye, float) if eye is not None else S.world(cam[0], 0)[0][:3, 3]; view = np.array([[1, 0, 0, -e[0]], [0, 0, 1, -e[2]], [0, -1, 0, e[1]]], np.float32)   # camera space: x right, y up, -z forward
        rows.sort(key=lambda r: -r['depth'])   # blended, no depth buffer: the far ones first (stable)
        # a mesh blown up far past the picture (the rings end 110 times their size, the eye inside their hole) is switched off
        # for the frames in which none of it covers the picture: nothing to see, nothing to send to the GPU
        # (the solid band of emu b102-6 was not these: it came from drawing all meshes out of one vertex buffer - fxscene.h)
        off = 0
        for r in rows:
            for f in range(n):
                if not r['cols'][f, 3]: continue
                M = r['mats'][f]; sc = np.abs(M[:, :3]).max()
                if sc * np.abs(r['P']).max() < 2000: continue
                if not covers((M[:, :3] @ r['P'].T).T + M[:, 3], r['I'], view.astype(np.float64), fov): r['cols'][f, 3] = 0; off += 1
        if off: print('  %d mesh-frames past the picture switched off' % off)
    if camera:
        C = Scene(camera); box = [k for k, b in enumerate(C.B) if C.T[k] == 'NiNode' and C.B[k].get('ctrl') not in (None, NONE)][0]; kid = C.B[box]['children'][0]; camtrack = []
        for t in ts:
            M = C.world(kid, t)[0]; camtrack.append(np.r_[M[:3, 3], -M[:3, 2], M[:3, 1]])   # a 3ds Max camera: it looks down its -z, its y is up
        camtrack = np.array(camtrack, np.float32); flags |= 1
    vt = np.concatenate(V) if V else np.zeros((0, 6), np.float32); it = np.concatenate(I) if I else np.zeros(0, np.uint16)
    with open(out, 'wb') as f:
        f.write(b'GSC2' + struct.pack('<HHHHIIHHf', n, fps, len(rows), len(texs), len(vt), len(it), flags, 0, fov) + view.tobytes())
        for w, h, fmt, wrap, data in texs: f.write(struct.pack('<HHBBHI', w, h, fmt, wrap, 0, len(data))); f.write(data)
        for r in rows: f.write(struct.pack('<IIIIhhBBH', r['g'][0], r['g'][1], r['g'][2], r['g'][3], r['t0'], r['t1'], r['add'], r['anim'], 0))
        f.write(vt.astype('<f4').tobytes()); f.write(it.astype('<u2').tobytes())
        if len(it) & 1: f.write(b'\0\0')
        f.write(np.stack([r['cols'] for r in rows], 1).tobytes())   # frames x meshes x 4
        pad = (-(n * len(rows) * 4)) % 4; f.write(b'\0' * pad)
        for r in rows: f.write((r['mats'] if r['anim'] & 1 else r['mats'][:1]).astype('<f4').tobytes())
        for r in rows: f.write((r['uv0'] if r['anim'] & 2 else r['uv0'][:1]).astype('<f4').tobytes())
        for r in rows:
            if r['t1'] >= 0: f.write((r['uv1'] if r['anim'] & 4 else r['uv1'][:1]).astype('<f4').tobytes())
        if camtrack is not None: f.write(camtrack.astype('<f4').tobytes())
    tb = sum(len(t[4]) for t in texs)
    print('%s: %d frames at %d fps, %d meshes (%d with a matrix / %d base uv / %d dark uv per frame), %d vertices, %d triangles, %d textures (%d bytes), %d bytes' % (
        os.path.basename(out), n, fps, len(rows), sum(r['anim'] & 1 for r in rows), sum(r['anim'] >> 1 & 1 for r in rows), sum(r['anim'] >> 2 & 1 for r in rows), len(vt), len(it) // 3, len(texs), tb, os.path.getsize(out)))
    return rows, camtrack

if __name__ == '__main__':
    a = sys.argv[1:]; kw = {}; ts = {}
    while '--camera' in a: i = a.index('--camera'); kw['camera'] = a[i + 1]; del a[i:i + 2]
    while '--fov' in a: i = a.index('--fov'); kw['fov'] = float(a[i + 1]); del a[i:i + 2]
    while '--eye' in a: i = a.index('--eye'); kw['eye'] = [float(x) for x in a[i + 1].split(',')]; del a[i:i + 2]
    while '--t0' in a: i = a.index('--t0'); kw['t0'] = float(a[i + 1]); del a[i:i + 2]
    while '--maxdim' in a: i = a.index('--maxdim'); kw['maxdim'] = int(a[i + 1]); del a[i:i + 2]
    while '--scale-tex' in a: i = a.index('--scale-tex'); nm, wh = a[i + 1].split('='); ts[nm.lower()] = tuple(int(x) for x in wh.split('x')); del a[i:i + 2]
    build(a[0], a[1], a[2], float(a[3]), int(a[4]) if len(a) > 4 else 30, texscale=ts, **kw)
