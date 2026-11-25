"""Reader for the parts of the game's morpheme network binaries (GIS_*.nmb) that tell which clip each animation node
plays (our own analysis of the file layout, no NaturalMotion code):

  bundle objects   [u32 0x18][u32 6][u32 type][16-byte guid][u32][u32 size][u32 align] + data
                   type 1: rigs (one per animation set = one per weapon), type 2: rig-to-animation channel maps,
                   type 10: the network definition, type 13: the animation library (clip file names, index = anim id)
  node names       table "mainNetwork", "Live|Loco_UpperBody|Forward|base_M4A1_3rd_stand_run_forward", ... (id -> name;
                   a state and its root node share an id: the deepest path names the node)
  source records   in the network definition, one 0xA0-byte attribute block per (animation source node, animation set),
                   marked 00000000 ffff 1500; at +0x60: u32 anim id, u32 map guid (first 4 bytes, little endian), u8 flags,
                   at +0x6c: f32 clip start fraction, f32 clip end fraction, f32 clip duration (s).
                   Blocks come in runs of n_sets (70 in the 3rd-person networks), in node-id order of the source nodes.
  animation set j uses rig j (checked: every record's map has as many rig bones as rig j).
"""
import struct, re, sys, collections
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))

def objects(d):
    o = 0; out = []
    while o + 40 <= len(d):
        a, b, typ = struct.unpack_from('<III', d, o)
        guid = d[o + 12:o + 28]; size, align = struct.unpack_from('<II', d, o + 32)
        data = o + 40; data = (data + align - 1) // align * align if align else data
        out.append(dict(off=o, type=typ, guid=guid.hex(), size=size, data=data))
        o = (data + size + 3) // 4 * 4
    return out

def node_names(d):
    base = d.find(b'mainNetwork\x00')
    for pad in range(0, 16):
        end = base - pad
        if end % 4: continue
        for n in range(100, 5000):
            st = end - 4 * n
            if struct.unpack_from('<I', d, st)[0] != 0: continue
            offs = struct.unpack_from('<%dI' % n, d, st)
            if all(offs[k] < offs[k + 1] for k in range(n - 1)) and d[base + offs[-1] - 1] == 0:
                ids = struct.unpack_from('<%dI' % n, d, st - 4 * n)
                names = [d[base + o:d.index(b'\x00', base + o)].decode('latin1') for o in offs]
                return list(zip(ids, names))
    raise ValueError('no node name table')

def fix_rigs(path, R):
    """rig.rigs() reads the bind-pose pointers at a fixed offset, which assumes a 2-word bone mask (<= 64 bones); rigs with
    more bones (M249) carry a longer mask -> re-read their bind pose (same fix as vm/vmw.py)"""
    import numpy as np
    d = open(path, 'rb').read()
    for r in R:
        n = len(r['names'])
        if n <= 64 or r.get('base') is None: continue
        nw = (n + 31) // 32; po, qo = struct.unpack_from('<II', d, r['base'] + 0x48 + 4 * (nw - 2))
        r['pos'] = np.frombuffer(d, '<f4', n * 4, r['base'] + po).reshape(-1, 4)[:, :3]
        r['quat'] = np.frombuffer(d, '<f4', n * 4, r['base'] + qo).reshape(-1, 4)
    return R

OPS = re.compile(r'^(PlaySpeedModifier|Blend|FeatherBlend|Filter_|PassThrough|MirrorTransforms|SoftKeyFrame|OperatorSmooth|Ragdoll|ActiveState|HeadLook|GunAimIK|HipsIK|Constant|SoftKey$|Animation$|Ready$)')
ANIMLIKE = re.compile(r'(_3rd_|_idle|_skill|Kra_|_Action|CQC|_Key$)')

class Network:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        self.path = path
        O = self.objs = objects(d)
        self.files = [m.group().decode() for m in re.finditer(rb'[A-Za-z0-9_]+\.qsa', d)]
        self.maps = {}
        for o in O:
            if o['type'] != 2: continue
            raw = d[o['data']:o['data'] + o['size']]
            k = raw.find(struct.pack('<H', 14) + b'\0\0' + struct.pack('<H', 0))
            n, n2 = struct.unpack_from('<HH', raw, k - 4); nch = struct.unpack_from('<I', raw, k - 8)[0]
            nrig = struct.unpack_from('<I', raw, 0x14)[0]
            lst = list(struct.unpack_from('<%dH' % (max(n, n2) + 1), raw, k + 4))
            self.maps[o['guid'][:8]] = dict(nrig=nrig, nch=nch, n=n, n2=n2, list=lst[:n + 1], extra=lst[n + 1:])
        names = {}
        allnames = set()
        for i, n in node_names(d):
            allnames.add(n)
            if i not in names or n.count('|') > names[i].count('|'): names[i] = n
        self.names = names
        src = []
        for i in sorted(names):
            n = names[i]
            if '|' not in n or n.startswith('ControlParameters'): continue
            leaf = n.split('|')[-1]
            if OPS.match(leaf) or leaf.endswith('_OnGround') or not ANIMLIKE.search(leaf): continue
            if any(m.startswith(n + '|') for m in allnames): continue
            src.append(i)
        net = [o for o in O if o['type'] == 10][0]
        recs = []
        blob = d[net['data']:net['data'] + net['size']]
        for m in re.finditer(re.escape(b'\x00\x00\x00\x00\xff\xff\x15\x00'), blob):
            b = net['data'] + m.start()
            aid, mg, fl = struct.unpack_from('<IIB', d, b + 0x60)
            f0, f1, dur = struct.unpack_from('<3f', d, b + 0x6c)
            recs.append(dict(aid=aid, file=self.files[aid], map=struct.pack('<I', mg).hex(), flags=fl, start=f0, end=f1, dur=dur))
        from rig import rigs, add_parents
        self.rigs = fix_rigs(path, add_parents(path, rigs(path)))
        self.nsets = len(self.rigs)
        assert len(recs) % self.nsets == 0
        ngroups = len(recs) // self.nsets
        # the source-node list from the names may hold a few extra (non-source) entries at the end (results-screen states)
        self.source = {}
        for k in range(ngroups):
            self.source[src[k]] = recs[k * self.nsets:(k + 1) * self.nsets]
        self.nsrc = ngroups
        # sanity: the leaf name of each node should match its first record's file name (first 10 characters)
        self.mismatch = [(i, names[i], self.source[i][0]['file']) for i in self.source
                         if names[i].split('|')[-1][:6].lower() != self.source[i][0]['file'][:6].lower()]

    def find(self, sub):
        """node ids whose name ends with sub (a tuple: alternatives, the first that exists)"""
        for s in (sub if isinstance(sub, tuple) else (sub,)):
            r = [i for i in self.source if self.names[i].endswith(s)]
            if r: return r
        return []

    def rec(self, node, aset):
        return self.source[node][aset]

    def mapof(self, r):
        return self.maps[r['map']]

if __name__ == '__main__':
    N = Network(sys.argv[1])
    print(len(N.files), 'clips,', len(N.maps), 'maps,', N.nsets, 'animation sets,', N.nsrc, 'source nodes')
    print('name/file mismatches:', len(N.mismatch))
    for m in N.mismatch[:40]: print('  ', m)

def _attach_events(N):
    """event tracks of every source record: the 0x17 attribute block that follows each 0x15 block lists the guids of its
    discrete (type 3) and duration (type 4) event tracks; a track = name + events (start fraction, duration fraction, user data)"""
    d = N.d
    objs = {o['guid'][:8]: o for o in N.objs}
    net = [o for o in N.objs if o['type'] == 10][0]
    blob = d[net['data']:net['data'] + net['size']]
    offs = [net['data'] + m.start() for m in re.finditer(re.escape(b'\x00\x00\x00\x00\xff\xff\x15\x00'), blob)]
    cache = {}
    def track(g):
        if g in cache: return cache[g]
        o = objs[g]; p = o['data']; n = struct.unpack_from('<I', d, p)[0]
        nameoff = struct.unpack_from('<I', d, p + 8)[0]; evoff = struct.unpack_from('<I', d, p + 0x14)[0]
        name = d[p + nameoff:d.index(b'\0', p + nameoff)].decode('latin1')
        ev = [struct.unpack_from('<ffI', d, p + evoff + 12 * i) for i in range(n)]
        cache[g] = (o['type'], name, ev); return cache[g]
    k = 0
    for nid in N.source:
        for rec in N.source[nid]:
            b = offs[k]; k += 1
            e = d.find(b'\x00\x00\x00\x00\xff\xff\x17\x00', b + 8, b + 0x200)
            nxt = d.find(b'\x00\x00\x00\x00\xff\xff\x15\x00', b + 8, b + 0x400)
            tr = []
            if e >= 0 and (nxt < 0 or e < nxt):
                for q in range(e + 8, e + 0x80, 4):
                    g = d[q:q + 4].hex()
                    if g in objs and objs[g]['type'] in (3, 4): tr.append(track(g))
            rec['events'] = tr
Network.events = _attach_events
