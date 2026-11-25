# dump a GITS:FA tutorial/shooting-range target file (world\TutShootBot*.tsd)
# layout (little endian): u32 magic 0x013378F1, u32 count, then count x 112-byte records:
#   i32 type (0 = stands still at waypoint 0, 1 = moves along the 4 waypoints), i32 id, f32 a, i32 b,
#   4 x { f32 x, y, z (cm, z up), f32 rx, ry, rz (radians; rz = yaw) }
import struct, sys
def read_tsd(path):
    d = open(path, 'rb').read()
    magic, n = struct.unpack_from('<II', d, 0)
    out = []
    for i in range(n):
        o = 8 + i * 112
        typ, rid, a, b = struct.unpack_from('<iifi', d, o)
        wps = [struct.unpack_from('<6f', d, o + 16 + 24 * k) for k in range(4)]
        out.append(dict(type=typ, id=rid, a=a, b=b, wp=wps))
    return magic, out
if __name__ == '__main__':
    for fn in sys.argv[1:]:
        magic, recs = read_tsd(fn)
        print('%s: magic %08X, %d targets' % (fn, magic, len(recs)))
        for r in recs:
            print('  id %d type %d a %.2f b %d' % (r['id'], r['type'], r['a'], r['b']))
            for w in (r['wp'] if r['type'] else r['wp'][:1]):
                print('     pos %9.2f %9.2f %8.2f  rot %6.3f %6.3f %6.3f' % w)
