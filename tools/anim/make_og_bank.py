# chr_og_m.gab / chr_og_f.gab: the out-of-game animation banks (b63). The game's own menu animations, from the
# out-of-game networks Data/anim/GIS_base_3rd_OutGame_runtimeBinary/GIS_base_3rd_OutGame.nmb (male) and
# GIS_female_3rd_OutGame_runtimeBinary/GIS_female_3rd_OutGame.nmb (female):
#   choice / choice_idle  each operator's pose when picked on the operative screen (footage: "EP. 24 Azuma" 20-23 s:
#                         Azuma raises his rifle, turns it, lowers it to his side), then the idle that loops after it
#   res_*                 the results-screen set: idle, win, lose, win_idle, lose_idle, win_ready, lose_ready, win_ready_idle
# Same file layout as chr_anim_m.gab (make_bank.py, read by engine/src/chr.h cb_load) and the same 42-bone skeleton in the
# same order, so the menu figure keeps its model binding. Sets: "op.<stem>" (slots choice + idle) and "result" (slots res_*).
# Decoded with our own readers (ognet.py, nmb.py, tools/lib/anim.py): no NaturalMotion code.
# Usage: python3 make_og_bank.py m|f [outdir]
import sys, os, struct, time, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib')); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
from ognet import OGNet
from anim import hdr, decode_clip, pose, pose_world
from rig import fk

G = sys.argv[1] if len(sys.argv) > 1 else 'm'
OUT = sys.argv[2] if len(sys.argv) > 2 else gitspaths.root('DATA')
ADIR = gitspaths.dirp('ANIMBIN', 'GIS_base_3rd_OutGame_runtimeBinary' if G == 'm' else 'GIS_female_3rd_OutGame_runtimeBinary')
NMB = ADIR + ('GIS_base_3rd_OutGame.nmb' if G == 'm' else 'GIS_female_3rd_OutGame.nmb')

ARM = ['Clavicle', 'UpperArm', 'Forearm', 'Hand', 'Finger0', 'Finger01', 'Finger02', 'Finger1', 'Finger11', 'Finger12', 'Finger2', 'Finger21', 'Finger22']
LEG = ['Thigh', 'Calf', 'Foot', 'Toe0']
CANON = ['Bip01', 'Bip01_Pelvis', 'Bip01_Spine', 'Bip01_Spine1', 'Bip01_Spine2', 'Bip01_Neck', 'Bip01_Head'] + \
        ['Bip01_L_' + a for a in ARM] + ['Bip01_R_' + a for a in ARM] + ['Grip'] + ['Bip01_L_' + l for l in LEG] + ['Bip01_R_' + l for l in LEG]
NB = len(CANON)

# operator stem (engine) -> regex of its clip files in this network
OPS = {'m': [('bato', r'^Bato_choice'), ('paz', r'^Paz_choice'), ('ishikawa', r'^Isikawa_choice'), ('saito', r'^Saito_choice'),
             ('togusa', r'^Togusa_choice'), ('borma', r'^Boma_choice'), ('kuro', r'^kuro_choice'), ('azuma', r'^Azuma_choice')],
       'f': [('maven', r'^maven_choice'), ('motoko', r'^kusanagi_choice'), ('reiko', r'^Reiko_choice'), ('sitara', r'^Sitara_choice')]}
RES = [('res_idle', 'result_idle_'), ('res_win', 'result_win_[bk]'), ('res_lose', 'result_lose_[bk]'), ('res_win_idle', 'result_win_idle'),
       ('res_lose_idle', 'result_lose_idle'), ('res_win_rdy', 'result_win_ready_[bk]'), ('res_lose_rdy', 'result_lose_ready_[bk]'),
       ('res_rdy_idle', 'result_win_ready_idl')]
# b99 (user: the TOP OPERATIVE screen's poses; the Demolition video 567.5-576 s): the stance each operative stands in on the lobby and
# in the results line-up, one per weapon family (<base|kusanagi>_<FAMILY>_3rd_lobby_primary_base: a rifle held low in both hands,
# an SMG hanging from the right hand with the left hand on the hip ...). Sets "lb.<set>" (the in-game bank's set names,
# tools/anim/slots.py WEAPON_SET), slot 'lobby'.
LOBBY = [('m4a1', 'M4A1'), ('mp5', 'MP5'), ('l118', 'L118A'), ('seburom5', 'p99'), ('m67', 'M67'), ('knife', 'knife'), ('mateba', 'Mateba'),
         ('kriss', 'Kriss'), ('870', 'M870'), ('scar', 'Scar'), ('acr', 'ACR'), ('m249', 'M249')]
LOOP = {'idle', 'res_idle', 'res_win_idle', 'res_lose_idle', 'res_rdy_idle', 'lobby'}
SLOTNAMES = ['choice', 'idle'] + [r[0] for r in RES] + ['lobby']

def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return np.array([aw*bx + ax*bw + ay*bz - az*by, aw*by - ax*bz + ay*bw + az*bx, aw*bz + ax*by - ay*bx + az*bw, aw*bw - ax*bx - ay*by - az*bz])
def conj(q): return np.array([-q[0], -q[1], -q[2], q[3]])
def qrot(q, v):
    u = np.asarray(q[:3]); w = q[3]; return v + 2 * np.cross(u, np.cross(u, v) + w * v)

t0 = time.time()
N = OGNet(NMB)
R0 = N.rigs[0]
def canon_parent(name):
    if name == 'Grip': return CANON.index('Bip01_R_Hand')
    if name == 'Bip01': return -1
    i = R0['names'].index(name); p = R0['parent'][i]
    return CANON.index(R0['names'][p])
PARENT = [canon_parent(n) for n in CANON]
for i, p in enumerate(PARENT): assert p < i

_dec = {}
def decoded(f):
    if f not in _dec:
        h = hdr(ADIR + f); _dec[f] = (h, decode_clip(h))
    return _dec[f]
def decode_rec(rec, aset):
    rig = N.rigs[aset]; m = N.mapof(rec); h, C0 = decoded(rec['file']); C = dict(C0); C['map'] = m['list']
    nf = C['nf']; f0 = int(round(rec['start'] * (nf - 1))); f1 = max(int(round(rec['end'] * (nf - 1))), int(round(rec['start'] * (nf - 1))))
    idx = [rig['names'].index(n) for n in CANON]
    P = []; W = []
    for i in range(f0, f1 + 1):
        if C['mode'] == 1: lp, lq = pose(rig, C, i); p, w = fk(rig['parent'], lp, lq)
        else: p, w = pose_world(rig, C, i)
        P.append(np.asarray(p, float)[idx]); W.append(np.asarray(w, float)[idx])
    return np.array(P), np.array(W)
def to_local(P, W):
    nf = len(P); LQ = np.zeros((nf, NB, 4)); LP = np.zeros((nf, NB, 3))
    for t in range(nf):
        for b in range(NB):
            p = PARENT[b]
            if p < 0: LQ[t, b] = W[t, b]; LP[t, b] = P[t, b]
            else: ci = conj(W[t, p]); LQ[t, b] = qmul(ci, W[t, b]); LP[t, b] = qrot(ci, P[t, b] - P[t, p])
            if LQ[t, b, 3] < 0 and t == 0: LQ[t, b] *= -1
            if t > 0 and np.dot(LQ[t, b], LQ[t - 1, b]) < 0: LQ[t, b] *= -1
    return LQ, LP
def valid(P, W):
    if not np.all(np.isfinite(P)) or not np.all(np.isfinite(W)): return 'nan'
    LQ, LP = to_local(P[:1], W[:1])
    for b, ref in [('Bip01_L_Foot', 43.2), ('Bip01_R_Foot', 43.2), ('Bip01_L_Calf', 43.0), ('Bip01_R_Hand', 27.3), ('Bip01_Head', 6.7)]:
        l = np.linalg.norm(LP[0, CANON.index(b)])
        if abs(l - ref) > 0.3 * ref + 3: return 'bone %s %.1f' % (b, l)
    return None
def run_for(pattern):
    import re
    rx = re.compile(pattern, re.I)
    for g in N.groups:
        fs = {}
        for r in g: fs[r['file']] = fs.get(r['file'], 0) + 1
        if rx.search(max(fs, key=fs.get)): return g
    return None
def decode_run(run, name):
    """the run's most common file, decoded with the first set (and rig) that uses it and gives a sane body"""
    fs = {}
    for r in run: fs[r['file']] = fs.get(r['file'], 0) + 1
    top = max(fs, key=fs.get); tried = set()
    for a, r in enumerate(run):
        if r['file'] != top or a >= len(N.rigs) or r['map'] in tried: continue
        tried.add(r['map'])
        try: P, W = decode_rec(r, a)
        except Exception as e: print('  %s set %d: %s' % (name, a, e)); continue
        bad = valid(P, W)
        if not bad: return P, W, r
        print('  %s set %d (%s): %s' % (name, a, top, bad))
    raise SystemExit('%s: no set decodes %s' % (name, top))

clips = []; table = {}
def add(setname, slot, run):
    P, W, rec = decode_run(run, '%s.%s' % (setname, slot))
    g = CANON.index('Grip'); h = CANON.index('Bip01_R_Hand'); far = float(np.linalg.norm(P[:, g] - P[:, h], axis=1).max())
    LQ, LP = to_local(P, W)
    clips.append(dict(name='%s.%s' % (setname.split('.')[-1], slot), file=rec['file'], nf=len(LQ), flags=1 if slot in LOOP else 0, LQ=LQ, LP=LP))
    table.setdefault(setname, [-1] * len(SLOTNAMES))[SLOTNAMES.index(slot)] = len(clips) - 1
    print('%-22s %-48s %4d frames %5.2f s  grip %3.0f cm from the hand  (%.0fs)' % ('%s.%s' % (setname, slot), rec['file'], len(LQ), (len(LQ) - 1) / 30, far, time.time() - t0), flush=True)

for stem, rx in OPS[G]:
    ch = run_for(rx + r'_(?!idle)'); ci = run_for(rx + r'_idle')
    if ch is None or ci is None: print('  %s: no choice clips' % stem); continue
    add('op.' + stem, 'choice', ch); add('op.' + stem, 'idle', ci)
for slot, pat in RES:
    r = run_for(pat)
    if r is None: print('  result %s: missing' % slot); continue
    add('result', slot, r)

def add_rec(setname, slot, rx):
    """a clip by its file's name (the per-weapon node: one record per animation set, each set its own file)"""
    import re
    rx = re.compile(rx, re.I); tried = set()
    for g in LOBBY_GROUP:
        for a, r in enumerate(g):
            if not rx.search(r['file']) or a >= len(N.rigs) or (r['file'], r['map']) in tried: continue
            tried.add((r['file'], r['map']))
            try: P, W = decode_rec(r, a)
            except Exception as e: continue
            if valid(P, W): continue
            LQ, LP = to_local(P, W)
            clips.append(dict(name='%s.%s' % (setname.split('.')[-1], slot), file=r['file'], nf=len(LQ), flags=1 if slot in LOOP else 0, LQ=LQ, LP=LP))
            table.setdefault(setname, [-1] * len(SLOTNAMES))[SLOTNAMES.index(slot)] = len(clips) - 1
            print('%-22s %-48s %4d frames %5.2f s  (%.0fs)' % ('%s.%s' % (setname, slot), r['file'], len(LQ), (len(LQ) - 1) / 30, time.time() - t0), flush=True)
            return True
    print('  %s.%s: no clip' % (setname, slot)); return False
# (the node with a file of its own for nearly every set is the base stance; the '01' / 'G' variants' nodes fall back to a few
#  families' files - and the files' names are cut at 29 characters, so the node, not the name, tells them apart)
LOBBY_GROUP = [max((g for g in N.groups if any('lobby_p' in r['file'] for r in g)), key=lambda g: len(set(r['file'] for r in g)))]
for sn, fam in LOBBY:
    add_rec('lb.' + sn, 'lobby', r'^(base|kusanagi)_%s_3rd_lobby_p' % fam)
# (the families' second stance - <...>_lobby_primary01, another node: 5-11 s of looking the gun over - is not taken)
SETS = sorted(table)
def qz(c): return np.round(c['LQ'] * 32767).astype(np.int16)
def pack_clip(c):
    q = qz(c); rot = []; pos = []; qdata = []; pdata = []
    for b in range(NB):
        tr = q[:, b]
        if np.abs(tr.astype(int) - tr[0].astype(int)).max() <= 1: rot.append(len(qdata)); qdata.append(tr[0])
        else: rot.append(len(qdata) | 0x80000000); qdata.extend(list(tr))
    for b in range(NB):
        tp = c['LP'][:, b]
        if np.abs(tp - tp[0]).max() < 0.02: pos.append(len(pdata)); pdata.append(tp[0])
        else: pos.append(len(pdata) | 0x80000000); pdata.extend(list(tp))
    return rot, pos, np.array(qdata, '<i2').tobytes() + np.array(pdata, '<f4').tobytes(), len(qdata)
path = os.path.join(OUT, 'chr_og_%s.gab' % G)
hdrs = []; blobs = b''
for c in clips:
    rot, pos, blob, nq = pack_clip(c); hdrs.append((c, rot, pos, len(blobs), len(blob), nq)); blobs += blob
    while len(blobs) % 4: blobs += b'\0'
with open(path, 'wb') as f:
    f.write(b'GAB1' + struct.pack('<5I', 1, NB, len(clips), len(SETS), len(SLOTNAMES)))
    for n, p in zip(CANON, PARENT): f.write(n.encode().ljust(32, b'\0') + struct.pack('<i', p))
    for s in SLOTNAMES: f.write(s.encode()[:15].ljust(16, b'\0'))
    for sn in SETS:
        rec = sn.encode()[:15].ljust(16, b'\0') + struct.pack('<%dh' % len(SLOTNAMES), *table[sn])
        while len(rec) % 4: rec += b'\0'
        f.write(rec)
    for c, rot, pos, off, ln, nq in hdrs:
        f.write(c['name'].encode()[:31].ljust(32, b'\0') + struct.pack('<HHfff2fIII', c['nf'], c['flags'], 30.0, 0.0, 0.0, 0.0, 0.0, off, ln, nq))
        f.write(struct.pack('<%dI' % NB, *rot)); f.write(struct.pack('<%dI' % NB, *pos))
    f.write(blobs)
    f.write(struct.pack('<I', 0)); f.write(struct.pack('<I', 0))   # no events, no strings
print('wrote', path, os.path.getsize(path), 'bytes;', len(clips), 'clips,', len(SETS), 'sets', '(%.0fs)' % (time.time() - t0))
