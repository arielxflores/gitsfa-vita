# first-person helpers for the host sheets (the viewmodel drawn like main.c / vision.h vis_viewmodel)
import math, numpy as np, moderngl, host_vis
VM = host_vis.DATA + '/vm_m4a1.gvm'
def vm_actor(H, clip='idle', t=0.5, off=(-2.37, 0.08, 0.0)):
    g, texs = H.model(VM); ci = g.find_clip(clip); q, p = g.sample(ci if ci >= 0 else 0, t); pal = g.palette(q, p); sk = g.skin(pal); meshes = []
    for m, (P, N) in zip(g.mesh, sk):
        if m['blend'] == 2 or (m.get('lods', 0) and not (m['lods'] & 1)): continue
        buf = np.c_[P, N, m['uv']].astype('f4'); meshes.append(dict(tex=texs[m['tex']], vbo=H.ctx.buffer(buf.tobytes()), ibo=H.ctx.buffer(m['I'].astype('u4').tobytes()), n=m['ni']))
    K = np.eye(4); K[:3, 3] = off; return dict(meshes=meshes, M=K, yaw=0.0, chest=np.zeros(3))
def fp_spawn(H, sc, sp=1.0):
    A = vm_actor(H); H.begin(sc['eye'], sc['yaw'], sc['pitch']); host_vis.glDepthMask(1); host_vis.glClearDepth(1.0); host_vis.glClear(0x100)
    VP0, cam0 = H.VP, H.cam; H.VP = host_vis.persp(math.radians(49.75), 960 / 544, 2, 400); H.cam = np.zeros(3)
    H.spawn_draw(A, sp, scale=1 / 12.0, pull=0.5, tint=(1, 1, 1)); H.VP, H.cam = VP0, cam0
    return H.end().crop((400, 180, 960, 544))
