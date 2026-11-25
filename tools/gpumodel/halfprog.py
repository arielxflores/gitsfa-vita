#!/usr/bin/env python3
"""fp16 against fp32 for the b39 half-precision programs outside the world (render b39): model.h's model program (characters,
viewmodel, drops), wpn_vm.h's lens, vision.h's VIS_FS / VIS_FS_R, hud.h's HUD_FS (HUD and menus), sprite.h's SP_FS (effects).

Each program's maths is written twice with the shader's operation order: in fp32 (the b38 float program) and with every
operation rounded to fp16 (the b39 half program; texture coordinates and the LUT address stay fp32, as in the sources).
Inputs are the game's own data: character / viewmodel / lens textures (build/data/*.gvm), the HUD and menu atlases with
the packs' node colours (*.ghud), the effects atlas (fx_sk.gfx), each map's fog and grading LUT (main.c MAPS, *.glut).
Varyings and uniforms the data does not give (shading, fog amount, fresnel, effect colours) are drawn over the ranges the
code sets. Error: |8-bit output (half) - 8-bit output (float)|, the worst channel per fragment, in steps of 255; and the
same on screen: both outputs blended over scene texels with the program's blend function (what the player sees).
usage: halfprog.py <out sheet.jpg> [samples per program]     (BUILD /root/gits/build) -> table on stdout
"""
import sys, os, re, struct
import numpy as np
from PIL import Image, ImageDraw
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..', 'vision')); sys.path.insert(0, os.path.join(HERE, '..', 'ui'))
from gradeprec import lut_eval, lut_eval_half
from gvm import dds_rgba
import ghud_read
B = '/root/gits/build'; D = B + '/data'
h = np.float16; f = np.float32
rng = np.random.default_rng(39)


# ---- fp16 arithmetic: every operation result rounded to half (numpy float16 does each op in fp32, then rounds once)
def H(x): return np.asarray(x, dtype=np.float64).astype(h)
def lerp(a, b, t): return a + (b - a) * t                      # (the USSE's lerp: a + t (b - a))
def sat(x, one): return np.clip(x, 0 * one, one)
def q8(x): return np.round(np.clip(np.asarray(x, np.float64), 0, 1) * 255)


def tex8(a): return a.astype(np.float64) / 255.0               # a texel as the sampler returns it (8-bit unorm)


# ---- data
def gvm_textures(path, lens_only=False):
    """GVM1 textures (decoded mip 0) + the lens meshes' texture ids and attachments (model.h layout)"""
    d = open(path, 'rb').read(); o = 4
    ntex, nb, npal, nmesh, nclip = struct.unpack_from('<5I', d, o); o += 20; tex = []
    for i in range(ntex):
        w, hh, fmt, nmp = struct.unpack_from('<HHBB', d, o); o += 6; lv = []
        for m in range(nmp):
            sz = struct.unpack_from('<I', d, o)[0]; o += 4; lv.append(d[o:o + sz]); o += sz
        tex.append((w, hh, fmt, lv[0]))
    o += 32 * nb + 52 * npal; lens = set()
    for i in range(nmesh):
        t, blend, nv, ni = struct.unpack_from('<iiii', d, o); o += 16 + nv * 32 + ni * 2 + (2 if ni & 1 else 0)
        if blend & 128: lens.add(t)
    for i in range(nclip):
        nf = struct.unpack_from('<i', d, o + 16)[0]; o += 28 + nf * nb * 28
    att = {}
    if o + 4 <= len(d):
        na = struct.unpack_from('<I', d, o)[0]; o += 4
        for i in range(min(na, 8)):
            nm = d[o:o + 16].split(b'\0')[0].decode(); att[nm] = struct.unpack_from('<3f', d, o + 16); o += 28
    want = sorted(lens) if lens_only else range(ntex)
    return {i: dds_rgba(*tex[i][:2], tex[i][3], tex[i][2]) for i in want if 0 <= i < ntex}, lens, att


def maps():
    t = open(os.path.join(HERE, '..', '..', 'engine', 'src', 'main.c')).read(); out = []
    for m in re.finditer(r'\{\s*"(\w+)",\s*"[^"]*",\s*\{\s*([-0-9.e]+)f?,\s*([-0-9.e]+)f?,\s*([-0-9.e]+)f?\s*\},\s*\{\s*([-0-9.e]+)f?,\s*([-0-9.e]+)f?,\s*([-0-9.e]+)f?\s*\}', t):
        raw = open('%s/%s.glut' % (D, m.group(1)), 'rb').read()
        out.append(dict(stem=m.group(1), fogc=np.array([float(m.group(i)) for i in (2, 3, 4)]), fogmax=float(m.group(7)),
                        lut=np.frombuffer(raw[:12288], np.uint8).reshape(16, 256, 3)))
    return out


def texels(imgs, n):
    """n texels drawn from a list of RGBA images (opaque-ish and transparent alike)"""
    out = []; cnt = np.bincount(rng.integers(0, len(imgs), n), minlength=len(imgs))
    for im, c in zip(imgs, cnt):
        a = im.reshape(-1, 4); out.append(a[rng.integers(0, len(a), c)])
    return np.concatenate(out)


def grade(fc32, fc16, lut):
    """the LUT2 tail: float (grade_code) on the float fc, half (grade_code_h) on the half fc"""
    return lut_eval(lut, fc32.astype(np.float64)), lut_eval_half(lut, fc16.astype(np.float64))


# ---- the programs (float, half); inputs as float64 arrays, outputs (rgb, a) in [0, 1]-ish
def model_prog(c, sh, tint, fogc, fog, lut):
    # MD_FS_BODY / MD_FS_BODY_H + LUT2 tail; return float4(gc, c.a * tint.a)
    cf = c.astype(f); fc32 = lerp(cf[:, :3] * sh.astype(f)[:, None] * tint[:, :3].astype(f), fogc.astype(f), (fog * 1.0).astype(f)[:, None])
    ch = H(c); fc16 = lerp(ch[:, :3] * H(sh)[:, None] * H(tint[:, :3]), H(fogc), (H(fog) * H(1.0))[:, None])
    g32, g16 = grade(fc32, fc16, lut)
    return (g32, cf[:, 3] * tint[:, 3].astype(f)), (g16, ch[:, 3] * H(tint[:, 3]))


def lens_prog(b, e, uv, fr, tint, coat, lk, lut):
    def run(T, cast):
        bb = cast(b); ee = cast(e)[:, :3]; d = cast(uv) * T(2.0) - T(1.0); rr = sat((d * d).sum(1).astype(d.dtype), T(1.0))
        k = cast(lk); ct = cast(coat); one = T(1.0)
        a = lerp(ct[:, 3], lerp(sat(bb[:, 3] * T(2.0) - one, one), bb[:, 3], k[:, 1]), k[:, 0])
        bc = bb[:, :3] * lerp(one, k[:, 1], k[:, 0])[:, None]
        rf = ee * ct[:, :3] * (cast(fr) * lerp(k[:, 3] + (one - k[:, 3]) * rr, rr * rr * k[:, 2], k[:, 0] * (one - k[:, 1])))[:, None]
        ao = sat(a + (rf * np.array([0.3, 0.5, 0.2]).astype(rf.dtype)).sum(1).astype(rf.dtype), one)
        fc = (bc * a[:, None] + rf) * cast(tint)[:, :3] / np.maximum(ao, T(0.002))[:, None]
        return fc, ao * cast(tint)[:, 3]
    fc32, a32 = run(f, lambda x: np.asarray(x, np.float64).astype(f)); fc16, a16 = run(h, H)
    g32, g16 = grade(fc32, fc16, lut)
    return (g32, a32), (g16, a16)


def vis_prog(d, pt, fr, col, rim, prm):
    def run(T, cast):
        q = cast(prm); ff = cast(fr); f2 = ff * ff; r = lerp(f2, f2 * f2, q[:, 3]); one = T(1.0)
        p = one + (cast(pt) - one) * q[:, 2]; dd = cast(d)
        c = (cast(col)[:, :3] + cast(rim)[:, :3] * r[:, None]) * p[:, None] + dd[:, :3] * q[:, 1:2]
        a = sat((cast(col)[:, 3] + cast(rim)[:, 3] * r) * p + dd[:, 3] * q[:, 0], one)
        return c, a
    return run(f, lambda x: np.asarray(x, np.float64).astype(f)), run(h, H)


def visr_prog(bg, d, pt, fr, rfr, col, rim, prm, shd):
    def run(T, cast):
        dd = cast(d); q = cast(prm); one = T(1.0)
        l = (dd[:, :3] * np.array([0.30, 0.59, 0.11]).astype(dd.dtype)).sum(1).astype(dd.dtype)
        ff = cast(fr); f2 = ff * ff; r = f2 + (f2 * f2 - f2) * q[:, 3]; p = one + (cast(pt) - one) * q[:, 2]
        c = cast(bg)[:, :3] * cast(rfr)[:, :3] * (one - cast(shd) * (one - l))[:, None] + cast(col)[:, :3] * (l * q[:, 0] + q[:, 1])[:, None] + cast(rim)[:, :3] * (r * p)[:, None]
        return c, sat(cast(rfr)[:, 3] + cast(col)[:, 3] + cast(rim)[:, 3] * r, one)
    return run(f, lambda x: np.asarray(x, np.float64).astype(f)), run(h, H)


def hud_prog(c, r, th, m0, a0):
    k32 = np.clip((np.asarray(th, np.float64).astype(f) - np.asarray(r, np.float64).astype(f)) * f(200.0) + f(0.5), 0, 1).astype(f)   # (the ramp stays float)
    def run(T, cast):
        cc = cast(c); k = k32.astype(T); cc = cc * k[:, None]
        m = cast(m0); rgb = (cc[:, :3] * m[:, :3] + cast(a0)[:, :3] * cc[:, 3:4]) * m[:, 3:4]; a = cc[:, 3] * m[:, 3]
        return np.minimum(rgb, a[:, None]), a
    return run(f, lambda x: np.asarray(x, np.float64).astype(f)), run(h, H)


def sprite_prog(t, c0):
    o32 = np.asarray(t, np.float64).astype(f) * np.asarray(c0, np.float64).astype(f); o16 = H(t) * H(c0)
    return (o32[:, :3], o32[:, 3]), (o16[:, :3], o16[:, 3])


def err(A, Bh):
    e = np.maximum(np.abs(q8(A[0]) - q8(Bh[0])).max(1), np.abs(q8(A[1]) - q8(Bh[1])))
    return e


def screen(o, dst, blend):
    """the program's output on the colour buffer (8-bit), with its blend function"""
    rgb = np.clip(q8(o[0]) / 255, 0, 1); a = np.clip(q8(o[1]) / 255, 0, 1)[:, None]
    if blend == 'opaque': return q8(rgb)
    if blend == 'alpha': return q8(rgb * a + dst * (1 - a))      # SRC_ALPHA, ONE_MINUS_SRC_ALPHA
    if blend == 'add': return q8(rgb * a + dst)                  # SRC_ALPHA, ONE
    if blend == 'premul': return q8(rgb + dst * (1 - a))         # ONE, ONE_MINUS_SRC_ALPHA
    raise ValueError(blend)


def err_screen(A, Bh, dst, blend): return np.abs(screen(A, dst, blend) - screen(Bh, dst, blend)).max(1)


# ---- inputs per program
def load_inputs(n):
    M = maps(); I = {}
    chr_imgs = []
    for p in sorted(os.listdir(D)):
        if p.startswith('chr_') and p.endswith('.gvm') and 'dummy' not in p:
            t, _, _ = gvm_textures(os.path.join(D, p)); chr_imgs += [v for v in t.values()][:3]
    vm_imgs = []; lens = []
    for p in ('vm_m4a1.gvm', 'vm_scar.gvm', 'vm_acr.gvm', 'vm_kriss.gvm', 'vm_l118.gvm', 'vm_mp5.gvm', 'vm_seburom5.gvm'):
        t, ls, att = gvm_textures(os.path.join(D, p)); vm_imgs += list(t.values())[:3]
        if ls and 'lensenv' in att:
            le = att['lensenv']; lc = att.get('lenscoat', (0.5, 0.5, 0.5))
            lens.append(dict(name=p[3:-4], tex=[t[i] for i in sorted(ls)], env=t.get(int(le[0])), le=le, lc=lc))
    I['maps'] = M; I['chr'] = chr_imgs; I['vm'] = vm_imgs; I['lens'] = lens
    P = ghud_read.read(D + '/hud.ghud'); dd = open(D + '/hud.ghud', 'rb').read(); (w0, h0), (w1, h1) = P['tex']
    w2, h2 = P['tex2']; I['hud_atlas'] = [np.frombuffer(dd, np.uint8, w0 * h0 * 4, len(dd) - (w0 * h0 + w1 * h1 + w2 * h2) * 4).reshape(h0, w0, 4)]
    I['hud_nodes'] = [(np.array(v['mul']), np.array(v['add'])) for v in P['nodes'].values()]
    P2 = ghud_read.read(D + '/menu.ghud'); d2 = open(D + '/menu.ghud', 'rb').read(); (w0, h0), (w1, h1) = P2['tex']
    w2, h2 = P2['tex2']; I['hud_atlas'].append(np.frombuffer(d2, np.uint8, w0 * h0 * 4, len(d2) - (w0 * h0 + w1 * h1 + w2 * h2) * 4).reshape(h0, w0, 4))   # (b92: + the third page)
    I['hud_nodes'] += [(np.array(v['mul']), np.array(v['add'])) for v in P2['nodes'].values()]
    g = open(D + '/fx_sk.gfx', 'rb').read(); aw, ah, nm = struct.unpack_from('<HHI', g, 4); sz = struct.unpack_from('<I', g, 12)[0]
    I['fx'] = dds_rgba(aw, ah, g[16:16 + sz], 5)
    return I


def run_all(I, n):
    """per program: (output error, on-screen error after its blend over scene texels)"""
    R = {}; M = I['maps']; bgimgs = I['chr'] + I['vm']
    def add(name, AB, blend, k):
        dst = tex8(texels(bgimgs, k))[:, :3]; R.setdefault(name, []).append((err(*AB), err_screen(AB[0], AB[1], dst, blend)))
    # model: characters and viewmodel textures, every map's fog and LUT (opaque; the alpha / additive meshes blend the same output)
    for name, imgs in (('model (characters)', I['chr']), ('model (viewmodel, drops)', I['vm'])):
        for m in M:
            c = tex8(texels(imgs, n // len(M))); k = len(c)
            sh = rng.uniform(0.40, 1.15, k); tint = np.ones((k, 4)); tint[:, :3] = rng.choice([1.0, 0.9, 0.95, 0.8, 1.2], (k, 3)); tint[k // 2:, 3] = rng.uniform(0, 1, k - k // 2)
            fog = rng.uniform(0, m['fogmax'], k)
            add(name, model_prog(c, sh, tint, m['fogc'], fog, m['lut']), 'opaque', k)
    # lens: each lens weapon (tube lenses at hip / half / aimed, glass), four maps' LUTs (SRC_ALPHA blend)
    for L in I['lens']:
        for m in M[::4]:
            b = tex8(texels(L['tex'], n // 20)); k = len(b); e = tex8(texels([L['env'] if L['env'] is not None else L['tex'][0]], k))
            uv = rng.uniform(0, 1, (k, 2)); fr = rng.uniform(0.6, 1.0, k); tint = np.ones((k, 4)); tint[:, :3] = rng.uniform(0.7, 1.2, (k, 3))
            glass = L['le'][1] >= 1; op = rng.choice([0.0, 0.5, 1.0], k) if not glass else np.ones(k)
            coat = np.tile(np.array([*L['lc'], 1.0 if glass else L['le'][1]]), (k, 1))
            lk = np.stack([op, np.full(k, 1.0 if glass else 0.0), np.full(k, 0.6), np.full(k, L['le'][2])], 1)
            add('lens (ACOG, holo, scope glass)', lens_prog(b, e, uv, fr, tint, coat, lk, m['lut']), 'alpha', k)
    # vision: character textures, pattern values, the looks' colour ranges (outlines: SRC_ALPHA + ONE; camo: premultiplied)
    k = n; d = tex8(texels(I['chr'], k)); pt = rng.uniform(0, 1, k); fr = rng.uniform(0, 1, k)
    col = np.c_[rng.uniform(0, 1, (k, 3)), rng.uniform(0, 1, k)]; rim = np.c_[rng.uniform(0, 2, (k, 3)), rng.uniform(0, 1, k)]
    prm = np.c_[rng.uniform(0, 1, k), rng.uniform(0, 1, k), rng.uniform(0, 1, k), rng.choice([0.0, 1.0], k)]
    add('vision VIS_FS (outlines, x-ray, hologram)', vis_prog(d, pt, fr, col, rim, prm), 'add', k)
    bg = tex8(texels(bgimgs, k)); rfr = np.c_[rng.uniform(0.5, 1.2, (k, 3)), rng.uniform(0, 0.5, k)]; shd = rng.uniform(0, 1, k)
    add('vision VIS_FS_R (camo refraction)', visr_prog(bg, d, pt, fr, rfr, col * 0.5, rim * 0.5, prm, shd), 'premul', k)
    # HUD and menus: atlas texels with the packs' node colours; gauges with a ramp threshold, the rest th = 2 (premultiplied)
    c = tex8(texels(I['hud_atlas'], n)); k = len(c); r = tex8(texels(I['hud_atlas'], k))[:, 0]
    th = np.where(rng.uniform(0, 1, k) < 0.3, rng.uniform(0, 1, k), 2.0)
    ni = rng.integers(0, len(I['hud_nodes']), k); m0 = np.array([I['hud_nodes'][i][0] for i in ni]); a0 = np.array([I['hud_nodes'][i][1] for i in ni])
    m0[:, 3] = np.where(rng.uniform(0, 1, k) < 0.5, m0[:, 3], rng.uniform(0, 1, k))
    add('HUD / menus HUD_FS', hud_prog(c, r, th, m0, a0), 'premul', k)
    # effects: the atlas times vertex colours (alpha blend)
    t = tex8(texels([I['fx']], n)); c0 = rng.uniform(0, 1, (len(t), 4)); c0[: len(t) // 3] = 1.0
    add('effects SP_FS (particles, decals, shadows)', sprite_prog(t, c0), 'alpha', len(t))
    return {k: (np.concatenate([a for a, b in v]), np.concatenate([b for a, b in v])) for k, v in R.items()}


# ---- sheet: each program on a picture of its own data (float | half | difference x32)
def pictures(I):
    rows = []; m = [x for x in I['maps'] if x['stem'] == 'urban'][0]
    def grid(im, W=320, Hh=180):
        return np.asarray(Image.fromarray(im).convert('RGBA').resize((W, Hh), Image.BILINEAR)).reshape(-1, 4).astype(np.float64) / 255.0
    W, Hh = 320, 180; yy, xx = np.mgrid[0:Hh, 0:W]; u = (xx / (W - 1)).ravel(); v = (yy / (Hh - 1)).ravel(); k = W * Hh
    c = grid(I['chr'][0]); tint = np.ones((k, 4))
    rows.append(('model, character (urban: fog, LUT)', model_prog(c, 0.4 + 0.75 * u, tint, m['fogc'], v * m['fogmax'], m['lut'])))
    if I['lens']:
        L = [x for x in I['lens'] if x['name'] == 'scar'] or I['lens']; L = L[0]
        b = grid(L['tex'][0]); e = grid(L['env'] if L['env'] is not None else L['tex'][0]); uv = np.c_[u, v]
        coat = np.tile(np.array([*L['lc'], L['le'][1]]), (k, 1)); lk = np.stack([np.where(u > 0.5, 1.0, 0.0), np.zeros(k), np.full(k, 0.6), np.full(k, L['le'][2])], 1)
        rows.append(('lens, %s (left hip, right aimed)' % L['name'], lens_prog(b, e, uv, 0.6 + 0.4 * v, tint, coat, lk, m['lut'])))
    d = grid(I['chr'][1]); col = np.tile([0.2, 0.6, 1.0, 0.3], (k, 1)); rim = np.tile([0.6, 1.2, 1.6, 0.8], (k, 1)); prm = np.tile([0.4, 0.5, 0.6, 0.0], (k, 1))
    rows.append(('vision VIS_FS (blue team look)', vis_prog(d, (np.sin(u * 40) * 0.5 + 0.5), v, col, rim, prm)))
    bg = grid(I['vm'][0]); rows.append(('vision VIS_FS_R (camo)', visr_prog(bg, d, (np.sin(v * 40) * 0.5 + 0.5), u, np.tile([0.9, 0.95, 1.0, 0.1], (k, 1)), col * 0.3, rim * 0.4, prm, np.full(k, 0.5))))
    hb = grid(I['hud_atlas'][1][:600, :1000]); rows.append(('HUD / menus (menu atlas, node colour)', hud_prog(hb, np.zeros(k), np.full(k, 2.0), np.tile([1, 1, 1, 0.9], (k, 1)), np.tile([0.05, 0.1, 0.12, 0], (k, 1)))))
    fx = grid(I['fx']); rows.append(('effects (fx atlas x colour)', sprite_prog(fx, np.c_[np.tile([1.0, 0.8, 0.6], (k, 1)), 0.3 + 0.7 * u])))
    out = []
    for name, (A, Bh) in rows:
        def img(o):   # premultiplied-style preview on grey: rgb + (1 - a) * 0.25 for the blended programs
            rgb = np.clip(np.asarray(o[0], np.float64), 0, 1); a = np.clip(np.asarray(o[1], np.float64), 0, 1)
            return (q8(rgb * a[:, None] + 0.25 * (1 - a[:, None]) if 'model' not in name else rgb)).reshape(Hh, W, 3).astype(np.uint8)
        e = err(A, Bh).reshape(Hh, W); diff = np.clip(e * 32, 0, 255).astype(np.uint8)
        row = Image.new('RGB', (W * 3, Hh + 16)); row.paste(Image.fromarray(img(A)), (0, 16)); row.paste(Image.fromarray(img(Bh)), (W, 16))
        row.paste(Image.fromarray(np.stack([diff] * 3, 2)), (2 * W, 16)); dr = ImageDraw.Draw(row)
        dr.text((3, 2), name + ': float (b38)', fill=(255, 255, 255)); dr.text((W + 3, 2), 'half (b39)', fill=(255, 255, 255))
        dr.text((2 * W + 3, 2), 'difference x32: mean %.3f, max %d' % (e.mean(), e.max()), fill=(255, 255, 255)); out.append(row)
    return out


def main():
    out = sys.argv[1]; n = int(sys.argv[2]) if len(sys.argv) > 2 else 200000
    I = load_inputs(n); R = run_all(I, n)
    print('%-44s %9s | %-27s | %-27s' % ('program', 'fragments', 'output: mean / p99.9 / worst', 'on screen: mean / p99.9 / worst'))
    for k, (e, s) in R.items():
        print('%-44s %9d | %8.3f %8.0f %8.0f  | %8.3f %8.0f %8.0f' % (k, len(e), e.mean(), np.percentile(e, 99.9), e.max(), s.mean(), np.percentile(s, 99.9), s.max()))
    rows = pictures(I); S = Image.new('RGB', (rows[0].width, sum(r.height for r in rows))); y = 0
    for r in rows: S.paste(r, (0, y)); y += r.height
    S.save(out, quality=90); print('sheet', out)


if __name__ == '__main__':
    main()
