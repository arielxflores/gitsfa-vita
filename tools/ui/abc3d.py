# databuilder: the Matrix3D placements of the Flash HUD (as3d.py) read straight from a SWF's ActionScript 3 bytecode
# (DoABC tags), without decompiling it. as3d.load() used to read the same numbers from JPEXS ffdec's decompiled .as files;
# this gives the same table: {(class name, child): 4x4} from every `this.____motion_<child>af<N>_mat3DVec__[i] = v;`
# (the code Flash's motion-tween export writes into each timeline class).
#   usage (module): abc3d.load(swf_path) -> dict as as3d.load();  python3 abc3d.py file.swf [scripts dir to compare]
import struct, re, zlib, numpy as np
VEC = re.compile(r'^____motion_(\w+?)af\d+_mat3DVec__$')

class R:
    def __init__(s, d, o=0): s.d = d; s.o = o
    def u8(s): v = s.d[s.o]; s.o += 1; return v
    def u16(s): v = struct.unpack_from('<H', s.d, s.o)[0]; s.o += 2; return v
    def s24(s): v = int.from_bytes(s.d[s.o:s.o + 3], 'little', signed=True); s.o += 3; return v
    def u30(s):
        v = 0; sh = 0
        while True:
            b = s.d[s.o]; s.o += 1; v |= (b & 0x7f) << sh; sh += 7
            if not b & 0x80 or sh >= 35: return v & 0xffffffff
    def s32(s):
        v = 0; sh = 0
        while True:
            b = s.d[s.o]; s.o += 1; v |= (b & 0x7f) << sh; sh += 7
            if not b & 0x80 or sh >= 35: break
        if sh < 32 and v & (1 << (sh - 1)): v -= 1 << sh   # sign-extend from the last byte's top bit
        v &= 0xffffffff
        return v - (1 << 32) if v & 0x80000000 else v
    def d64(s): v = struct.unpack_from('<d', s.d, s.o)[0]; s.o += 8; return v
    def bytes_(s, n): v = s.d[s.o:s.o + n]; s.o += n; return v

def parse_abc(d):
    r = R(d); r.u16(); r.u16()
    ints = [0] + [r.s32() for _ in range(max(0, r.u30() - 1))]
    uints = [0] + [r.u30() for _ in range(max(0, r.u30() - 1))]
    dbls = [float('nan')] + [r.d64() for _ in range(max(0, r.u30() - 1))]
    strs = ['']
    for _ in range(max(0, r.u30() - 1)): n = r.u30(); strs.append(r.bytes_(n).decode('utf8', 'replace'))
    nss = [None]
    for _ in range(max(0, r.u30() - 1)): k = r.u8(); nss.append((k, r.u30()))
    nsset = [None]
    for _ in range(max(0, r.u30() - 1)): n = r.u30(); nsset.append([r.u30() for _ in range(n)])
    mn = [None]
    for _ in range(max(0, r.u30() - 1)):
        k = r.u8()
        if k in (0x07, 0x0D): r.u30(); nm = r.u30(); mn.append(('Q', strs[nm]))          # QName(A)
        elif k in (0x0F, 0x10): nm = r.u30(); mn.append(('RTQ', strs[nm]))               # RTQName(A)
        elif k in (0x11, 0x12): mn.append(('RTQL', None))                                # RTQNameL(A)
        elif k in (0x09, 0x0E): nm = r.u30(); r.u30(); mn.append(('M', strs[nm]))        # Multiname(A)
        elif k in (0x1B, 0x1C): r.u30(); mn.append(('ML', None))                          # MultinameL(A)
        elif k == 0x1D: r.u30(); n = r.u30(); [r.u30() for _ in range(n)]; mn.append(('T', None))   # TypeName
        else: raise ValueError('multiname kind %x' % k)
    # methods
    nmeth = r.u30()
    for _ in range(nmeth):
        pc = r.u30(); r.u30(); [r.u30() for _ in range(pc)]; r.u30(); fl = r.u8()
        if fl & 0x08:
            for _ in range(r.u30()): r.u30(); r.u8()
        if fl & 0x80: [r.u30() for _ in range(pc)]
    for _ in range(r.u30()):   # metadata
        r.u30(); n = r.u30(); [r.u30() for _ in range(2 * n)]
    def traits():
        out = []
        for _ in range(r.u30()):
            nm = r.u30(); kb = r.u8(); k = kb & 0x0F
            if k in (0, 6): r.u30(); r.u30(); vi = r.u30(); (r.u8() if vi else None)
            elif k in (1, 2, 3): r.u30(); out.append(r.u30())        # method / getter / setter -> method index
            elif k == 4: r.u30(); r.u30()                            # class
            elif k == 5: r.u30(); out.append(r.u30())                # function
            if kb & 0x40: [r.u30() for _ in range(r.u30())]           # metadata
        return out
    nclass = r.u30(); inst = []
    for _ in range(nclass):
        nm = r.u30(); r.u30(); fl = r.u8()
        if fl & 0x08: r.u30()
        [r.u30() for _ in range(r.u30())]
        iinit = r.u30(); ms = traits(); inst.append((mn[nm][1], [iinit] + ms))
    cls = []
    for i in range(nclass): cinit = r.u30(); ms = traits(); cls.append([cinit] + ms)
    for _ in range(r.u30()): r.u30(); traits()                   # scripts
    bodies = {}
    for _ in range(r.u30()):
        m = r.u30(); r.u30(); r.u30(); r.u30(); r.u30(); n = r.u30(); code = r.bytes_(n)
        for _ in range(r.u30()): [r.u30() for _ in range(5)]      # exceptions
        traits()
        bodies[m] = code
    return dict(ints=ints, uints=uints, dbls=dbls, strs=strs, mn=mn, inst=inst, cls=cls, bodies=bodies)

# operand layouts of the AVM2 opcodes (u30 = 'u', u8 = 'b', s24 = 's'; lookupswitch 'L'); the rest take none
OPS = {op: 'u' for op in (0x04, 0x05, 0x06, 0x08, 0x25, 0x2C, 0x2D, 0x2E, 0x2F, 0x31, 0x40, 0x41, 0x42, 0x49, 0x53, 0x55, 0x56, 0x58, 0x59,
                          0x5A, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x66, 0x67, 0x68, 0x6A, 0x6C, 0x6D, 0x6E, 0x6F, 0x80, 0x86,
                          0x92, 0x94, 0xB2, 0xC2, 0xC3, 0xF0, 0xF1, 0xF2)}
OPS.update({op: 'uu' for op in (0x32, 0x43, 0x44, 0x45, 0x46, 0x4A, 0x4C, 0x4E, 0x4F)})
OPS.update({op: 's' for op in range(0x0C, 0x1B)})
OPS.update({0x1B: 'L', 0x24: 'b', 0x65: 'b', 0xEF: 'bubu'})
def operands(code, o, op):
    r = R(code, o); lay = OPS.get(op, '')
    if lay == 'L':   # lookupswitch: s24 default, u30 count, count+1 x s24
        r.s24(); n = r.u30(); [r.s24() for _ in range(n + 1)]; return [], r.o
    vals = []
    for c in lay:
        vals.append(r.u8() if c == 'b' else r.u30() if c == 'u' else r.s24())
    return vals, r.o

def scan(abc, code, out):
    """symbolic run over one method body: records ('vec name', index, value) for every element store into a mat3DVec"""
    st = []; o = 0; mn = abc['mn']
    while o < len(code):
        op = code[o]; vals, o = operands(code, o + 1, op)
        if op == 0xD0: st.append(('this',))                                   # getlocal0
        elif op in (0x66, 0x60):                                              # getproperty / getlex
            m = mn[vals[0]]
            if op == 0x66 and st: st.pop()
            st.append(('prop', m[1]) if m and m[0] in ('Q', 'M') else ('?',))
        elif op == 0x24: st.append(('num', float((vals[0] ^ 0x80) - 0x80)))   # pushbyte (signed)
        elif op == 0x25: v = vals[0] & 0xffff; st.append(('num', float(v - 0x10000 if v & 0x8000 else v)))   # pushshort
        elif op == 0x2D: st.append(('num', float(abc['ints'][vals[0]])))      # pushint
        elif op == 0x2E: st.append(('num', float(abc['uints'][vals[0]])))     # pushuint
        elif op == 0x2F: st.append(('num', abc['dbls'][vals[0]]))             # pushdouble
        elif op == 0x28: st.append(('num', float('nan')))                     # pushnan
        elif op == 0x90:                                                      # negate
            if st and st[-1][0] == 'num': st[-1] = ('num', -st[-1][1])
        elif op in (0x73, 0x74, 0x75, 0x82, 0x85, 0x80, 0x76):                # convert_i/u/d, coerce_a/s/x, convert_b: value kept
            pass
        elif op == 0x61:                                                      # setproperty
            m = mn[vals[0]]
            if m and m[0] in ('ML', 'RTQL') and len(st) >= 3:
                val, idx, obj = st.pop(), st.pop(), st.pop()
                if obj[0] == 'prop' and obj[1] and VEC.match(obj[1]) and idx[0] == 'num' and val[0] == 'num':
                    out.append((obj[1], int(idx[1]), val[1]))
            else: st[-2:] = []
        elif op == 0x29: st[-1:] = []                                         # pop
        elif op == 0x2A and st: st.append(st[-1])                             # dup
        else: st = []                                                         # anything else: forget (not a simple store)
    return out

def doabc_tags(path):
    raw = open(path, 'rb').read(); body = zlib.decompress(raw[8:]) if raw[:3] in (b'CWS', b'CFX') else raw[8:]
    nb = body[0] >> 3; o = (5 + 4 * nb + 7) // 8 + 4
    while o < len(body):
        cl = struct.unpack_from('<H', body, o)[0]; o += 2; code = cl >> 6; ln = cl & 63
        if ln == 63: ln = struct.unpack_from('<I', body, o)[0]; o += 4
        t = body[o:o + ln]; o += ln
        if code == 82:   # DoABC2: flags u32, name string, abc
            e = t.index(b'\0', 4); yield t[e + 1:]
        elif code == 72: yield t
        if code == 0: break

def load(swf_path):
    """{(class, child): 4x4} as as3d.load() made from the decompiled scripts"""
    out = {}
    for abcd in doabc_tags(swf_path):
        abc = parse_abc(abcd)
        for (cname, ms), cms in zip(abc['inst'], abc['cls']):
            stores = []
            seen = set(); todo = list(ms) + list(cms)
            while todo:
                m = todo.pop(0)
                if m in seen or m not in abc['bodies']: continue
                seen.add(m); code = abc['bodies'][m]; scan(abc, code, stores)
                o = 0   # nested closures (newfunction) belong to the class too
                while o < len(code):
                    op = code[o]; vals, o = operands(code, o + 1, op)
                    if op == 0x40: todo.append(vals[0])
            vecs = {}
            for name, i, v in stores:
                m = VEC.match(name); vecs.setdefault(m.group(1), {})[i] = v
            for vec, el in vecs.items():
                if 0 not in el: continue
                vals = [0.0] * 16
                for i, v in el.items():
                    if 0 <= i < 16: vals[i] = float(v)
                out[(cname, vec)] = np.array(vals).reshape(4, 4)
    return out

if __name__ == '__main__':
    import sys
    M = load(sys.argv[1]); print(len(M), 'placements')
    if len(sys.argv) > 2:
        import as3d
        A = as3d.load(sys.argv[2]); bad = [k for k in set(A) | set(M) if k not in A or k not in M or not np.array_equal(A[k], M[k])]
        print('compared with', len(A), 'from the scripts:', 'IDENTICAL' if not bad else '%d differ: %s' % (len(bad), sorted(bad)[:8]))
