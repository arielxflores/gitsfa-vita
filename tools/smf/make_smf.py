# b87 (user: "please make sure you can move around and traverse the game like you can in the pc version ... ladders that looked
# like they could be climbed"): the maps' special maneuvers (map.dat Fix_<Map>\Specialmaneuver.smf) -> smf.txt for player.h.
# Each one is an authored traversal: a floor zone (4 corners) in front of an obstacle and the path the body takes from it -
# vaults over cover and through windows (~1.3 m up, ~4 m across), climbs onto ledges / containers (up to ~1.8 m, ~0.5 m
# across), and leaps from upper walkways over a railing down to the floor below (up to ~3 m up then ~7 m down).
# .smf: u16 0x11fc, u16 3, u32 count, then count records; a record's path = 5 points (p0 p1 p2 p3 p0) followed by 0x80000000,
# its zone corners 240 bytes before the path, the facing yaw 96 bytes before it (records vary in length: the paths are found
# by that pattern, and their number is checked against the count).
# smf.txt: one line per maneuver: map x0 y0 x1 y1 x2 y2 x3 y3 zone_z p0x p0y p0z p1x p1y p1z p2x p2y p2z p3x p3y p3z, then the
# obstacle's face (4 corners x y z, 172 bytes after the path's start): for a climb (a wall, a ladder, a container) the path
# only reaches the first hold (~1.8 m up) and the body goes on up the face to its top and over (player.h); for a vault
# the face is the cover / window frame the path passes
# usage: make_smf.py <map root with Fix_<Map>/Specialmaneuver.smf> <out dir>
import sys, os, glob, struct, numpy as np
def scan(path):
    d = open(path, 'rb').read(); out = []
    if len(d) < 8: return out, 0
    magic, ver, n = struct.unpack_from('<HHI', d, 0)
    for o in range(8, len(d) - 64, 4):
        P = np.array(struct.unpack_from('<15f', d, o)).reshape(5, 3)
        if not np.all(np.isfinite(P)) or np.abs(P).max() > 1e6: continue
        if np.allclose(P[0], P[4]) and np.abs(P[0]).sum() > 1 and not np.allclose(P[0], P[1]) and np.linalg.norm(P[3] - P[0]) > 10 \
           and struct.unpack_from('<I', d, o + 60)[0] == 0x80000000 and o >= 240:
            Z = np.array(struct.unpack_from('<12f', d, o - 240)).reshape(4, 3)
            F = np.array(struct.unpack_from('<12f', d, o + 172)).reshape(4, 3)   # the obstacle's face (vertical quad): a wall / ladder's top, a window's frame
            if np.all(np.isfinite(Z)) and np.linalg.norm(Z[:, :2].mean(0) - P[0, :2]) < 300 and np.all(np.isfinite(F)): out.append((Z, P[:4], F))
    return out, n
if __name__ == '__main__':
    root, out = sys.argv[1], sys.argv[2]; lines = []
    for f in sorted(glob.glob(os.path.join(root, 'Fix_*', 'Specialmaneuver.smf'))):
        stem = os.path.basename(os.path.dirname(f))[4:].lower(); R, n = scan(f)
        for Z, P, F in R: lines.append(' '.join([stem] + ['%.1f' % v for v in Z[:, :2].ravel()] + ['%.1f' % Z[:, 2].mean()] + ['%.1f' % v for v in P.ravel()] + ['%.1f' % v for v in F.ravel()]))
        print('%-14s %2d of %2d' % (stem, len(R), n))
    open(os.path.join(out, 'smf.txt'), 'w').write('# player.h: the maps\' special maneuvers (tools/smf/make_smf.py)\n' + '\n'.join(lines) + '\n')
    print(len(lines), 'maneuvers')
