# b71: mask_m.gvm / mask_f.gvm - the enemy team's masks (the PC's ActorGhost\EnemyMask: EnemyMaleMask.nif, EnemyFemaleMask.nif,
# static meshes hung on each operator's Head_Attach node, as its Hair.nif). In the PC footage and screenshots the other
# team's operators wear them: a black helmet, its visor strips glowing red (the female mask's glow map Enemymask_Female_E;
# the male mask has none - its red strips are painted in its _D map: gvmlib.emissive_image synth_red).
# One copy for every operator (engine bots.h draws it on the head bone): the vertices are in the head bone's own space minus
# the operator's Head_Attach offset - the rotation from the head bone to Head_Attach is the same for every operator of a
# skeleton (male 12 deg, female 8.7 deg), the offset is not (7-12 cm: make_chr.py stores it as attachment 'mask').
# Usage: python3 make_masks.py [outdir]
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gvmlib import *
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'anim'))
from nmb import Network
import gitspaths
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DATA')
EM = gitspaths.path('ANIMOUT', 'model', 'ActorGhost', 'EnemyMask', '')
AG = gitspaths.path('ANIMOUT', 'model', 'ActorGhost', '')
# the operator whose skeleton gives each mask's rotation (every operator of the same skeleton has the same one)
REF = dict(m=('Bato', 'Bato_A', 'Bato_A_TPS.nif', 'GIS_base_3rd'), f=('Motoko', 'Motoko_A', 'Motoko_A_TPS.nif', 'GIS_female_3rd'))
NIF = dict(m='EnemyMaleMask.nif', f='EnemyFemaleMask.nif')

def head_attach(tps):
    """(R 3x3, t 3): Head_Attach in the head bone's space"""
    _, Wb, nb_, _ = skinrun(tps); bn = {v: k for k, v in nb_.items()}
    X = np.linalg.inv(Wb[bn['Bip01 Head']]) @ Wb[bn['Head_Attach']]
    return X[:3, :3], X[:3, 3]

def build(g):
    op, fold, tps, skel = REF[g]
    R, t = head_attach(AG + op + '/' + fold + '/' + tps)
    N = Network(gitspaths.path('ANIMBIN', skel + '_runtimeBinary', skel + '.nmb')); RIG = N.rigs[0]
    b = Builder(RIG); b.spec = True
    X = np.eye(4); X[:3, :3] = R   # (no offset: the engine adds the operator's own)
    parts = static_meshes(EM + NIF[g])
    b.add_rigid(parts, X, 'Bip01_Head', np.eye(4), EM, texmap=lambda tx: EM + tx,
                emis_for=lambda m, tp: (lambda W, H: emissive_image(tp, [m], W, H, synth_red=True)))
    out = OUT + '/mask_%s.gvm' % g
    print('mask', g, b.write(out), 'bytes; meshes', [(len(x['P']), len(x['I']) // 3, hex(x['blend'])) for x in b.meshes], 'textures', [(w, h, f) for w, h, f, _ in b.texlist], 'glow', sorted(b.EMIS))

for g in (sys.argv[2].split(',') if len(sys.argv) > 2 else ('m', 'f')): build(g)
