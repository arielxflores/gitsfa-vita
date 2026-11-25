# render b45, tools/shbank: merges the shbank_src.bin files of the host game's start-ups (enum.sh) into one list, one entry
# per key (shcache.h shb_key: FNV-1a 64 over 'V' / 'F' and the source), sorted by key.
#   merge.py <out src.bin> <out keys.txt> <in src.bin>...
import sys, struct
def key(kind, src):
    h = 14695981039346656037
    for b in bytes([kind]) + src: h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h
def read(path):
    d = open(path, 'rb').read(); o = 0
    while o + 5 <= len(d):
        k = d[o]; n = struct.unpack_from('<I', d, o + 1)[0]; s = d[o + 5:o + 5 + n]; o += 5 + n
        if len(s) == n: yield k, s
out, keys, ins = sys.argv[1], sys.argv[2], sys.argv[3:]
allp = {}
for p in ins:
    for k, s in read(p): allp[key(k, s)] = (k, s)
with open(out, 'wb') as f:
    for kk in sorted(allp): k, s = allp[kk]; f.write(bytes([k]) + struct.pack('<I', len(s)) + s)
with open(keys, 'w') as f:
    for kk in sorted(allp): k, s = allp[kk]; f.write('%016x %s %d %s\n' % (kk, chr(k), len(s), s[:60].decode(errors='replace').replace('\n', ' ')))
nv = sum(1 for k, s in allp.values() if k == ord('V'))
print('shbank: %d sources (%d vertex, %d fragment) from %d start-ups' % (len(allp), nv, len(allp) - nv, len(ins)))
