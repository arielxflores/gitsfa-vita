# Demolition models -> GVM1 (engine/src/model.h) static models: one bone "root", one palette entry (identity), one
# 2-frame identity clip "idle", so model_load / the normal skinned draw path works unchanged (pose = identity).
#   dem_bomb.gvm     planted bomb (worldEnv/EnvBomb/bomb_box.nif): the TATP-2M device standing on its long edge, origin at
#                    the bottom centre, long axis x (53 cm), thin axis y (12 cm, the face the player looks at is -y),
#                    height z (30 cm). mesh 0 = device (opaque), mesh 1 = scan shell dm_upline_R (additive, blend 2:
#                    draw it while armed, or skip it with md_mesh1 = 1). attachment "flare" = blinking light position.
#   dem_bomb_1p.gvm  first-person device held while planting/defusing (model/Objects/DM/bomb_1p.nif), in the NIF's own
#                    Bomb_Root space (cm). The game animates it with the arms (base_M4A1_1st_install_start/_end); without
#                    those clips draw it as a static camera-space prop (see the note in the report for a placement).
# usage: make_dem_models.py [rawdir] [outdir]
import sys, os, numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'vm')); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
import gvmlib
RAW = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DEMX')
OUT = sys.argv[2] if len(sys.argv) > 2 else gitspaths.root('DATA')
TEXMAX = 256
ENV = RAW + '/worldEnv/EnvBomb/'

def build(parts, out, att=None):
    B = gvmlib.Builder({'names': ['root']})
    for m, tex, blend in parts:
        B.add_rigid([m], np.eye(4), 'root', np.eye(4), '', blend_for=lambda t, b=blend: b, maxs=TEXMAX, texmap=lambda t, p=tex: p)
    B.bake('idle', [(np.zeros((1, 3)), [np.array([0, 0, 0, 1.0])])] * 2, 1)   # W = bone quaternions (x,y,z,w)
    B.att = att or {}
    n = B.write(out)
    P = np.concatenate([m['P'] for m, _, _ in parts])
    print(os.path.basename(out), n, 'bytes, meshes', len(parts), 'verts', len(P), 'tris', sum(len(m['I']) // 3 for m, _, _ in parts), 'bbox', P.min(0).round(1), P.max(0).round(1))

if __name__ == '__main__':
    # planted bomb
    ms = gvmlib.static_meshes(RAW + '/worldEnv/EnvBomb/bomb_box.nif')
    dev = [m for m in ms if m['tex'] and m['tex'].lower() == 'bomb_01_d.dds'][0]
    shell = [m for m in ms if m['tex'] and m['tex'].lower() == 'dm_upline_r.dds'][0]
    flare = [m for m in ms if m['name'] == 'Plane'][0]
    for m in (dev, shell):
        if m['UV'] is None: m['UV'] = np.zeros((len(m['P']), 2), np.float32)
    build([(dev, ENV + 'bomb_01_D.dds', 0), (shell, ENV + 'dm_upline_R.DDS', 2)], OUT + '/dem_bomb.gvm',
          {'flare': flare['P'].mean(0).tolist(), 'top': [0.0, 0.0, float(dev['P'][:, 2].max())]})
    # first-person device (both textured meshes)
    ms = gvmlib.static_meshes(RAW + '/model/Objects/DM/bomb_1p.nif')
    parts = [(m, RAW + '/model/Objects/DM/bomb_01_D.dds', 0) for m in ms if m['tex'] and m['UV'] is not None]
    build(parts, OUT + '/dem_bomb_1p.gvm')
