# Finds camera / character placements on a map for the host renders: a character partly hidden behind low cover
# (head visible, chest and knees hidden), fully hidden behind a wall, and in the open.
import math, numpy as np
def find(H, kind, maxn=3, dists=(450, 650, 900, 1200), seed=0):
    C = H.col; out = []; rng = np.random.default_rng(seed); sp = list(C.spawns); rng.shuffle(sp)
    for g, ty, x, y, z, syaw in sp:
        eye = np.array([x, y, z + 152.6])
        for k in range(16):
            yaw = syaw + k * math.pi / 8; f = np.array([math.cos(yaw), math.sin(yaw), 0]); r = np.array([math.sin(yaw), -math.cos(yaw), 0])
            for dist in dists:
                for lat in (0, -120, 120):
                    p = eye + f * dist + r * lat; fz = H.floor_at(p[0], p[1], z)
                    if fz is None or abs(fz - z) > 60: continue
                    foot = np.array([p[0], p[1], fz]); head, chest, knee = foot + [0, 0, 160], foot + [0, 0, 115], foot + [0, 0, 50]
                    ch, cc, ck = C.clear(eye, head), C.clear(eye, chest), C.clear(eye, knee)
                    if kind == 'low' and not (ch and not cc and not ck): continue
                    if kind == 'wall' and (ch or cc or ck): continue
                    if kind == 'open' and not (ch and cc and ck): continue
                    d = chest - eye; hit = C.ray(eye, d, np.linalg.norm(d))
                    if kind in ('low', 'wall') and not (dist - hit < 260 and hit > 150): continue   # cover close to the character
                    # the character must have room (no wall right at it)
                    if not C.clear(foot + [0, 0, 100], foot + [60 * f[0], 60 * f[1], 100]) or not C.clear(foot + [0, 0, 100], foot - [60 * f[0], 60 * f[1], -100]): continue
                    tyaw = yaw + math.pi + (0.6 if lat >= 0 else -0.6)
                    out.append(dict(eye=eye, yaw=yaw, pitch=-math.atan2(eye[2] - (fz + 100), dist) * 0.8, foot=foot, tyaw=tyaw, dist=dist))
                    if len(out) >= maxn: return out
                    break
    return out
