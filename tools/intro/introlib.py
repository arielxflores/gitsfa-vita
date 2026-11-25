# b118 / b119: readers for the game's intro cut scene (world.dat world\IntroMovie: MovieMaker.txt, model\character\INTRO_CH.nif,
# Intro_sc00N_CAM.nif, model\effect\Intro_sc00N_effect.nif), on the project's own NIF parsers (tools/fx/nifps.py,
# tools/fx/make_scene.py's Scene: keyed node transforms, visibility, material alpha, texture transforms).
import os, sys, struct, numpy as np
H = os.path.dirname(os.path.abspath(__file__))
for p in ('..', '../fx', '../lib', '../../vm'): sys.path.insert(0, os.path.join(H, p))
import nifps
from make_scene import Scene, qmat, keyval, ctime
NONE = nifps.NONE
NODES = ('NiNode', 'NiBillboardNode', 'NiSortAdjustNode', 'NiSwitchNode', 'NiLODNode')
SZ = {0x40214: 8, 0x30437: 12, 0x40110: 4, 0x20436: 8, 0x10425: 4, 0x10215: 2, 0x40438: 16, 0x40401: 4, 0x40406: 4, 0x40409: 4, 0x4040a: 4, 0x20422: 4, 0x40424: 8,
      0x10413: 2, 0x10414: 2, 0x30423: 6, 0x40108: 4, 0x40102: 4, 0x10213: 2, 0x40404: 4, 0x40405: 4}

def movie(path):
    """MovieMaker.txt -> dict(level, char, bg sound, scenes: [dict(cam, eff, begin, end, hide: [names], sound: (group, event) or None)])"""
    M = dict(scenes=[], ibl=[]); cur = None; grp = None
    for ln in open(path, encoding='latin1'):
        ln = ln.strip()
        if not ln.startswith('['): continue
        k, _, v = ln.partition(']'); k = k[1:]; v = v.strip()
        if k == 'SCENE': cur = dict(cam=None, eff=None, begin=0.0, end=0.0, hide=[], sound=None, dof=None, map=False); M['scenes'].append(cur)
        elif k == '/SCENE': cur = None
        elif k == 'SOUND_BG': M['bgm'] = v
        elif k == 'LEVEL_DATA': M['level'] = v.split()[-1]
        elif k == 'CHAR_DATA': M['char'] = v.split()[-1]
        elif cur is not None:
            if k == 'CAM_FILE': cur['cam'] = v
            elif k == 'EFF_FILE': cur['eff'] = None if v.lower() == 'null' else v
            elif k == 'BEGIN_TIME': cur['begin'] = float(v)
            elif k == 'END_TIME': cur['end'] = float(v)
            elif k == 'HIDE_MESH': cur['hide'].append(v)
            elif k == 'SOUND_SCENE_GROUP': grp = v
            elif k == 'SOUND_SCENE' and v: cur['sound'] = (grp, v)
            elif k == 'DOF_CONTROL': cur['dof'] = v
            elif k == 'MAP_FILE': cur['map'] = True   # (the shot draws the room; shot 0 - the text - has no such line)
    return M

class Nif(Scene):
    """make_scene.Scene + the mesh streams"""
    def __init__(s, path):
        Scene.__init__(s, path); s.d = s.h['d']; s.path = path
    def name(s, k): return s.B[k].get('name')
    def path_of(s, k):
        out = []
        while k is not None: out.append(s.name(k) or '#%d' % k); k = s.par.get(k)
        return out[::-1]
    def streams(s, k):
        """NiMesh block -> {(semantic, index): (array, format, regions)}"""
        out = {}
        for st in s.B[k]['streams']:
            t, o, sz = s.h['blocks'][st['ref']]; r = nifps.R(s.d, o); size, clone, nr = r.arr('I', 3); regs = [tuple(r.arr('I', 2)) for _ in range(nr)]
            nc = r.u('I'); fm = r.arr('I', nc); stride = sum(SZ[f] for f in fm); raw = np.frombuffer(s.d, 'u1', size, r.o).reshape(-1, stride); off = 0
            for (sem, idx), f in zip(st['comps'], fm):
                nm = sem if isinstance(sem, str) else nifps.S(s.h, sem); out[(nm, idx)] = (raw[:, off:off + SZ[f]].copy(), f, regs); off += SZ[f]
        return out
    def skin(s, k):
        """the NiSkinningMeshModifier of mesh k -> dict(bones: [block], s2b: [4x4], rp2s) or None"""
        for m in s.B[k].get('mods', []):
            if s.T[m] != 'NiSkinningMeshModifier': continue
            o = s.h['blocks'][m][1]; d = s.d
            def xf(o):
                r = np.array(struct.unpack_from('<9f', d, o)).reshape(3, 3); t = np.array(struct.unpack_from('<3f', d, o + 36)); sc = struct.unpack_from('<f', d, o + 48)[0]
                M = np.eye(4); M[:3, :3] = r * sc; M[:3, 3] = t; return M
            ns = struct.unpack_from('<I', d, o)[0]; o += 4 + 2 * ns; nc = struct.unpack_from('<I', d, o)[0]; o += 4 + 2 * nc
            flags, root = struct.unpack_from('<HI', d, o); o += 6; rp2s = xf(o); o += 52; nb = struct.unpack_from('<I', d, o)[0]; o += 4
            bones = list(struct.unpack_from('<%dI' % nb, d, o)); o += 4 * nb
            return dict(bones=bones, s2b=[xf(o + 52 * i) for i in range(nb)], rp2s=rp2s, root=root)
        return None
    def base_tex(s, k):
        for p in s.B[k]['props']:
            if s.T[p] == 'NiTexturingProperty':
                td = s.texdesc(p)
                if td and td[0] and td[0][0]: return td[0][0]
        return None
    def texslots(s, k):
        """NiTexturingProperty block k -> {slot: dict(file, uvset)}: the fixed slots and the material's own 'shaderN' maps (b120)"""
        d = s.d; B = s.h['blocks']; po = B[k][1]; name, ne = struct.unpack_from('<II', d, po); o = po + 8 + 4 * ne + 4
        flags, count = struct.unpack_from('<HI', d, o); o += 6; out = {}
        SL = ['base', 'dark', 'detail', 'gloss', 'glow', 'bump', 'normal', 'parallax', 'decal0', 'decal1', 'decal2', 'decal3']
        def one(o, nm, shader):
            src, mf, an, ht = struct.unpack_from('<iHHB', d, o); o += 9
            if ht: o += 32
            if shader: nm = 'shader%d' % struct.unpack_from('<I', d, o)[0]; o += 4
            fn = nifps.S(s.h, struct.unpack_from('<I', d, B[src][1] + 13)[0]) if 0 <= src < len(B) and B[src][0] == 'NiSourceTexture' else None
            out[nm] = dict(file=fn, uvset=mf & 0xff); return o
        for i in range(count):
            has = d[o]; o += 1
            if has: o = one(o, SL[i] if i < len(SL) else 'slot%d' % i, False)
        ns = struct.unpack_from('<I', d, o)[0]; o += 4
        for i in range(ns):
            has = d[o]; o += 1
            if has: o = one(o, None, True)
        return out
    def env_cubes(s):
        """{node block: cube map file}: the environment NiTextureEffect each node lists for its own subtree (the last entries of the node's block;
        the root lists them all and is left out) (b120)"""
        d = s.d; B = s.h['blocks']; fx = {}
        for k, t in enumerate(s.T):
            if t != 'NiTextureEffect': continue
            o, sz = B[k][1], B[k][2]; refs = [r for r in struct.unpack_from('<%di' % (sz // 4), d, o + sz % 4) + struct.unpack_from('<%di' % ((sz - 1) // 4), d, o + (sz - 1) % 4) if 0 <= r < len(B) and B[r][0] == 'NiSourceCubeMap']
            if refs: fx[k] = nifps.S(s.h, struct.unpack_from('<I', d, B[refs[-1]][1] + 13)[0])
        out = {}
        for k, t in enumerate(s.T):
            if t not in NODES or s.par.get(k) is None: continue
            o, sz = B[k][1], B[k][2]; n = min(3, sz // 4); tail = struct.unpack_from('<%di' % n, d, o + sz - 4 * n)   # (count, ref) at the block's end
            if tail[-1] in fx and (n < 2 or tail[-2] == 1): out[k] = fx[tail[-1]]
        return out
    def extras(s, k):
        """the mesh's own extra data {name: value} (floats, integers, strings; a colour as a tuple): the material's numbers (b120)"""
        out = {}
        for e in s.B[k].get('extra', []) or []:
            if not (0 <= e < len(s.T)): continue
            t, o, sz = s.h['blocks'][e]; b = s.B[e]
            if t == 'NiColorExtraData': out[nifps.S(s.h, struct.unpack_from('<I', s.d, o)[0])] = struct.unpack_from('<4f', s.d, o + 4)
            elif 'name' in b and 'val' in b: out[b['name']] = b['val']
        return out
    def meshes(s):
        return [k for k, t in enumerate(s.T) if t == 'NiMesh']
    def geom(s, k):
        """-> dict(P, UV (or None), N (or None), I, regs (vertex regions), iregs, BI, BW, PAL) of mesh k"""
        st = s.streams(k); pk = ('POSITION_BP', 0) if ('POSITION_BP', 0) in st else ('POSITION', 0)
        P = st[pk][0].view('<f4').reshape(-1, 3); UV = st[('TEXCOORD', 0)][0].view('<f4').reshape(-1, 2) if ('TEXCOORD', 0) in st and SZ[st[('TEXCOORD', 0)][1]] == 8 else None
        nk = ('NORMAL_BP', 0) if ('NORMAL_BP', 0) in st else ('NORMAL', 0); N = st[nk][0].view('<f4').reshape(-1, 3) if nk in st and SZ[st[nk][1]] == 12 else None
        I = st[('INDEX', 0)]; I_ = I[0].view('<u2' if SZ[I[1]] == 2 else '<u4').ravel().astype(np.int64)
        g = dict(P=P, UV=UV, N=N, I=I_, regs=st[pk][2], iregs=I[2], C=st.get(('COLOR', 0)))
        for nm_, key in (('TANGENT', 'T'), ('BINORMAL', 'B')):   # (b120: the normal map's frame)
            tk = (nm_ + '_BP', 0) if (nm_ + '_BP', 0) in st else (nm_, 0)
            g[key] = st[tk][0].view('<f4').reshape(-1, 3) if tk in st and SZ[st[tk][1]] == 12 else None
        for i in (1, 2):   # (further texture coordinate sets: a dark map's own)
            if ('TEXCOORD', i) in st and SZ[st[('TEXCOORD', i)][1]] == 8: g['UV%d' % i] = st[('TEXCOORD', i)][0].view('<f4').reshape(-1, 2)
        if ('BLENDINDICES', 0) in st:
            bi = st[('BLENDINDICES', 0)][0]; g['BI'] = (bi.view('<u2').reshape(-1, 4) if bi.shape[1] == 8 else bi).astype(int)
            bw = st[('BLENDWEIGHT', 0)][0].view('<f4').reshape(len(P), -1); g['BW'] = np.c_[bw, 1 - bw.sum(1)] if bw.shape[1] == 3 else bw / np.maximum(bw.sum(1, keepdims=True), 1e-6)
            pal = st.get(('BONE_PALETTE', 0)); g['PAL'] = (pal[0].view('<u2').ravel(), pal[2]) if pal else None
        return g

FPS = 30
# ---------------------------------------------------------------- keyed transforms, every frame at once
def _keys_linear(ks, tt, dim):
    kt = np.array([k['t'] for k in ks]); kv = np.array([np.atleast_1d(k['v']) for k in ks], float)
    return np.stack([np.interp(tt, kt, kv[:, j]) for j in range(dim)], 1)
def sample_group(g, tt, dim):
    """a key group at the times tt -> (F, dim), or None without keys (linear keys at once; other types key by key)"""
    ks = g.get('keys') if g else None
    if not ks: return None
    if g.get('it', 1) == 1 or len(ks) == 1: return _keys_linear(ks, tt, dim)
    return np.array([np.atleast_1d(keyval(g, t, vec=dim > 1)) for t in tt], float)
def rot_xyz(e):   # (F, 3) Euler angles -> (F, 3, 3), R = Rz Ry Rx (make_scene.py)
    cx, sx, cy, sy, cz, sz = np.cos(e[:, 0]), np.sin(e[:, 0]), np.cos(e[:, 1]), np.sin(e[:, 1]), np.cos(e[:, 2]), np.sin(e[:, 2]); R = np.zeros((len(e), 3, 3))
    R[:, 0, 0] = cz * cy; R[:, 0, 1] = cz * sy * sx - sz * cx; R[:, 0, 2] = cz * sy * cx + sz * sx
    R[:, 1, 0] = sz * cy; R[:, 1, 1] = sz * sy * sx + cz * cx; R[:, 1, 2] = sz * sy * cx - cz * sx
    R[:, 2, 0] = -sy; R[:, 2, 1] = cy * sx; R[:, 2, 2] = cy * cx; return R
def qmats(q):   # (F, 4) w x y z -> (F, 3, 3)
    q = q / np.linalg.norm(q, axis=1, keepdims=True); w, x, y, z = q.T; R = np.zeros((len(q), 3, 3))
    R[:, 0, 0] = 1 - 2 * (y * y + z * z); R[:, 0, 1] = 2 * (x * y - z * w); R[:, 0, 2] = 2 * (x * z + y * w)
    R[:, 1, 0] = 2 * (x * y + z * w); R[:, 1, 1] = 1 - 2 * (x * x + z * z); R[:, 1, 2] = 2 * (y * z - x * w)
    R[:, 2, 0] = 2 * (x * z - y * w); R[:, 2, 1] = 2 * (y * z + x * w); R[:, 2, 2] = 1 - 2 * (x * x + y * y); return R
class Baked:
    """every node's local / world matrix and visibility at the times tt (controller time = ctime(controller, t))"""
    def __init__(s, N, tt):
        s.N = N; s.tt = np.asarray(tt, float); s.F = len(s.tt); s._L = {}; s._W = {}; s._V = {}
    def local(s, k):
        if k in s._L: return s._L[k]
        N = s.N; b = N.B[k]; F = s.F; T = np.tile(np.array(b['t'], float), (F, 1)); R = np.tile(np.array(b['r'], float).reshape(3, 3), (F, 1, 1)); S = np.full(F, float(b['s']))
        for c, cb, ct in N.chain(b.get('ctrl')):
            if ct != 'NiTransformController' or cb.get('interp') in (None, NONE): continue
            ib = N.B[cb['interp']]; ct_ = np.array([ctime(cb, t) for t in s.tt])
            if abs(ib['tr'][0]) < 1e30: T = np.tile(np.array(ib['tr'], float), (F, 1))
            if abs(ib['q'][0]) < 1e30: R = np.tile(qmat(ib['q']), (F, 1, 1))
            if abs(ib['sc']) < 1e30: S = np.full(F, float(ib['sc']))
            d = N.B[ib['data']] if ib.get('data') not in (None, NONE) else None
            if d is None: continue
            v = sample_group(d.get('trans'), ct_, 3)
            if v is not None: T = v
            if d.get('xyz') and any(g['keys'] for g in d['xyz']):
                e = np.stack([sample_group(g, ct_, 1)[:, 0] if g['keys'] else np.zeros(F) for g in d['xyz']], 1); R = rot_xyz(e)
            elif d.get('rot'):
                kt = np.array([k[0] for k in d['rot']]); kq = np.array([k[1] for k in d['rot']], float)
                for i in range(1, len(kq)):
                    if kq[i] @ kq[i - 1] < 0: kq[i] = -kq[i]
                if len(kt) == 1: R = np.tile(qmat(kq[0] / np.linalg.norm(kq[0])), (F, 1, 1))
                else:
                    j = np.clip(np.searchsorted(kt, ct_, 'right') - 1, 0, len(kt) - 2); u = np.clip((ct_ - kt[j]) / np.maximum(kt[j + 1] - kt[j], 1e-9), 0, 1)[:, None]
                    R = qmats(kq[j] * (1 - u) + kq[j + 1] * u)
            v = sample_group(d.get('scale'), ct_, 1)
            if v is not None: S = v[:, 0]
        M = np.zeros((F, 4, 4)); M[:, :3, :3] = R * S[:, None, None]; M[:, :3, 3] = T; M[:, 3, 3] = 1; s._L[k] = M; return M
    def world(s, k):
        if k in s._W: return s._W[k]
        p = s.N.par.get(k); M = s.local(k) if p is None else s.world(p) @ s.local(k); s._W[k] = M; return M
    def vis(s, k):
        """(F,) bool: the node and every ancestor shown"""
        if k in s._V: return s._V[k]
        N = s.N; v = None
        for c, cb, ct in N.chain(N.B[k].get('ctrl')):
            if ct == 'NiVisController' and cb.get('interp') not in (None, NONE):
                ib = N.B[cb['interp']]; d = N.B[ib['data']] if ib.get('data') not in (None, NONE) else None
                v = np.array([bool(keyval(d, ctime(cb, t), step=True)) for t in s.tt]) if d is not None and d['keys'] else np.full(s.F, bool(ib.get('value', 1)))
        if v is None: v = np.full(s.F, not (N.B[k].get('flags', 0) & 1))
        p = N.par.get(k)
        if p is not None: v = v & s.vis(p)
        s._V[k] = v; return v

# ---------------------------------------------------------------- the camera of a shot
def camera_block(N):
    """the shot's camera: the one under the INTRO_CAM node (shot 9's file carries a second, older one beside it)"""
    c = [k for k, t in enumerate(N.T) if t == 'NiCamera']; u = [k for k in c if 'INTRO_CAM' in N.path_of(k)]
    return (u or c)[0]
def camera_lens(N, k):
    """NiCamera -> (local 4x4, tan half-fov x, tan half-fov y)"""
    t, o, sz = N.h['blocks'][k]; r = nifps.R(N.d, o); a = nifps.avobj(r); r.u('H'); l, rr, tp, bt, near, far = r.arr('f', 6)
    M = np.eye(4); M[:3, :3] = np.array(a['r']).reshape(3, 3) * a['s']; M[:3, 3] = a['t']; return M, (rr - l) / 2, (tp - bt) / 2
def camera_track(path, tt):
    """a shot's camera file at the shot's own times tt -> (F, 9) eye dir up, tanx, tany. An NiCamera looks along its +x, its +y is up"""
    N = Nif(path); k = camera_block(N); L, tx, ty = camera_lens(N, k); tt = np.asarray(tt, float)
    B = Baked(N, tt); W = B.world(N.par[k]) @ L
    return np.concatenate([W[:, :3, 3], W[:, :3, 0], W[:, :3, 1]], 1).astype('<f4'), tx, ty
