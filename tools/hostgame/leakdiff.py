#!/usr/bin/env python3
# leakdiff.py <binary> <snap A> <snap B> [n]: what grew between two leak-tracker snapshots (build.sh leaks, hg_snapshot):
# live heap blocks, GL textures and buffers grouped by allocation stack, the stacks resolved with addr2line.
# Heap blocks whose stack starts in hostshim.c are the GL shim's own texture / buffer memory (vitaGL's pools on the Vita).
import subprocess, sys, collections
def load(p):
    d = {}
    for l in open(p):
        f = l.split()
        if len(f) < 5: continue
        d[tuple(f[5:])] = [int(f[1]), int(f[2]), int(f[3]), int(f[4])]
    return d
binary, a, b = sys.argv[1], load(sys.argv[2]), load(sys.argv[3]); n = int(sys.argv[4]) if len(sys.argv) > 4 else 25
cache = {}
def where(pc):
    if pc not in cache:
        r = subprocess.run(['addr2line', '-f', '-i', '-e', binary, hex(int(pc, 16) - 1)], capture_output=True, text=True).stdout.split('\n')
        fr = []
        for i in range(0, len(r) - 1, 2): fr.append('%s %s' % (r[i], r[i + 1].split('/')[-1].split(' ')[0]))
        cache[pc] = fr
    return cache[pc]
rows = []
for k in set(a) | set(b):
    x, y = a.get(k, [0, 0, 0, 0]), b.get(k, [0, 0, 0, 0])
    d = [y[i] - x[i] for i in range(4)]
    if any(d): rows.append((d, k, y))
tot = [sum(r[0][i] for r in rows) for i in range(4)]
def gl(k): return len(k) > 1 and any('hostshim.c' in w for w in where(k[1]))   # (frame 0 is the malloc wrapper; a GL shim function next: texture / buffer memory)
heap_game = sum(r[0][1] for r in rows if not gl(r[1])); heap_gl = sum(r[0][1] for r in rows if gl(r[1]))
print('change: %+d blocks, %+d bytes (game heap %+d, GL shim memory %+d), %+d textures, %+d buffers' % (tot[0], tot[1], heap_game, heap_gl, tot[2], tot[3]))
if '-q' in sys.argv: sys.exit(0)
rows.sort(key=lambda r: (-abs(r[0][1]) - 100000 * (abs(r[0][2]) + abs(r[0][3]))))
for d, k, y in rows[:n]:
    print('%+6d blocks %+10d bytes %+4d tex %+4d buf  (now %d blocks, %d bytes)' % (d[0], d[1], d[2], d[3], y[0], y[1]))
    shown = 0
    for pc in k:
        for w in where(pc):
            if shown < 7: print('      ' + w); shown += 1
