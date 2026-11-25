#!/usr/bin/env python3
"""Minimal Direct3D 9 shader bytecode (vs/ps 2.x-3.0) disassembler + CTAB reader, and a reader for the parameter
defaults of compiled D3DX effects (fx_2_0 .fxo). Written from the public D3D9 token format documentation (and the layout
Wine's d3dx9 effect parser documents), to study the original game's own shader files (Data/shaders) - read-only.
usage: d3dasm.py FILE [--fx] [--grep NAME]"""
import struct, sys, re

OPS = {0: 'nop', 1: 'mov', 2: 'add', 3: 'sub', 4: 'mad', 5: 'mul', 6: 'rcp', 7: 'rsq', 8: 'dp3', 9: 'dp4', 10: 'min', 11: 'max',
       12: 'slt', 13: 'sge', 14: 'exp', 15: 'log', 16: 'lit', 17: 'dst', 18: 'lrp', 19: 'frc', 20: 'm4x4', 21: 'm4x3', 22: 'm3x4',
       23: 'm3x3', 24: 'm3x2', 25: 'call', 26: 'callnz', 27: 'loop', 28: 'ret', 29: 'endloop', 30: 'label', 31: 'dcl', 32: 'pow',
       33: 'crs', 34: 'sgn', 35: 'abs', 36: 'nrm', 37: 'sincos', 38: 'rep', 39: 'endrep', 40: 'if', 41: 'ifc', 42: 'else',
       43: 'endif', 44: 'break', 45: 'breakc', 46: 'mova', 47: 'defb', 48: 'defi', 64: 'texcoord', 65: 'texkill', 66: 'texld',
       80: 'cnd', 81: 'def', 88: 'cmp', 89: 'bem', 90: 'dp2add', 91: 'dsx', 92: 'dsy', 93: 'texldd', 94: 'setp', 95: 'texldl', 96: 'breakp'}
NSRC = {'mov': 1, 'add': 2, 'sub': 2, 'mad': 3, 'mul': 2, 'rcp': 1, 'rsq': 1, 'dp3': 2, 'dp4': 2, 'min': 2, 'max': 2, 'slt': 2,
        'sge': 2, 'exp': 1, 'log': 1, 'lit': 1, 'dst': 2, 'lrp': 3, 'frc': 1, 'm4x4': 2, 'm4x3': 2, 'm3x4': 2, 'm3x3': 2, 'm3x2': 2,
        'pow': 2, 'crs': 2, 'sgn': 3, 'abs': 1, 'nrm': 1, 'sincos': 1, 'mova': 1, 'texkill': 0, 'texld': 2, 'cnd': 3, 'cmp': 3,
        'dp2add': 3, 'dsx': 1, 'dsy': 1, 'texldd': 4, 'setp': 2, 'texldl': 2, 'bem': 2}
NODST = {'if': 1, 'ifc': 2, 'else': 0, 'endif': 0, 'rep': 1, 'endrep': 0, 'loop': 2, 'endloop': 0, 'break': 0, 'breakc': 2,
         'ret': 0, 'call': 1, 'callnz': 2, 'label': 1, 'texkill': 1, 'breakp': 1}
RT = {0: 'r', 1: 'v', 2: 'c', 3: 'a', 4: 'oPos', 5: 'oD', 6: 'o', 7: 'i', 8: 'oC', 9: 'oDepth', 10: 's', 14: 'b', 15: 'aL',
      17: 'misc', 18: 'l', 19: 'p'}
USAGE = ['position', 'blendweight', 'blendindices', 'normal', 'psize', 'texcoord', 'tangent', 'binormal', 'tessfactor',
         'positiont', 'color', 'fog', 'depth', 'sample']
CMP = {1: 'gt', 2: 'eq', 3: 'ge', 4: 'lt', 5: 'ne', 6: 'le'}

def rtype(t): return ((t >> 28) & 7) | ((t >> 8) & 0x18)

def reg(t, vs):
    ty, n = rtype(t), t & 0x7ff
    if ty == 3: return ('a%d' if vs else 't%d') % n
    if ty == 4: return ['oPos', 'oFog', 'oPts'][n] if n < 3 else 'oR%d' % n
    if ty == 17: return ['vPos', 'vFace'][n] if n < 2 else 'misc%d' % n
    if ty == 9: return 'oDepth'
    return '%s%d' % (RT.get(ty, '?%d_' % ty), n)

def swz(t):
    s = (t >> 16) & 0xff; c = 'xyzw'; out = ''.join(c[(s >> (2 * i)) & 3] for i in range(4))
    if out == 'xyzw': return ''
    if out[1:] == out[0] * 3: return '.' + out[0]
    return '.' + out

def src(tok, rel, vs, names):
    r = reg(tok, vs); mod = (tok >> 24) & 0xf
    if rtype(tok) == 2 and names:
        n = tok & 0x7ff; nm = names.get(n)
        if nm: r = '%s<%s>' % (r, nm)
    if rel is not None: r = r.replace(r.split('<')[0], '%s[%s]' % (r.split('<')[0], reg(rel, vs) + swz(rel)[:2]))
    r += swz(tok)
    return {0: r, 1: '-' + r, 11: 'abs(%s)' % r, 12: '-abs(%s)' % r, 13: '!' + r, 7: r + '_x2', 2: r + '_bias'}.get(mod, '%s_mod%d' % (r, mod))

def dst(tok, vs):
    m = (tok >> 16) & 0xf; s = reg(tok, vs); mods = (tok >> 20) & 0xf
    if m != 0xf: s += '.' + ''.join('xyzw'[i] for i in range(4) if m >> i & 1)
    return s, ('_sat' if mods & 1 else '') + ('_pp' if mods & 2 else '') + ('_centroid' if mods & 4 else '')

def ctab(words):
    """CTAB comment: name -> (regset, index, count, typeinfo); returns dict c-index -> name and a list"""
    b = struct.pack('<%dI' % len(words), *words)
    if b[:4] != b'CTAB': return None
    d = b[4:]
    size, creator, ver, nconst, cinfo, flags, target = struct.unpack_from('<7I', d, 0)
    out = []
    for i in range(nconst):
        name, regset, regidx, regcnt, _r, tinfo, defv = struct.unpack_from('<IHHHHII', d, cinfo + 20 * i)
        nm = d[name:d.index(b'\0', name)].decode()
        cls, typ, rows, cols, elems, members = struct.unpack_from('<6H', d, tinfo)
        dv = None
        if defv: dv = struct.unpack_from('<%df' % (regcnt * 4), d, defv)
        out.append(dict(name=nm, set=['bool', 'int4', 'float4', 'sampler'][regset], reg=regidx, n=regcnt, rows=rows, cols=cols, defv=dv))
    return out

def disasm(words, pos=0, show=True):
    """words: list of dwords starting at the version token. Returns (text lines, consts, end index)"""
    ver = words[pos]; vs = (ver >> 16) == 0xfffe; lines = ['%s_%d_%d' % ('vs' if vs else 'ps', (ver >> 8) & 0xff, ver & 0xff)]
    i = pos + 1; consts = []; cnames = {}; snames = {}; depth = 0
    while i < len(words):
        t = words[i]
        if t == 0xffff: i += 1; break
        op = t & 0xffff
        if op == 0xfffe:
            n = (t >> 16) & 0x7fff; c = ctab(words[i + 1:i + 1 + n])
            if c:
                consts = c
                for k in c:
                    for j in range(k['n']):
                        if k['set'] == 'float4': cnames[k['reg'] + j] = k['name'] + ('[%d]' % j if k['n'] > 1 else '')
                        if k['set'] == 'sampler': snames[k['reg'] + j] = k['name']
            i += 1 + n; continue
        ln = (t >> 24) & 0xf; name = OPS.get(op, 'op%d' % op); args = words[i + 1:i + 1 + ln]; i += 1 + ln
        ctl = (t >> 16) & 0xff
        if name in ('else', 'endif', 'endrep', 'endloop'): depth -= 1
        ind = '  ' * max(depth, 0)
        if name == 'dcl':
            u = args[0]; d, m = dst(args[1], vs)
            if rtype(args[1]) == 10: lines.append(ind + 'dcl_%s %s  ; %s' % (['?', '?', '2d', 'cube', 'volume'][(u >> 27) & 0xf], d, snames.get(args[1] & 0x7ff, '')))
            else: lines.append(ind + 'dcl_%s%s %s%s' % (USAGE[u & 0x1f] if (u & 0x1f) < len(USAGE) else '?', (u >> 16) & 0xf or '', d, m))
        elif name == 'def':
            d, _ = dst(args[0], vs); f = struct.unpack('<4f', struct.pack('<4I', *args[1:5])); lines.append(ind + 'def %s, %s' % (d, ', '.join('%g' % x for x in f)))
        elif name in ('defi', 'defb'):
            d, _ = dst(args[0], vs); lines.append(ind + '%s %s, %s' % (name, d, ', '.join(str(struct.unpack('<i', struct.pack('<I', x))[0]) for x in args[1:])))
        else:
            # walk params (sources may carry a relative-address token in sm3)
            toks = []; j = 0
            while j < len(args):
                a = args[j]; rel = None
                if a & (1 << 13) and j + 1 < len(args) and ver >= 0xfffe0200 and not (len(toks) == 0 and name not in NODST):
                    rel = args[j + 1]; j += 1
                toks.append((a, rel)); j += 1
            if name in NODST:
                s = ', '.join(src(a, r, vs, cnames) for a, r in toks)
                nm = name + ('_' + CMP[ctl] if name in ('ifc', 'breakc') and ctl in CMP else '')
                lines.append(ind + nm + ' ' + s)
            elif toks:
                d, m = dst(toks[0][0], vs); s = ', '.join(src(a, r, vs, cnames) for a, r in toks[1:])
                pred = ''
                nm = name + ('_' + CMP[ctl] if name == 'setp' and ctl in CMP else '')
                lines.append(ind + '%s%s%s %s, %s' % (pred, nm, m, d, s))
            else: lines.append(ind + name)
        if name in ('if', 'ifc', 'else', 'rep', 'loop'): depth += 1
    return lines, consts, i

def find_shaders(b):
    """offsets of shader version tokens followed by a CTAB comment (or any dcl) in a blob"""
    out = []
    for m in re.finditer(rb'(\x00\x03|\x00\x02|\x01\x02|\xff\x02)(\xfe\xff|\xff\xff)', b):
        o = m.start()
        if o % 4: continue
        nxt = struct.unpack_from('<I', b, o + 4)[0] if o + 8 <= len(b) else 0
        if (nxt & 0xffff) == 0xfffe or (nxt & 0xffff) in (31, 81): out.append(o)
    return out

def fx_params(b):
    """D3DX effect (fx_2_0) top-level parameters with their default values"""
    tag, off = struct.unpack_from('<II', b, 0)
    if tag != 0xfeff0901: return []
    data = b[8:]; p = off
    def rd():
        nonlocal p; v = struct.unpack_from('<I', data, p)[0]; p += 4; return v
    def sname(o):
        if not o: return ''
        n = struct.unpack_from('<I', data, o)[0]; return data[o + 4:o + 4 + n].split(b'\0')[0].decode('latin1')
    def tdef(o):
        typ, cls, nm, sem, elems = struct.unpack_from('<5I', data, o); o += 20; t = dict(type=typ, cls=cls, name=sname(nm), sem=sname(sem), elems=elems)
        if cls in (0, 1, 2, 3): t['cols'], t['rows'] = struct.unpack_from('<2I', data, o); o += 8
        elif cls == 5:
            nmem = struct.unpack_from('<I', data, o)[0]; o += 4; mem = []
            for _ in range(nmem): m, o = tdef(o); mem.append(m)
            t['members'] = mem
        return t, o
    def tsize(t):
        if t['cls'] in (0, 1, 2, 3): n = t['cols'] * t['rows']
        elif t['cls'] == 5: n = sum(tsize(m) for m in t['members'])
        else: n = 1
        return n * max(1, t['elems'])
    npar, ntech, _u, nobj = rd(), rd(), rd(), rd()
    out = []
    for _ in range(npar):
        to, vo, flags, nann = rd(), rd(), rd(), rd()
        for _a in range(nann): rd(); rd()
        t, _ = tdef(to); n = tsize(t); val = None
        if t['cls'] in (0, 1, 2, 3) and t['type'] in (1, 2, 3):   # bool int float
            raw = struct.unpack_from('<%dI' % n, data, vo)
            val = [struct.unpack('<f', struct.pack('<I', x))[0] if t['type'] == 3 else x for x in raw]
        out.append(dict(name=t['name'], sem=t['sem'], type=t['type'], cls=t['cls'], rows=t.get('rows'), cols=t.get('cols'), elems=t['elems'], value=val))
    return out

if __name__ == '__main__':
    path = sys.argv[1]; b = open(path, 'rb').read(); grep = None
    if '--grep' in sys.argv: grep = sys.argv[sys.argv.index('--grep') + 1]
    if '--fx' in sys.argv:
        for q in fx_params(b):
            v = q['value']
            if v is not None: v = [round(x, 4) if isinstance(x, float) else x for x in v]
            print('%-34s %-10s %s' % (q['name'], q['sem'], v))
        sys.exit(0)
    words = struct.unpack('<%dI' % (len(b) // 4 * 1), b[:len(b) // 4 * 4]) if False else None
    for o in find_shaders(b):
        # shaders inside caches are 4-byte aligned relative to their own start: re-read from o
        w = struct.unpack_from('<%dI' % ((len(b) - o) // 4), b, o)
        lines, consts, end = disasm(w, 0)
        txt = '\n'.join(lines)
        if grep and grep not in txt: continue
        # the name / key string before the shader (caches): last printable run in the 200 bytes before
        pre = re.findall(rb'[\x20-\x7e]{6,}', b[max(0, o - 220):o]); key = pre[-2:] if pre else []
        print('==== @%d %s' % (o, ' | '.join(k.decode() for k in key)))
        for c in consts:
            print('   ; %-28s %s%d n%d %s' % (c['name'], c['set'], c['reg'], c['n'], '' if c['defv'] is None else [round(x, 4) for x in c['defv']]))
        print(txt)
