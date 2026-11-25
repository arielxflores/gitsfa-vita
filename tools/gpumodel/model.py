#!/usr/bin/env python3
"""Offline GPU cost model of the world (render b38): fit to the b32 device probe, predict any build.

Per view v of map m drawn with program set s, the world's GPU work is
    F(v,s) = sum over draws: fragments x (cycles + d * dependent reads + t * non-dependent reads)
    V(v,s) = sum over drawn meshes / sky layers: vertices x vertex-program cycles
(cycles, reads: gxpcost.py from the GXP binaries the Vita's compiler made; fragments: fragcount.py on the host), and
    GPU time G = other_m + k * (F + V)
k = ns per USSE cycle-fragment (fitted, or fixed at 1 / (4 cores x 4 pipes x 222 MHz) = 0.2815), other_m = everything
that is not the world (characters, weapon, effects, HUD, clears, tiling): one value per map, fitted. The b32 device
probe measured, per map and set, the mean frame time with vsync and triple buffering, so what is compared with it is
mean_v max(16.67, G(v,s)).

Files under <S> (the work folder): frag/<stem>.json (fragcount.py), assign/<stem>_<build>.json (assign.c),
prog<build>.json (progsrc.c), cost<build>.json (gxpcost.py; b32's programs are found among b37's GXPs by source).
usage: model.py <S> <b32 boot.log> fit            fit, print the variants and the residuals, write <S>/fit.json
       model.py <S> - table [build ...]           predicted GPU ms per map (shipping set) from <S>/fit.json
       model.py <S> - header <out.h>              engine/src/gpumodel.h: the newest build's world ms per probe set + other, per map
"""
import sys, os, json, math, re
import numpy as np
from scipy.optimize import least_squares
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mapvis'))
import gmapio

VSYNC = 1000.0 / 60
MAPS = os.environ.get('MAPS', '/root/gits/build/maps')
K_PHYS = 1e3 / (16 * 222)   # ns per cycle: 4 cores x 4 USSE2 pipes at 222 MHz
POLY_MAPS = ('port', 'tutorial3', 'tutorial')
ALU = os.environ.get('ALU', 'instr')   # b43: the device (b41-2 probes) costs an instruction per fragment whatever its width: 'instr'; b38's 'cyc' counted F32 vec ops at 2 components a cycle   # world.h w_poly_maps (b35+): shipping set 1
BUILDS = {'32': ('prog32.json', 'cost37.json', 'prog37.json'), '37': ('prog37.json', 'cost37.json', None), '38': ('prog38.json', 'cost38.json', None),
          '39': ('prog39.json', 'cost39.json', None), '43': ('prog43.json', 'cost43.json', None)}   # (b39: the world's fog varying in half; b43: the shorter LUT tail)
HEADER_BUILD = os.environ.get('HEADER_BUILD', '39')   # the build gpumodel.h describes


def load_programs(S, build):
    pj, cj, ref = BUILDS[build]
    P = json.load(open(os.path.join(S, pj)))['programs']; C = json.load(open(os.path.join(S, cj)))
    if ref:   # an older build's programs, found among a newer build's GXPs by their source
        by_src = {p['fs_src']: p for p in json.load(open(os.path.join(S, ref)))['programs']}
        return {p['i']: dict(fs=C[by_src[p['fs_src']]['fs']], vs=C[by_src[p['fs_src']]['vs']]) for p in P}
    return {p['i']: dict(fs=C[p['fs']], vs=C[p['vs']]) for p in P}


def bucket_programs(S, stem, build):
    """fragcount.py's buckets (meshes with the same programs in b37 and b32) -> the build's program per set"""
    g = gmapio.read(os.path.join(MAPS, stem + '.gmap'), decode=False)
    A = json.load(open('%s/assign/%s_37.json' % (S, stem))); B = json.load(open('%s/assign/%s_32.json' % (S, stem)))
    X = json.load(open('%s/assign/%s_%s.json' % (S, stem, build)))
    keys = {}; progs = []
    for i, m in enumerate(g['mesh']):
        cls = 'opaque' if m['mode'] == 0 else 'test' if m['mode'] == 1 else 'blend'
        k = (cls, tuple(A['meshes'][i]), tuple(B['meshes'][i]))
        if k not in keys: keys[k] = len(progs); progs.append(list(X['meshes'][i]))
        elif progs[keys[k]] != list(X['meshes'][i]): raise ValueError('%s: bucket %d mixes programs in build %s' % (stem, keys[k], build))
    return progs, X['sky']


def view_terms(frag, bprog, sprog, progs, s, d, t):
    """per view: (fragment cycles incl. reads, vertex cycles) for set s"""
    out = []
    for w in frag['views']:
        f_c = v_c = 0.0
        for b, pp in enumerate(bprog):
            p = pp[s]
            if p < 0: continue
            # opaque: the visible pixels (HSR); alpha-tested: every fragment in front of the opaque depth (its visible
            # pixels are among them); blended: every fragment in front of the opaque + tested depth
            f = w['test'][b] if frag['buckets'][b][0] == 'test' else w['vis'][b] + w['blend'][b]; ft = progs[p]['fs']
            f_c += f * (ft[ALU] + d * ft['smp'] + t * ft['nondep']); v_c += w['verts'][b] * progs[p]['vs'][ALU]
        for l, pp in enumerate(sprog):
            p = pp[s]
            if p < 0: continue
            ft = progs[p]['fs']
            f_c += w['sky'][l] * (ft[ALU] + d * ft['smp'] + t * ft['nondep']); v_c += frag['sky_nv'][l] * progs[p]['vs'][ALU]
        out.append((f_c, v_c))
    return np.array(out)


class Case:   # one map of one build: the per-view terms for given read costs (cached)
    def __init__(s, S, stem, build):
        s.frag = json.load(open('%s/frag/%s.json' % (S, stem))); s.bprog, s.sprog = bucket_programs(S, stem, build); s.progs = load_programs(S, build); s.c = {}

    def terms(s, set_, d, t):
        key = (set_, round(d, 6), round(t, 6))
        if key not in s.c: s.c[key] = view_terms(s.frag, s.bprog, s.sprog, s.progs, set_, d, t)
        return s.c[key]


def gpu(T, other, k): return other + k * 1e-6 * (T[:, 0] + T[:, 1])


PROBE = re.compile(r'world probe (\w+) \(ms/frame\): (?:shipping )?([0-9.]+) \((\d+) frames\) \| opaque polynomial ([0-9.]+) \((\d+)\) \| no grading ([0-9.]+) \((\d+)\) \| all LUT \(b31\) ([0-9.]+) \((\d+)\)')


def device_probe(boot_log):
    rows = []
    for line in open(boot_log, errors='replace'):
        m = PROBE.search(line)
        if m:
            g = m.groups(); rows.append((g[0], [float(g[1]), float(g[3]), float(g[5]), float(g[7])], [int(g[2]), int(g[4]), int(g[6]), int(g[8])]))
    return rows


PROBE_OFF = re.compile(r'world probe (\w+) \(ms/frame \| GPU fragment ms/scene, vsync off\): opaque LUT(?: \(shipping\))? [0-9.]+ \| ([0-9.]+) \((\d+) frames\) \| '
                       r'opaque polynomial(?: \(shipping\))? [0-9.]+ \| ([0-9.]+) \((\d+)\) \| b37 float [0-9.]+ \| ([0-9.]+) \((\d+)\) \| no grading [0-9.]+ \| ([0-9.]+) \((\d+)\)')
PARTS = re.compile(r'gpu parts (\w+) \(GPU fragment ms/scene, (\w+) set, vsync off\): all ([0-9.]+) \((\d+)\) \| without the world ([0-9.]+) \((\d+)\)'
                   r' \| without characters ([0-9.]+) \((\d+)\) \| without effects, drops, skills ([0-9.]+) \((\d+)\) \| without the weapon ([0-9.]+) \((\d+)\) \| without the HUD ([0-9.]+) \((\d+)\)')


def device_probe41(boot_log):
    """b41+ device log (vsync off): per map the 4 world sets' GPU fragment ms/scene (sets 0 LUT, 1 poly, 2 float, 3 none)
    and the parts line (all, -world, -chars, -fx, -weapon, -HUD)"""
    sets = {}; parts = {}
    for line in open(boot_log, errors='replace'):
        m = PROBE_OFF.search(line)
        if m: g = m.groups(); sets[g[0]] = ([float(g[1]), float(g[3]), float(g[5]), float(g[7])], [int(g[2]), int(g[4]), int(g[6]), int(g[8])])
        m = PARTS.search(line)
        if m: g = m.groups(); parts[g[0]] = dict(set=1 if g[1] == 'polynomial' else 0, ms=[float(g[i]) for i in (2, 4, 6, 8, 10, 12)], n=[int(g[i]) for i in (3, 5, 7, 9, 11, 13)])
    return sets, parts


def fit41(S, boot_log, build='39', k_fixed=None, d_fixed=None, t_free=False, w_other=0.3):
    """fit the device's vsync-off GPU times (b41-2): G(map, set) = other_map + k * (F + V) over the probe sets; the parts
    line's 'without the world' is a second, weaker measure of other_map (weight 0.3: 1 s windows of other views)"""
    sets, parts = device_probe41(boot_log); maps = sorted(sets); cases = {m: Case(S, m, build) for m in maps}; mi = {m: i for i, m in enumerate(maps)}
    def unpack(x):
        j = 0; k = k_fixed if k_fixed else x[j]; j += 0 if k_fixed else 1
        d = d_fixed if d_fixed is not None else x[j]; j += 0 if d_fixed is not None else 1
        tt = x[j] if t_free else 0.0; j += 1 if t_free else 0
        return k, d, tt, x[j:]
    def resid(x):
        k, d, tt, o = unpack(x); r = []
        for m in maps:
            for s in range(4):
                G = gpu(cases[m].terms(s, d, tt), o[mi[m]], k); r.append(G.mean() - sets[m][0][s])
            if m in parts: r.append(w_other * (o[mi[m]] - parts[m]['ms'][1]))
        return np.array(r)
    x0 = ([] if k_fixed else [0.28]) + ([] if d_fixed is not None else [2.0]) + ([4.0] if t_free else []) + [7.0] * len(maps)
    R = least_squares(resid, np.array(x0), bounds=(0, 40), diff_step=1e-3, loss='soft_l1', f_scale=1.0)
    k, d, tt, o = unpack(R.x)
    rows = []; e = []; ew = []
    for m in maps:
        pr = [float(gpu(cases[m].terms(s, d, tt), o[mi[m]], k).mean()) for s in range(4)]; e += [p - q for p, q in zip(pr, sets[m][0])]
        if m in parts: ew.append(o[mi[m]] - parts[m]['ms'][1])
        rows.append(dict(map=m, other=float(o[mi[m]]), meas=sets[m][0], pred=pr, parts=parts.get(m)))
    e = np.array(e); ew = np.array(ew)
    return dict(k=float(k), d=float(d), t=float(tt), other={m: float(o[mi[m]]) for m in maps}, rms=float(np.sqrt((e ** 2).mean())), maxerr=float(np.abs(e).max()),
                rms_other=float(np.sqrt((ew ** 2).mean())) if len(ew) else 0.0, rows=rows, build=build)


def fit(S, boot_log, k_fixed=K_PHYS, permap=True):
    rows = device_probe(boot_log); cases = {}; data = []
    for st, ms, n in rows:
        cases.setdefault(st, Case(S, st, '32'))
        for s in range(4): data.append((st, s, ms[s], n[s]))
    maps = sorted(cases); mi = {m: i for i, m in enumerate(maps)}

    def unpack(x):
        k = k_fixed if k_fixed else x[0]; j = 0 if k_fixed else 1
        return k, x[j], x[j + 1], x[j + 2:]

    def resid(x):
        k, d, t, o = unpack(x); r = []
        for st, s, m, n in data:
            G = gpu(cases[st].terms(s, d, t), o[mi[st]] if permap else o[0], k)
            r.append((np.maximum(G, VSYNC).mean() - m) * math.sqrt(n / 150))
        return np.array(r)
    x0 = ([] if k_fixed else [0.28]) + [3.0, 0.5] + [9.0] * (len(maps) if permap else 1)
    R = least_squares(resid, np.array(x0), bounds=(0, 40), diff_step=1e-3)
    k, d, t, o = unpack(R.x); r = resid(R.x)
    return dict(k=float(k), d=float(d), t=float(t), other={m: float(o[mi[m]] if permap else o[0]) for m in maps}, rms=float(np.sqrt((r ** 2).mean())),
                maxerr=float(np.abs(r).max()), resid=[(st, s, m, float(m + rr)) for (st, s, m, n), rr in zip(data, r)])


def shipping_set(build, stem): return 0 if build == '32' else (1 if stem in POLY_MAPS else 0)


def other_of(P, stem):   # b43: maps without a device probe line take the mean of the others
    return P['other'][stem] if stem in P['other'] else float(np.mean(list(P['other'].values())))


def predict(S, P, build, stem, s=None):
    c = Case(S, stem, build); s = shipping_set(build, stem) if s is None else s
    o = other_of(P, stem); T = c.terms(s, P['d'], P['t']); G = gpu(T, o, P['k'])
    return dict(set=s, gpu=float(G.mean()), p90=float(np.percentile(G, 90)), world=float(G.mean() - o), frame=float(np.maximum(G, VSYNC).mean()))


def main():
    S, boot, cmd = sys.argv[1:4]
    if cmd == 'fit':
        for name, kw in (('one "other" for all maps, k fitted', dict(k_fixed=None, permap=False)), ('per-map "other", k fitted', dict(k_fixed=None)),
                         ('per-map "other", k = 1/(16 pipes x 222 MHz)', dict())):
            P = fit(S, boot, **kw)
            print('%-46s k %.3f ns/cycle  dependent read %.2f cycles  non-dependent read %.2f  rms %.2f ms (worst %.2f)' % (name, P['k'], P['d'], P['t'], P['rms'], P['maxerr']), flush=True)
        json.dump(P, open(os.path.join(S, 'fit.json'), 'w'), indent=1)
        rows = {}
        for st, s, m, p in P['resid']: rows.setdefault(st, []).append((m, p))
        print('\nmap            other | probe set: measured / model (ms/frame)   [0 LUT opaque (b32 shipping), 1 polynomial, 2 no grading, 3 all LUT]')
        for st in sorted(rows):
            print('%-13s %6.2f | ' % (st, P['other'][st]) + '  '.join('%5.2f/%5.2f' % mp for mp in rows[st][:4]))
        return
    if cmd == 'fit41':   # b43: the device's own GPU times (b41-2, vsync off)
        for name, kw in (('sets only (other free)', dict(w_other=0.0)), ('sets + "-world" weak (0.3)', dict()),
                         ('sets + "-world" (1.0), texture read cost fitted', dict(t_free=True, w_other=1.0))):
            P = fit41(S, boot, **kw)
            print('%-48s k %.3f ns/cycle  dependent read %.2f  texture read %.2f cycles | sets rms %.2f ms (worst %.2f) | other vs "-world" rms %.2f ms' % (
                name, P['k'], P['d'], P['t'], P['rms'], P['maxerr'], P['rms_other']), flush=True)
            if kw.get('t_free'): best = P
        P = best; json.dump({k: v for k, v in P.items() if k != 'rows'}, open(os.path.join(S, 'fit41.json'), 'w'), indent=1)
        print('\nmap            other  -world(meas) | sets LUT / poly / float / none: measured -> model (GPU ms/scene)')
        for r in P['rows']:
            print('%-13s %6.2f %6.2f       | ' % (r['map'], r['other'], r['parts']['ms'][1] if r['parts'] else float('nan')) + '  '.join('%5.2f->%5.2f' % (a, b) for a, b in zip(r['meas'], r['pred'])))
        return
    P = json.load(open(os.path.join(S, os.environ.get('FIT', 'fit.json'))))
    if cmd == 'table':
        builds = sys.argv[4:] or ['32', '37', '38', '39']; stems = sorted(os.path.basename(f)[:-5] for f in os.listdir(os.path.join(S, 'frag')) if f.endswith('.json'))
        res = {b: {st: predict(S, P, b, st) for st in stems} for b in builds}
        print('%-13s %6s | ' % ('map', 'other') + ' | '.join('b%s: GPU ms (world, p90 view)' % b for b in builds))
        for st in stems:
            print('%-13s %6.2f | ' % (st, other_of(P, st)) + ' | '.join('%5.2f (%4.2f, %5.2f) s%d' % (res[b][st]['gpu'], res[b][st]['world'], res[b][st]['p90'], res[b][st]['set']) for b in builds))
        json.dump(res, open(os.path.join(S, 'pred.json'), 'w'), indent=1)
    elif cmd == 'header':
        out = sys.argv[4]; lines = []; stems = sorted(os.path.basename(f)[:-5] for f in os.listdir(os.path.join(S, 'frag')) if f.endswith('.json'))
        for st in stems:
            w = [predict(S, P, HEADER_BUILD, st, s)['world'] for s in range(4)]
            lines.append('\t{ "%s", { %.2ff, %.2ff, %.2ff, %.2ff }, %.2ff },' % (st, w[0], w[1], w[2], w[3], other_of(P, st)))
        open(out, 'w').write('''// GPU cost model per map (render b%s; generated by tools/gpumodel/model.py header -- do not edit by hand).
// world[s]: the world's GPU ms per frame with the test run's program set s (0 LUT opaque, 1 polynomial opaque, both in
// half precision; 2 = set 0 in float, as b37 drew; 3 no grading), mean over the map's spawn views; other: the ms the
// fit gives everything else (characters, weapon, effects, HUD, clears). k %.4f ns per instruction-fragment (or cycle,
// b38), dependent read %.2f, texture read %.2f (fit: %s).
#pragma once
#define GPUMODEL_H
typedef struct { const char *stem; float world[4], other; } GpuModelMap;
static const GpuModelMap gpumodel[] = {
%s
};
#define GPUMODEL_N %d
''' % (HEADER_BUILD, P['k'], P['d'], P.get('t', 0.0), os.environ.get('FIT', 'fit.json'), '\n'.join(lines), len(lines)))
        print('wrote', out)


if __name__ == '__main__':
    main()
