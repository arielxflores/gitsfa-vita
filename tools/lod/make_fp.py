#!/usr/bin/env python3
# make_fp.py (b60): vm_<gun>.gfp, the light first-person mesh of a viewmodel (engine/src/wpn_fp.h).
# The arms + gun model is 23-31 thousand triangles, drawn two-sided every frame; tools/lod/vmvis.c marks which triangles
# win a pixel in any frame of each clip. While only the everyday clips play (COMMON: standing, walking, running, sprinting,
# aiming, firing, jumping, sliding: 85-90 % of a match), only 2,500-3,000 of the arms' 18,878 triangles can be on screen.
# The light mesh:
#   - every triangle seen in an everyday clip or a mix of them, and every triangle touching one, EXACTLY as in the model (same vertices,
#     same order of the three corners): the picture is the same, pixel for pixel, as with the full mesh;
#   - the rest simplified (meshoptimizer, within 0.3 % of the mesh's size, about 2 mm, the vertices on the border to the
#     kept part locked: no cracks), never removed: a pose nobody foresaw shows the arm 2 mm off, not a hole.
# The engine draws the light mesh only while base / fading / action clips are all COMMON (the file lists them by name);
# reload, draw, holster, climb, vault and the rest use the full model as before.
#   make_fp.py DATA_DIR VIS_DIR OUT_DIR [gun ...]
# File: 'GFP1', u32 nmesh, u32 nclip, nclip x char[16]; per mesh: u32 nv, u32 ni (0 0: the model's own mesh), nv x 32
# bytes (the model's vertex format), ni x u16 (+ 2 bytes of padding when ni is odd).
import os, sys, struct, glob
import numpy as np
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(H, '..', 'lib'))
import meshopt_c
RINGS = 1            # neighbours kept around what is seen
COARSE_ERR = 0.003   # the unseen part: error bound, relative to the mesh's size
MIN_TRIS = 100       # smaller meshes are left alone

def read_gvm(path):
    d = open(path, 'rb').read(); assert d[:4] == b'GVM1', path
    ntex, nb, npal, nmesh, nclip = struct.unpack_from('<5I', d, 4); o = 24
    for i in range(ntex):
        w, h, fmt, nmp = struct.unpack_from('<HHBB', d, o); o += 6
        for m in range(nmp): sz, = struct.unpack_from('<I', d, o); o += 4 + sz
    o += 32 * nb + npal * 52; meshes = []
    for i in range(nmesh):
        tex, bl, nv, ni = struct.unpack_from('<4I', d, o); o += 16
        V = d[o:o + nv * 32]; o += nv * 32; I = np.frombuffer(d, '<u2', ni, o).astype(np.int64); o += ni * 2 + (2 if ni & 1 else 0)
        meshes.append(dict(blend=bl & 127, nv=nv, ni=ni, V=V, I=I))
    return meshes
def read_vis(path):
    d = open(path, 'rb').read(); assert d[:4] == b'VMV3', path
    nmesh, nclip, every = struct.unpack_from('<IIQ', d, 4); o = 20
    names = [d[o + 16 * c:o + 16 * c + 16].split(b'\0')[0].decode() for c in range(nclip)]; o += 16 * nclip; vis = []
    for i in range(nmesh):
        nt, = struct.unpack_from('<I', d, o); o += 4; vis.append(np.frombuffer(d, '<u8', nt, o)); o += 8 * nt
    return names, vis, every
def light_mesh(M, seen):
    """seen: bool per triangle -> (vertex bytes, indices) of the light mesh, and counts (kept exact, coarse)"""
    I = M['I'].reshape(-1, 3); nt = len(I); P = np.frombuffer(M['V'], '<f4').reshape(-1, 8)[:, :3]
    # vertices at the same place are one point for the neighbourhood and for the simplifier (UV / normal seams)
    _, pid = np.unique(np.frombuffer(M['V'], 'V32').view([('p', 'V12'), ('r', 'V20')])['p'], return_inverse=True); pid = pid.ravel()
    T = pid[I]; keep = seen.copy()
    for r in range(RINGS):
        pt = np.zeros(pid.max() + 1, bool); pt[T[keep].ravel()] = True; keep = pt[T].any(axis=1)
    A = I[keep]; B = I[~keep]
    if len(B):
        # the border to the kept part stays as it is (no cracks). A looser stand-in (5 % and no locks: 500 triangles for the
        # arms instead of 3,000) was tried: it pokes through the kept part in 4 of 10 random views (fpcheck.c).
        lock = np.zeros(len(P), np.uint8); bp = np.zeros(pid.max() + 1, bool); bp[T[keep].ravel()] = True; lock[bp[pid]] = 1
        Bs = meshopt_c.simplify_locked(B.ravel(), P, lock, 0, COARSE_ERR).reshape(-1, 3).astype(np.int64)
    else: Bs = B[:0]
    J = np.concatenate([A, Bs]); used, inv = np.unique(J.ravel(), return_inverse=True)   # (kept triangles first, in the model's order)
    Vb = np.frombuffer(M['V'], 'V32')[used].tobytes(); return Vb, inv.ravel().astype('<u2'), len(A), len(Bs)
def build(gvm, visf, out):
    meshes = read_gvm(gvm); names, vis, every = read_vis(visf); assert len(vis) == len(meshes)
    common = [c for c in range(len(names)) if (every >> c) & 1]; mask = np.uint64(every)   # (vmvis.c's EVERYDAY list; bit 63: their mixes)
    body = b''; tot0 = tot1 = v0 = v1 = 0; rep = []
    for i, M in enumerate(meshes):
        nt = M['ni'] // 3; assert len(vis[i]) == nt; tot0 += nt; v0 += M['nv']
        if M['blend'] != 0 or nt < MIN_TRIS: body += struct.pack('<II', 0, 0); tot1 += nt; v1 += M['nv']; continue
        seen = (vis[i] & mask) != 0
        Vb, Ix, na, nb_ = light_mesh(M, seen)
        if len(Ix) >= M['ni'] * 0.9: body += struct.pack('<II', 0, 0); tot1 += nt; v1 += M['nv']; continue   # nothing to gain
        body += struct.pack('<II', len(Vb) // 32, len(Ix)) + Vb + Ix.tobytes() + (b'\0\0' if len(Ix) & 1 else b''); tot1 += len(Ix) // 3; v1 += len(Vb) // 32
        rep.append('mesh %d: %d -> %d (%d seen, %d as they are, %d coarse)' % (i, nt, len(Ix) // 3, int(seen.sum()), na, nb_))
    hdr = b'GFP1' + struct.pack('<II', len(meshes), len(common)) + b''.join(names[c].encode().ljust(16, b'\0') for c in common)
    open(out, 'wb').write(hdr + body)
    print('%-12s %5d -> %5d triangles, %5d -> %5d vertices in the everyday clips (%d of %d clips) | %s' % (os.path.basename(out), tot0, tot1, v0, v1, len(common), len(names), '; '.join(rep)))
if __name__ == '__main__':
    data, visd, outd = sys.argv[1], sys.argv[2], sys.argv[3]; guns = sys.argv[4:]
    os.makedirs(outd, exist_ok=True)
    for g in sorted(glob.glob(os.path.join(data, 'vm_*.gvm'))):
        n = os.path.basename(g)[3:-4]
        if guns and n not in guns: continue
        v = os.path.join(visd, 'vm_%s.vis' % n)
        if not os.path.exists(v): print('no', v); continue
        build(g, v, os.path.join(outd, 'vm_%s.gfp' % n))
