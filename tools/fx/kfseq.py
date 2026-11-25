# Gamebryo 30.2 .kf reader (NiSequenceData with evaluators): constant, keyed and compressed B-spline float / transform
# evaluators, sampled at a time. Layouts follow the public niftools nif.xml description (b101: effect\kfm\DM_device_install).
# usage: python3 kfseq.py <file.kf> [step]   -> every track sampled along the sequence
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nifps
import numpy as np
NONE = 0xffffffff

def _bspline(cps, nctrl, u):
    """open uniform cubic B-spline through nctrl control points (rows of cps), u in 0..1"""
    deg = 3; n = nctrl
    if n <= deg: return cps[0]
    x = u * (n - deg); i = min(int(x), n - deg - 1); t = x - i
    # uniform cubic basis on the span, with the open (clamped) ends handled by de Boor on the clamped knot vector
    knots = [0] * (deg + 1) + list(range(1, n - deg)) + [n - deg] * (deg + 1)
    k = i + deg; d = [np.array(cps[j + k - deg], float) for j in range(deg + 1)]
    for r in range(1, deg + 1):
        for j in range(deg, r - 1, -1):
            a0 = knots[j + k - deg]; a1 = knots[j + 1 + k - r]
            al = 0.0 if a1 == a0 else (x - a0) / (a1 - a0)
            d[j] = (1 - al) * d[j - 1] + al * d[j]
    return d[deg]

class Seq:
    def __init__(s, path):
        h = nifps.load(path); s.h = h; d = h['d']; s.d = d; B = h['blocks']; S = lambda i: None if i == NONE else h['strs'][i]
        k0 = [k for k, b in enumerate(B) if b[0] == 'NiSequenceData'][0]; o = B[k0][1]
        name, n = struct.unpack_from('<II', d, o); refs = struct.unpack_from('<%dI' % n, d, o + 8); q = o + 8 + 4 * n
        s.textkeys, s.dur, s.cycle, s.freq, accum, s.accum_flags = struct.unpack_from('<IfIfII', d, q); s.name = S(name); s.tracks = []
        for r in refs:
            t, o, sz = B[r]; node, pt, ct, cid, iid = struct.unpack_from('<5I', d, o); ch = d[o + 20:o + 24]; e = dict(node=S(node), prop=S(pt), ctrl=S(ct), cid=S(cid), iid=S(iid), type=t, ch=list(ch)); p = o + 24
            if t == 'NiConstFloatEvaluator': e['const'] = struct.unpack_from('<f', d, p)[0]
            elif t == 'NiFloatEvaluator': e['keys'] = s._floatdata(struct.unpack_from('<I', d, p)[0])
            elif t == 'NiBSplineCompFloatEvaluator':
                t0, t1, da, ba, hd, off, hr = struct.unpack_from('<ffIIIff', d, p); e.update(t0=t0, t1=t1, cps=s._cps(da, ba, hd, 1, off, hr), n=s._nctrl(ba))
            elif t == 'NiConstTransformEvaluator': e['T'] = struct.unpack_from('<3f', d, p); e['Q'] = struct.unpack_from('<4f', d, p + 12); e['Sc'] = struct.unpack_from('<f', d, p + 28)[0]
            elif t == 'NiBSplineCompTransformEvaluator':
                t0, t1, da, ba = struct.unpack_from('<ffII', d, p); T = struct.unpack_from('<3f', d, p + 16); Q = struct.unpack_from('<4f', d, p + 28); Sc = struct.unpack_from('<f', d, p + 44)[0]
                ht, hq, hs = struct.unpack_from('<3I', d, p + 48); ot, rt, oq, rq, os_, rs = struct.unpack_from('<6f', d, p + 60); nc = s._nctrl(ba)
                e.update(t0=t0, t1=t1, n=nc, T=T, Q=Q, Sc=Sc, cT=s._cps(da, ba, ht, 3, ot, rt) if ht != 0xffff and ht != NONE else None,
                         cQ=s._cps(da, ba, hq, 4, oq, rq) if hq != 0xffff and hq != NONE else None, cS=s._cps(da, ba, hs, 1, os_, rs) if hs != 0xffff and hs != NONE else None)
            elif t == 'NiTransformEvaluator': e['T'] = struct.unpack_from('<3f', d, p); e['Q'] = struct.unpack_from('<4f', d, p + 12); e['Sc'] = struct.unpack_from('<f', d, p + 28)[0]; e['data'] = struct.unpack_from('<I', d, p + 32)[0]
            else: e['unknown'] = sz
            s.tracks.append(e)
    def _nctrl(s, ba): return struct.unpack_from('<I', s.d, s.h['blocks'][ba][1])[0]
    def _cps(s, da, ba, hd, w, off, hr):
        o = s.h['blocks'][da][1]; nf = struct.unpack_from('<I', s.d, o)[0]; o += 4 + 4 * nf; nc = struct.unpack_from('<I', s.d, o)[0]; n = s._nctrl(ba)
        a = np.frombuffer(s.d, '<i2', n * w, o + 4 + 2 * hd).astype(float).reshape(n, w); return off + a / 32767.0 * hr
    def _floatdata(s, ref):
        o = s.h['blocks'][ref][1]; n, it = struct.unpack_from('<II', s.d, o) if struct.unpack_from('<I', s.d, o)[0] else (0, 0); o += 8; ks = []; w = {1: 2, 2: 4, 3: 5}.get(it, 2)
        for _ in range(n): v = struct.unpack_from('<%df' % w, s.d, o); o += 4 * w; ks.append(v)
        return it, ks
    def fval(s, e, t):
        if 'const' in e: return e['const']
        if 'keys' in e:
            it, ks = e['keys']
            if not ks: return 0.0
            if t <= ks[0][0]: return ks[0][1]
            for a, b in zip(ks, ks[1:]):
                if t <= b[0]:
                    u = (t - a[0]) / max(1e-9, b[0] - a[0])
                    if it == 2: h00 = 2 * u**3 - 3 * u * u + 1; h10 = u**3 - 2 * u * u + u; h01 = -2 * u**3 + 3 * u * u; h11 = u**3 - u * u; return h00 * a[1] + h10 * a[2] + h01 * b[1] + h11 * b[3]
                    return a[1] + (b[1] - a[1]) * u
            return ks[-1][1]
        u = min(1.0, max(0.0, (t - e['t0']) / max(1e-9, e['t1'] - e['t0']))); return float(_bspline(e['cps'], e['n'], u)[0])
    def xform(s, e, t):
        """(translation, quaternion wxyz, scale); None parts keep the node's own"""
        if 'cT' not in e: return e.get('T'), e.get('Q'), e.get('Sc')
        u = min(1.0, max(0.0, (t - e['t0']) / max(1e-9, e['t1'] - e['t0']))); F = 3.4e38
        T = _bspline(e['cT'], e['n'], u) if e['cT'] is not None else (e['T'] if abs(e['T'][0]) < F else None)
        Q = _bspline(e['cQ'], e['n'], u) if e['cQ'] is not None else (e['Q'] if abs(e['Q'][0]) < F else None)
        Sc = float(_bspline(e['cS'], e['n'], u)[0]) if e['cS'] is not None else (e['Sc'] if abs(e['Sc']) < F else None)
        if Q is not None: Q = np.array(Q, float); Q = Q / max(1e-9, np.linalg.norm(Q))
        return T, Q, Sc

if __name__ == '__main__':
    q = Seq(sys.argv[1]); step = float(sys.argv[2]) if len(sys.argv) > 2 else q.dur / 8
    print('sequence', q.name, 'duration %.3f' % q.dur, 'cycle', q.cycle, 'tracks', len(q.tracks)); ts = np.arange(0, q.dur + 1e-6, step)
    for e in q.tracks:
        hd = '%-12s %-26s %-18s %s' % (e['node'], (e['ctrl'] or '')[2:], e['cid'] or '', e['type'][2:].replace('Evaluator', ''))
        if 'Transform' in e['type']:
            out = []
            for t in ts:
                T, Q, Sc = q.xform(e, t); out.append('T%s Q%s S%s' % (None if T is None else np.round(T, 2).tolist(), None if Q is None else np.round(Q, 2).tolist(), None if Sc is None else round(Sc, 3)))
            print(hd); [print('      %.2f %s' % (t, o)) for t, o in zip(ts, out) if o != (out[0] if t else None)]
        elif 'unknown' in e: print(hd, 'UNKNOWN', e['unknown'])
        else: print(hd, ' '.join('%.2f' % q.fval(e, t) for t in ts))
