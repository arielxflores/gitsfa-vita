# Azuma's plasma shield, from the game's own azuma_ally_shield_tier1 / tier2.nif (model.dat Object\shield):
#   engine/src/sk_shield_geo.h  outline loop of the panel (y, z), its curvature x(y), its planar UV mapping, and the
#                               break fragments ("crack": pieces + where each flies in 0.17 s, spin) for both tiers
#   build/data/sk_shield.gtx    one 256 x 512 DXT5 texture (GTX1 fmt 2) for every part of the effect:
#       A (x 0..127, y 0..255)   as_out_tier2: bright hex cells with darker borders (the panel's outline / flash texture)
#       B (128..255, 0..127)     cm_noise__big_line: the noise lines on the break pieces
#       B2 (128..255, 256..511)  cm_noise__big_line at its full 256 rows: the fine horizontal lines the panel shows when it is
#                                hit ('damage', the game's Shield video frame 186)
#       C (128..191, 128..191)   linkable_substrate_glow: the round glow billboard of the start / end flash
#       D (192..255, 128..143)   rim profile: a soft tube cross-section (the panel's solid rim, drawn as a ribbon)
#       F (192..207, 144..159)   flat white (per-vertex fills)
#       E (192..255, 160..255)   as_solid_tier2, blurred: the panel's always-on fill (dark middle band, brighter sides)
#       H (0..127, 256..511)     as_dark_ally_tier2: the same hex grid as A, as thin bright lines on black (b50: Tier 2's rising
#                                scan band - the game's Shield video shows glowing hex lines, not cells)
# NIF frame: x = forward (away from the holder), y = left, z = up, cm; the panel node is offset 6.5 cm to the right (y -6.5)
# and the first-person footage (Shield.usm, fitted in tools/skills/SKILLS.md 3.7) puts the panel centred on the view axis,
# so the tables are centred on y = 0.
# databuilder: the tables are data now, build/data/sk_shield.gsg (engine/src/sk_shield_geo.h loads it; it used to be the
# generated header itself). --c OUT.h writes the old header text (checks).
# usage: python3 make_shield.py [datadir] [--c OUT.h]
import sys, os, struct, re, numpy as np, etcpak
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'fx'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import sklib, nifps, gitspaths
from PIL import Image, ImageFilter
_a = [a for i, a in enumerate(sys.argv[1:]) if not a.startswith('--') and sys.argv[i] != '--c']
OUT = _a[0] if _a else gitspaths.root('DATA')
CTEXT = sys.argv[sys.argv.index('--c') + 1] if '--c' in sys.argv else None
R = sklib.RAW + 'model/Object/shield/'
PNG = sklib.PNG

def loop_of(P, I):
    """ordered boundary loop (vertex indices) of a triangle mesh with one boundary"""
    from collections import Counter, defaultdict
    T = I.reshape(-1, 3); E = Counter(tuple(sorted((a, b))) for t in T for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])))
    adj = defaultdict(list)
    for (a, b), c in E.items():
        if c == 1: adj[a].append(b); adj[b].append(a)
    start = min(adj, key=lambda k: (-P[k, 2], P[k, 1])); L = [start]; prev = None
    while True:
        nx = [k for k in adj[L[-1]] if k != prev]
        if not nx: break
        n = nx[0] if len(nx) == 1 or nx[0] != start else nx[1]
        if n == start: break
        prev = L[-1]; L.append(n)
    # counter-clockwise seen from behind the panel (from -x): y left, z up -> order by angle
    c = P[L, 1:3].mean(0); ang = np.arctan2(P[L, 2] - c[1], P[L, 1] - c[0])
    if np.sum(np.diff(np.unwrap(ang))) < 0: L = L[::-1]
    return L

def tier(t):
    path = R + 'azuma_ally_shield_tier%d.nif' % t
    sm = sklib.static_parts(path)
    g = [g for g in sm if g['node'] == 'CollMesh'][0]; P = g['P'].copy(); P[:, 1] += 6.5
    L = loop_of(P, g['I'])
    A = np.c_[P[:, 1], P[:, 2], np.ones(len(P))]
    cu = np.linalg.lstsq(A, g['UV'][:, 0], rcond=None)[0]; cv = np.linalg.lstsq(A, g['UV'][:, 1], rcond=None)[0]
    cx = np.polyfit(P[:, 1], P[:, 0], 2)
    # break fragments: the as_solid pieces under 'crack' (their cm_noise twins share the geometry), each node's path
    h, B = nifps.parse(path)
    byname = {b.get('name'): (k, b) for k, b in enumerate(B) if b['type'] == 'NiNode'}
    crack = byname['crack'][1]; ct = np.array(crack['t'])
    frags = []
    for f in sm:
        if not (f['node'] or '').startswith(('T20_frag', 'T15_frag')) or 'solid' not in (f['tex'] or ''): continue
        k, nb = byname[f['node']]; t0 = np.array(nb['t'])
        tr = B[nb['ctrl']]; ip = B[tr['interp']]; D = B[ip['data']]
        keys = D['trans']['keys']; d = np.array(keys[-1]['v']) - np.array(keys[0]['v']); tend = keys[-1]['t']
        rot = [ (g2['keys'][-1]['v'] - g2['keys'][0]['v']) if g2['keys'] else 0.0 for g2 in D.get('xyz', [{'keys': []}] * 3)]
        Q = f['P'] - (ct + t0); Q[:, 1] += 0   # piece about its own node origin
        org = ct + t0; org = org.copy(); org[1] += 6.5
        frags.append(dict(P=Q, I=f['I'], org=org, d=d, rot=np.array(rot, float), tend=tend))
    return dict(P=P, L=L, cu=cu, cv=cv, cx=cx, frags=frags, hw=np.abs(P[:, 1]).max(), hh=np.abs(P[:, 2]).max(), xr=(P[:, 0].min(), P[:, 0].max()))

T = [tier(1), tier(2)]

# ---------------------------------------------------------------- header
o = ['// generated by tools/skills/make_shield.py from the game\'s azuma_ally_shield_tier1 / tier2.nif - do not edit',
     '// NIF frame: x forward (away from the holder), y left, z up, cm; centred on the view axis (y = 0), z = 0 at the panel centre',
     '#pragma once']
for k, d in enumerate(T):
    n = len(d['L'])
    o.append('static const float SKSH_OUT%d[%d][2] = {   // panel outline loop (y, z)' % (k + 1, n))
    o.append('\t' + ', '.join('{ %.2ff, %.2ff }' % (d['P'][i, 1], d['P'][i, 2]) for i in d['L']) + ' };')
o.append('static const int SKSH_NOUT[2] = { %d, %d };' % (len(T[0]['L']), len(T[1]['L'])))
o.append('static const float (*const SKSH_OUT[2])[2] = { SKSH_OUT1, SKSH_OUT2 };')
o.append('static const float SKSH_CURV[2][2] = { %s };   // x = a + b * y^2 (the panel bulges forward in the middle)' % ', '.join('{ %.3ff, %.6ff }' % (d['cx'][2], d['cx'][0]) for d in T))
o.append('static const float SKSH_UVMAP[2][4] = { %s };   // u = a + b * y, v = c + d * z (the NIF\'s own planar UVs)' % ', '.join('{ %.4ff, %.6ff, %.4ff, %.6ff }' % (d['cu'][2], d['cu'][0], d['cv'][2], d['cv'][1]) for d in T))
o.append('static const float SKSH_DIM[2][3] = { %s };   // half width, half height, half depth (cm)' % ', '.join('{ %.1ff, %.1ff, %.1ff }' % (d['hw'], d['hh'], (d['xr'][1] - d['xr'][0]) / 2) for d in T))
for k, d in enumerate(T):
    V = []; I = []; F = []
    for f in d['frags']:
        b = len(V); V += [tuple(p) for p in f['P']]; I += list(np.asarray(f['I']) + b)
        F.append((b, len(f['P']), len(I) - len(f['I']), len(f['I']), *f['org'], *f['d'], *f['rot']))
    o.append('static const float SKSH_FV%d[%d][3] = {   // break fragments: vertices about each piece\'s origin (x, y, z)' % (k + 1, len(V)))
    o.append('\t' + ', '.join('{ %.1ff, %.1ff, %.1ff }' % v for v in V) + ' };')
    o.append('static const unsigned short SKSH_FI%d[%d] = { %s };' % (k + 1, len(I), ', '.join(str(int(i)) for i in I)))
    o.append('typedef struct { unsigned short v0, nv, i0, ni; float org[3], d[3], rot[3]; } SkShFrag;' if k == 0 else '')
    o.append('static const SkShFrag SKSH_FR%d[%d] = {   // first vertex, count, first index, count, origin, flight in 0.17 s, spin (rad)' % (k + 1, len(F)))
    o.append('\t' + ', '.join('{ %d, %d, %d, %d, { %.1ff, %.1ff, %.1ff }, { %.1ff, %.1ff, %.1ff }, { %.2ff, %.2ff, %.2ff } }' % f for f in F) + ' };')
o.append('static const int SKSH_NFR[2] = { %d, %d };' % (len(T[0]['frags']), len(T[1]['frags'])))
o.append('static const SkShFrag *const SKSH_FR[2] = { SKSH_FR1, SKSH_FR2 };')
o.append('static const float (*const SKSH_FV[2])[3] = { SKSH_FV1, SKSH_FV2 };')
o.append('static const unsigned short *const SKSH_FI[2] = { SKSH_FI1, SKSH_FI2 };')
ctext = '\n'.join(x for x in o if x != '') + '\n'
if CTEXT: open(CTEXT, 'w').write(ctext)
# ---- databuilder: sk_shield.gsg, parsed from the header text above so every number is the float the compiler made of it
#   'GSG1', then per tier: u32 nout, nfv, nfi, nfr; f32 out[nout][2]; f32 curv[2], uvmap[4], dim[3]; f32 fv[nfv][3];
#   u16 fi[nfi] (+ u16 pad when odd); nfr x { u16 v0, nv, i0, ni; f32 org[3], d[3], rot[3] }
def _f32(t):
    from fractions import Fraction
    q = Fraction(t.rstrip('f')); x = np.float32(float(q)); best = None
    for c in (np.nextafter(x, np.float32(-np.inf)), x, np.nextafter(x, np.float32(np.inf))):
        dd = abs(Fraction(float(c)) - q); ev = (int(np.float32(c).view(np.uint32)) & 1) == 0
        if best is None or dd < best[0] or (dd == best[0] and ev): best = (dd, c)
    if q == 0 and t.lstrip().startswith('-'): return struct.pack('<f', -0.0)   # '-0.00f' is -0.0f to the compiler
    return struct.pack('<f', best[1])
def _nums(line): return re.findall(r'-?[0-9]+(?:\.[0-9]+)?f?', line.split('=', 1)[1].split('//')[0])
_ln = {re.match(r'static const [\w ]+?\(?\*?(?:const )?(\w+)', l).group(1): l for l in o if l.startswith('static const')}
_txt = '\n'.join(o)
def _body(name):   # the initializer text of an array (it may run over the next line)
    i = _txt.index(' ' + name + '['); j = _txt.index('=', i); k = _txt.index(';', j); return _txt[j + 1:k]
_curv = [x for x in re.findall(r'-?[0-9.]+f', _body('SKSH_CURV'))]; _uv = re.findall(r'-?[0-9.]+f', _body('SKSH_UVMAP')); _dim = re.findall(r'-?[0-9.]+f', _body('SKSH_DIM'))
gsg = b'GSG1'
for t in (0, 1):
    out_ = re.findall(r'-?[0-9.]+f', _body('SKSH_OUT%d' % (t + 1))); fv = re.findall(r'-?[0-9.]+f', _body('SKSH_FV%d' % (t + 1)))
    fi = [int(x) for x in re.findall(r'[0-9]+', _body('SKSH_FI%d' % (t + 1)))]
    fr = re.findall(r'\{ (\d+), (\d+), (\d+), (\d+), \{ ([^}]*) \}, \{ ([^}]*) \}, \{ ([^}]*) \} \}', _body('SKSH_FR%d' % (t + 1)))
    gsg += struct.pack('<4I', len(out_) // 2, len(fv) // 3, len(fi), len(fr))
    gsg += b''.join(_f32(x) for x in out_) + b''.join(_f32(x) for x in _curv[2 * t:2 * t + 2] + _uv[4 * t:4 * t + 4] + _dim[3 * t:3 * t + 3])
    gsg += b''.join(_f32(x) for x in fv) + struct.pack('<%dH' % len(fi), *fi) + (b'\0\0' if len(fi) % 2 else b'')
    for r in fr: gsg += struct.pack('<4H', *map(int, r[:4])) + b''.join(_f32(x.strip()) for g in r[4:] for x in g.split(','))
# optional trailing chunk (b50): 'SHX1', u32 size, then u8 noise[32][32] = the dark map the panel's hex surface is multiplied by
# (CollMesh: as_out_tier2 x turret_laser_noise, NiTextureTransformController slot 1 scrolling u +1 / v -1 every 8 s) - the
# engine samples it per vertex - and u8 prof[16] = the brightest texel of each sixteenth of region E (the as_solid fill) along u,
# so quads that would add nothing are not drawn
_nz = np.asarray(Image.open(PNG + 'turret_laser_noise.png').convert('L').resize((32, 32), Image.LANCZOS), np.uint8)
_sol = np.asarray(Image.fromarray((np.asarray(Image.open(PNG + 'as_solid_tier2.png').convert('RGBA').resize((64, 96), Image.LANCZOS), np.float32)[..., :3].mean(2)).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3)), np.uint8)
_prof = np.array([_sol[:, k * 4:(k + 1) * 4].max() for k in range(16)], np.uint8)
# u8 up[32]: upup.dds (T03, Tier 2's rising band) down the texture, v 0.50 .. 0.72 (it is black elsewhere; constant along u)
_upr = np.asarray(Image.open(PNG + 'upup.png').convert('RGBA'), np.float32)[..., :3].mean(2).mean(1)
_up = np.array([np.interp(0.50 + 0.22 * (k + 0.5) / 32, (np.arange(len(_upr)) + 0.5) / len(_upr), _upr) for k in range(32)])
gsg += b'SHX1' + struct.pack('<I', 32 * 32 + 16 + 32) + _nz.tobytes() + _prof.tobytes() + np.clip(_up + 0.5, 0, 255).astype(np.uint8).tobytes()
os.makedirs(OUT, exist_ok=True)
open(os.path.join(OUT, 'sk_shield.gsg'), 'wb').write(gsg)

# ---------------------------------------------------------------- texture
W = 256; TH = 512; img = np.zeros((TH, W, 4), np.float32); img[..., :3] = 1
def lum(name, w, h):
    a = np.asarray(Image.open(PNG + name + '.png').convert('RGBA').resize((w, h), Image.LANCZOS), np.float32) / 255
    return a[..., :3].mean(2)
img[0:256, 0:128, 3] = lum('as_out_tier2', 128, 256)
img[0:128, 128:256, 3] = lum('cm_noise__big_line', 128, 128)
img[256:512, 128:256, 3] = lum('cm_noise__big_line', 128, 256)   # B2 (b50): every line row kept
img[128:192, 128:192, 3] = lum('linkable_substrate_glow', 64, 64)
v = (np.arange(16) + 0.5) / 16; prof = np.clip(1 - np.abs(v - 0.5) / 0.5, 0, 1); prof = np.clip(prof * 1.6, 0, 1) ** 0.8
img[128:144, 192:256, 3] = prof[:, None]
img[144:160, 192:208, 3] = 1
sol = Image.fromarray((lum('as_solid_tier2', 64, 96) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3))   # its gradient, not its hex lines: the
img[160:256, 192:256, 3] = np.asarray(sol, np.float32) / 255                                                   # game's first-person haze shows no lines
hl = np.asarray(Image.open(PNG + 'as_dark_ally_tier2.png').convert('RGBA').resize((128, 256), Image.BOX), np.float32)[..., :3].mean(2) / 255
img[256:512, 0:128, 3] = np.clip(hl / max(hl.max(), 1e-3), 0, 1)   # (box filter: the thin lines stay; normalised to full brightness)
q = (np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)
dxt = etcpak.compress_to_dxt5(np.ascontiguousarray(q).tobytes(), W, TH)
os.makedirs(OUT, exist_ok=True)
with open(os.path.join(OUT, 'sk_shield.gtx'), 'wb') as f:
    f.write(b'GTX1' + struct.pack('<HHBBHI', W, TH, 2, 0, 0, len(dxt))); f.write(dxt)
print('sk_shield.gsg', len(gsg), 'bytes; sk_shield.gtx', 16 + len(dxt), 'bytes;',
      'outline', [len(d['L']) for d in T], 'frags', [len(d['frags']) for d in T])
