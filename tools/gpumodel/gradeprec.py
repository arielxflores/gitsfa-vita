#!/usr/bin/env python3
"""Colour-grading accuracy on the maps' own colours (render b38): the map LUT (as world.h samples it) against the cubic
polynomial fit (GFIT trailer) evaluated in fp32 and in fp16 with the shader's operation order, and the b38 half LUT tail
(clamp and blend in fp16, the address in fp32) against the float one.
Pre-grade colours: host renders (render.py, grading off) from the map's spawn points. Errors in 8-bit steps.
usage: gradeprec.py <stem>...   (BUILD tree: /root/gits/build)"""
import sys, os, math, json
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mapvis'))
from render import Renderer
from gcol import spawns
B = '/root/gits/build'


def lut_eval(lut, fc):
    q = np.clip(fc, 0, 1) * 15.0; b = np.minimum(np.floor(q[..., 2]), 14.0); f = q[..., 2] - b
    u = (q[..., 0] + 0.5 + b * 16.0) / 256.0; v = (q[..., 1] + 0.5) / 16.0
    def bil(u, v):
        x = u * 256 - 0.5; y = v * 16 - 0.5; x0 = np.floor(x); y0 = np.floor(y); fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
        x0 = x0.astype(int); y0 = y0.astype(int); x1 = np.clip(x0 + 1, 0, 255); y1 = np.clip(y0 + 1, 0, 15); x0 = np.clip(x0, 0, 255); y0 = np.clip(y0, 0, 15)
        L = lut.astype(np.float64) / 255.0
        return (L[y0, x0] * (1 - fx) + L[y0, x1] * fx) * (1 - fy) + (L[y1, x0] * (1 - fx) + L[y1, x1] * fx) * fy
    ga = bil(u, v); gh = bil(u + 0.0625, v)
    return ga + (gh - ga) * f[..., None]


def lut_eval_half(lut, fc):   # world.h w_grade_code_h(GRADE_LUT2): half gs = saturate(fc); float address; half blend
    h = np.float16
    gs = np.minimum(fc.astype(h), h(1)).astype(h); q = gs.astype(np.float64) * 15.0; b = np.minimum(np.floor(q[..., 2]), 14.0); f = (q[..., 2] - b).astype(h)
    u = (q[..., 0] + 0.5 + b * 16.0) / 256.0; v = (q[..., 1] + 0.5) / 16.0
    def bil(u, v):
        x = u * 256 - 0.5; y = v * 16 - 0.5; x0 = np.floor(x); y0 = np.floor(y); fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
        x0 = x0.astype(int); y0 = y0.astype(int); x1 = np.clip(x0 + 1, 0, 255); y1 = np.clip(y0 + 1, 0, 15); x0 = np.clip(x0, 0, 255); y0 = np.clip(y0, 0, 15)
        L = lut.astype(np.float64) / 255.0
        return (L[y0, x0] * (1 - fx) + L[y0, x1] * fx) * (1 - fy) + (L[y1, x0] * (1 - fx) + L[y1, x1] * fx) * fy
    ga = bil(u, v).astype(h); gh = bil(u + 0.0625, v).astype(h)
    return (ga + ((gh - ga).astype(h) * f[..., None]).astype(h)).astype(h).astype(np.float64)


def lut_eval_half43(lut, fc):   # render b43 grade_code_h(GRADE_LUT2): half clamp, scale 14.999, gf = frac, half blend
    h = np.float16
    gs = np.minimum(fc.astype(h), h(1)).astype(h).astype(np.float64); gz = (gs[..., 2].astype(np.float32) * np.float32(14.999)).astype(np.float64)
    gf = gz - np.floor(gz); b = gz - gf
    u = (gs[..., 0] * (14.999 / 256.0) + b * 0.0625 + 0.5 / 256.0); v = gs[..., 1] * (14.999 / 16.0) + 0.5 / 16.0
    def bil(u, v):
        x = u * 256 - 0.5; y = v * 16 - 0.5; x0 = np.floor(x); y0 = np.floor(y); fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
        x0 = x0.astype(int); y0 = y0.astype(int); x1 = np.clip(x0 + 1, 0, 255); y1 = np.clip(y0 + 1, 0, 15); x0 = np.clip(x0, 0, 255); y0 = np.clip(y0, 0, 15)
        L = lut.astype(np.float64) / 255.0
        return (L[y0, x0] * (1 - fx) + L[y0, x1] * fx) * (1 - fy) + (L[y1, x0] * (1 - fx) + L[y1, x1] * fx) * fy
    ga = bil(u, v).astype(h); gh = bil(u + 0.0625, v).astype(h)
    return (ga + ((gh - ga).astype(h) * gf.astype(h)[..., None]).astype(h)).astype(h).astype(np.float64)


def poly_eval(k, fc, dt):
    k = k.astype(dt); x = np.clip(fc, 0, 1).astype(dt); y = (x * x).astype(dt); z = (y * x).astype(dt)
    g = np.broadcast_to(k[9, :3], fc.shape).astype(dt)
    for j, src in ((0, x), (3, y), (6, z)):
        for c in range(3):
            g = (g + (k[j + c, :3] * src[..., c:c + 1]).astype(dt)).astype(dt)
    return g.astype(np.float64)


def main():
    out = {}
    for stem in sys.argv[1:]:
        raw = open('%s/data/%s.glut' % (B, stem), 'rb').read(); lut = np.frombuffer(raw[:12288], np.uint8).reshape(16, 256, 3)
        k = np.frombuffer(raw, '<f4', 80, 12296).reshape(2, 10, 4)[0]
        R = Renderer(ss=1); R.load(stem, lut=False)
        sp = spawns('%s/maps/%s.gcol' % (B, stem)); sp = sp[::max(1, len(sp) // 6)]
        px = []
        for gi, t, x, y, z, yw in sp:
            for yi in range(2):
                im = np.asarray(R.shot((x, y, z + 152), yw + math.pi * yi, 0.0)).reshape(-1, 3)[::7]; px.append(im)
        fc = np.concatenate(px).astype(np.float64) / 255.0
        L = lut_eval(lut, fc); P32 = poly_eval(k, fc, np.float32); P16 = poly_eval(k, fc, np.float16)
        def st(a, b):
            e = np.abs(np.round(np.clip(a, 0, 1) * 255) - np.round(np.clip(b, 0, 1) * 255)).max(1)
            return dict(mean=float(e.mean()), p95=float(np.percentile(e, 95)), max=float(e.max()))
        L16 = lut_eval_half(lut, fc); L43 = lut_eval_half43(lut, fc)
        out[stem] = dict(lut_vs_poly32=st(L, P32), poly16_vs_poly32=st(P16, P32), lut_vs_poly16=st(L, P16), lut16_vs_lut32=st(L16, L), lut43_vs_lut32=st(L43, L), lut43_vs_lut38=st(L43, L16), kmax=float(np.abs(k[:, :3]).max()))
        o = out[stem]
        print('%-13s LUT vs poly %.2f mean / %.0f p95 / %.0f max | fp16 poly vs fp32 poly %.2f / %.0f / %.0f | half LUT tail vs float %.2f / %.0f / %.0f' % (stem, o['lut_vs_poly32']['mean'], o['lut_vs_poly32']['p95'], o['lut_vs_poly32']['max'],
              o['poly16_vs_poly32']['mean'], o['poly16_vs_poly32']['p95'], o['poly16_vs_poly32']['max'], o['lut16_vs_lut32']['mean'], o['lut16_vs_lut32']['p95'], o['lut16_vs_lut32']['max']), flush=True)
        print('%-13s b43 half LUT tail vs float %.3f mean / %.0f max | vs the b38 half tail %.3f / %.0f' % (stem, o['lut43_vs_lut32']['mean'], o['lut43_vs_lut32']['max'], o['lut43_vs_lut38']['mean'], o['lut43_vs_lut38']['max']), flush=True)
    if os.environ.get('JSON'): json.dump(out, open(os.environ['JSON'], 'w'), indent=1)


if __name__ == '__main__':
    main()
