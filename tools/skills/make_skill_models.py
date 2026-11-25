# Builds the skill models as GVM1 files (engine/src/model.h) from the game's own NIFs (model.dat / weapon.dat / effect.dat):
#   sk_turret.gvm    Ishikawa's sentry gun (TURRET.nif): rigid parts base / yaw / pitch, the tier-3 grenade pod as the LAST mesh
#   sk_drone.gvm     Sitara's DMD drone (DMD_DRONE.nif), one rigid part (+ spinning rotors are part of the hull)
#   sk_emp.gvm       Kurutsu's EMP device (CB_EMP.nif): legs / body / upper body + wings, rigid parts
#   sk_wallpod.gvm   Maven's cyber-wall projector (CBWALL.nif)
#   sk_wall.gvm      the wall itself: unit panel (x -0.5..0.5, z 0..1), hex grid, additive - scale it in the MVP (T1 600 x 250 cm)
#   sk_shield.gvm    Azuma's hand-held plasma shield (azuma_ally_shield_tier2.nif) - additive hex panel
#   sk_dome.gvm      Azuma's tier-3 shield dome (azuma_sp_shield.nif) - additive, radius 100 cm (scale it in the engine)
#   sk_missile.gvm   Batou's arm-launcher missile (armlauncher_missile.nif / missile.nif)
#   sk_heatghost.gvm Saito's heat-scan silhouettes (heat_scan_target_moving / _stop.nif): mesh 0 = moving, mesh 1 = standing
#   sk_minitachi.gvm Togusa's special-recon unit: the mini-Tachikoma (CB_SPIDERMINE/CB_TACHIKOMA_MINI.nif - the same shape
#                    SpecialRecon_ngon_complete.nif builds: its meshes are named TACHIKOMA_S), legs + body rigid parts
# Conventions as the characters: z up, centimetres, the object faces -y. Every mesh is rigid (one bone), see sklib.Rigid.
# usage: python3 make_skill_models.py [outdir]   (default /root/gits/build/data) ; previews -> /tmp/claude-0/skills/prev_*.png
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sklib
from sklib import RAW, Rigid, tex_png, skinned_rest, node_tree, subtree, submesh, vnormals, qaxis, pivot_pose, static_parts
from PIL import Image
import gitspaths
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DATA')
PREV = gitspaths.dirp('TMP', 'skills'); os.makedirs(PREV, exist_ok=True)
N = RAW + 'model/Npc/'
report = []

def save(b, name, prev=((30, 20), (200, 15))):
    path = os.path.join(OUT, name); sz = b.write(path)
    nv = sum(len(m['P']) for m in b.meshes); nt = sum(len(m['I']) // 3 for m in b.meshes)
    report.append('%-16s %8d bytes  bones %-34s meshes %d  verts %d  tris %d  att %s' % (name, sz, ','.join(b.bones), len(b.meshes), nv, nt, ','.join(b.att)))
    try:
        import preview_gvm, preview as PV
        G = PV.read(path)
        for k, (yw, pt) in enumerate(prev): preview_gvm.render(G, PREV + 'prev_%s_%d.png' % (name[:-4], k), yw, pt)
    except Exception as e: print('preview failed', name, e)


# ---------------------------------------------------------------- sentry gun
def turret():
    W, par, h = node_tree(N + 'CB_Turret/TURRET.nif')
    m = skinned_rest(N + 'CB_Turret/TURRET.nif')[0]
    pitch = subtree(par, 'turret_pitch'); yaw = subtree(par, 'turret_yaw') - pitch
    part = np.array([2 if b in pitch else 1 if b in yaw else 0 for b in m['bone']])
    b = Rigid(); t = b.texture(tex_png('CB_TURRET_D'), 512)
    for k, bn in enumerate(('base', 'yaw', 'pitch')):
        sm = submesh(m, part == k); b.mesh(sm['P'], sm['N'], sm['UV'], sm['I'], bn, t)
    # tier-3 grenade pod: static mesh under the Grenade node, rides on the pitch part -> LAST mesh (hide below tier 3)
    tg = b.texture(tex_png('CB_Turret_Grenade_D'), 256)
    for g in static_parts(N + 'CB_Turret/TURRET.nif'):
        if g['tex'] and 'grenade' in g['tex'].lower(): b.mesh(g['P'], g['N'], g['UV'], g['I'], 'pitch', tg)
    b.att = {'yaw': W['turret_yaw'][:3, 3], 'pitch': W['turret_pitch'][:3, 3], 'muzzle': W['Turret_Fire'][:3, 3], 'laser': W['Turret_Laser'][:3, 3],
             'gren': W['Grenade_Fire01'][:3, 3], 'tag': W['name_tag'][:3, 3], 'fx': W['Effect_turret'][:3, 3]}
    # demo clips (the engine drives yaw / pitch itself): scan = +-45 deg sweep, 4 s
    fr = []
    for i in range(121):
        a = np.radians(45) * np.sin(i / 120 * 2 * np.pi)
        fr.append({'yaw': pivot_pose(qaxis((0, 0, 1), a), b.att['yaw']), 'pitch': pivot_pose(qaxis((0, 0, 1), a), b.att['yaw'])})
    b.clip('scan', fr, 1, 30.0)
    save(b, 'sk_turret.gvm', ((30, 20), (150, 25)))

# ---------------------------------------------------------------- DMD drone
def drone():
    W, par, h = node_tree(N + 'CB_Drain_Drone/DMD_DRONE.nif')
    m = skinned_rest(N + 'CB_Drain_Drone/DMD_DRONE.nif')[0]
    b = Rigid(); t = b.texture(tex_png(m['tex'] or 'DMD_DRONE_D'), 512, glow=N + 'CB_Drain_Drone/CB_Drain_Drone_E.dds' if (m['tex'] or '').lower().startswith('cb_drain_drone_d') else None)   # (b71: its glow map)
    b.mesh(m['P'], m['N'], m['UV'], m['I'], 'body', t)
    b.att = {'laser': W['effect'][:3, 3], 'eye1': W['eye01'][:3, 3], 'eye2': W['eye02'][:3, 3]}
    report.append('  drone texture: %s' % m['tex'])
    save(b, 'sk_drone.gvm', ((30, 20), (200, -10)))

# ---------------------------------------------------------------- EMP device
def emp():
    W, par, h = node_tree(N + 'CB_EMP/CB_EMP.nif')
    m = skinned_rest(N + 'CB_EMP/CB_EMP.nif')[0]
    up = subtree(par, 'Bip01_upperbody'); body = subtree(par, 'Bip01_body') - up
    part = np.array([2 if b in up else 1 if b in body else 0 for b in m['bone']])
    b = Rigid(); t = b.texture(tex_png(m['tex'] or 'EMP_D'), 512)
    for k, bn in enumerate(('legs', 'body', 'top')):
        sm = submesh(m, part == k)
        if len(sm['I']): b.mesh(sm['P'], sm['N'], sm['UV'], sm['I'], bn, t)
    b.att = {'body': W['Bip01_body'][:3, 3], 'top': W['Bip01_upperbody'][:3, 3], 'head': W['Bip01_Head'][:3, 3]}
    report.append('  emp texture: %s' % m['tex'])
    # 'spin' clip: the upper body turns (2 turns/s) while the device charges, as in the EMP video
    fr = [{'top': pivot_pose(qaxis((0, 0, 1), 2 * np.pi * i / 15), b.att['top'])} for i in range(16)]
    b.clip('spin', fr, 1, 30.0)
    save(b, 'sk_emp.gvm')

# ---------------------------------------------------------------- cyber wall projector + wall panel
def wall():
    W, par, h = node_tree(N + 'CB_Cbwall/CBWALL.nif')
    m = skinned_rest(N + 'CB_Cbwall/CBWALL.nif')[0]
    b = Rigid(); t = b.texture(tex_png(m['tex'] or 'CW_CBWALL_D'), 256)
    b.mesh(m['P'], m['N'], m['UV'], m['I'], 'pod', t)
    # the wall: 1 x 1 unit panel (x -0.5..0.5, z 0..1) with a hex-grid texture; the engine scales it (T1 600 x 250 cm).
    # Built from the game's own textures: as_dark_ally_tier2 (hex grid) over a vertical cyan gradient, additive.
    hexg = np.asarray(Image.open(tex_png('as_dark_ally_tier2')).convert('L').resize((256, 512)), np.float32) / 255
    hexg = hexg[:256, :]                                                     # square hex tile
    yy = np.linspace(0, 1, 256)[:, None]
    a = np.clip(0.10 + 0.75 * hexg + 0.30 * (1 - yy) ** 3 + 0.5 * np.exp(-((yy - 0.02) / 0.02) ** 2), 0, 1)
    col = np.stack([a * 0.35, a * 0.95, a * 1.0, np.ones_like(a)], 2)
    nx, nz = 8, 4; P = []; UV = []
    for j in range(nz + 1):
        for i in range(nx + 1): P.append((i / nx - 0.5, 0, j / nz)); UV.append((i / nx * 6, 1 - j / nz))   # hex tiles repeat 6x along
    I = []
    for j in range(nz):
        for i in range(nx):
            a0 = j * (nx + 1) + i; I += [a0, a0 + 1, a0 + nx + 2, a0, a0 + nx + 2, a0 + nx + 1]
    I2 = I + [x for k in range(0, len(I), 3) for x in (I[k], I[k + 2], I[k + 1])]   # two-sided
    w = Rigid(); tw2 = w.texture(None, 256, img=Image.fromarray((col * 255).astype(np.uint8), 'RGBA'))
    w.mesh(P, [(0, -1, 0)] * len(P), UV, I2, 'wall', tw2, blend=2)
    save(w, 'sk_wall.gvm', ((20, 10),))
    b.att = {'lens': W['lens_effect'][:3, 3], 'shield': W['shield_position'][:3, 3], 'tag': W['name_tag'][:3, 3]}
    save(b, 'sk_wallpod.gvm', ((30, 20),))

# ---------------------------------------------------------------- Azuma: hand-held shield (tiers 1-2) and dome (tier 3)
def shield():
    sm = static_parts(RAW + 'model/Object/shield/azuma_ally_shield_tier2.nif')
    b = Rigid()
    # the panel = the 40-vertex hexagon meshes: as_solid_tier2 (fill) + as_out_tier2 (hex outline) + upup (rising lines)
    seen = set()
    for g in sm:
        tn = (g['tex'] or '').lower()
        if len(g['P']) != 40 or tn in seen or tn not in ('as_solid_tier2.dds', 'as_out_tier2.dds', 'upup.dds'): continue
        seen.add(tn); t = b.texture(tex_png('as_dark_ally_tier2' if tn == 'as_out_tier2.dds' else tn), 256)   # additive: hex lines on black
        P = g['P'].copy()
        # NIF: thin along x, panel in y/z -> rotate so the panel faces -y (the holder's forward) and stands on z
        P = np.c_[P[:, 1], P[:, 0], P[:, 2]]
        I2 = np.r_[g['I'], g['I'].reshape(-1, 3)[:, [0, 2, 1]].ravel()]
        b.mesh(P, vnormals(P, g['I']), g['UV'], I2, 'panel', t, blend=2)
    save(b, 'sk_shield.gvm', ((10, 10),))
    # dome: the game's sphere (azuma_sp_shield.nif PolyMesh, r 287 cm) normalised to r = 100 cm, upper half only (the lower
    # half is underground: half the fill), spherical UVs tiling the hex-line texture 4 x 3 times, additive; tint it in the engine
    dm = static_parts(RAW + 'model/Object/Dome/azuma_sp_shield.nif')
    g = [x for x in dm if x['name'] == 'PolyMesh'][0]
    P = g['P'] - (g['P'].max(0) + g['P'].min(0)) / 2; P = P / np.linalg.norm(P, axis=1).max() * 100.0
    tri = g['I'].reshape(-1, 3); tri = tri[(P[tri][:, :, 2] > -12).all(1)]
    used = np.unique(tri); rm = -np.ones(len(P), int); rm[used] = np.arange(len(used)); P = P[used]; I = rm[tri].ravel()
    r = np.linalg.norm(P, axis=1, keepdims=True); n = P / r
    UV = np.c_[(np.arctan2(n[:, 1], n[:, 0]) / (2 * np.pi) + 0.5) * 4, np.arccos(np.clip(n[:, 2], -1, 1)) / np.pi * 3]
    # unwrap the seam: triangles spanning u 0..12 get their low-u vertices shifted by +12 (duplicated)
    P = list(P); UV = list(UV); NN = list(n); I = I.reshape(-1, 3).copy()
    for t in I:
        us = [UV[k][0] for k in t]
        if max(us) - min(us) > 2:
            for j in range(3):
                if UV[t[j]][0] < 2: P.append(P[t[j]]); NN.append(NN[t[j]]); UV.append(UV[t[j]] + np.array([4, 0])); t[j] = len(P) - 1
    P = np.array(P); UV = np.array(UV); NN = np.array(NN); I = I.ravel()
    b = Rigid(); tex = b.texture(tex_png('as_dark_ally_tier2'), 256)
    I2 = np.r_[I, I.reshape(-1, 3)[:, [0, 2, 1]].ravel()]
    b.mesh(P, NN, UV, I2, 'dome', tex, blend=2)
    save(b, 'sk_dome.gvm', ((30, 20),))

# ---------------------------------------------------------------- arm-launcher missile
def missile():
    sm = static_parts(RAW + 'effect/cyberbrain/armlauncher_missile.nif')
    b = Rigid()
    for g in sm:
        if g['tex'] and g['tex'].lower().startswith('missile_d'):
            P = g['P'][:, [0, 2, 1]] * np.array([1, -1, 1])   # NIF: along z -> along -y (forward)
            P = g['P'].copy(); P = np.c_[P[:, 0], -P[:, 2], P[:, 1]]
            b.mesh(P, vnormals(P, g['I']), g['UV'], g['I'], 'body', b.texture(tex_png('missile_d'), 128))
    b.att = {'tail': (0, 9, 0), 'nose': (0, -9, 0)}
    save(b, 'sk_missile.gvm', ((60, 20),))

# ---------------------------------------------------------------- heat-scan silhouettes
def heatghost():
    b = Rigid(); white = b.texture(None, 64, img=Image.new('RGBA', (8, 8), (255, 255, 255, 255)))
    for fn in ('heat_scan_target_moving.nif', 'heat_scan_target_stop.nif'):
        sm = static_parts(RAW + 'effect/cyberbrain/' + fn); P = []; I = []
        for g in sm:
            if g['I'] is None or len(g['P']) < 30: continue
            I.append(g['I'] + sum(len(x) for x in P)); P.append(g['P'])
        P = np.concatenate(P); I = np.concatenate(I)
        P = P - np.array([0, 0, P[:, 2].min()])
        P = np.c_[P[:, 1], -P[:, 0], P[:, 2]] * (170.0 / 148.0)   # the NIF figures face +x -> face -y like the characters; the
                                                                   # standing one scaled from 148 to 170 cm (same factor for both)
        b.mesh(P, vnormals(P, I), np.zeros((len(P), 2)), I, 'body', white, blend=2)
    save(b, 'sk_heatghost.gvm', ((30, 10),))

# ---------------------------------------------------------------- mini-Tachikoma (Togusa's recon unit)
def minitachi():
    W, par, h = node_tree(N + 'CB_SPIDERMINE/CB_TACHIKOMA_MINI.nif')
    m = skinned_rest(N + 'CB_SPIDERMINE/CB_TACHIKOMA_MINI.nif')[0]
    legs = np.array(['leg' in b.lower() or 'foot' in b.lower() for b in m['bone']])
    b = Rigid(); t = b.texture(tex_png(m['tex'] or 'CB_TACHIKOMA_MINI_B_D'), 256, glow=N + 'CB_SPIDERMINE/B_glow.dds' if (m['tex'] or 'CB_TACHIKOMA_MINI_B_D').lower().startswith('cb_tachikoma_mini_b_d') else None)   # (b71: its glow map)
    sm = submesh(m, ~legs); b.mesh(sm['P'], sm['N'], sm['UV'], sm['I'], 'body', t)
    sm = submesh(m, legs); b.mesh(sm['P'], sm['N'], sm['UV'], sm['I'], 'legs', t)
    b.att = {'eye': W['Bip01 body'][:3, 3] if 'Bip01 body' in W else (0, -10, 20)}
    # 'walk' clip: body bob 1.5 cm at 6 Hz (the legs part stays; the engine slides the whole model along the ground)
    fr = [{'body': (np.array([0, 0, 0, 1.0]), np.array([0, 0, 1.5 * abs(np.sin(i / 10 * np.pi))]))} for i in range(11)]
    b.clip('walk', fr, 1, 30.0)
    save(b, 'sk_minitachi.gvm')

if __name__ == '__main__':
    only = sys.argv[2:] if len(sys.argv) > 2 else None
    for f in (turret, drone, emp, wall, shield, missile, heatghost, minitachi):
        if only and f.__name__ not in only: continue
        try: f()
        except Exception as e:
            import traceback; traceback.print_exc(); report.append('%s FAILED: %s' % (f.__name__, e))
    print('\n'.join(report))
