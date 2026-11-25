# chr_cyborg.gvm (b63): the Ghost Assault cyborg (footage: "9 Minutes of Ghost Assault Gameplay" and "NEW GAME MODE GHOST
# ASSAULT": white chrome bodies with a red eye). The game's own bare cyborg body, model\Cyborg\Cyborg\cy_male.nif (the
# practice dummy's body, tools/range/make_dummy.py), built like an operator (make_chr.py): the male rig, character space
# (z up, cm, faces -y), three distance LODs flagged in the blend field (chr.h), the rifle idle clip for the menu / fallback.
#   - body: the PolyMesh drawn with cyborgbody_D (the second PolyMesh is the same surface with the damage-glow textures: left
#     out); LOD 0 the whole mesh, LODs 1 / 2 simplified with meshoptimizer (indices only, as make_dummy.py)
#   - texture: cyborgbody_D at 1024 with the glow map cyborgbody_e added (the red eye and the seams light up, as the footage),
#     specular mask from cyborgbody_s (gvmlib Builder.spec)
# Usage: python3 make_cyborg.py [outdir]
import sys, os, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'anim')); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from gvmlib import *
from nmb import Network
from slots import SLOTS
from decode import decode
import gitspaths
import meshopt_c as meshoptimizer
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DATA')
SRC = gitspaths.dirp('ANIMOUT', 'model', 'Cyborg', 'Cyborg')
A = gitspaths.dirp('ANIMBIN', 'GIS_base_3rd_runtimeBinary'); NMB = A + 'GIS_base_3rd.nmb'
N = Network(NMB); RIG = N.rigs[0]
LOD_TRIS = (8400, 4600, 1900)   # (the operators: ~8400 / 6100 / 4700 triangles; the raw body is 16790)

TMP = tempfile.mkdtemp(prefix='cyborg_tex_')
def glow_tex():
    """cyborgbody_D with the glow map added (its alpha kept), written next to a copy of the _s map so Builder.spec finds it"""
    d = Image.open(SRC + 'cyborgbody_D.dds').convert('RGBA'); D = np.asarray(d).astype(np.float32)
    e = Image.open(SRC + 'cyborgbody_e.dds').convert('RGB').resize(d.size, Image.LANCZOS); E = np.asarray(e).astype(np.float32)
    out = D.copy(); out[..., :3] = np.clip(D[..., :3] + E * 0.85, 0, 255)   # (b112: the colour map as it is - b63-b111 x 1.22 stood in for the reflection the body now has: its plates lit by cybogCUBE, vm/matdb.py 'wgenv')
    p = os.path.join(TMP, 'cyborg_D.png'); Image.fromarray(out.astype(np.uint8)).save(p)
    MAT_ALIAS[p] = SRC + 'cyborgbody_D.dds'   # (b68: its material numbers, vm/matdb.py)
    Image.open(SRC + 'cyborgbody_s.dds').convert('RGBA').save(os.path.join(TMP, 'cyborg_s.png'))
    return p

def simplify(M, tris):
    I = M['I'].astype(np.uint32); P = np.ascontiguousarray(M['P'], np.float32)
    if len(I) // 3 <= tris: return
    dst = np.zeros(len(I), np.uint32)
    n = meshoptimizer.simplify(dst, I, P, target_index_count=tris * 3, target_error=0.05)
    I2 = dst[:n].astype(np.int64)
    cell = 1.0   # (the body is split at nearly every vertex: the edge collapses stop early; then vertex clustering, as make_dummy.py)
    while len(I2) // 3 > tris * 1.1 and cell < 8:
        U = np.unique(I2); key = np.floor(P[U] / cell).astype(np.int64)
        _, cid = np.unique(key, axis=0, return_inverse=True); cid = cid.ravel()
        cen = np.zeros((cid.max() + 1, 3)); np.add.at(cen, cid, P[U]); cen /= np.bincount(cid)[:, None]
        d = np.linalg.norm(P[U] - cen[cid], axis=1); order = np.lexsort((d, cid)); rep = np.full(cid.max() + 1, -1, np.int64)
        rep[cid[order[::-1]]] = U[order[::-1]]
        vmap = np.full(len(P), -1, np.int64); vmap[U] = rep[cid]
        T = vmap[I2].reshape(-1, 3); T = T[(T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])]
        _, u = np.unique(np.sort(T, 1), axis=0, return_index=True); T2 = T[np.sort(u)].ravel()
        if len(T2) // 3 <= tris * 1.1 or cell >= 7: I2 = T2; break
        cell += 0.5
    used = np.unique(I2); remap = np.full(len(P), -1, np.int64); remap[used] = np.arange(len(used))
    for k in list(M.keys()):   # every per-vertex array (positions, normals, UVs, skin, the specular mask)
        if k != 'I' and isinstance(M[k], np.ndarray) and M[k].ndim >= 1 and len(M[k]) == len(P): M[k] = M[k][used]
    M['I'] = remap[I2]

b = Builder(RIG); b.spec = True
body, Wb, nb_, _ = skinrun(SRC + 'cy_male.nif')
m = body[0]
alias = {n: 'Bip01 Head' for n in m['bone_names'] if n.replace(' ', '_') not in RIG['names']}
tex = glow_tex()
for k, tris in enumerate(LOD_TRIS):
    b.add_skinned(m, tex, 1024, alias)
    if tris: simplify(b.meshes[-1], tris)
    b.meshes[-1]['blend'] = (1 << k) << 4
b.envmap(SRC + 'cybogCUBE.dds')   # b112: the body's own reflection picture (its NiTextureEffect's cube; vm/matdb.py 'wgenv')
node = N.find(dict((s[0], s[1]) for s in SLOTS)['idle'])[0]
fr, rig, h = decode(N, A, N.rec(node, 0), 0)
b.bake('idle', fr, 1)
os.makedirs(OUT, exist_ok=True)
print('chr_cyborg.gvm', b.write(OUT + '/chr_cyborg.gvm'), 'bytes; bones', len(b.bones), 'palette', len(b.palette),
      'meshes', [(len(x['P']), len(x['I']) // 3, x['blend']) for x in b.meshes])
