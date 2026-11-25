# lists clips whose Grip bone (the weapon) sits far from the right hand: python3 gripscan.py bank.gab
import sys, os, struct, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); from gabinfo import load
path = sys.argv[1]; G = load(path); d = open(path, 'rb').read()
nb = len(G['bones']); names = [b[0] for b in G['bones']]; gi = names.index('Grip')
o = 24 + 36 * nb + 16 * len(G['slots']); o += ((16 + 2 * len(G['slots']) + 3) & ~3) * len(G['sets'])
hsz = 68 + 8 * nb; data = o + hsz * len(G['clips'])
for ci, C in enumerate(G['clips']):
    h = o + hsz * ci; nf, = struct.unpack_from('<H', d, h + 32); off, ln, nq = struct.unpack_from('<3I', d, h + 56)
    t = struct.unpack_from('<I', d, h + 68 + 4 * nb + 4 * gi)[0]; pi = t & 0x7fffffff
    P = np.array([struct.unpack_from('<3f', d, data + off + 8 * nq + 12 * (pi + (k if t >> 31 else 0))) for k in range(nf)])
    L = np.linalg.norm(P, axis=1)
    if L.max() > 15: print('%3d %-22s grip %s, distance from the hand %.0f..%.0f cm' % (ci, C['name'], 'animated' if t >> 31 else 'static', L.min(), L.max()))
