# Shooting Range / tutorial target data for the engine (engine/src/range.h): build/data/<stem>.grange
#
# Source (world.dat, plain files): world\TutorialStep.txt lists "events"; each names a target file and its target groups:
#   [EVENT] 0  [EVENTFILE] TutShootBot.tsd   [LOOP] 1 [RESET_TIME] 1.5  one group with every target  -> the Shooting Range
#   [EVENT] 1  [EVENTFILE] TutShootBot1.tsd  groups 0..9 shown one after another                     -> the tutorial's shooting lesson
# (TutShootBot2.tsd is a byte-identical copy of TutShootBot.tsd.)  Which map uses which event was found by standing the
# targets on the maps' geometry: event 0's 17 targets all stand on the render mesh of Fix_Shooting (14 within 1 cm,
# 3 within 15 cm; they are outside the player collision), event 1's 18 targets all stand on Fix_Tutorial3's collision floor.
#
# .tsd (little endian): u32 magic 0x013378F1, u32 count, count x 112 bytes:
#   i32 type (0 = stands at waypoint 0, 1 = walks along the 4 waypoints), i32 id, f32 a, i32 b,
#   4 x { f32 x, y, z (cm, z up), f32 rx, ry, rz (radians) }
#   a: seconds to walk the whole path (1012: 1988 cm in 10 s = 199 cm/s, the walk speed); 3.0 on standing targets (unused)
#   b: 200 on every target (meaning unknown; kept in the file)
#   rz: the game's heading; the engine's yaw (facing = (cos yaw, sin yaw)) is -rz: every target then faces the
#   player's area within 16 degrees. Standing targets keep editor defaults (+50 cm steps) in waypoints 1..3.
#
# .grange (little endian), "GRG1":
#   char magic[4]; u32 version (1); u32 flags (bit 0 = loop: every group is up at once and a destroyed target comes
#   back reset_s later; otherwise the groups play in order, the next one reset_s after the last target of the current one
#   is destroyed); f32 reset_s; f32 hp (health of one target); u32 event; u32 ntarget; u32 ngroup;
#   ntarget x 84 bytes: i32 id, i32 type, f32 travel_s, i32 tsd_b, i32 nwp, 4 x f32[4] { x, y, z, yaw }
#   ngroup x { u32 n; char cue[48]; i32 target_index[n] }   cue: voice line started when the group comes up ("" = none)
#
# Usage: python3 make_grange.py [srcdir=/root/gits/raw/world] [outdir=/root/gits/build/data]
import struct, sys, os, re, math

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
SRC = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('WORLD')
OUT = sys.argv[2] if len(sys.argv) > 2 else gitspaths.root('DATA')
HP = 100.0          # the dummies' health: two M4A1 head hits kill one in the footage (2 rounds per kill, 35.33 s / 35.40 s)
MAPS = {0: ['shooting'], 1: ['tutorial3']}
# The lesson's voice lines are in the game (sh_tut_operator_*); which group starts which line is not in the data.
# Best guess from what each group asks for (unverified, no footage of the lesson): close standing targets = hip fire,
# far = aim, the reused target 2000 = switch to the sidearm, the tight close pair = grenade, 59 m = sniper rifle.
CUES = {1: {0: 'sh_tut_operator_hip_firing', 2: 'sh_tut_operator_precision_firing', 5: 'sh_tut_operator_secondary',
            6: 'sh_tut_operator_grenade', 7: 'sh_tut_operator_sniper_rifle'}}

def read_tsd(path):
    d = open(path, 'rb').read()
    magic, n = struct.unpack_from('<II', d, 0)
    assert magic == 0x013378F1 and len(d) == 8 + 112 * n, path
    out = []
    for i in range(n):
        o = 8 + 112 * i
        typ, rid, a, b = struct.unpack_from('<iifi', d, o)
        wp = [struct.unpack_from('<6f', d, o + 16 + 24 * k) for k in range(4)]
        out.append(dict(type=typ, id=rid, a=a, b=b, wp=wp))
    return out

def read_steps(path):
    """TutorialStep.txt -> list of events {event, file, loop, reset, groups: [[ids]]} (tolerates the file's '[[/TARGET_GROUP]' typos)"""
    ev = []; cur = None; grp = None
    for line in open(path, encoding='latin-1'):
        line = line.strip()
        m = re.match(r'\[EVENT\]\s*(\d+)', line)
        if m: cur = dict(event=int(m.group(1)), file=None, loop=0, reset=1.5, groups=[]); ev.append(cur); continue
        if cur is None: continue
        m = re.match(r'\[EVENTFILE\]\s*(\S+)', line)
        if m: cur['file'] = m.group(1); continue
        m = re.match(r'\[LOOP\]\s*(\d+)', line)
        if m: cur['loop'] = int(m.group(1)); continue
        m = re.match(r'\[RESET_TIME\]\s*([\d.]+)', line)
        if m: cur['reset'] = float(m.group(1)); continue
        m = re.match(r'\[TARGET_GROUP\]\s*(\d+)', line)
        if m: grp = []; cur['groups'].append(grp); continue
        m = re.match(r'\[ID\]\s*(\d+)', line)
        if m and grp is not None: grp.append(int(m.group(1)))
    return ev

def wrap(a): return (a + math.pi) % (2 * math.pi) - math.pi

def build(e, tsd):
    recs = []
    for r in tsd:
        n = 4 if r['type'] == 1 else 1
        wp = [(w[0], w[1], w[2], wrap(-w[5])) for w in r['wp']]
        recs.append((r['id'], r['type'], r['a'], r['b'], n, wp))
    idx = {r[0]: i for i, r in enumerate(recs)}
    groups = []
    for gi, g in enumerate(e['groups']):
        ids = [i for i in g if i in idx]
        missing = [i for i in g if i not in idx]
        if missing: print('  event %d group %d: ids %s are not in %s (left out)' % (e['event'], gi, missing, e['file']))
        groups.append(([idx[i] for i in ids], CUES.get(e['event'], {}).get(gi, '')))
    b = bytearray(b'GRG1')
    b += struct.pack('<IIffIII', 1, 1 if e['loop'] else 0, e['reset'], HP, e['event'], len(recs), len(groups))
    for rid, typ, a, tb, n, wp in recs:
        b += struct.pack('<iifii', rid, typ, a, tb, n)
        for w in wp: b += struct.pack('<4f', *w)
    for ix, cue in groups:
        b += struct.pack('<I', len(ix)) + cue.encode()[:47].ljust(48, b'\0') + struct.pack('<%di' % len(ix), *ix)
    return bytes(b), recs, groups

if __name__ == '__main__':
    events = read_steps(os.path.join(SRC, 'TutorialStep.txt'))
    for e in events:
        tsd = read_tsd(os.path.join(SRC, e['file']))
        data, recs, groups = build(e, tsd)
        for stem in MAPS.get(e['event'], []):
            p = os.path.join(OUT, stem + '.grange'); open(p, 'wb').write(data)
            print('%s: event %d (%s), %s, reset %.1f s, %d targets (%d walking), %d groups, %d bytes' % (p, e['event'], e['file'],
                  'loop' if e['loop'] else 'groups in order', e['reset'], len(recs), sum(r[1] == 1 for r in recs), len(groups), len(data)))
            for gi, (ix, cue) in enumerate(groups):
                print('   group %d: %s%s' % (gi, ' '.join(str(recs[i][0]) for i in ix), ('   cue ' + cue) if cue else ''))
