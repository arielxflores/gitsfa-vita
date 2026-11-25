# render b45, tools/shbank: packs the binaries the Vita's compiler made (gen.sh: ux0:data/gitsfa/shbank_gxp.bin, records of
# u64 key, u32 length, GXP) into the bank the engine reads (shcache.h): "SHB1", u32 n, u32 0, u32 0; n x { u64 key,
# u32 offset, u32 length } sorted by key (length 0 = rejected by the compiler); the binaries, each at a 16-byte offset.
#   pack.py <gxp.bin> <keys.txt (enum.sh)> <out shbank.bin>     fails when a key of keys.txt has no record
import sys, struct
gx, keys, out = sys.argv[1:4]
d = open(gx, 'rb').read(); o = 0; rec = {}
while o + 12 <= len(d):
    k, n = struct.unpack_from('<QI', d, o); rec[k] = d[o + 12:o + 12 + n]; o += 12 + n
want = [int(l.split()[0], 16) for l in open(keys)]
miss = [k for k in want if k not in rec]
if miss: print('shbank pack: %d of %d sources have no compiled binary (first: %016x): run gen.sh again' % (len(miss), len(want), miss[0])); sys.exit(1)
ks = sorted(want); idx = b''; blob = b''
for k in ks:
    b = rec[k]; off = len(blob); idx += struct.pack('<QII', k, off, len(b)); blob += b + b'\0' * (-len(b) % 16)
open(out, 'wb').write(b'SHB1' + struct.pack('<III', len(ks), 0, 0) + idx + blob)
rej = sum(1 for k in ks if not rec[k])
print('shbank: %d programs (%d rejected by the compiler), %d bytes of GXP, bank %d bytes -> %s' % (len(ks), rej, sum(len(rec[k]) for k in ks), 16 + len(idx) + len(blob), out))
