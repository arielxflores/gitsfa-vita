#!/usr/bin/env python3
"""HUD fill per frame (render b39, GPU cost model): the screen area of every quad the HUD draws, from the captures of
tools/ui/host (hud_host.c: the engine's hud_game.h / hud_round.h / menu.h code with the recording GL stand-in). Every pixel
of a HUD quad runs HUD_FS, transparent texels too, so the fragment count is the clipped quad area summed over the draws.
usage: hudfill.py <capture.cap>... [--top N]   (make captures: tools/ui/host/run.sh, or build hud_host.c and run it)
"""
import sys, os, struct
import numpy as np
W, H = 960, 544


def clip(poly):   # Sutherland-Hodgman against the screen
    for ax, lim, keep_ge in ((0, 0, True), (0, W, False), (1, 0, True), (1, H, False)):
        out = []
        for i in range(len(poly)):
            a, b = poly[i], poly[(i + 1) % len(poly)]
            ka = a[ax] >= lim if keep_ge else a[ax] <= lim; kb = b[ax] >= lim if keep_ge else b[ax] <= lim
            if ka: out.append(a)
            if ka != kb:
                t = (lim - a[ax]) / (b[ax] - a[ax]); out.append((a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])))
        poly = out
        if not poly: return []
    return poly


def area(p): return 0.5 * abs(sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p)))) if len(p) >= 3 else 0.0


def quads(path):
    """(area in pixels, texture id, gauge threshold) per quad: a draw = tex, premultiplied flag, vertex count, then 17 floats
    a vertex (pos4 uv4 mul4 add4 thr), two triangles a quad (tools/ui/host/vitaGL.h)"""
    d = open(path, 'rb').read(); o = 0; out = []
    while o + 12 <= len(d):
        tex, pm, n = struct.unpack_from('<3i', d, o); o += 12; v = np.frombuffer(d, '<f4', n * 17, o).reshape(n, 17); o += n * 68
        P = [((x / w + 1) * W / 2, (1 - y / w) * H / 2) for x, y, w in zip(v[:, 0], v[:, 1], v[:, 3])]
        for q in range(0, n - 2, 6): out.append((sum(area(clip(P[i:i + 3])) for i in range(q, min(q + 6, n) - 2, 3)), tex, float(v[q, 16])))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    for p in args:
        Q = quads(p); a = sum(q[0] for q in Q); ramp = sum(q[0] for q in Q if q[2] < 1.5)
        print('%-18s %5.3f screens (%7.0f fragments) in %3d quads; gauge quads (threshold < 2) %.3f screens' % (os.path.basename(p)[:-4], a / (W * H), a, len(Q), ramp / (W * H)))


if __name__ == '__main__':
    main()
