# dem_fx.gtx: the textures of the bomb device's first-person effects (b100; the user: "make sure that the bomb device renders and
# functions properly with all it's animations and effects"), one 512 x 512 DXT5 atlas in the GTX1 format (skills.h sk_tex_load):
#   y   0-255  red_wave01..32.dds, 64 x 64 each, 8 x 4 (the flip book of the display filling red: effect\DM\DM_bomb_1p_fx.nif)
#   y 256-287  bubble.dds, y 288-319 bubble_small.dds (512 x 32 each: the sparks that drift along the display)
#   y 320-383  x 0-63 lenz01.dds (the planted bomb's lamp flare), x 64-191 y 320-351 dm_up_light.dds turned on its side (its beam)
#   y 384-447  x 0-63 dm_device_button.dds (the keys' flash: DM_device_install_button.nif), x 72-87 white (the display's tint)
#   y 384-511  x 128-383 dm_device_w.dds (the hexagon read-out: DM_device_install_success.nif), x 384-511 arrow.dds (the red
#              diamond round the planted bomb) - the last three from worldEnv\EnvBomb (bomb_box.nif's parts, b101)
# It also prints the effects' places and times as engine/src/hands.h has them (HVF_*), read from the NIFs - compare after a
# change of the game's files. Places: the device's 'bomb_01' node space (x along it, -y toward the holder, z up; cm).
# usage: make_bomb_fx.py [rawdir] [outdir]
import sys, os, struct, numpy as np, etcpak
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'vm')); sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', 'fx'))
import gitspaths, gvmlib, nifps
RAW = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DEMX')
OUT = sys.argv[2] if len(sys.argv) > 2 else gitspaths.root('DATA')
TEX = RAW + '/effect/texture/'; FX = RAW + '/effect/DM/'
def img(name, size):
    im = Image.open(TEX + name).convert('RGBA'); return np.asarray(im.resize(size, Image.LANCZOS) if im.size != size else im)
A = np.zeros((512, 512, 4), np.uint8)
for i in range(32): A[(i // 8) * 64:(i // 8) * 64 + 64, (i % 8) * 64:(i % 8) * 64 + 64] = img('red_wave%02d.dds' % (i + 1), (64, 64))
A[256:288] = img('bubble.dds', (512, 32)); A[288:320] = img('bubble_small.dds', (512, 32))
ENV = RAW + '/worldEnv/EnvBomb/'
def eimg(name, size): im = Image.open(ENV + name).convert('RGBA'); return np.asarray(im.resize(size, Image.LANCZOS) if im.size != size else im)
A[320:384, 0:64] = eimg('lenz01.DDS', (64, 64)); A[320:352, 64:192] = np.rot90(eimg('dm_up_light.DDS', (32, 128)), 1)   # (the beam's root at the left)
A[384:512, 384:512] = eimg('arrow.DDS', (128, 128))
A[384:448, 0:64] = img('dm_device_button.dds', (64, 64)); A[388:404, 72:88] = 255
A[384:512, 128:384] = img('dm_device_w.dds', (256, 128))
dxt = etcpak.compress_to_dxt5(np.ascontiguousarray(A).tobytes(), 512, 512)
with open(os.path.join(OUT, 'dem_fx.gtx'), 'wb') as f: f.write(b'GTX1' + struct.pack('<HHBBHI', 512, 512, 2, 0, 0, len(dxt))); f.write(dxt)
print('dem_fx.gtx', 16 + len(dxt), 'bytes')
# ---- dem_dev.gtx (b101): the read-out over the forearm while defusing - effect\kfm\DM_device_install.nif with its start /
# loop / end sequences (DM_device_install_root_*.kf). 512 x 256 DXT5: x 0-255 y 0-127 dm_device_bg.dds (the cells' fill, added
# and scrolled), x 256-511 y 0-127 the cells' silhouette (dm_device_w_start.dds: white, its brightness as alpha - the dark backing
# and the flash), x 0-127 y 128-255 dm_device_ring.dds, x 128-255 dm_device_bomb_B.dds (the bomb's picture), x 256-511
# dm_device_w.dds (the cells' outline).
D2 = np.zeros((256, 512, 4), np.uint8)
D2[0:128, 0:256] = img('dm_device_bg.dds', (256, 128)); D2[0:128, 0:256, 3] = 255
sh = img('dm_device_w_start.dds', (256, 128)); D2[0:128, 256:512, :3] = 255; D2[0:128, 256:512, 3] = sh[..., :3].max(2)
D2[128:256, 0:128] = img('dm_device_ring.dds', (128, 128)); D2[128:256, 128:256] = img('dm_device_bomb_B.dds', (128, 128)); D2[128:256, 0:256, 3] = 255
D2[128:256, 256:512] = img('dm_device_w.dds', (256, 128))
dxt = etcpak.compress_to_dxt5(np.ascontiguousarray(D2).tobytes(), 512, 256)
with open(os.path.join(OUT, 'dem_dev.gtx'), 'wb') as f: f.write(b'GTX1' + struct.pack('<HHBBHI', 512, 256, 2, 0, 0, len(dxt))); f.write(dxt)
print('dem_dev.gtx', 16 + len(dxt), 'bytes')
# ---- the numbers
np.set_printoptions(suppress=True, precision=4)
nm = gvmlib.nifmesh; h = nm.load(RAW + '/model/Objects/DM/bomb_1p.nif'); d = h['d']; B = h['blocks']; names = {}
for k, (t, o, s) in enumerate(B):
    if t == 'NiNode': a = nm.node(d, o); names[a['name'] if isinstance(a['name'], str) else nm.S(h, a['name'])] = k
def L(k):
    a = nm.node(d, B[k][1]); M = np.eye(4); M[:3, :3] = np.array(a['r']).reshape(3, 3) * a['s']; M[:3, 3] = a['t']; return M
Wb = L(names['Bomb_Root']) @ L(names['bomb']) @ L(names['bomb_01']); fx1 = nm.node(d, B[names['Fx_1']][1])['t']
print('bomb_01 -> Bomb_Root (HVF_M):\n', Wb[:3]); print('Fx_1 in bomb_01 (added to the ink / knob places below):', np.round(fx1, 3))
hf, F = nifps.parse(FX + 'DM_bomb_1p_fx.nif')
def node(F, name): return [b for b in F if b.get('name') == name][0]
def keys(F, hh, ctrl): b = F[F[ctrl['interp']]['data']]; return [(round(k['t'], 3), np.round(k['v'], 3).tolist() if hasattr(k['v'], '__len__') else round(k['v'], 3)) for k in b['keys']]
for nme in ('B_ink01', 'B_ink02', 'small_bubble', 'small_bubble01', 'Plane01'):
    print(nme, 't', np.round(np.array(node(F, nme)['t']) + (fx1 if nme in ('B_ink01', 'B_ink02', 'Plane01') else 0), 3))
for k, b in enumerate(F):
    t = hf['blocks'][k][0]
    if t == 'NiMesh':
        for o in nifps.stream_pos(hf, F, k):
            if 'P' in o: P = o['P'] * b['s']; print(' mesh', k, b.get('name'), 'min', P.min(0), 'max', P.max(0))
    if t in ('NiAlphaController', 'NiFlipController', 'NiMaterialColorController', 'NiTextureTransformController'):
        print('  ', k, t, 'op', b.get('op'), '%.3f-%.3f' % (b['start'], b['stop']), keys(F, hf, b)[:4])
for fn in ('DM_device_install_button.nif', 'DM_device_install_success.nif'):
    h2, F2 = nifps.parse(FX + fn); print(fn)
    for k, b in enumerate(F2):
        t = h2['blocks'][k][0]
        if t == 'NiNode' and b.get('name') not in ('Scene Root', 'Camera'): print('  node', b.get('name'), np.round(b['t'], 3))
        if t == 'NiMesh':
            for o in nifps.stream_pos(h2, F2, k):
                if 'P' in o: P = o['P'] * b['s']; print('   plane half', np.abs(P).max(0))
        if t == 'NiAlphaController': print('   alpha %.3f-%.3f' % (b['start'], b['stop']), keys(F2, h2, b))
# ---- the forearm read-out's tracks (hands.h HVD_*): the sequences sampled evenly
import kfseq
KF = RAW + '/effect/kfm/DM_device_install_root_%s.kf'
def tr(q, node, ctrl, cid=None): return [e for e in q.tracks if e['node'] == node and (e['ctrl'] or '').endswith(ctrl) and (cid is None or (e['cid'] or '').endswith(cid))][0]
def row(name, v): print('static const float %s[%d] = { %s };' % (name, len(v), ', '.join('%.3ff' % x for x in v)))
hd, FD = nifps.parse(RAW + '/effect/kfm/DM_device_install.nif'); print('DM_device_install.nif (the planes under "ani" are scaled by its key: 10 in the loop):')
for k, b in enumerate(FD):
    if hd['blocks'][k][0] == 'NiMesh':
        par = [x for x in FD if isinstance(x, dict) and k in (x.get('children') or [])][0]
        for o in nifps.stream_pos(hd, FD, k):
            if 'P' in o and len(o['P']) == 4: print('  %-10s under %-8s at %s half %s' % (b.get('name'), par.get('name'), np.round(par['t'], 2), np.round(np.abs(o['P'] * b['s']).max(0)[:2], 3)))
for sq, n in (('start', 9), ('loop', 17), ('end', 9)):
    q = kfseq.Seq(KF % sq); ts = np.linspace(0, q.dur, n); print('sequence', sq, 'duration %.4f' % q.dur)
    if sq != 'loop':
        row('HVD_%s_S' % sq.upper(), [q.xform(tr(q, 'ani', 'TransformController'), t)[2] / 10 for t in ts]); row('HVD_%s_FLASH' % sq.upper(), [q.fval(tr(q, 'Plane@#10', 'AlphaController'), t) for t in ts])
        row('HVD_%s_A' % sq.upper(), [q.fval(tr(q, 'Plane@#11', 'AlphaController'), t) for t in ts])
    else:
        row('HVD_BG_A', [q.fval(tr(q, 'Plane@#6', 'AlphaController'), t) for t in ts]); row('HVD_BG_V', [q.fval(tr(q, 'Plane@#6', 'TextureTransformController'), t) for t in ts])
        row('HVD_PIC_A', [q.fval(tr(q, 'Plane@#12', 'AlphaController'), t) for t in ts]); row('HVD_GHOST_A', [q.fval(tr(q, 'Plane@#13', 'AlphaController'), t) for t in ts])
        for nd in ('red', 'green'):
            T = [q.xform(tr(q, nd, 'TransformController'), t)[0] for t in ts]; row('HVD_%s_X' % nd.upper(), [v[0] for v in T]); row('HVD_%s_Z' % nd.upper(), [v[2] for v in T])
        Q = [q.xform(tr(q, 'red_ring', 'TransformController'), t)[1] for t in ts]; print('  ring quaternions (w x y z) at 0, 1/4, 1/2:', np.round(Q[0], 2), np.round(Q[4], 2), np.round(Q[8], 2), '= one turn in its plane a loop')
