#!/usr/bin/env python3
"""Per-program cost features of PS Vita GXP binaries (render b38, GPU cost model).

For each shader-cache entry (<key>.bin: 24-byte SHC header + GXP) and its USSE disassembly (<key>.bin.dsm from gxpdsm):
  instr   primary-program instructions (after PHAS: what runs per fragment / vertex; NOP and PHAS not counted)
  cyc     estimated ALU cycles: F32 vector ops take a cycle per 2 components (USSE2), F16 ones per 4, the dual (..2) and pack ops 1, NOP 1
  smp     dependent texture reads in the primary program (SMP* instructions: the LUT reads, computed coordinates)
  nondep  non-dependent texture reads (texture units flagged 0x1 in the header: fetched by the PDS before the shader)
  discard the program can discard (alpha test), from the program flags
  frag    fragment (1) or vertex (0) program
  br      b81: USSE branches (BR) in the primary program. Vita3K's disassembly does not print them (its control-flow
          analysis consumes them), so they are found in the code words with the full BR pattern of Vita3K's decoder
          (usse_translator_entry.cpp "11111ppps000e-----wynba00r..."); a looser "opcode 11111, op 000" test also matches
          every NOP (the Burst Limit chat's gxpbr.py does that). With branches, instr / cyc count both paths.
  kill    b81: KILL instructions (discard) in the primary program
usage: gxpcost.py <cache dir> <dsm dir> > costs.json
"""
import sys, os, json, struct


def _pat(p):
    m = v = 0
    for i, c in enumerate(p):
        if c in '01': m |= 1 << (63 - i); v |= (c == '1') << (63 - i)
    return m, v
BR_PAT = _pat('11111ppps000e-----wynba00r----------------iloooooooooooooooooooo')     # Vita3K usse_translator_entry.cpp
KILL_PAT = _pat('11111001--11000000000pp0000001101111----------------------------')


def branches(g):
    """(BR count, KILL count) in a GXP's primary program (b81)"""
    n, = struct.unpack_from('<I', g, 60); off, = struct.unpack_from('<I', g, 64)
    code = g[64 + off: 64 + off + 8 * n]; code = code[:len(code) // 8 * 8]
    w = struct.unpack('<%dQ' % (len(code) // 8), code)
    return sum(1 for x in w if x & BR_PAT[0] == BR_PAT[1]), sum(1 for x in w if x & KILL_PAT[0] == KILL_PAT[1])


def gxp_of(path):
    b = open(path, 'rb').read(); o = b.find(b'GXP\0')
    if o < 0: return None
    sz = struct.unpack_from('<I', b, o + 8)[0]
    return b[o:o + sz]


def features(g, dsm):
    flags = struct.unpack_from('<I', g, 20)[0]
    tf = struct.unpack_from('<II', g, 28)
    nondep = sum(1 for w in tf for k in range(8) if (w >> (4 * k)) & 1)
    hdr_instr = struct.unpack_from('<I', g, 60)[0]
    lines = [l.split(': ', 1)[1] if ': ' in l else l for l in dsm.splitlines() if l.strip()]
    if any(l.startswith('PHAS') for l in lines):
        lines = lines[max(i for i, l in enumerate(lines) if l.startswith('PHAS')) + 1:]
    ops = [l.split()[0] for l in lines if l.split()]
    instr = sum(1 for o in ops if o not in ('NOP', 'PHAS'))
    cyc = 0
    for l in lines:
        t = l.split()
        if not t or t[0] == 'PHAS': continue
        if t[0].startswith('SMP'): continue
        n = 1
        if t[0].startswith('V') and not t[0].startswith('VPCK') and '2' not in t[0].replace('F16', '') and len(t) > 1 and '.' in t[1]:
            nc = sum(1 for c in t[1].split('.', 1)[1] if c in 'xyzw')
            n = ((nc + 3) // 4 if 'F16' in t[0] else (nc + 1) // 2) or 1   # F32 vector ops: 2 components a cycle, F16: 4
        cyc += n
    smp = sum(1 for o in ops if o.startswith('SMP'))
    br, kill = branches(g)
    return dict(frag=int(flags & 1), discard=int(bool(flags & 0x8)), instr=instr, cyc=cyc, smp=smp, nondep=nondep, hdr_instr=hdr_instr, br=br, kill=kill,
                ops={o: ops.count(o) for o in sorted(set(ops))})


if __name__ == '__main__':
    cache, dsmdir = sys.argv[1], sys.argv[2]; out = {}
    for f in sorted(os.listdir(cache)):
        if not f.endswith('.bin'): continue
        g = gxp_of(os.path.join(cache, f)); dp = os.path.join(dsmdir, f + '.dsm')
        if g is None or not os.path.exists(dp): continue
        out[f[:-4]] = features(g, open(dp).read())
    json.dump(out, sys.stdout, indent=1)
