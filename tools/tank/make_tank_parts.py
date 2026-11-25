# b74 (the user sent the Fandom "Tachikoma" page and two PC screenshots of "My Think Tank / CUSTOMIZE THINK TANK"): the
# Think Tank's swappable parts, as separate models on the tank's own bones, and their stats.
# The game's data: model\Npc\TACHIKOMAPARTS\ - Tachikoma_basebody.nif (the frame and the upper legs: skinned on Bip01 body +
# the four leg01 bones; its attach nodes Leg_F_L / Leg_B_L / Leg_F_R / Leg_B_R on the leg02 bones, Cabin and Lower_core on
# the body), Tachikoma_basebody_upper.nif (the turret frame: Upper_core on Lower_core, Hull on Head_yaw, MainArm_L / _R on
# the arm02 bones, SubArm on the weapon bone), and one folder per slot with rigid meshes in their attach node's space:
# Hull\, Cabin\ (its own Support_L / _R nodes carry the Cabin_Support parts), Leg\ (_L / _R), MainArm\<x>\3rd\ (_L / _R),
# SubArm\<x>\3rd\, Cabin_Support\ (_L / _R). The basebody's frame is Tachikoma_G.nif's (tk_blue.gvm's) moved 63.6 cm along
# -y; placed so, the default parts land within ~3-6 cm (median) of Tachikoma_G's surface (the assembled default tank).
# Each part rides one bone, as in the game (rigid on its attach node): legs on their leg02 bone, the cabin and its supports
# on Bip01 tail (Tachikoma_G skins the pod to the tail: 1916 of 1990 nearest vertices), the hull, the arms, the rocket
# launcher and the turret frame on Bip01 body (this rig has no turret / arm bones; tk_blue rides them on the body too).
# The parts offered are the ones the Fandom page lists with their stats (the 001A and 004A sets; the support's heavy part
# is SMOKE-005A in the game's screenshot, the page's SMOKE-004A): the folders by their texture number (Hull_01 / Hull_04,
# cabin_04, Leg_04, MainArm_04, SubArm_04, Cabin_Support_05). Heavy meshes are simplified (welded first, as the dummy).
# Out: tkp_core.gvm, tkp_<slot>_<k>.gvm (hull, cabin, prop, main, sub), tkp_supp_<k>_c<cabin>.gvm (the supports sit on the
# cabin's own nodes), tk_parts.txt (thinktank.h / menu_tk.h read it). Every model has the same 22 bones in the same order as
# tk_blue.gvm (FIXED in make_tank.py) and no clips: the engine poses them with tk_blue's clips.
# usage: python3 make_tank_parts.py [outdir]
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'skills'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'vm')); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
import gitspaths
from gvmlib import *
import sklib, meshopt_c as meshoptimizer
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DATA')
SRC = gitspaths.dirp('ANIMOUT', 'model', 'Npc', 'Tachikoma'); PT = gitspaths.dirp('ANIMOUT', 'model', 'Npc', 'TACHIKOMAPARTS')
A = gitspaths.dirp('ANIMBIN', 'GIS_Tachikoma_runtimeBinary'); NMB = A + 'GIS_tachikoma.nmb'
RIG = add_parents(NMB, rigs(NMB))[0]
FIXED = ['Bip01', 'Bip01_body', 'Bip01_L_back_leg01', 'Bip01_L_back_leg02', 'Bip01_L_back_foot', 'Bip01_L_front_leg01', 'Bip01_L_front_leg02',
         'Bip01_L_front_foot', 'Bip01_R_back_leg01', 'Bip01_R_back_leg02', 'Bip01_R_back_foot', 'Bip01_R_front_leg01', 'Bip01_R_front_leg02',
         'Bip01_R_front_foot', 'Bip01_tail', 'Bip01_bottom_eyeball', 'Bip01_R_wire', 'Bip01_L_wire', 'sidedoor_Bone01', 'sidedoor_Bone02',
         'Updoor_Bone01', 'eye01']
WG, _, _ = sklib.node_tree(SRC + 'Tachikoma_G.nif')            # the bone frames of tk_blue.gvm (the palettes' S2B)
WB, _, _ = sklib.node_tree(PT + 'Tachikoma_basebody.nif'); WU, _, _ = sklib.node_tree(PT + 'Tachikoma_basebody_upper.nif')
SH = np.eye(4); SH[1, 3] = 63.6                                    # basebody frame -> Tachikoma_G's
LC = WB['Lower_core']
TEXMAX = 512

def find_tex(folder, name):
    """the part's texture by name, in its folder (any case, any depth; its own Tex\ first - D_CAMO\ / D_YELLOW\ hold paint
    variants under the same names), else the parts root"""
    want = os.path.basename(name.replace('\\', '/')).lower(); hits = []
    for root in (PT + folder, PT):
        for dp, _, fs in os.walk(root):
            for f in fs:
                if f.lower() == want: hits.append(os.path.join(dp, f))
        if hits: break
    if not hits: raise FileNotFoundError(name)
    return sorted(hits, key=lambda h: (os.sep + 'D_' in h, os.sep + 'Tex' + os.sep not in h, len(h)))[0]

def merged(parts):
    """the NIF's meshes merged by texture (one draw per texture)"""
    by = {}
    for m in parts: by.setdefault(m['tex'].lower(), []).append(m)
    out = []
    for k, ms in by.items():
        o = 0; P = []; N = []; UV = []; I = []
        for m in ms: P.append(m['P']); N.append(m['N']); UV.append(m['UV']); I.append(m['I'] + o); o += len(m['P'])
        out.append(dict(P=np.concatenate(P), N=np.concatenate(N), UV=np.concatenate(UV), I=np.concatenate(I), tex=ms[0]['tex']))
    return out

def simplify(m, tris, what):
    """welded (same position + UV), then meshopt to `tris` triangles; normals re-summed over the welded copies"""
    I = m['I'].astype(np.int64)
    if len(I) // 3 <= tris: return m
    key = np.hstack([np.round(m['P'], 3), np.round(m['UV'], 5)])
    _, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True); inv = inv.ravel()
    N = np.zeros((len(first), 3)); np.add.at(N, inv, m['N']); N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9
    P = np.ascontiguousarray(m['P'][first], np.float32); UV = m['UV'][first]; I = inv[I].astype(np.uint32)
    dst = np.zeros(len(I), np.uint32)
    n = meshoptimizer.simplify(dst, I, P, target_index_count=tris * 3, target_error=0.05)
    I2 = dst[:n].astype(np.int64); used = np.unique(I2); remap = np.full(len(P), -1, np.int64); remap[used] = np.arange(len(used))
    print('  %s: %d -> %d triangles' % (what, len(m['I']) // 3, n // 3))
    return dict(P=P[used].astype(np.float64), N=N[used], UV=UV[used], I=remap[I2], tex=m['tex'])

def builder():
    b = Builder(RIG); b.spec = True
    for nm in FIXED: b.bone(nm)
    return b

def add_part(b, folder, nif, Watt, bone, tris):
    """a rigid part: the NIF's meshes (in their attach node's space) placed by Watt (basebody frame), riding `bone`"""
    X = SH @ Watt
    for m in merged(static_meshes(PT + folder + '/' + nif)):
        m = simplify(m, tris, nif) if tris else m
        tp = find_tex(folder, m['tex'])
        b.add_rigid([m], X, bone, np.linalg.inv(WG[bone.replace('_', ' ') if bone.replace('_', ' ') in WG else bone]), '', texmap=lambda t, tp=tp: tp, maxs=TEXMAX)

def finish(b, name):
    assert b.bones == [RIG['names'].index(n) for n in FIXED], 'bone list changed'
    sz = b.write(os.path.join(OUT, name))
    print('%s %d bytes: %d meshes, %d verts, %d tris, textures %s' % (name, sz, len(b.meshes), sum(len(x['P']) for x in b.meshes),
          sum(len(x['I']) // 3 for x in b.meshes), [(w, h) for w, h, f, _ in b.texlist]))
    return sum(len(x['I']) // 3 for x in b.meshes)

def core():
    """the frame + upper legs (skinned: body and the 4 leg01 bones) + the turret frame (rigid on the body)"""
    b = builder()
    ms, _, _, _ = skinrun(PT + 'Tachikoma_basebody.nif')
    b.add_skinned(ms[0], PT + 'Tachikoma_Body_D.dds', TEXMAX)
    for m in skinrun(PT + 'Tachikoma_basebody_upper.nif')[0]:
        k = 0; Wn = m['bone_world'][k] if m.get('bone_world') is not None else WU[m['bone_names'][k]]
        Xw = SH @ LC @ Wn @ m['sm']['s2b'][k]                      # skin space -> G frame (the turret frame sits on Lower_core)
        P = (np.c_[m['P'], np.ones(len(m['P']))] @ Xw.T)[:, :3]
        tri = m['I'].reshape(-1, 3); Nn = np.zeros_like(P); fn = np.cross(P[tri[:, 1]] - P[tri[:, 0]], P[tri[:, 2]] - P[tri[:, 0]])
        for c in range(3): np.add.at(Nn, tri[:, c], fn)
        Nn /= np.linalg.norm(Nn, axis=1, keepdims=True) + 1e-9
        part = dict(P=P, N=Nn, UV=m['UV'], I=m['I'], tex='Tachikoma_Body_D.dds')
        b.add_rigid([part], np.eye(4), 'Bip01_body', np.linalg.inv(WG['Bip01 body']), '', texmap=lambda t: PT + 'Tachikoma_Body_D.dds', maxs=TEXMAX)
    return finish(b, 'tkp_core.gvm')

# (slot, file stem, [(code, folder, nif pattern, stats...)]); stats: weight, hp, loadage, speed, accel, dps, rocket damage, spread
PARTS = {
    'hull': [('HL-001A', 'Hull/DEFAULT', 'DEFAULT.nif', 300, 1000, 0, 0, 0, 0, 0, 0), ('HL-004A', 'Hull/R-TYPE', 'R-TYPE.nif', 500, 1600, 0, 0, 0, 0, 0, 0)],
    'cabin': [('CN-001A', 'Cabin/DEFAULT', 'DEFAULT.nif', 300, 1000, 0, 0, 0, 0, 0, 0), ('CN-004A', 'Cabin/UNION-77', 'UNION-77.nif', 500, 1600, 0, 0, 0, 0, 0, 0)],
    'prop': [('PN-001A', 'Leg/DEFAULT', 'DEFAULT_%s.nif', 300, 1000, 1300, 700, 0.8, 0, 0, 0), ('PN-004A', 'Leg/QUARTER-BACK', 'QUARTER-BACK_%s.nif', 500, 1600, 3000, 650, 1.1, 0, 0, 0)],
    'main': [('AR-001A', 'MainArm/DEFAULT/3rd', 'DEFAULT_%s.nif', 200, 0, 0, 0, 0, 105, 0, 0), ('AR-004A', 'MainArm/MINI-ASSAULT/3rd', 'MINI-ASSAULT_%s.nif', 500, 0, 0, 0, 0, 136, 0, 0)],
    'sub': [('ROCKET-001A', 'SubArm/DEFAULT/3rd', 'DEFAULT.nif', 100, 0, 0, 0, 0, 0, 200, 2.0), ('ROCKET-004A', 'SubArm/Z-ROCKET/3rd', 'Z-ROCKET.nif', 250, 0, 0, 0, 0, 0, 250, 1.7)],
    'supp': [('SMOKE-001A', 'Cabin_Support/DEFAULT', 'DEFAULT_%s.nif', 100, 0, 0, 0, 0, 0, 0, 0), ('SMOKE-005A', 'Cabin_Support/MMM_3', 'MMM_3_%s.nif', 300, 0, 0, 0, 0, 0, 0, 0)],
}
LEGS = [('Leg_F_L', 'L', 'Bip01_L_front_leg02'), ('Leg_B_L', 'L', 'Bip01_L_back_leg02'), ('Leg_F_R', 'R', 'Bip01_R_front_leg02'), ('Leg_B_R', 'R', 'Bip01_R_back_leg02')]
SIMPLE = {'hull': 3000, 'cabin': 3000, 'prop': 1300, 'main': 0, 'sub': 0, 'supp': 1600}   # triangle caps per mesh (0: as is)

def build_slot(slot, k, cabin=None):
    code, folder, nif, *_ = PARTS[slot][k]; b = builder(); cap = SIMPLE[slot]
    if slot == 'hull': add_part(b, folder, nif, LC @ WU['Hull'], 'Bip01_body', cap)
    elif slot == 'cabin': add_part(b, folder, nif, WB['Cabin'], 'Bip01_tail', cap)
    elif slot == 'prop':
        for node, side, bone in LEGS: add_part(b, folder, nif % side, WB[node], bone, cap)
    elif slot == 'main':
        for side in 'LR': add_part(b, folder, nif % side, LC @ WU['MainArm_' + side], 'Bip01_body', cap)
    elif slot == 'sub': add_part(b, folder, nif, LC @ WU['SubArm'], 'Bip01_body', cap)
    elif slot == 'supp':
        cf, cn = PARTS['cabin'][cabin][1], PARTS['cabin'][cabin][2]; WC, _, _ = sklib.node_tree(PT + cf + '/' + cn)
        for side in 'LR': add_part(b, folder, nif % side, WB['Cabin'] @ WC['Support_' + side], 'Bip01_tail', cap)
    return finish(b, 'tkp_%s_%d.gvm' % (slot, k) if slot != 'supp' else 'tkp_supp_%d_c%d.gvm' % (k, cabin))

if __name__ == '__main__':
    tris = {'core': core()}
    for slot in ('hull', 'cabin', 'prop', 'main', 'sub'):
        for k in range(len(PARTS[slot])): tris['%s%d' % (slot, k)] = build_slot(slot, k)
    for k in range(2):
        for c in range(2): tris['supp%d_c%d' % (k, c)] = build_slot('supp', k, c)
    with open(os.path.join(OUT, 'tk_parts.txt'), 'w') as f:
        f.write('# b74: Think Tank parts (tools/tank/make_tank_parts.py; stats: the Fandom "Tachikoma" page, the PC screenshots)\n')
        f.write('# slot k code weight hp loadage speed accel dps rocket_dmg rocket_spread tris\n')
        for slot in ('prop', 'hull', 'cabin', 'main', 'sub', 'supp'):
            for k, (code, folder, nif, w, hp, ld, sp, ac, dps, rd, rs) in enumerate(PARTS[slot]):
                t = tris.get('%s%d' % (slot, k)) or tris.get('%s%d_c0' % (slot, k))
                f.write('%s %d %s %d %d %d %d %.2f %d %d %.2f %d\n' % (slot, k, code, w, hp, ld, sp, ac, dps, rd, rs, t))
    print('triangles', tris)
