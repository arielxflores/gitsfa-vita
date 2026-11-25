# reclip.py (b105): put freshly decoded clips of a weapon into an EXISTING vm_<..>.gvm whose model cannot be rebuilt here
# (vm_knife_cy.gvm: the Ghost Assault cyborg's arms, model/Cyborg/Cyborg/cy_male.nif is only on the PC). The clips are
# baked for the target's own bone list exactly as gvmlib's builder bakes them (camera space: q' = RC q, p' = RC (p - eye));
# every other byte of the file is kept. A clip that is not named is compared with the fresh decode and reported, so a
# target built from different clips is noticed.
# usage: python3 reclip.py <short of the clips' weapon> <target.gvm> <scratch dir> <clip> [clip ...]
import sys, os, struct, numpy as np
H_ = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, H_)
short, target, scratch, clips = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
sys.argv = ['make_vm_weapon.py', short, scratch]
import make_vm_weapon as M
from gvmlib import qmat, mat2q
os.makedirs(scratch, exist_ok=True); M.build(short)
d = bytearray(open(target, 'rb').read()); assert d[:4] == b'GVM1'; o = 4
nt, nb, npal, nm, nc = struct.unpack_from('<5I', d, o); o += 20
for i in range(nt):
    w, h, fmt, nmp = struct.unpack_from('<HHBB', d, o); o += 6
    for m in range(nmp): sz, = struct.unpack_from('<I', d, o); o += 4 + sz
bones = [d[o + 32 * i:o + 32 * i + 32].split(b'\0')[0].decode() for i in range(nb)]; o += 32 * nb + 52 * npal
for i in range(nm):
    tex, bl, nv, ni = struct.unpack_from('<iiII', d, o); o += 16 + nv * 32 + ni * 2 + (2 if ni % 2 else 0)
def bake(name):
    fr, loop, RN, cam = M.LASTFR[name]; idx = [RN.index(b) for b in bones]; data = np.zeros((len(fr), nb, 7), 'f4')
    for i, (P, W) in enumerate(fr):
        for s, b in enumerate(idx): data[i, s, :4] = mat2q(M.RC @ qmat(W[b])); data[i, s, 4:] = M.RC @ (P[b] - cam)
    for s in range(nb):
        for i in range(1, len(fr)):
            if np.dot(data[i, s, :4], data[i - 1, s, :4]) < 0: data[i, s, :4] *= -1
    return data
done = []
for i in range(nc):
    name = d[o:o + 16].split(b'\0')[0].decode(); nf, loop, fps = struct.unpack_from('<IIf', d, o + 16); o += 28
    old = np.frombuffer(bytes(d[o:o + nf * nb * 28]), '<f4').reshape(nf, nb, 7)
    if name in M.LASTFR:
        new = bake(name)
        if name in clips:
            assert new.shape == old.shape, (name, new.shape, old.shape); d[o:o + nf * nb * 28] = new.astype('<f4').tobytes(); done.append(name)
            print('  %-10s replaced (%d frames; moved up to %.1f cm)' % (name, nf, float(np.abs(new[:, :, 4:] - old[:, :, 4:]).max())))
        else: print('  %-10s kept; differs from the fresh decode by %.4f' % (name, float(np.abs(new - old).max()) if new.shape == old.shape else -1))
    o += nf * nb * 28
assert sorted(done) == sorted(clips), ('not found', set(clips) - set(done))
open(target, 'wb').write(d); print(target, 'clips replaced:', done)
