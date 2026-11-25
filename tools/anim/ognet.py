"""The out-of-game animation networks (GIS_base_3rd_OutGame.nmb / GIS_female_3rd_OutGame.nmb: the operator-select
'choice' poses, the lobby weapon idles, the results-screen win / lose sets). Same bundle layout as the in-game networks
(nmb.py, our own reading of the file); these networks have no usable node-name table for nmb.Network, so the source
records are grouped by their clip files instead: records come in runs of one per animation set, one run per source node."""
import re, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nmb

class OGNet:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read(); self.path = path
        O = self.objs = nmb.objects(d)
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
        net = [o for o in O if o['type'] == 10][0]
        blob = d[net['data']:net['data'] + net['size']]
        recs = []
        for m in re.finditer(re.escape(b'\x00\x00\x00\x00\xff\xff\x15\x00'), blob):
            b = net['data'] + m.start()
            aid, mg, fl = struct.unpack_from('<IIB', d, b + 0x60)
            f0, f1, dur = struct.unpack_from('<3f', d, b + 0x6c)
            recs.append(dict(aid=aid, file=self.files[aid], map=struct.pack('<I', mg).hex(), flags=fl, start=f0, end=f1, dur=dur))
        from rig import rigs, add_parents
        self.rigs = nmb.fix_rigs(path, add_parents(path, rigs(path)))
        self.nsets = len(self.rigs)
        if len(recs) % self.nsets: self.nsets += 1   # (the female network: 69 sets over 68 rig objects; set j < 68 uses rig j)
        assert len(recs) % self.nsets == 0, (len(recs), self.nsets)
        self.groups = [recs[k * self.nsets:(k + 1) * self.nsets] for k in range(len(recs) // self.nsets)]
        # events (the same 0x17 blocks as nmb._attach_events)
        self.source = {k: g for k, g in enumerate(self.groups)}
        nmb._attach_events(self)

    def mapof(self, r): return self.maps[r['map']]
    def group_of(self, pattern):
        """the run whose most common clip file matches the regex pattern (case-insensitive), or None"""
        rx = re.compile(pattern, re.I)
        for g in self.groups:
            fs = {}
            for r in g: fs[r['file']] = fs.get(r['file'], 0) + 1
            top = max(fs, key=fs.get)
            if rx.search(top): return g
        return None

if __name__ == '__main__':
    N = OGNet(sys.argv[1])
    print(len(N.files), 'clips,', N.nsets, 'sets,', len(N.groups), 'record runs')
    for k, g in enumerate(N.groups):
        fs = {}
        for r in g: fs[r['file']] = fs.get(r['file'], 0) + 1
        top = max(fs, key=fs.get); ev = [(t, n, len(e)) for t, n, e in g[0].get('events', [])]
        print('%2d %-48s x%-2d (%d files) dur %.2f ev %s' % (k, top[:48], fs[top], len(fs), g[0]['dur'], ev[:6]))
