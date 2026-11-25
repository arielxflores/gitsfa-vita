# Shared helpers for the operator-skill asset tools (make_skill_models.py, build_skills_fx.py, make_skill_overlays.py).
# Reads the game's NIFs with the project's own parsers (/root/gits/wraw/nifmesh.py, skin.py; /root/fx/nifps.py) and writes
# GVM1 files with /root/gits/vm/gvmlib.py's layout (engine/src/model.h). Raw inputs: /root/gits/raw/skills (extracted from
# model.dat / weapon.dat / effect.dat with VitaPort/tools/gitsdat.py; png/ = the DDS textures converted to PNG).
import sys, os, struct, numpy as np
_H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(_H, '..', 'lib')); sys.path.insert(0, os.path.join(_H, '..', '..', 'vm')); sys.path.insert(0, os.path.join(_H, '..'))
import nifmesh, gitspaths
from skin import run as skinrun
RAW = gitspaths.dirp('SKRAW')
NODES = ('NiNode', 'NiSortAdjustNode', 'NiBillboardNode', 'NiSwitchNode', 'NiLODNode')   # node-like blocks (NiNode layout first)
PNG = RAW + 'png/'

def tex_png(name):
    """PNG of a game texture by file name (case-insensitive, extension ignored) -> path or None"""
    if not name: return None
    p = PNG + os.path.splitext(os.path.basename(name.replace('\\', '/')))[0].lower() + '.png'
    return p if os.path.exists(p) else None

def node_tree(path):
    """-> {name: world 4x4}, {name: parent name}, h (all NiNode / NiMesh of the scene graph, rest pose)"""
    h = nifmesh.load(path); d = h['d']; B = h['blocks']; W = {}; par = {}
    def walk(k, M, pn):
        t, o, s = B[k]
        a = nifmesh.node(d, o) if t in NODES else nifmesh.mesh(d, o) if t == 'NiMesh' else None
        if a is None: return
        L = np.eye(4); L[:3, :3] = np.array(a['r']).reshape(3, 3) * a['s']; L[:3, 3] = a['t']; Wk = M @ L
        nm = nifmesh.S(h, a['name']) or ('#%d' % k)
        if t in NODES:
            if nm not in W: W[nm] = Wk; par[nm] = pn
            for c in a['children']: walk(c, Wk, nm)
    walk(0, np.eye(4), None)
    return W, par, h

def subtree(par, root):
    out = {root}; ch = True
    while ch:
        ch = False
        for k, p in par.items():
            if p in out and k not in out: out.add(k); ch = True
    return out

def mesh_texture(h, block):
    """base texture file name of an NiMesh block (via its NiTexturingProperty)"""
    d = h['d']; B = h['blocks']; t, o, s = B[block]; a = nifmesh.mesh(d, o)
    for p in a['props']:
        if p < len(B) and B[p][0] == 'NiTexturingProperty':
            raw = d[B[p][1]:B[p][1] + B[p][2]]; v = struct.unpack_from('<I', raw, 19)[0]
            if v < len(B) and B[v][0] == 'NiSourceTexture': return nifmesh.S(h, struct.unpack_from('<I', d, B[v][1] + 13)[0])
    return None

def skinned_rest(path):
    """every skinned mesh of a NIF in its rest pose (model space): list of dict(P, N, UV, I, bone (dominant bone name per
    vertex), tex)"""
    out, W, names, h = skinrun(path)
    res = []
    for m in out:
        BIr = (m['BI'][0].view('<u2').reshape(-1, 4) if m['BI'][0].shape[1] == 8 else m['BI'][0]).astype(int)
        BW = m['BW'][0].view('<f4').reshape(len(BIr), -1)
        BW = np.c_[BW, 1 - BW.sum(1)] if BW.shape[1] == 3 else BW / np.maximum(BW.sum(1, keepdims=True), 1e-6)
        palr = m['PAL'][0].view('<u2').ravel()
        M = [Wb @ s2b for Wb, s2b in zip(m['bone_world'], m['sm']['s2b'])]   # skin space -> model space per palette bone
        Ps = []; Bs = []; UVs = []; Is = []
        for (vs, vc), (ps, pc), (is_, ic) in zip(m['vregs'], m['PAL'][2], m['idxregs']):
            P = m['P'][vs:vs + vc].astype(float); P4 = np.c_[P, np.ones(len(P))]
            acc = np.zeros((len(P), 3)); bi = BIr[vs:vs + vc]; bw = BW[vs:vs + vc]
            for j in range(4):
                gb = palr[ps + bi[:, j]]
                for b in np.unique(gb):
                    sel = gb == b; acc[sel] += bw[sel, j:j + 1] * (P4[sel] @ M[b].T)[:, :3]
            dom = palr[ps + bi[np.arange(len(P)), np.argmax(bw, 1)]]
            Ps.append(acc); Bs.append([m['bone_names'][b] for b in dom]); UVs.append(m['UV'][vs:vs + vc]); Is.append(m['I'][is_:is_ + ic] + sum(len(x) for x in Ps[:-1]))
        P = np.concatenate(Ps); I = np.concatenate(Is); UV = np.concatenate(UVs); bone = sum(Bs, [])
        res.append(dict(P=P, N=vnormals(P, I), UV=UV, I=I, bone=np.array(bone, dtype=object), tex=mesh_texture(h, m['block']), name=m['name']))
    return res

def static_parts(path, with_skinned=False):
    """the static (non-skinned) NiMesh blocks of a NIF in model space (node transforms applied):
    list of dict(name, node, P, N, UV, I, tex). Robust version of gvmlib.static_meshes (more stream formats, skips skinned
    meshes unless with_skinned, never aborts on one odd mesh)."""
    from skin import SZ
    h = nifmesh.load(path); d = h['d']; B = h['blocks']; out = []
    def walk(k, M, pname):
        t, o, s = B[k]
        a = nifmesh.node(d, o) if t in NODES else nifmesh.mesh(d, o) if t == 'NiMesh' else None
        if a is None: return
        L = np.eye(4); L[:3, :3] = np.array(a['r']).reshape(3, 3) * a['s']; L[:3, 3] = a['t']; Wk = M @ L
        if t in NODES:
            for c in a['children']: walk(c, Wk, nifmesh.S(h, a['name']))
            return
        try:
            st = {}
            for sd in a['streams']:
                ds = nifmesh.stream(d, B[sd['ref']][1]); stride = sum(SZ[f] for f in ds['fmt'])
                raw = np.frombuffer(ds['data'], 'u1').reshape(-1, stride); off = 0
                for (sem, idx), f in zip(sd['comps'], ds['fmt']):
                    nm = nifmesh.S(h, sem); sz = SZ[f]; st[(nm, idx)] = (raw[:, off:off + sz].copy(), f); off += sz
            if ('BLENDINDICES', 0) in st and not with_skinned: return
            P = st.get(('POSITION', 0), st.get(('POSITION_BP', 0)))[0].view('<f4').reshape(-1, 3).astype(float)
            uv = st.get(('TEXCOORD', 0)); UV = uv[0].view('<f4').reshape(-1, 2).astype(float) if uv and SZ[uv[1]] == 8 else np.zeros((len(P), 2))
            ix, f = st[('INDEX', 0)]; I = ix.view('<u2' if SZ[f] == 2 else '<u4').ravel().astype(np.int64)
            if a.get('prim') == 1 and len(I) >= 3:   # strip -> list
                T = np.stack([I[:-2], I[1:-1], I[2:]], 1); T[1::2] = T[1::2][:, [1, 0, 2]]
                I = T[(T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])].ravel()
            I = I[:len(I) // 3 * 3]
            P = (np.c_[P, np.ones(len(P))] @ Wk.T)[:, :3]
            out.append(dict(name=nifmesh.S(h, a['name']), node=pname, P=P, N=vnormals(P, I) if len(I) else np.zeros_like(P), UV=UV, I=I, tex=mesh_texture(h, k), W=Wk))
        except Exception as e:
            pass
    walk(0, np.eye(4), None)
    return out

def vnormals(P, I):
    N = np.zeros_like(P); tri = I.reshape(-1, 3); fn = np.cross(P[tri[:, 1]] - P[tri[:, 0]], P[tri[:, 2]] - P[tri[:, 0]])
    for k in range(3): np.add.at(N, tri[:, k], fn)
    return N / (np.linalg.norm(N, axis=1, keepdims=True) + 1e-9)

def submesh(m, vmask):
    """the triangles of mesh m assigned to vmask (a triangle belongs to the part most of its vertices are in, so no
    triangle is lost at a part boundary when the parts partition the vertices), re-indexed"""
    tri = m['I'].reshape(-1, 3); keep = vmask[tri].sum(1) >= 2; tri = tri[keep]
    used = np.unique(tri); remap = -np.ones(len(m['P']), int); remap[used] = np.arange(len(used))
    o = dict(m); o.update(P=m['P'][used], N=m['N'][used], UV=m['UV'][used], I=remap[tri].ravel())
    if 'bone' in m: o['bone'] = m['bone'][used]
    return o

# ---------------------------------------------------------------- GVM1 writer for rigid (per-mesh single bone) models
import etcpak
from PIL import Image
class Rigid:
    """GVM1 with named rigid bones. Every mesh sits on ONE bone (weight 255) with an identity skin->bone palette entry, so the
    engine draws it through model.h's rigid GPU path (model_prepare_rigid) and a bone's pose is simply the part's model-space
    transform (identity = rest). Clip 'rest' (1 frame, identity poses) is always written."""
    def __init__(s): s.bones = []; s.tex = []; s.texk = {}; s.meshes = []; s.att = {}; s.clips = []; s.emis = set()
    def bone(s, name):
        if name not in s.bones: s.bones.append(name)
        return s.bones.index(name)
    def texture(s, png, maxs=512, force_alpha=False, img=None, glow=None):
        """glow (b71): the NIF's glow map on uv set 0 (a file): baked in alpha as vm/gvmlib.py does (alpha = 1 - glow, the colour
        towards its hue; meshes on it get the glow flag, engine model.h MDF_EMIS)"""
        key = (png, maxs, force_alpha, glow) if img is None else id(img)
        if key in s.texk: return s.texk[key]
        im = (img if img is not None else Image.open(png)).convert('RGBA'); W, H = im.size; sc = min(1, maxs / max(W, H))
        im = im.resize((max(4, int(W * sc)), max(4, int(H * sc))), Image.LANCZOS)
        fmt = 5 if (force_alpha or np.asarray(im)[:, :, 3].min() < 250) else 1
        lit = False
        if glow and fmt == 1 and os.path.exists(glow):
            E = np.asarray(Image.open(glow).convert('RGB').resize(im.size, Image.BILINEAR)).astype(np.float64) / 255; e = E.max(2)
            if e.max() > 0.02:
                a = np.asarray(im).astype(np.float64); hue = E / np.maximum(e, 1e-3)[:, :, None]
                a[:, :, :3] = a[:, :, :3] * (1 - e[:, :, None]) + hue * 255 * e[:, :, None]; a[:, :, 3] = 255 * (1 - e)
                im = Image.fromarray(np.clip(a + 0.5, 0, 255).astype(np.uint8), 'RGBA'); fmt = 5; lit = True
                print('glow: %s <- %s, %.1f %% of the texels lit' % (os.path.basename(png or '?'), os.path.basename(glow), 100 * (e > 0.05).mean()))
        mm = []; w_, h_ = im.size; cur = im
        while w_ >= 4 and h_ >= 4:
            a = np.ascontiguousarray(np.asarray(cur.convert('RGBA'))).tobytes()
            mm.append(etcpak.compress_to_dxt1(a, w_, h_) if fmt == 1 else etcpak.compress_to_dxt5(a, w_, h_))
            if w_ == 4 or h_ == 4: break
            w_ //= 2; h_ //= 2; cur = cur.resize((w_, h_), Image.BOX)
        s.tex.append((im.size[0], im.size[1], fmt, mm)); s.texk[key] = len(s.tex) - 1
        if lit: s.emis.add(s.texk[key])
        return s.texk[key]
    def mesh(s, P, N, UV, I, bone, tex, blend=0):
        """blend: 0 opaque (alpha-tested by the texture's alpha in the shader's output), 2 additive (drawn in the second pass)"""
        P = np.asarray(P, float); assert len(P) < 65536, len(P)
        s.meshes.append(dict(P=P, N=np.asarray(N, float), UV=np.asarray(UV, float), I=np.asarray(I).astype(int), b=s.bone(bone), tex=tex, blend=blend))
        return len(s.meshes) - 1
    def clip(s, name, frames, loop=1, fps=30.0):
        """frames: list of {bone: (q xyzw, p)} (missing bones = identity)"""
        s.clips.append((name, loop, fps, frames))
    def write(s, path):
        nb = len(s.bones)
        clips = [('rest', 1, 30.0, [{}])] + s.clips
        with open(path, 'wb') as f:
            f.write(b'GVM1' + struct.pack('<IIIII', len(s.tex), nb, nb, len(s.meshes), len(clips)))
            for W_, H_, fmt, mm in s.tex:
                f.write(struct.pack('<HHBB', W_, H_, fmt, len(mm)))
                for b in mm: f.write(struct.pack('<I', len(b))); f.write(b)
            for b in s.bones: f.write(b.encode()[:31].ljust(32, b'\0'))
            for i in range(nb): f.write(struct.pack('<i', i)); f.write(np.eye(4)[:3].astype('<f4').tobytes())   # palette entry i = bone i, identity
            for m in s.meshes:
                n = len(m['P']); ni = len(m['I'])
                v = np.zeros(n, dtype=[('p', '<f4', 3), ('n', 'i1', 4), ('uv', '<f4', 2), ('bi', 'u1', 4), ('bw', 'u1', 4)])
                v['p'] = m['P']; v['n'][:, :3] = np.clip(np.round(m['N'] * 127), -127, 127); v['uv'] = m['UV']
                v['bi'] = m['b']; v['bw'][:, 0] = 255
                f.write(struct.pack('<iiII', m['tex'], m['blend'] | (1 << 16 if m['tex'] in s.emis else 0), n, ni)); f.write(v.tobytes()); f.write(m['I'].astype('<u2').tobytes())   # (b71: + the glow flag)
                if ni % 2: f.write(b'\0\0')
            for name, loop, fps, frames in clips:
                data = np.zeros((len(frames), nb, 7), '<f4'); data[:, :, 3] = 1
                for i, fr in enumerate(frames):
                    for bn, (q, p) in fr.items(): data[i, s.bones.index(bn), :4] = q; data[i, s.bones.index(bn), 4:] = p
                f.write(name.encode()[:15].ljust(16, b'\0')); f.write(struct.pack('<IIf', len(frames), loop, fps)); f.write(data.tobytes())
            f.write(struct.pack('<I', len(s.att)))
            for k, v in list(s.att.items())[:8]: f.write(k.encode()[:15].ljust(16, b'\0')); f.write(np.asarray(v, '<f4').tobytes())
        return os.path.getsize(path)

def qaxis(axis, ang):
    a = np.asarray(axis, float); a /= np.linalg.norm(a); s = np.sin(ang / 2)
    return np.array([a[0] * s, a[1] * s, a[2] * s, np.cos(ang / 2)])
def pivot_pose(q, pivot):
    """pose (q, p) of a rigid part rotated by q about a model-space pivot"""
    x, y, z, w = q
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    pv = np.asarray(pivot, float); return q, pv - R @ pv
