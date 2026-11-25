# b87 (user: "the train in the train station map moves and is animated ... please include the train moving"): the maps'
# animated world objects (map.dat Fix_<Map>\WorldObjFx.txt -> world.dat world\WorldObjFx\<obj>.nif), for engine/src/wobj.h.
#
# Per object: wobj_<name>.gvm - its meshes rigid on one bone per animated node (the node's own space), the node animation
# baked as one clip at 30 fps in MAP space (bone poses = node world transforms; NiTransformData keys: translation linear /
# quadratic, rotation XYZ float keys (Hermite) or quaternion keys (slerp)), and wobj_<map>.txt - the timeline from
# WorldObjFx.txt (begin / end / reset times, sounds, effects) and the dummies the timeline names (static ones in map space,
# moving ones as bone + offset).
# Train_ani.nif (Train Station): TR01 waits north of the station turned 0.197 rad, eases straight between 5.7 and 6.33 s and
# runs through the station at ~125 m/s (5.7-12.6 s), then waits off the map; BEGIN 0 / END 21 / RESET 20: one pass every 41 s
# (the user's video "GITS First Assault - FTO - Train station revamped": passes 7:23 and 8:03).
# usage: make_wobj.py <world root (x/raw/world)> <map root with Fix_<Map>/WorldObjFx.txt> <out dir>
import sys, os, struct, numpy as np
H = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(H, '..')); sys.path.insert(0, os.path.join(H, '..', '..', 'vm'))
from nifhdr import hdr
import nifmesh
from gvmlib import Builder, mat2q

def kgroup(d, p, dim):
    n = struct.unpack_from('<I', d, p)[0]; p += 4; ks = []; it = 0
    if n:
        it = struct.unpack_from('<I', d, p)[0]; p += 4
        for _ in range(n):
            tm = struct.unpack_from('<f', d, p)[0]; p += 4; v = struct.unpack_from('<%df' % dim, d, p); p += 4 * dim; ex = None
            if it == 2: ex = struct.unpack_from('<%df' % (2 * dim), d, p); p += 8 * dim
            elif it == 3: p += 12
            ks.append((tm, np.array(v), None if ex is None else (np.array(ex[:dim]), np.array(ex[dim:]))))
    return dict(it=it, keys=ks), p

def tdata(d, o):
    p = o; nr = struct.unpack_from('<I', d, p)[0]; p += 4; R = None
    if nr:
        rt = struct.unpack_from('<I', d, p)[0]; p += 4
        if rt == 4:
            R = dict(kind='xyz', g=[])
            for _ in range(3): g, p = kgroup(d, p, 1); R['g'].append(g)
        else:
            ks = []
            for _ in range(nr):
                v = struct.unpack_from('<5f', d, p); p += 20; ks.append((v[0], np.array(v[1:]))); p += 12 if rt == 3 else 0
            R = dict(kind='quat', keys=ks)
    T, p = kgroup(d, p, 3); S, p = kgroup(d, p, 1)
    return dict(rot=R, trans=T, scale=S)

def ev(g, t, default):
    """a key group at time t (clamped): linear (1), Hermite with the stored tangents (2), else step"""
    ks = g['keys']
    if not ks: return default
    if t <= ks[0][0]: return ks[0][1]
    if t >= ks[-1][0]: return ks[-1][1]
    for a, b in zip(ks, ks[1:]):
        if a[0] <= t <= b[0]:
            u = (t - a[0]) / max(b[0] - a[0], 1e-6)
            if g['it'] == 2 and a[2] is not None and b[2] is not None:
                m0, m1 = a[2][0], b[2][1]
                return (2 * u ** 3 - 3 * u ** 2 + 1) * a[1] + (u ** 3 - 2 * u ** 2 + u) * m0 + (-2 * u ** 3 + 3 * u ** 2) * b[1] + (u ** 3 - u ** 2) * m1
            if g['it'] in (1, 2): return a[1] + (b[1] - a[1]) * u
            return a[1]
    return ks[-1][1]

def qmat_wxyz(q):
    w, x, y, z = q / np.linalg.norm(q)
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])

def rotxyz(a, b, c):
    cx, sx, cy, sy, cz, sz = np.cos(a), np.sin(a), np.cos(b), np.sin(b), np.cos(c), np.sin(c)
    X = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]]); Y = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]); Z = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Z @ Y @ X

def slerp(q0, q1, u):
    d = float(q0 @ q1)
    if d < 0: q1 = -q1; d = -d
    if d > 0.9995: q = q0 + (q1 - q0) * u; return q / np.linalg.norm(q)
    th = np.arccos(d); return (np.sin((1 - u) * th) * q0 + np.sin(u * th) * q1) / np.sin(th)

def node_rot(R, t, rest):
    if R is None: return rest
    if R['kind'] == 'xyz': return rotxyz(*[float(ev(g, t, np.zeros(1))[0]) for g in R['g']])
    ks = R['keys']
    if t <= ks[0][0]: return qmat_wxyz(ks[0][1])
    if t >= ks[-1][0]: return qmat_wxyz(ks[-1][1])
    for a, b in zip(ks, ks[1:]):
        if a[0] <= t <= b[0]: return qmat_wxyz(slerp(a[1], b[1], (t - a[0]) / max(b[0] - a[0], 1e-6)))

SIZES = {0x30437: 12, 0x20436: 8, 0x10215: 2, 0x10425: 4, 0x40110: 4, 0x40401: 4}
def mesh_block(h, k):
    """block k's NiMesh in its own space: P, N, UV, I (triangle list), texture name, alpha blend mode (0 / 2 add / 3 alpha)"""
    d = h['d']; B = h['blocks']; a = nifmesh.mesh(d, B[k][1]); tex = None; blend = 0; alpha = 1.0
    for p in a['props']:
        if B[p][0] == 'NiTexturingProperty':
            raw = d[B[p][1]:B[p][1] + B[p][2]]; v = struct.unpack_from('<I', raw, 19)[0]
            if v < len(B) and B[v][0] == 'NiSourceTexture': tex = nifmesh.S(h, struct.unpack_from('<I', d, B[v][1] + 13)[0])
        if B[p][0] == 'NiMaterialProperty':   # (its last float: the material's alpha - the helicopter's searchlight beams 0.06, rotor discs 0.7)
            alpha = struct.unpack_from('<f', d, B[p][1] + B[p][2] - 4)[0]
        if B[p][0] == 'NiAlphaProperty':
            o = B[p][1]; ne = struct.unpack_from('<I', d, o + 4)[0]; fl = struct.unpack_from('<H', d, o + 8 + 4 * ne + 4)[0]
            if fl & 1: blend = 2 if ((fl >> 5) & 15) == 0 else 3   # dst ONE: additive, else (INV_SRC_ALPHA): alpha-blended
    P = N = UV = I = None
    for st in a['streams']:
        ds = nifmesh.stream(d, B[st['ref']][1]); stride = sum(SIZES.get(f, 12) for f in ds['fmt'])
        raw = np.frombuffer(ds['data'], 'u1').reshape(-1, stride); off = 0
        for (sem, idx), f in zip(st['comps'], ds['fmt']):
            nm = nifmesh.S(h, sem); sz = SIZES.get(f, 12); col = raw[:, off:off + sz].copy()
            if nm == 'POSITION' and P is None: P = col.view('<f4').reshape(-1, 3).astype(np.float64)
            if nm == 'NORMAL' and N is None: N = col.view('<f4').reshape(-1, 3).astype(np.float64)
            if nm == 'TEXCOORD' and idx == 0 and UV is None: UV = col.view('<f4').reshape(-1, 2)
            if nm == 'INDEX': I = col.view('<u2' if sz == 2 else '<u4').ravel().astype(np.int64)
            off += sz
    if P is None or I is None or UV is None: return None
    if a.get('prim') == 1 and len(I) >= 3:   # strip -> list
        T = np.stack([I[:-2], I[1:-1], I[2:]], 1); T[1::2] = T[1::2][:, [1, 0, 2]]; I = T[(T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])].ravel()
    if N is None: N = np.zeros_like(P); N[:, 2] = 1
    return dict(P=P, N=N, UV=UV, I=I, tex=tex, blend=blend, alpha=alpha)

def read_objfx(path):
    """WorldObjFx.txt -> list of objects: dict(file, begin, end, reset, sounds=[...], effects=[...])"""
    objs = []; cur = None; blk = None
    for ln in open(path, encoding='latin1'):
        ln = ln.split('//')[0].strip()
        if not ln.startswith('['): continue
        k, _, v = ln[1:].partition(']'); v = v.strip()
        if k == 'OBJID': cur = dict(file='', begin=0, end=0, reset=0, sounds=[], effects=[]); objs.append(cur)
        elif cur is None: continue
        elif k == 'FILE_NAME': cur['file'] = v.replace('\\', '/')
        elif k in ('BEGIN_TIME', 'END_TIME', 'RESET_TIME'): cur[k.split('_')[0].lower()] = float(v)
        elif k in ('AMB_SOUND_TAG', 'EVENT_SOUND_TAG'): blk = dict(kind='amb' if k[0] == 'A' else 'event', tag=v, at=0, loop=0); cur['sounds'].append(blk)
        elif k == 'EFFECT_TAG': blk = dict(kind='fx', tag=v, at=0, loop=0, snd=''); cur['effects'].append(blk)
        elif blk is not None:
            f = k.split('_', 2)[-1] if k.startswith(('AMB_SOUND_', 'EVENT_SOUND_')) else k[len('EFFECT_'):] if k.startswith('EFFECT_') else k
            if k.startswith('EFFECT_SOUND_'): f = 'SND_' + k[len('EFFECT_SOUND_'):]
            blk[{'NAME': 'name', 'DUMMY': 'dummy', 'PLAYTIME': 'at', 'LOOPTIME': 'loop', 'RADIUS_IN': 'rin', 'RADIUS_OUT': 'rout', 'DUMMY_STICK': 'stick',
                 'SND_TAG': 'stag', 'SND_NAME': 'snd', 'SND_RADIUS_IN': 'srin', 'SND_RADIUS_OUT': 'srout', 'SND_LOOPTIME': 'sloop', 'SND_USE': 'suse',
                 'REVERSETIME': 'rev'}.get(f, f.lower())] = v
    return objs

def build(world, mapdir, mapname, out):
    objs = read_objfx(os.path.join(mapdir, 'Fix_' + mapname, 'WorldObjFx.txt'))
    lines = ['# wobj.h: the map\'s animated objects (tools/wobj/make_wobj.py)']
    for oi, ob in enumerate(objs):
        nif = os.path.join(world, 'WorldObjFx', ob['file']); d = open(nif, 'rb').read(); h = hdr(d); h['d'] = d; B = h['blocks']; S = lambda i: nifmesh.S(h, i)
        name = os.path.splitext(os.path.basename(nif))[0].lower()
        # nodes: parent, rest local transform, controller -> NiTransformData
        par = {}; loc = {}; nm = {}; ctl = {}
        for k, (t, o, sz) in enumerate(B):
            if t in ('NiNode', 'NiMesh', 'NiBillboardNode'):
                a = nifmesh.node(d, o) if t != 'NiMesh' else nifmesh.mesh(d, o)
                L = np.eye(4); L[:3, :3] = np.array(a['r']).reshape(3, 3) * a['s']; L[:3, 3] = a['t']; loc[k] = L; nm[k] = S(a['name']) if a['name'] != 0xffffffff else ''
                for c in a.get('children', []): par[c] = k
                c = struct.unpack_from('<i', d, o + 8 + 4 * len(a['extra']))[0]
                while c >= 0 and c < len(B) and B[c][0] == 'NiTransformController':
                    co = B[c][1]; nxt, fl, fr, ph, st, sp, tgt, ip = struct.unpack_from('<iHffffii', d, co)
                    if ip >= 0 and B[ip][0] == 'NiTransformInterpolator':
                        dref = struct.unpack_from('<i', d, B[ip][1] + 32)[0]
                        if dref >= 0: ctl[k] = (tdata(d, B[dref][1]), st, sp, (fl >> 1) & 3)   # cycle: 0 loop (rotors), 2 clamp
                    c = nxt
        def anc(k):   # nearest animated ancestor (or the node itself)
            while k is not None and k not in ctl: k = par.get(k)
            return k
        def world_rest(k):
            M = np.eye(4)
            while k is not None: M = loc[k] @ M; k = par.get(k)
            return M
        def world_at(k, t):
            M = np.eye(4)
            while k is not None:
                L = loc[k]
                if k in ctl:
                    td = ctl[k][0]; tt = t if ctl[k][3] != 0 or ctl[k][2] <= ctl[k][1] else ctl[k][1] + (t - ctl[k][1]) % (ctl[k][2] - ctl[k][1]); L = np.eye(4); L[:3, :3] = node_rot(td['rot'], tt, loc[k][:3, :3]); L[:3, 3] = ev(td['trans'], tt, loc[k][:3, 3])
                M = L @ M; k = par.get(k)
            return M
        animated = sorted(ctl); bones = [nm[k] or 'node%d' % k for k in animated] or ['root']
        rig = dict(names=bones); b = Builder(rig); b.spec = True
        for k in animated: b.bone(nm[k] or 'node%d' % k)
        tdir = os.path.dirname(nif); tmp_files = []
        def under_billboard(k):
            while k is not None:
                if B[k][0] == 'NiBillboardNode': return True
                k = par.get(k)
            return False
        for m in [x for x in range(len(B)) if B[x][0] == 'NiMesh']:
            A = anc(m)
            if A is None or under_billboard(m): continue   # (billboard glows: camera-facing quads the model program cannot draw)
            mb = mesh_block(h, m)
            if mb is None or not mb['tex']: continue
            X = np.linalg.inv(world_rest(A)) @ world_rest(m)   # the mesh's own space -> the animated node's space
            tp = os.path.join(tdir, os.path.basename(mb['tex'].replace('\\', '/')))
            if not os.path.exists(tp):
                alt = [f for f in os.listdir(tdir) if f.lower() == os.path.basename(tp).lower()]
                if not alt: print('  no texture', tp); continue
                tp = os.path.join(tdir, alt[0])
            bl = mb['blend']; al = float(mb['alpha'])
            if bl and 0 < al < 0.99:   # the material's alpha baked into a copy of the texture: an additive one darker, an alpha-blended one more see-through
                from PIL import Image
                im = Image.open(tp).convert('RGBA'); a_ = np.asarray(im).astype(np.float64)
                if bl == 2: a_[:, :, :3] *= al
                else: a_[:, :, 3] *= al
                tq = os.path.join(out, '_wobj_%s_a%02d.png' % (os.path.splitext(os.path.basename(tp))[0], int(round(al * 100))))
                Image.fromarray(np.clip(a_ + 0.5, 0, 255).astype(np.uint8), 'RGBA').save(tq); tp = tq; tmp_files.append(tq)
            b.add_rigid([mb], X, nm[A] or 'node%d' % A, np.eye(4), '', lambda t, bl=bl: bl, 512 if bl == 0 else 256, texmap=lambda t, tp=tp: tp)
        for f_ in tmp_files:
            if os.path.exists(f_): os.remove(f_)
        T0 = max([ctl[k][2] for k in animated] + [0.0]); nf = int(round(T0 * 30)) + 1; frames = []
        for f in range(nf):
            t = f / 30.0; P = np.zeros((len(bones), 3)); W = np.zeros((len(bones), 4))   # (gvmlib bake: quaternions x y z w by rig index)
            for s, k in enumerate(animated): M = world_at(k, t); P[s] = M[:3, 3]; W[s] = mat2q(M[:3, :3])
            frames.append((P, W))
        b.bake('anim', frames, 0)
        gvm = 'wobj_%s.gvm' % name; sz = b.write(os.path.join(out, gvm))
        print('%s: %d bytes, bones %s, %d frames (%.2f s), meshes %s' % (gvm, sz, bones, nf, T0, [(len(x['P']), len(x['I']) // 3) for x in b.meshes]))
        # dummies: every named node the timeline uses, at rest: static (map space) or riding an animated bone
        want = set(s.get('dummy', '') for s in ob['sounds'] + ob['effects'])
        lines.append('obj %s %.3f %.3f %.3f %.3f' % (gvm, ob['begin'], ob['end'], ob['reset'], T0))
        Pa = np.concatenate([x['P'] for x in b.meshes]); lines.append('box %.0f %.0f %.0f %.0f %.0f %.0f' % (*Pa.min(0), *Pa.max(0)))   # (bone space: wobj.h's frustum test)
        for k in sorted(nm):
            if nm[k] in want or (nm[k].startswith('shock')):
                A = anc(k); Wr = world_rest(k)   # (an animated dummy rides its own bone)
                if A is None: lines.append('dummy %s -1 %.1f %.1f %.1f' % (nm[k], *Wr[:3, 3]))
                else: o_ = np.linalg.inv(world_rest(A)) @ Wr; lines.append('dummy %s %d %.1f %.1f %.1f' % (nm[k], animated.index(A), *o_[:3, 3]))
        for s in ob['sounds']:
            lines.append('sound %s %s/%s %s %s %s %s %s' % (s['kind'], s['tag'], s.get('name', ''), s.get('dummy', '-'), s.get('at', 0), s.get('loop', 0), s.get('rin', 0), s.get('rout', 0)))
        for e in ob['effects']:
            lines.append('effect %s %s %s %s %s %s/%s' % (e['tag'], e.get('dummy', '-'), e.get('stick', 0), e.get('at', 0), e.get('loop', 0), e.get('stag', ''), e.get('snd', '') or '-'))
    open(os.path.join(out, 'wobj_%s.txt' % mapname.lower()), 'w').write('\n'.join(lines) + '\n')
    print('\n'.join(lines))

if __name__ == '__main__':
    world, mapdir, out = sys.argv[1:4]
    for mp in (sys.argv[4:] or ['Trainstation']): build(world, mapdir, mp, out)
