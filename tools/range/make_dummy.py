# chr_dummy.gvm: the practice target of the Shooting Range and the tutorial's shooting lesson.
# The game dresses it as model\Cyborg\Cyborg\OUTFIT_Target_Dummy.nif, an empty outfit (nodes OUTFIT_Male_Taget_Dummy /
# OUTFIT_Female_Target_Dummy, no meshes) on the bare male cyborg body cy_male.nif, and draws it as a cyan hologram.
# Built like vm/make_chr.py (same rig, same third-person M4A1 clips, character space: z up, cm, faces -y) with:
#   - the body mesh "PolyMesh" that uses cyborgbody_D.dds (the second PolyMesh is the same surface again with the
#     damage-glow textures and is left out), simplified with meshoptimizer (indices only: UVs and skin weights are kept)
#   - the third-person M4A1 on the Grip bone
#   - textures pre-tinted to the hologram colour (the engine draws the dummy additively; see engine/src/range.h)
#   - b73 (user: "the blue dummy target character models looked a little low polygon"): three distance LODs (blend bits
#     4..6; engine range.h picks one by the dummy's size on screen, model.h md_lod_sel), from the body welded first (the
#     game's mesh repeats ~5400 vertices at the same position and UV with split normals: faceted, and the seams stopped
#     the simplifier at ~3800 triangles, after which b71 clustered the rest on a 2 cm grid = the blocky head and chest)
# Usage: python3 make_dummy.py [outdir=/root/gits/build/data] [LOD0 body triangles=6000]   (LOD1 / LOD2: 1/2, 1/4; gun 1184 / 600 / 300)
# Source files (copied out of model.dat): /root/anim/out/model/Cyborg/Cyborg/{cy_male.nif,cyborgbody_D.dds}
import sys, os, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'vm')); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
from gvmlib import *
import meshopt_c as meshoptimizer   # databuilder: meshoptimizer 0.23 through ctypes (tools/lib), same results as the pip wrapper

OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DATA')
TRIS = int(sys.argv[2]) if len(sys.argv) > 2 else 6000
BODY_LODS = (TRIS, TRIS // 2, TRIS // 4); GUN_LODS = (100000, 600, 300)
SRC = gitspaths.dirp('ANIMOUT', 'model', 'Cyborg', 'Cyborg')
A = gitspaths.dirp('ANIMBIN', 'GIS_base_3rd_runtimeBinary'); NMB = A + 'GIS_base_3rd.nmb'
R = add_parents(NMB, rigs(NMB)); RIG = R[0]; MAPS = find_maps(NMB)
CLIPS = [('idle', 'base_M4A1_3rd_stand_idle_base_42430102', 1),
         ('walk_f', 'base_M4A1_3rd_stand_walk_forw_2699f5a5', 1), ('walk_b', 'base_M4A1_3rd_stand_walk_back_66cbd758', 1),
         ('walk_l', 'base_M4A1_3rd_stand_walk_left_d9131239', 1), ('walk_r', 'base_M4A1_3rd_stand_walk_right_9b7fa0f', 1),
         ('hit', 'base_M4A1_3rd_stand_damage_fi_2a098fca', 0)]

TMP = tempfile.mkdtemp(prefix='dummy_tex_')
def holo(src, dst, base=(0.04, 0.26, 0.24), gain=(0.30, 0.78, 0.70)):
    """cyan hologram version of a diffuse texture: brightness from the texture's luminance (contrast-stretched)"""
    im = np.asarray(Image.open(src).convert('RGB')).astype(np.float32) / 255
    L = im @ np.array([0.30, 0.59, 0.11], np.float32)
    lo, hi = np.percentile(L, 3), np.percentile(L, 97); L = np.clip((L - lo) / max(hi - lo, 1e-3), 0, 1) ** 0.8
    out = np.clip(np.array(base)[None, None] + L[..., None] * np.array(gain)[None, None], 0, 1)
    Image.fromarray((out * 255).astype('u1')).save(dst); return dst

b = Builder(RIG)
body, Wb, nb_, _ = skinrun(SRC + 'cy_male.nif')
m = body[0]                               # the PolyMesh drawn with cyborgbody_D.dds (body[1] = same surface, glow layer)
alias = {n: 'Bip01 Head' for n in m['bone_names'] if n.replace(' ', '_') not in RIG['names']}
print('body: %d vertices, %d triangles, %d bones; aliased %s' % (len(m['P']), len(m['I']) // 3, len(m['bone_names']), alias))
BODY_TEX = holo(SRC + 'cyborgbody_D.dds', TMP + '/dummy_body.png')
b.add_skinned(m, BODY_TEX, 1024, alias)   # (b73: 1024, was 512)

def weld_last(b, what):
    """b73: vertices at the same position with the same UV and skin are one (the normal: the sum of the copies' face normals)"""
    M = b.meshes[-1]; key = np.hstack([np.round(M['P'], 3), np.round(M['UV'], 5), M['BI'], np.round(M['BW'], 4)])
    _, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True); inv = inv.ravel()
    N = np.zeros((len(first), 3)); np.add.at(N, inv, M['N']); N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9
    n0 = len(M['P'])
    for k in ('P', 'UV', 'BI', 'BW'): M[k] = M[k][first]
    if M.get('SP') is not None: M['SP'] = M['SP'][first]
    M['N'] = N; M['I'] = inv[M['I']]
    print('%s welded: %d -> %d vertices' % (what, n0, len(M['P'])))
weld_last(b, 'body')

# simplify (index buffer only: positions, UVs and skin weights of the kept vertices are untouched), then drop unused vertices
def simplify_last(b, tris, what):
    M = b.meshes[-1]; I = M['I'].astype(np.uint32); P = np.ascontiguousarray(M['P'], np.float32)
    if len(I) // 3 <= tris: return
    dst = np.zeros(len(I), np.uint32)
    n = meshoptimizer.simplify(dst, I, P, target_index_count=tris * 3, target_error=0.05)
    I2 = dst[:n].astype(np.int64)
    # the body is split at nearly every vertex (UV / normal seams: 16712 vertices for 16790 triangles), so the edge
    # collapses stop at ~3800 triangles; the rest by vertex clustering on a fine grid (cell grows until the target is met).
    # Each cell keeps the used vertex nearest its centre (with that vertex's UV and skin weights).
    cell = 1.0
    while len(I2) // 3 > tris * 1.1 and cell < 8:
        key = np.floor(P[np.unique(I2)] / cell).astype(np.int64)
        U = np.unique(I2); _, cid = np.unique(key, axis=0, return_inverse=True); cid = cid.ravel()
        cen = np.zeros((cid.max() + 1, 3)); np.add.at(cen, cid, P[U]); cen /= np.bincount(cid)[:, None]
        d = np.linalg.norm(P[U] - cen[cid], axis=1); order = np.lexsort((d, cid)); rep = np.full(cid.max() + 1, -1, np.int64)
        rep[cid[order[::-1]]] = U[order[::-1]]          # the nearest one is written last
        vmap = np.full(len(P), -1, np.int64); vmap[U] = rep[cid]
        T = vmap[I2].reshape(-1, 3); T = T[(T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])]
        _, u = np.unique(np.sort(T, 1), axis=0, return_index=True); T2 = T[np.sort(u)].ravel()
        if len(T2) // 3 <= tris * 1.1 or cell >= 7: I2 = T2; print('  %s: clustered on a %.1f cm grid' % (what, cell)); break
        cell += 0.5
    used = np.unique(I2); remap = np.full(len(P), -1, np.int64); remap[used] = np.arange(len(used))
    for k in ('P', 'N', 'UV', 'BI', 'BW'): M[k] = M[k][used]
    M['I'] = remap[I2]
    print('%s simplified: %d -> %d triangles, %d vertices' % (what, len(I) // 3, len(M['I']) // 3, len(M['P'])))
def lod_copies(b, targets, what):
    """the last mesh -> one mesh per LOD target (each flagged for its LOD; a target at or above the mesh's size: the mesh itself)"""
    base = b.meshes.pop(); out = []
    for k, t in enumerate(targets):
        M = {key: (val.copy() if isinstance(val, np.ndarray) else val) for key, val in base.items()}
        b.meshes.append(M); simplify_last(b, t, '%s LOD%d' % (what, k)); M['blend'] = base['blend'] | (1 << k) << 4
        if out and len(M['I']) == len(out[-1]['I']) and np.array_equal(M['I'], out[-1]['I']): b.meshes.pop(); out[-1]['blend'] |= (1 << k) << 4; continue
        out.append(M)
lod_copies(b, BODY_LODS, 'body')

# the rifle (third-person M4A1) on the Grip bone, like the playable characters' guns
G = gitspaths.dirp('ANIMOUT', 'weapon', 'AR_M4A1', '3rd')
gun, Wgun, ng, _ = skinrun(G + 'M4A1_body_3rd.nif')
gtex = holo(G + 'M4A1_3rd.dds', TMP + '/dummy_gun.png', base=(0.03, 0.20, 0.19), gain=(0.25, 0.65, 0.60))
for gm in gun:
    gm = dict(gm); gm['bone_names'] = ['Grip'] * len(gm['bone_names']); gm['sm'] = dict(gm['sm']); gm['sm']['s2b'] = [np.eye(4)] * len(gm['bone_names'])
    b.add_skinned(gm, gtex, 256)
    if len(gm['I']) // 3 > 200: weld_last(b, 'gun mesh'); lod_copies(b, GUN_LODS, 'gun mesh')   # (the small part: every LOD)
for ent in CLIPS:
    fr, h = clip_frames(A, ent[1], RIG, MAPS)
    b.bake(ent[0], fr, ent[2]); print('clip', ent[0], len(fr), 'frames')
os.makedirs(OUT, exist_ok=True)
print('chr_dummy.gvm', b.write(OUT + '/chr_dummy.gvm'), 'bytes; bones', len(b.bones), 'palette', len(b.palette), 'meshes', [(len(x['P']), len(x['I']) // 3) for x in b.meshes])
