# render b45, tools/pvs: shared pieces of the world visibility tables (pvs.py, pvscheck.py).
# Chunks: each world mesh's triangles are grouped by the CH x CH cm cell of their first vertex (x, y): cell index
# floor(x / CH) in float32, exactly as world.h computes it at load; key = (cx + 32768) | (cy + 32768) << 16; a mesh's chunks
# in ascending key order, chunk ids running over the meshes in file order. world.h checks every chunk's triangle count
# against the table and drops the table (draws everything) on any mismatch.
import os, sys, struct, math
import numpy as np, moderngl
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mapvis'))
import gmapio
CH = np.float32(1000.0)
MAPS = os.environ.get('MAPS', '/root/gits/build/maps')

def cell_of(x):   # the largest integer c with c * 1000 <= x (exact; world.h w_cell_of does the same)
    x = np.asarray(x, np.float64); c = np.floor(x / 1000.0).astype(np.int64)
    c -= (c * 1000.0 > x); c += ((c + 1) * 1000.0 <= x); return c

def load(stem):
    g = gmapio.read(os.path.join(MAPS, stem + '.gmap'), decode=False)
    meshes = []; chunks = []   # chunk: (mesh, key, tri indices (in the mesh's original order), mn, mx)
    for i, m in enumerate(g['mesh']):
        V = np.asarray(m['V'][:, :3], dtype=np.float32); I = np.asarray(m['I'], dtype=np.int64).reshape(-1, 3)
        f = V[I[:, 0]]; cx = cell_of(f[:, 0]); cy = cell_of(f[:, 1])
        key = (cx + 32768) | ((cy + 32768) << 16)
        first = len(chunks)
        for k in np.unique(key):   # ascending
            t = np.nonzero(key == k)[0]; P = V[I[t].ravel()]
            chunks.append(dict(mesh=i, key=int(k), tris=t, mn=P.min(0), mx=P.max(0)))
        meshes.append(dict(V=V, I=I, mode=m['mode'], two=bool(m['two']), occ=m['mode'] == 0 and bool(m.get('zwrite', 1)), mn=np.array(m['mn']), mx=np.array(m['mx']), c0=first, c1=len(chunks)))
    return meshes, chunks

def nav_points(stem, spacing=100.0):
    """points on the navigation mesh (where players, bots and so the cameras are), about one per spacing^2"""
    d = open(os.path.join(MAPS, stem + '.gnav'), 'rb').read(); n = struct.unpack_from('<I', d, 4)[0]
    T = np.frombuffer(d, dtype=[('v', '<f4', (3, 3)), ('nb', '<i4', 3)], count=n, offset=8)['v'].astype(np.float64)
    return tri_points(T, spacing)

def tri_points(T, spacing):
    area = 0.5 * np.linalg.norm(np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]), axis=1); rng = np.random.default_rng(7); out = [T.mean(1)]
    k = np.round(area / spacing ** 2).astype(int)
    for t, kk in zip(T, k):
        if kk <= 0: continue
        r = rng.random((kk, 2)); s = r.sum(1) > 1; r[s] = 1 - r[s]
        out.append(t[0] + r[:, :1] * (t[1] - t[0]) + r[:, 1:] * (t[2] - t[0]))
    return np.concatenate(out)

def walk_points(stem, spacing=100.0):
    """points on the collision mesh's walkable (up-facing) triangles, about one per spacing^2"""
    d = open(os.path.join(MAPS, stem + '.gcol'), 'rb').read(); mg, ver, nv, ni = struct.unpack_from('<4sIII', d, 0)
    P = np.frombuffer(d, '<f4', nv * 3, 16).reshape(-1, 3).astype(np.float64); I = np.frombuffer(d, '<u4', ni * 3, 16 + nv * 12).reshape(-1, 3)
    T = P[I]; n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); ln = np.linalg.norm(n, axis=1) + 1e-9; up = n[:, 2] / ln > 0.55
    T = T[up]; area = 0.5 * ln[up]; rng = np.random.default_rng(7); out = []
    k = np.maximum(1, np.round(area / spacing ** 2).astype(int))
    for t, kk in zip(T, k):
        r = rng.random((kk, 2)); s = r.sum(1) > 1; r[s] = 1 - r[s]
        out.append(t[0] + r[:, :1] * (t[1] - t[0]) + r[:, 1:] * (t[2] - t[0]))
    return np.concatenate(out)

def persp(fovy, asp, n, f):
    t = 1 / math.tan(fovy / 2); m = np.zeros((4, 4)); m[0, 0] = t / asp; m[1, 1] = t; m[2, 2] = (f + n) / (n - f); m[2, 3] = 2 * f * n / (n - f); m[3, 2] = -1; return m

def look(eye, fwd, up):
    f = np.asarray(fwd, float); f /= np.linalg.norm(f); s = np.cross(f, up); s /= np.linalg.norm(s); u = np.cross(s, f)
    m = np.eye(4); m[0, :3] = s; m[1, :3] = u; m[2, :3] = -f; m[:3, 3] = -m[:3, :3] @ eye; return m

def box_in(mvp, mn, mx):
    c = np.array([[x, y, z, 1.0] for x in (mn[0], mx[0]) for y in (mn[1], mx[1]) for z in (mn[2], mx[2])]) @ mvp.T
    for ax in range(3):
        if (c[:, ax] > c[:, 3]).all() or (c[:, ax] < -c[:, 3]).all(): return False
    return not (c[:, 3] < 0).all()

VS = '''#version 330
in vec3 pos; uniform mat4 mvp; void main(){ gl_Position = mvp*vec4(pos,1.0); }'''
FS = '''#version 330
uniform int cid; out int o; void main(){ o = cid; }'''
FS_TRI = '''#version 330
uniform int base; out int o; void main(){ o = base + gl_PrimitiveID; }'''

class Scene:
    """chunk-ordered GPU buffers: one vertex array per chunk (its triangles, in the mesh's order)"""
    def __init__(self, meshes, chunks, w, h):
        self.ctx = moderngl.create_standalone_context(backend='egl'); ctx = self.ctx
        self.p = ctx.program(vertex_shader=VS, fragment_shader=FS); self.pt = ctx.program(vertex_shader=VS, fragment_shader=FS_TRI)
        self.resize(w, h); self.meshes = meshes; self.chunks = chunks; self.va = []; self.vat = []; base = 0
        for c in chunks:
            m = meshes[c['mesh']]; tri = m['I'][c['tris']]; vb = ctx.buffer(np.ascontiguousarray(m['V'][tri.ravel()], dtype='f4').tobytes())
            self.va.append(ctx.vertex_array(self.p, [(vb, '3f', 'pos')])); self.vat.append(ctx.vertex_array(self.pt, [(vb, '3f', 'pos')])); c['base'] = base; base += len(tri)
        self.ntri = base
    def boxes_in(self, mvp):   # every chunk's box against the frustum at once
        if not hasattr(self, 'corn'):
            mn = np.array([c['mn'] for c in self.chunks], float); mx = np.array([c['mx'] for c in self.chunks], float)
            self.corn = np.stack([np.c_[np.where(b & 1, mx[:, 0], mn[:, 0]), np.where(b & 2, mx[:, 1], mn[:, 1]), np.where(b & 4, mx[:, 2], mn[:, 2]), np.ones(len(mn))] for b in range(8)], 1)
        c = self.corn @ mvp.T; out = (c[:, :, 3] < 0).all(1)
        for ax in range(3): out |= (c[:, :, ax] > c[:, :, 3]).all(1) | (c[:, :, ax] < -c[:, :, 3]).all(1)
        return ~out
    def resize(self, w, h):
        self.w, self.h = w, h; self.ids = self.ctx.texture((w, h), 1, dtype='i4'); self.dep = self.ctx.depth_texture((w, h)); self.fbo = self.ctx.framebuffer([self.ids], self.dep)
    def render(self, mvp, which=None, tri=False):
        """visible chunk ids (or triangle ids with tri) in `which` (default all): an ID buffer of the occluders (opaque
        surfaces that write depth), then the see-through ones (alpha-tested, blended, no depth write: they hide nothing)
        drawn against that depth into a second ID buffer. Returns (occluder ids, see-through ids), -1 = none."""
        ctx = self.ctx; self.fbo.use(); ctx.viewport = (0, 0, self.w, self.h); self.fbo.clear(depth=1.0); self.ids.write(np.full((self.h, self.w), -1, 'i4').tobytes())
        ctx.enable(moderngl.DEPTH_TEST); ctx.depth_func = '<='; ctx.front_face = 'ccw'; ctx.cull_face = 'back'
        p = self.pt if tri else self.p; p['mvp'].write(mvp.astype('f4').T.tobytes()); inb = self.boxes_in(mvp)
        if which is not None: inb &= which
        out = []
        for occ in (True, False):
            if not occ: ctx.depth_mask = False; self.ids.write(np.full((self.h, self.w), -1, 'i4').tobytes())
            for ci in np.nonzero(inb)[0]:
                c = self.chunks[ci]; m = self.meshes[c['mesh']]
                if m['occ'] != occ: continue
                ctx.disable(moderngl.CULL_FACE) if m['two'] else ctx.enable(moderngl.CULL_FACE)
                if tri: p['base'].value = c['base']; self.vat[ci].render(moderngl.TRIANGLES)
                else: p['cid'].value = int(ci); self.va[ci].render(moderngl.TRIANGLES)
            out.append(np.frombuffer(self.ids.read(), 'i4').reshape(self.h, self.w))
        ctx.depth_mask = True
        return out
