# chr_anim_m.gab / chr_anim_f.gab: the third-person animation banks shared by all male / female characters.
# Every clip the original network plays for the roles in slots.py (per animation set = per weapon), decoded with the
# channel map the network itself uses (nmb.py), stored as LOCAL bone rotations (int16 quaternions) on a 42-bone skeleton,
# root (Bip01) position + rotation in character space (z up, cm, the character faces -y). The engine (engine/src/chr.h)
# blends clips in local space and runs FK.
#
# File layout (little endian):
#   'GAB1' u32 version(1) u32 nbones u32 nclips u32 nsets u32 nslots
#   bones  nbones x { char name[32]; i32 parent }                       (parent -1 = character space)
#   slots  nslots x char[16]
#   sets   nsets  x { char name[16]; i16 clip[nslots] }  (padded to 4)    (-1: the slot does not exist for this set)
#   clips  nclips x { char name[32]; u16 nf; u16 flags; f32 fps; f32 speed; f32 phase; f32 dir[2]; u32 data_off; u32 data_len; u32 nquat;
#                     u32 rot[nbones]; u32 pos[nbones] }
#          rot[b]: bit 31 = animated (nf quaternions) else 1; low bits = index of the first int16x4 quaternion in the clip data
#          pos[b]: bit 31 = animated (nf vectors) else 1; low bits = index of the first f32x3 vector in the position block,
#                  which starts after the nquat quaternions (data_off + 8 * nquat)
#          flags: 1 loop, 2 mirrored copy, 4 locomotion (speed/phase/dir valid)
#          speed: ground speed of the clip (cm/s, from the planted foot); dir: unit direction of travel (character space);
#          phase: normalised time at which the left foot is furthest forward along dir (foot-sync of blended clips)
#   data   per clip: int16 quaternions (x,y,z,w * 32767), then f32 positions
#   events u32 nevents, nevents x { f32 t (normalised clip time); u16 clip; u8 kind; u8 pad; u16 str; u16 pad }, sorted by clip
#          kind 1 left foot plant, 2 right foot plant, 3 sound event (FMOD name, lower case), 4 effect marker (name)
#   strings u32 nbytes, NUL-terminated strings (str = byte offset)
# Usage: python3 make_bank.py m|f [outdir]
import sys, os, struct, numpy as np, collections, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nmb import Network
from slots import SLOTS, SHARED, WEAPON_SET
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib')); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
from anim import hdr, decode_clip, pose, pose_world
from rig import fk

G = sys.argv[1] if len(sys.argv) > 1 else 'm'
OUT = sys.argv[2] if len(sys.argv) > 2 else gitspaths.root('DATA')
ADIR = gitspaths.dirp('ANIMBIN', 'GIS_base_3rd_runtimeBinary' if G == 'm' else 'GIS_female_3rd_runtimeBinary')
NMB = ADIR + ('GIS_base_3rd.nmb' if G == 'm' else 'GIS_female_3rd.nmb')

ARM = ['Clavicle', 'UpperArm', 'Forearm', 'Hand', 'Finger0', 'Finger01', 'Finger02', 'Finger1', 'Finger11', 'Finger12', 'Finger2', 'Finger21', 'Finger22']
LEG = ['Thigh', 'Calf', 'Foot', 'Toe0']
CANON = ['Bip01', 'Bip01_Pelvis', 'Bip01_Spine', 'Bip01_Spine1', 'Bip01_Spine2', 'Bip01_Neck', 'Bip01_Head'] + \
        ['Bip01_L_' + a for a in ARM] + ['Bip01_R_' + a for a in ARM] + ['Grip'] + ['Bip01_L_' + l for l in LEG] + ['Bip01_R_' + l for l in LEG]
NB = len(CANON)

def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return np.array([aw*bx + ax*bw + ay*bz - az*by, aw*by - ax*bz + ay*bw + az*bx, aw*bz + ax*by - ay*bx + az*bw, aw*bw - ax*bx - ay*by - az*bz])
def conj(q): return np.array([-q[0], -q[1], -q[2], q[3]])
def qrot(q, v):
    u = np.asarray(q[:3]); w = q[3]; return v + 2 * np.cross(u, np.cross(u, v) + w * v)
def qmat(q):
    x, y, z, w = q
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
def mat2q(m):
    t = np.trace(m)
    if t > 0: s = np.sqrt(t + 1) * 2; q = [(m[2,1]-m[1,2])/s, (m[0,2]-m[2,0])/s, (m[1,0]-m[0,1])/s, 0.25*s]
    elif m[0,0] > m[1,1] and m[0,0] > m[2,2]: s = np.sqrt(1+m[0,0]-m[1,1]-m[2,2])*2; q = [0.25*s, (m[0,1]+m[1,0])/s, (m[0,2]+m[2,0])/s, (m[2,1]-m[1,2])/s]
    elif m[1,1] > m[2,2]: s = np.sqrt(1+m[1,1]-m[0,0]-m[2,2])*2; q = [(m[0,1]+m[1,0])/s, 0.25*s, (m[1,2]+m[2,1])/s, (m[0,2]-m[2,0])/s]
    else: s = np.sqrt(1+m[2,2]-m[0,0]-m[1,1])*2; q = [(m[0,2]+m[2,0])/s, (m[1,2]+m[2,1])/s, 0.25*s, (m[1,0]-m[0,1])/s]
    q = np.array(q); return q / np.linalg.norm(q)

t0 = time.time()
N = Network(NMB); N.events()
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
    """-> canonical model-space frames: P (nf, NB, 3), W (nf, NB, 4)"""
    rig = N.rigs[aset]; m = N.mapof(rec); h, C0 = decoded(rec['file']); C = dict(C0); C['map'] = m['list']
    nf = C['nf']; f0 = int(round(rec['start'] * (nf - 1))); f1 = int(round(rec['end'] * (nf - 1)))
    f1 = max(f1, f0)
    idx = [rig['names'].index(n) for n in CANON]
    P = []; W = []
    for i in range(f0, f1 + 1):
        if C['mode'] == 1: lp, lq = pose(rig, C, i); p, w = fk(rig['parent'], lp, lq)
        else: p, w = pose_world(rig, C, i)
        p = np.asarray(p, float); w = np.asarray(w, float)
        P.append(p[idx]); W.append(w[idx])
    return np.array(P), np.array(W), h

# the weapon (Grip) in the right hand: clips that leave the Grip channel to the rig's default (deaths, climbs, the female
# grenade set ...) put the gun up to 2 m away from the body. When the Grip ever strays further than GRIP_MAX from the hand
# (reloads move it up to ~40 cm), the whole clip keeps it in the hand at the rifle idle's hand-relative transform.
GRIP_MAX = 45.0
GRIP_LOCAL = None   # (p, q) relative to Bip01_R_Hand, from the rifle idle (set below)
def fix_grip(P, W):
    g = CANON.index('Grip'); h = CANON.index('Bip01_R_Hand')
    d = np.linalg.norm(P[:, g] - P[:, h], axis=1)
    if GRIP_LOCAL is None or d.max() <= GRIP_MAX: return P, W, 0
    P = P.copy(); W = W.copy(); lp, lq = GRIP_LOCAL
    for t in range(len(P)): P[t, g] = P[t, h] + qrot(W[t, h], lp); W[t, g] = qmul(W[t, h], lq)
    return P, W, d.max()

def to_local(P, W):
    nf = len(P); LQ = np.zeros((nf, NB, 4)); LP = np.zeros((nf, NB, 3))
    for t in range(nf):
        for b in range(NB):
            p = PARENT[b]
            if p < 0: LQ[t, b] = W[t, b]; LP[t, b] = P[t, b]
            else:
                ci = conj(W[t, p]); LQ[t, b] = qmul(ci, W[t, b]); LP[t, b] = qrot(ci, P[t, b] - P[t, p])
            if LQ[t, b, 3] < 0 and t == 0: LQ[t, b] *= -1
            if t > 0 and np.dot(LQ[t, b], LQ[t - 1, b]) < 0: LQ[t, b] *= -1
    return LQ, LP

def valid(P, W):
    """reject garbage decodes: feet below the knees, head above the pelvis at frame 0, finite numbers, sane bone lengths"""
    if not np.all(np.isfinite(P)) or not np.all(np.isfinite(W)): return 'nan'
    LQ, LP = to_local(P[:1], W[:1])
    for b, ref in [('Bip01_L_Foot', 43.2), ('Bip01_R_Foot', 43.2), ('Bip01_L_Calf', 43.0), ('Bip01_R_Hand', 27.3), ('Bip01_Head', 6.7)]:
        l = np.linalg.norm(LP[0, CANON.index(b)])
        if abs(l - ref) > 0.25 * ref + 3: return 'bone %s %.1f' % (b, l)
    return None

# ---- mirror (left-side deaths are the right-side clips mirrored, as the original network does with MirrorTransforms)
def partner(n):
    if '_L_' in n: return n.replace('_L_', '_R_')
    if '_R_' in n: return n.replace('_R_', '_L_')
    return n
MIR = np.diag([-1.0, 1, 1])
Pb, Wb = fk(R0['parent'], [p.astype(float) for p in R0['pos']], [q.astype(float) for q in R0['quat']])
Pb = np.asarray(Pb); Wb = np.asarray(Wb)
SLOC = {}
for n in CANON:
    if n == 'Grip': SLOC[n] = np.diag([1.0, 1, -1]); continue
    i = R0['names'].index(n); j = R0['names'].index(partner(n))
    S = np.linalg.inv(MIR @ qmat(Wb[j])) @ qmat(Wb[i]); SLOC[n] = np.diag(np.sign(np.diag(S)))
def mirror(P, W):
    P2 = np.zeros_like(P); W2 = np.zeros_like(W)
    for b, n in enumerate(CANON):
        m = CANON.index(partner(n)) if partner(n) in CANON else b
        for t in range(len(P)):
            P2[t, b] = MIR @ P[t, m]
            W2[t, b] = mat2q(MIR @ qmat(W[t, m]) @ SLOC[n])
    return P2, W2

# ---- locomotion analysis
def loco_info(P):
    iL = CANON.index('Bip01_L_Toe0'); iR = CANON.index('Bip01_R_Toe0')
    nf = len(P)
    if nf < 4: return 0, 0, (0, 0)
    vel = []
    for t in range(nf - 1):
        k = iL if P[t, iL, 2] < P[t, iR, 2] else iR
        vel.append(-(P[t + 1, k, :2] - P[t, k, :2]) * 30)
    v = np.median(np.array(vel), 0); sp = float(np.hypot(*v))
    if sp < 20: return 0, 0, (0, 0)
    d = v / sp
    fw = P[:-1, iL, :2] @ d
    ph = int(np.argmax(fw)) / (nf - 1)
    return sp, ph, (float(d[0]), float(d[1]))

LOCO = {'walk_f', 'walk_b', 'walk_l', 'walk_r', 'run_f', 'run_b', 'run_l', 'run_r', 'cwalk_f', 'cwalk_b', 'cwalk_l', 'cwalk_r',
        'crun_f', 'crun_b', 'crun_l', 'crun_r', 'sprint', 'sprint_ll', 'sprint_lr', 'sprint_fl', 'sprint_fr'}
MIRROR = {'d_hs1': 'd_hs1m', 'd_hs2': 'd_hs2m', 'd_bs1': 'd_bs1m', 'd_bs2': 'd_bs2m'}

SETS = sorted(WEAPON_SET.items(), key=lambda kv: kv[1])
SETS = [('m4a1', 0)] + [kv for kv in SETS if kv[0] != 'm4a1']
SLOTNAMES = [s[0] for s in SLOTS] + list(MIRROR.values())
clips = []          # dict(name, nf, flags, speed, phase, dir, LQ(int16), LP)
by_key = {}
table = {sname: [-1] * len(SLOTNAMES) for sname, _ in SETS}
report = []
for si, (slot, suf, loop) in enumerate(SLOTS):
    ids = N.find(suf)
    if not ids: report.append('missing node for %s' % slot); continue
    node = ids[0]
    for sname, aset in SETS:
        if slot in SHARED and aset != 0: table[sname][si] = table['m4a1'][si]; continue
        rec = N.rec(node, aset)
        key = (rec['file'], round(rec['start'], 4), round(rec['end'], 4))
        if key in by_key: table[sname][si] = by_key[key]; continue
        P, W, h = decode_rec(rec, aset)
        if GRIP_LOCAL is None and slot == 'idle' and aset == 0:   # the first clip decoded: the rifle idle
            g = CANON.index('Grip'); hb = CANON.index('Bip01_R_Hand'); ci = conj(W[0, hb])
            GRIP_LOCAL = (qrot(ci, P[0, g] - P[0, hb]), qmul(ci, W[0, g])); print('grip in the hand:', np.round(GRIP_LOCAL[0], 1), np.round(GRIP_LOCAL[1], 3))
        bad = valid(P, W)
        if bad:   # a broken decode: try another set that uses the same file with a different map
            alt = None
            for a2 in range(N.nsets):
                r2 = N.rec(node, a2)
                if r2['file'] == rec['file'] and r2['map'] != rec['map']:
                    P2, W2, h = decode_rec(r2, a2)
                    if not valid(P2, W2): alt = (P2, W2); break
            report.append('%s set %s (%s): %s -> %s' % (slot, sname, rec['file'], bad, 'fixed with another map' if alt else 'falls back to the rifle set'))
            if not alt: by_key[key] = table['m4a1'][si]; table[sname][si] = by_key[key]; continue
            P, W = alt
        P, W, far = fix_grip(P, W)
        if far: report.append('%s set %s: the weapon strayed %.0f cm from the hand: kept in the hand' % (slot, sname, far))
        LQ, LP = to_local(P, W)
        sp, ph, d = loco_info(P) if slot in LOCO else (0, 0, (0, 0))
        evs = []
        for typ, tname, ev in rec.get('events', []):
            for (st, du, ud) in ev:
                if tname == 'Left': evs.append((st, 1, ''))
                elif tname == 'Right': evs.append((st, 2, ''))
                elif '/' in tname: evs.append((st, 3, tname.lower()))
                elif tname.startswith('Effect_'): evs.append((st, 4, tname))
        lefts = [e[0] for e in evs if e[1] == 1]
        if slot in LOCO and lefts: ph = lefts[0]
        name = slot if aset == 0 else '%s.%s' % (slot, sname)
        clips.append(dict(name=name, file=rec['file'], nf=len(LQ), flags=(1 if loop else 0) | (4 if slot in LOCO else 0), speed=sp, phase=ph, dir=d, LQ=LQ, LP=LP, ev=sorted(evs)))
        by_key[key] = len(clips) - 1; table[sname][si] = by_key[key]
        if slot in MIRROR:
            P2, W2 = mirror(P, W); P2, W2, _ = fix_grip(P2, W2); LQ2, LP2 = to_local(P2, W2)   # (the mirrored gun would sit in the left hand)
            clips.append(dict(name=MIRROR[slot], file=rec['file'], nf=len(LQ2), flags=2, speed=0, phase=0, dir=(0, 0), LQ=LQ2, LP=LP2, ev=sorted(evs)))
            mi = SLOTNAMES.index(MIRROR[slot])
            for sn in table: table[sn][mi] = len(clips) - 1
    print('%-10s %3d clips %5.0fs' % (slot, len(clips), time.time() - t0), flush=True)

# dedupe identical decoded data (different maps for the same file usually decode the same body)
def qz(c): return np.round(c['LQ'] * 32767).astype(np.int16)
sig = {}; remap = {}
for i, c in enumerate(clips):
    k = (c['nf'], qz(c).tobytes(), np.round(c['LP'], 2).astype(np.float32).tobytes(), tuple(c['ev']))
    if k in sig: remap[i] = sig[k]
    else: sig[k] = i
keep = [i for i in range(len(clips)) if i not in remap]
newidx = {old: new for new, old in enumerate(keep)}
for i, j in remap.items(): newidx[i] = newidx[j]
for sn in table: table[sn] = [newidx[c] if c >= 0 else -1 for c in table[sn]]
clips = [clips[i] for i in keep]

# ---- write
def pack_clip(c):
    q = qz(c); nf = c['nf']; rot = []; pos = []; qdata = []; pdata = []
    for b in range(NB):
        tr = q[:, b]
        if np.abs(tr.astype(int) - tr[0].astype(int)).max() <= 1: rot.append(len(qdata)); qdata.append(tr[0])
        else: rot.append(len(qdata) | 0x80000000); qdata.extend(list(tr))
    for b in range(NB):
        tp = c['LP'][:, b]
        if np.abs(tp - tp[0]).max() < 0.02: pos.append(len(pdata)); pdata.append(tp[0])
        else: pos.append(len(pdata) | 0x80000000); pdata.extend(list(tp))
    blob = np.array(qdata, '<i2').tobytes() + np.array(pdata, '<f4').tobytes()
    # positions start after the quaternions: their index counts f32x3 from the start of the position block
    return rot, pos, blob, len(qdata)

path = os.path.join(OUT, 'chr_anim_%s.gab' % G)
hdrs = []; blobs = b''
for c in clips:
    rot, pos, blob, nq = pack_clip(c)
    hdrs.append((c, rot, pos, len(blobs), len(blob), nq)); blobs += blob
    while len(blobs) % 4: blobs += b'\0'
with open(path, 'wb') as f:
    f.write(b'GAB1' + struct.pack('<5I', 1, NB, len(clips), len(SETS), len(SLOTNAMES)))
    for n, p in zip(CANON, PARENT): f.write(n.encode().ljust(32, b'\0') + struct.pack('<i', p))
    for s in SLOTNAMES: f.write(s.encode()[:15].ljust(16, b'\0'))
    for sn, _ in SETS:
        rec = sn.encode().ljust(16, b'\0') + struct.pack('<%dh' % len(SLOTNAMES), *table[sn])
        while len(rec) % 4: rec += b'\0'
        f.write(rec)
    base = 0
    for c, rot, pos, off, ln, nq in hdrs:
        f.write(c['name'].encode()[:31].ljust(32, b'\0') + struct.pack('<HHfff2fIII', c['nf'], c['flags'], 30.0, c['speed'], c['phase'], c['dir'][0], c['dir'][1], off, ln, nq))
        f.write(struct.pack('<%dI' % NB, *rot)); f.write(struct.pack('<%dI' % NB, *pos))
        # the position block of a clip starts at off + 8 * nq
    f.write(blobs)
    strs = b''; soff = {}; evrec = []
    for ci, c in enumerate(clips):
        for (t, kind, name) in c['ev']:
            if name not in soff: soff[name] = len(strs); strs += name.encode() + b'\0'
            evrec.append(struct.pack('<fHBBHH', t, ci, kind, 0, soff[name], 0))
    f.write(struct.pack('<I', len(evrec)) + b''.join(evrec))
    while len(strs) % 4: strs += b'\0'
    f.write(struct.pack('<I', len(strs)) + strs)
sz = os.path.getsize(path)
print('wrote', path, sz, 'bytes;', len(clips), 'clips,', sum(c['nf'] for c in clips), 'frames,', len(SETS), 'sets,', len(SLOTNAMES), 'slots')
for r in report: print('  note:', r)
with open(os.path.join(OUT, 'chr_anim_%s.txt' % G), 'w') as f:
    for i, c in enumerate(clips): f.write('%3d %-24s nf %3d flags %d speed %5.0f phase %.2f dir %5.2f %5.2f  %s %s\n' % (i, c['name'], c['nf'], c['flags'], c['speed'], c['phase'], c['dir'][0], c['dir'][1], c['file'], ' '.join('%.2f:%d%s' % (t, k, (':' + n) if n else '') for t, k, n in c['ev'])))
    for sn, _ in SETS: f.write('set %-9s %s\n' % (sn, ' '.join('%s=%d' % (s, k) for s, k in zip(SLOTNAMES, table[sn]))))
