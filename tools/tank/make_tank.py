# Think Tank (Tachikoma) models for the Vita engine: tk_blue.gvm (Tachikoma_G.nif, the blue team-A Think Tank) and
# tk_red.gvm (Tachikoma_K.nif, the black red-eyed team-B one), skinned on the game's own Tachikoma rig (GIS_tachikoma.nmb)
# with every tachikoma_* clip baked from the morpheme binaries by our own decoder (/root/anim/py: qsa.py, anim.py, rig.py).
# Conventions as the characters (vm/make_chr.py): cm, z up, the model faces -y, poses in model space.
# Both files list the SAME bones in the same order (FIXED below), so the engine shares one clip set between them
# (tk_red.gvm is written with its clips too, but thinktank.h only keeps the first model's).
# The rig has no arm / finger / gun / eyeball bones: those NIF bones ride on Bip01_body (their bind offset is folded
# into the skin->bone matrix), exactly as the original could not animate them either.
# usage: python3 make_tank.py [outdir] [--preview]
import shutil, sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'skills')); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'vm')); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
from gvmlib import *
import sklib
from PIL import Image
OUT = next((a for a in sys.argv[1:] if not a.startswith('--')), None) or gitspaths.root('DATA')
PREV = gitspaths.dirp('TMP', 'tank'); os.makedirs(PREV, exist_ok=True)
SRC = gitspaths.dirp('ANIMOUT', 'model', 'Npc', 'Tachikoma')
A = gitspaths.dirp('ANIMBIN', 'GIS_Tachikoma_runtimeBinary'); NMB = A + 'GIS_tachikoma.nmb'
R = add_parents(NMB, rigs(NMB)); RIG = R[0]; MAPS = find_maps(NMB)
FIXED = ['Bip01', 'Bip01_body', 'Bip01_L_back_leg01', 'Bip01_L_back_leg02', 'Bip01_L_back_foot', 'Bip01_L_front_leg01', 'Bip01_L_front_leg02',
         'Bip01_L_front_foot', 'Bip01_R_back_leg01', 'Bip01_R_back_leg02', 'Bip01_R_back_foot', 'Bip01_R_front_leg01', 'Bip01_R_front_leg02',
         'Bip01_R_front_foot', 'Bip01_tail', 'Bip01_bottom_eyeball', 'Bip01_R_wire', 'Bip01_L_wire', 'sidedoor_Bone01', 'sidedoor_Bone02',
         'Updoor_Bone01', 'eye01']
# (engine name, clip file prefix, loop). Names <= 15 chars.
CLIPS = [('idle', 'tachikoma_idle_tachikoma_idle', 1), ('idle2', 'tachikoma_idle2_', 1),
         ('walk_f', 'tachikoma_walk_forward_', 1), ('walk_b', 'tachikoma_walk_backward_', 1), ('walk_l', 'tachikoma_walk_left_', 1),
         ('walk_r', 'tachikoma_walk_right_', 1), ('walk_stop', 'tachikoma_walk_stop_', 0),
         ('run', 'tachikoma_run_forward_', 1), ('run_start', 'tachikoma_run_start_', 0), ('run_stop', 'tachikoma_run_stop_', 0),
         ('turn_l', 'tachikoma_turn_left_tachikoma', 1), ('turn_r', 'tachikoma_turn_right_tachikom', 1),
         ('turn_lm', 'tachikoma_turn_left_move_', 1), ('turn_rm', 'tachikoma_turn_right_move_', 1),
         ('fire_start', 'tachikoma_fire_start_', 0), ('fire_loop', 'tachikoma_fire_loop_', 1), ('fire_end', 'tachikoma_fire_end_', 0),
         ('fire', 'tachikoma_fire_tachikoma_fire', 0), ('fire2', 'tachikoma_fire2_', 0),
         ('chk', 'tachikoma_fire_check_tachikom', 0), ('chk_loop', 'tachikoma_fire_check_loop_', 1),
         ('gren_start', 'tachikoma_grenade_start_', 0), ('gren_loop', 'tachikoma_grenade_loop_', 1), ('gren', 'tachikoma_grenade_launcher_', 0),
         ('dmg', 'tachikoma_damage_body_', 0), ('dmg_crit', 'tachikoma_damage_critical_', 0), ('destroy', 'tachikoma_destroy_', 0),
         ('stun_start', 'tachikoma_stun_start_', 0), ('stun_loop', 'tachikoma_stun_loop_', 1), ('stun_end', 'tachikoma_stun_end_', 0),
         ('stun_dmg1', 'tachikoma_stun_damage1_', 0), ('stun_dmg2', 'tachikoma_stun_damage2_', 0),
         ('jump', 'tachikoma_jump_up_', 0), ('fall', 'tachikoma_jump_fall_', 1), ('land', 'tachikoma_jump_land_', 0),
         ('dance', 'tachikoma_Dance_', 1), ('suicide', 'tachikoma_suicide_', 0), ('og_idle_a', 'OutGame_Tachikoma_IdleA_', 1), ('og_idle_b', 'OutGame_Tachikoma_IdleB_', 1)]
report = []

def build(nif, tex_png, lod_verts, out_name, keep_clips=True, emis=None, maxs=512):
    W, par, h = sklib.node_tree(SRC + nif)
    meshes, Wb, names, _ = skinrun(SRC + nif)
    m = sorted([x for x in meshes if len(x['bone_names']) >= 20], key=lambda x: abs(len(x['P']) - lod_verts))[0]
    b = Builder(RIG); b.spec = True   # (b66: specular masks)
    for nm in FIXED: b.bone(nm)
    # aliases: NIF bones without a rig bone ride on their nearest rig ancestor; s2b' = inv(W_anc) @ W_b @ s2b
    rigset = set(RIG['names']); alias = {}; m = dict(m); m['sm'] = dict(m['sm']); s2b = list(m['sm']['s2b'])
    for k, bn in enumerate(m['bone_names']):
        if bn.replace(' ', '_') in rigset: continue
        a = bn
        while a is not None and a.replace(' ', '_') not in rigset: a = par.get(a)
        assert a is not None, bn
        alias[bn] = a; s2b[k] = np.linalg.inv(W[a]) @ W[bn] @ s2b[k]
    m['sm']['s2b'] = s2b
    b.add_skinned(m, tex_png, maxs, alias, emis=emis)   # (b71: the glow, 1024 colour maps)
    nb_before = len(b.bones); assert b.bones == [RIG['names'].index(n) for n in FIXED], 'bone list changed'
    if keep_clips:
        for nm, pfx, loop in CLIPS:
            fr, hd = clip_frames(A, pfx, RIG, MAPS)
            b.bake(nm, fr, loop)
            report.append('  clip %-10s %3d frames %.2f s mode %d' % (nm, len(fr), (len(fr) - 1) / 30.0, hd['a28']))
    assert len(b.bones) == nb_before
    # attachment points in their bone's own frame (thinktank.h: model_bone_xform(&pose[bone], att)): bone in the name's prefix
    def att(node, bone):
        Wbone = W[bone]; return (np.linalg.inv(Wbone) @ np.r_[W[node][:3, 3], 1])[:3]
    b.att = {'muzzle': att('Tachikoma_Fire', 'Bip01 body'), 'gren': att('Tachikoma_grenade', 'Bip01 body'),
             'eye': att('Bip01 front eyeball', 'Bip01 body'), 'tag': att('name_tag', 'Bip01'), 'smoke': att('smoke01', 'Bip01 body'),
             'spark1': att('spark01', 'Bip01 body'), 'spark2': att('spark02', 'Bip01 body'), 'pod': att('Bip01 bottom eyeball', 'Bip01 tail')}
    sz = b.write(os.path.join(OUT, out_name))
    nv = sum(len(x['P']) for x in b.meshes); nt = sum(len(x['I']) // 3 for x in b.meshes)
    report.append('%s %d bytes: %d bones, palette %d, %d verts, %d tris, %d clips, aliases %s' % (out_name, sz, len(b.bones), len(b.palette), nv, nt, len(b.clips), sorted(set(alias.values()))))
    return os.path.join(OUT, out_name)

def tex(name, size):
    im = Image.open(SRC + name).convert('RGB').convert('RGBA'); w, h = im.size; sc = size / max(w, h)
    p = PREV + os.path.splitext(name)[0] + '.png'; im.resize((int(w * sc), int(h * sc)), Image.LANCZOS).save(p)
    # b66: the game's specular map next to it (Tachi_S / tachi_K_S), so Builder.spec (gvmlib) finds it: the glossy paint's
    # highlight and reflection (the PC footage: bright sheen on the pods and legs)
    st = os.path.splitext(name)[0]; sp = SRC + st[:-2] + '_S.dds'
    if st[-2:].lower() == '_d' and os.path.exists(sp): shutil.copyfile(sp, PREV + st[:-2] + '_S.dds')
    return p

# b71 (the user: "make sure the think tanks/tachikoma look and function the best they can"): the colour maps at the PC's own size
# (Tachi_D 1024 x 1024, tachi_K_D 1024 x 512; were 512) and the glow: the blue Think Tank's own glow map (Tachi_g.dds, the NIF's
# glow slot on uv set 0: its eye and lights), the black one's red eyes and lights painted in its colour map (it has no glow map:
# gvmlib.emissive_image synth_red, as the male enemy mask)
def glow_map(name):
    def f(W, H):
        E = np.asarray(Image.open(SRC + name).convert('RGB').resize((W, H), Image.BILINEAR)).astype(np.float64) / 255
        print('glow: %s, %.1f %% of the texels lit' % (name, 100 * (E.max(2) > 0.05).mean()), file=sys.stderr); return E if E.max() > 0.02 else None
    return f
def glow_red(png):
    return lambda W, H: emissive_image(png, [], W, H, synth_red=True)
if __name__ == '__main__':
    pb = build('Tachikoma_G.nif', tex('Tachi_D.dds', 1024), 5644, 'tk_blue.gvm', emis=glow_map('Tachi_g.dds'), maxs=1024)
    pr = build('Tachikoma_K.nif', tex('tachi_K_D.dds', 1024), 4822, 'tk_red.gvm', keep_clips=False, emis=glow_red(PREV + 'tachi_K_D.png'), maxs=1024)
    print('\n'.join(report))
    if '--preview' in sys.argv:
        import preview_gvm, preview as PV
        for p, tag in ((pb, 'blue'), (pr, 'red')):
            G = PV.read(p)
            for yaw in (30, 200):
                preview_gvm.render(G, PREV + 'prev_%s_%d.png' % (tag, yaw), yaw, 15, 650, 'rest', 0, 480, 360, target=(0, 0, 90))
