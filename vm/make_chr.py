# chr_<name>.gvm: a third-person operator for the bots (engine/src/chr.h + bots.h).
#  - body: three distance LODs of the game's own <Name>_A_TPS.nif (the LODs nearest 6000, 3500 and 1300 vertices), each
#    flagged in the mesh's blend field (bits 4..6 = the LODs that draw it; chr.h picks one by distance); hair and goggles
#    (rigid on the head) are drawn at every LOD
#  - no weapon: the bots carry their own third-person weapon (w3_<weapon>.gvm, vm/make_w3.py) on the Grip bone
#  - b71: the PC's glow maps baked into the colour map (gvmlib.emissive_image), the hair / goggles flagged MESH_UNMASK (hidden
#    under the enemy team's mask, make_masks.py), attachment 'mask' = the operator's Head_Attach offset from the head bone
#  - one clip, "idle" (the rifle stance, decoded with the network's own channel map), for the menu preview and the
#    shooting range's fallback; the bots animate from the shared banks chr_anim_m.gab / chr_anim_f.gab (tools/anim)
# Character space: z up, cm, the character faces -y (rig space of GIS_base_3rd / GIS_female_3rd).
# b71 skins: chr_<name>_<skin>.gvm from the operator's other folders (vm/make_skins.py lists them)
# Usage: python3 make_chr.py [outdir] [Name] [skin folder suffix, default A]
import sys, os, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'anim'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'mapvis'))   # (b71: nifmat)
from gvmlib import *
from nmb import Network
from slots import SLOTS
from decode import decode
import gitspaths
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DATA')
CH = sys.argv[2] if len(sys.argv) > 2 else 'Bato'
SKIN = sys.argv[3] if len(sys.argv) > 3 else 'A'   # b71: the skin = the operator's folder <Name>_<SKIN> (A: the default, chr_<name>.gvm)
FEMALE = CH.lower() in ('motoko', 'maven', 'reiko', 'sitara')   # these use the female skeleton and its clips (GIS_female_3rd)
A = gitspaths.dirp('ANIMBIN', 'GIS_female_3rd_runtimeBinary' if FEMALE else 'GIS_base_3rd_runtimeBinary')
NMB = A + ('GIS_female_3rd.nmb' if FEMALE else 'GIS_base_3rd.nmb')
N = Network(NMB); RIG = N.rigs[0]
DA = [d.replace(os.sep, '/') for d in glob.glob(gitspaths.path('ANIMOUT', 'model', 'ActorGhost', '*', '*_A', '')) if d.replace(os.sep, '/').lower().endswith('/%s_a/' % CH.lower())][0]
CHF = DA.rstrip('/').split('/')[-1][:-2]   # file-name spelling of the character (ReiKo -> Reiko)
OPD = DA.rstrip('/').rsplit('/', 1)[0] + '/'
FOLD = {f.lower(): OPD + f + '/' for f in os.listdir(OPD) if os.path.isdir(OPD + f)}
tps_of = lambda d: sorted(f for f in os.listdir(d) if f.lower().endswith('_tps.nif'))
def body_tex(d):
    """the colour map of a folder's TPS body meshes (every skin's body meshes share one)"""
    import nifmat
    h, ms = nifmat.walk_all(d + tps_of(d)[0]); body = {x['block'] for x in skinrun(d + tps_of(d)[0])[0] if len(x['bone_names']) >= 30}; t = []
    for m in ms:
        b_ = m['props'].get('NiTexturingProperty', (0, {}))[1].get('base')
        if isinstance(b_, dict) and m['k'] in body: t.append(b_['file'])
    return max(set(t), key=t.count)
SK = FOLD[('%s_%s' % (CHF, SKIN)).lower()]   # the skin's own folder
if tps_of(SK): D = SK   # a skin with its own meshes
else:   # b71: a texture-only skin (its _D replaces a mesh folder's): the folder whose body wears a map of the same name (the _A folder first)
    own = {f.lower() for f in os.listdir(SK) if f.lower().endswith('_d.dds')}
    src = [d for k, d in sorted(FOLD.items(), key=lambda kv: (not kv[0].endswith('_a'), len(kv[0]), kv[0])) if tps_of(d) and body_tex(d).lower() in own]
    assert src, 'no mesh folder for the skin %s' % SK
    D = src[0]
def tex_in(d, t):
    if not t: return None
    for f in os.listdir(d):
        if f.lower() == t.lower(): return d + f
    return None
texf = lambda t: tex_in(SK, t) or tex_in(D, t)   # the skin's own file, else the mesh folder's
b = Builder(RIG); b.spec = True   # b62: specular masks (gvmlib: the _S map where the game has one, else a guess from the colours)
body, Wb, nb_, _ = skinrun(D + tps_of(D)[0])
bnode = {v: k for k, v in nb_.items()}
cands = [x for x in body if len(x['bone_names']) >= 30]
# b116 (user: "kuro seems to be missing part of his body between his arms and vest you can kind of see right through like
# there's nothing connecting his arms to his torso with the standard, camo and gold black skins"): a body LOD node can
# hold a second, small skinned mesh (/Scene Root/L1/L1_A under L1's own mesh): Kuro_A's / Kuro_Goldblack's torn sleeve
# edges around the armholes (266 / 203 vertices on Spine2 + the clavicles, their own alpha-tested two-sided texture
# EMP_Body_D_A.dds), Ishikawa's strands (76-155 vertices, the hair's texture). The ">= 30 bones" pick left them out.
import nifmat as _nm
_path = {m['k']: m.get('path', '') for m in _nm.walk_all(D + tps_of(D)[0])[1]}
def companions(big):
    """the small skinned meshes of the LOD node that holds the body mesh `big`, each with its colour map"""
    out = []
    for x in body:
        if len(x['bone_names']) >= 30 or not _path.get(x['block'], '').startswith(_path.get(big['block'], '?') + '/'): continue
        tp = [m for m in _nm.walk_all(D + tps_of(D)[0])[1] if m['k'] == x['block']][0]['props'].get('NiTexturingProperty', (0, {}))[1].get('base')
        if isinstance(tp, dict) and texf(tp['file']): out.append((x, texf(tp['file'])))
    return out
def add_companions(bb, big, lodbits, only_known=False):
    for x, tp in companions(big):
        if only_known and tp not in b.TEX:   # (the menu model borrows the in-game model's textures: a companion the game's LOD 0 does not have -
            print('companion: %d vertices of %s (%s) left out of the menu model: the in-game model has no such part' % (len(x['P']), _path[x['block']], os.path.basename(tp)), file=sys.stderr); continue   # Ishikawa's strands, L1 only)
        alias = {n: 'Bip01 Head' for n in x['bone_names'] if n.replace(' ', '_') not in RIG['names']}
        bb.add_skinned(x, tp, 256, alias, flags=MESH_CUT | lodbits, alias_s2b=True)
        print('companion: %d vertices of %s (%s) on %s' % (len(x['P']), _path[x['block']], os.path.basename(tp), '/'.join(x['bone_names'])), file=sys.stderr)
BODY = texf(body_tex(D))   # (= b62-b70's pick for the _A folders: the first *_d.dds that is not hair or goggles)
if SK != D:   # a texture-only skin: its maps take the mesh folder's material (vm/matdb.py: no NIF names them), its own glow map if it has one
    for f in os.listdir(SK):
        if tex_in(D, f): MAT_ALIAS[SK + f] = tex_in(D, f)
    rec = material_record(tex_in(D, os.path.basename(BODY)))
    if rec and rec.get('G') and tex_in(SK, os.path.basename(rec['G'])): GLOW_ALIAS[BODY] = tex_in(SK, os.path.basename(rec['G']))
# three distance LODs, each lighter than the last (b71: a skin's LOD set can run 26594 .. 3855: the next lighter one, or the last again)
lods = []
for target in (6000, 3500, 1300):
    pool = [x for x in cands if all(x is not y for y in lods) and (not lods or len(x['P']) < len(lods[-1]['P']))]
    lods.append(sorted(pool, key=lambda x: abs(len(x['P']) - target))[0] if pool else lods[-1])
# hair and goggles (rigid on the head): the skin's own head parts, else the mesh folder's
HEAD_NIFS = ('hair.nif', 'default_goggle.nif', 'goggle.nif', 'gogle.nif', 'goggles.nif')
HD = SK if any(f.lower() in HEAD_NIFS for f in os.listdir(SK)) else D
texmap = lambda t: texf(t) or BODY
def part_glows(path):
    """b75: the part's own material has a glow map. Motoko_A's / Paz_A's Hair.nif have none (Togusa_A's has): b71-b74 baked
    the body's glow map through every head part's uv set 1, so those hairs came out with lit specks and cyan patches"""
    import nifmat
    h, ms = nifmat.walk_all(path)
    return any(isinstance(m['props'].get('NiTexturingProperty', (0, {}))[1].get('glow'), dict) for m in ms)
heads = []; hglow = []
if 'Head_Attach' in bnode:
    for part in sorted((f for f in os.listdir(HD) if f.lower() in HEAD_NIFS), key=lambda f: HEAD_NIFS.index(f.lower())):
        heads.append(static_meshes(HD + part)); hglow.append(part_glows(HD + part))
        if not hglow[-1]: print('glow: %s has no glow map of its own - not baked through it (b75)' % part, file=sys.stderr)
# b71: the PC's glow maps (gvmlib.emissive_image), baked from every mesh that uses the texture (all body LODs, the head parts on
# it whose own material has the glow map: b75)
users = lambda tp: (cands if tp == BODY else []) + [m for parts, g in zip(heads, hglow) if g for m in parts if texmap(m['tex']) == tp]
glow = lambda tp: (lambda W, H: emissive_image(tp, users(tp), W, H))
for k, m in enumerate(lods):
    if any(m is x['src'] for x in b.meshes if 'src' in x):   # (the same mesh for two LODs: one mesh, both LOD bits)
        [x for x in b.meshes if x.get('src') is m][0]['blend'] |= (1 << k) << 4; continue
    alias = {n: 'Bip01 Head' for n in m['bone_names'] if n.replace(' ', '_') not in RIG['names']}
    if alias: print('LOD', k, 'aliased bones', alias)
    b.add_skinned(m, BODY, 1024, alias, emis=glow(BODY), alias_s2b=True)   # b62: 1024 (was 512: the 2048 x 1024 bodies were 512 x 256; +3.75 MB for all 12, shared by the 3 LODs)
    b.meshes[-1]['blend'] = (1 << k) << 4; b.meshes[-1]['src'] = m
for parts in heads:   # b71: MESH_UNMASK - hidden under the enemy mask (make_masks.py; engine bots.h)
    b.add_rigid(parts, Wb[bnode['Head_Attach']], 'Bip01_Head', np.linalg.inv(Wb[bnode['Bip01 Head']]), D, texmap=texmap,
                emis_for=lambda m, tp: glow(tp), flags=MESH_UNMASK)
_done = []   # b116: the LODs' small companion meshes, after the head parts (the mesh indices of b71-b115 models stay)
for k, m in enumerate(lods):
    if any(m is y for y in _done): continue
    _done.append(m); add_companions(b, m, sum((1 << j) << 4 for j, y in enumerate(lods) if y is m))
if 'Head_Attach' in bnode:   # b71: where the enemy mask sits (make_masks.py: its vertices leave out this offset)
    b.att['mask'] = (np.linalg.inv(Wb[bnode['Bip01 Head']]) @ Wb[bnode['Head_Attach']])[:3, 3]
# the rifle idle (menu preview / range fallback)
node = N.find(dict((s[0], s[1]) for s in SLOTS)['idle'])[0]
fr, rig, h = decode(N, A, N.rec(node, 0), 0)
b.bake('idle', fr, 1)
assert len(b.meshes) <= 8, 'chr.h / model.h handle up to 8 meshes'
OUTF = OUT + ('/chr_%s.gvm' % CH.lower() if SKIN.lower() == 'a' else '/chr_%s_%s.gvm' % (CH.lower(), SKIN.lower()))
# b76 (user: the operatives' fidelity / polygon count when they are turned and looked at): the menus' operator (main.c
# chr_preview) - the TPS body's top LOD (12-15 k vertices; the game's LOD 0 ~6 k) with the same head parts and NO textures:
# the menu borrows the loaded in-game model's (this builder shares its texture list, so the indices are the same; the glow
# flags too). chr_<op>_hi.gvm for the _A folder, chr_<op>_<folder>_hi.gvm otherwise; written by the folder that owns the
# meshes (a texture-only skin shares its mesh folder's); "hi <stem>" for make_skins.py's chr_hi.txt.
_suffix = lambda f: f.rstrip('/').split('/')[-1][len(CHF) + 1:].lower()
OWNER = SK if (D == SK or HD == SK) else D
HI = 'chr_%s_hi' % CH.lower() if _suffix(OWNER) == 'a' else 'chr_%s_%s_hi' % (CH.lower(), _suffix(OWNER))
def _hi_build(top):
    b2 = Builder(RIG); b2.spec = True; b2.TEX = b.TEX; b2.texlist = b.texlist; b2.EMIS = b.EMIS
    alias = {n: 'Bip01 Head' for n in top['bone_names'] if n.replace(' ', '_') not in RIG['names']}
    b2.add_skinned(top, BODY, 1024, alias, emis=glow(BODY), alias_s2b=True)
    for parts in heads:
        b2.add_rigid(parts, Wb[bnode['Head_Attach']], 'Bip01_Head', np.linalg.inv(Wb[bnode['Bip01 Head']]), D, texmap=texmap,
                     emis_for=lambda m, tp: glow(tp), flags=MESH_UNMASK)
    add_companions(b2, top, 0, only_known=True)
    assert len(b2.palette) <= 48 and len(b2.meshes) <= 8, '%d palette entries, %d meshes' % (len(b2.palette), len(b2.meshes))
    assert len(b2.texlist) == len(b.texlist), 'the menu model must not add textures'
    return b2
if OWNER == SK:   # (the largest body LOD that builds - Borma_ordinary's top one has palette references past its bone list - and is
    hi_ok = False  # heavier than the game's LOD 0; never in the way of the game's model)
    for top in sorted(cands, key=lambda x: -len(x['P'])):
        if len(top['P']) <= len(lods[0]['P']): break
        try: b2 = _hi_build(top)
        except Exception as e: print('hi: %s: a %d-vertex body LOD does not build (%s)' % (HI, len(top['P']), str(e)[:80]), file=sys.stderr); continue
        keep = b2.texlist; b2.texlist = []; nhi = b2.write(OUT + '/' + HI + '.gvm'); b2.texlist = keep
        print('hi', HI, nhi, 'bytes;', [(len(x['P']), len(x['I']) // 3) for x in b2.meshes]); hi_ok = True; break
    if not hi_ok: print('hi: %s not made (no body LOD heavier than the game\'s builds)' % HI, file=sys.stderr)
else: print('hi', HI, '(the mesh folder\'s)')
print('chr', CH, SKIN, '(meshes of %s, head parts of %s)' % (D.rstrip('/').split('/')[-1], HD.rstrip('/').split('/')[-1]), b.write(OUTF), 'bytes; bones', len(b.bones), 'palette', len(b.palette),
      'meshes', [(len(x['P']), len(x['I']) // 3, hex(x['blend'])) for x in b.meshes], 'glow textures', sorted(b.EMIS))
