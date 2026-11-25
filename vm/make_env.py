# env_ref.gtx (b65): the reflection the models' metal and armour show (engine/src/model.h, MD_VS_ENV). The original game's
# weapons, operators and cyborg carry an environment cube map (NiTextureEffect + NiSourceCubeMap "CUBE.dds", extra data
# g_CubeMapBrightness 1.0, weapon/Textures/CUBE.dds: a 128 x 128 DXT1 cube of a street, 6 faces with mips). The Vita draws
# it as a sphere map looked up per vertex with the view-space reflection vector (one texture read per pixel, no cube
# sampling, no seams in front of the viewer): this script turns the cube into that sphere map.
#   sphere map pixel (s, t) in [-1, 1]^2 (t up) = the mirror ball's normal n = (s, t, sqrt(1 - s^2 - t^2)); the view looks
#   along -z, so the reflected ray is r = (2 nz nx, 2 nz ny, 2 nz^2 - 1) (view space, +y up); the cube (D3D layout: +X -X
#   +Y -Y +Z -Z, +Y up, left-handed, +Z ahead) is read in direction (rx, ry, -rz).
# Output: GTX1 RGBA8 128 x 128 (engine/src/skills.h sk_tex_load layout, read by model.h md_env_load), prefiltered as the PC's
# mip (b68, below), outside the ball the edge colour.
# Usage: python3 make_env.py [outdir]
import sys, os, struct, numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
import gitspaths
from PIL import Image, ImageFilter
N = 128

def dxt1(b, w, h):
    """one DXT1 level -> (h, w, 3) float RGB 0..1"""
    out = np.zeros((h, w, 3), np.float32); k = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            c0, c1, bits = struct.unpack_from('<HHI', b, k); k += 8
            def rgb(c): return np.array([((c >> 11) & 31) / 31.0, ((c >> 5) & 63) / 63.0, (c & 31) / 31.0], np.float32)
            p0, p1 = rgb(c0), rgb(c1)
            pal = [p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3] if c0 > c1 else [p0, p1, (p0 + p1) / 2, np.zeros(3, np.float32)]
            for i in range(16): out[by + i // 4, bx + i % 4] = pal[(bits >> (2 * i)) & 3]
    return out

def cube_faces(path):
    d = open(path, 'rb').read(); assert d[:4] == b'DDS '
    h = struct.unpack_from('<31I', d, 4); hh, ww, mips = h[2], h[3], max(1, h[6]); caps2 = h[27]
    assert struct.pack('<I', h[20]) == b'DXT1' and caps2 & 0xfe00 == 0xfe00, 'not a DXT1 cube map'
    o = 128; faces = []
    for f in range(6):
        faces.append(dxt1(d[o:], ww, hh))
        for m in range(mips): w_, h_ = max(1, ww >> m), max(1, hh >> m); o += max(1, (w_ + 3) // 4) * max(1, (h_ + 3) // 4) * 8
    return faces   # +X -X +Y -Y +Z -Z

def sample(faces, d):
    """D3D cube lookup: d (..., 3) -> RGB"""
    x, y, z = d[..., 0], d[..., 1], d[..., 2]; ax, ay, az = np.abs(x), np.abs(y), np.abs(z); n = faces[0].shape[0]
    out = np.zeros(d.shape, np.float32)
    sel = [(ax >= ay) & (ax >= az) & (x > 0), (ax >= ay) & (ax >= az) & (x <= 0), (ay > ax) & (ay >= az) & (y > 0), (ay > ax) & (ay >= az) & (y <= 0),
           (az > ax) & (az > ay) & (z > 0), (az > ax) & (az > ay) & (z <= 0)]
    for f, m in enumerate(sel):
        if not m.any(): continue
        X, Y, Z = x[m], y[m], z[m]
        if f == 0: sc, tc, ma = -Z, -Y, np.abs(X)
        elif f == 1: sc, tc, ma = Z, -Y, np.abs(X)
        elif f == 2: sc, tc, ma = X, Z, np.abs(Y)
        elif f == 3: sc, tc, ma = X, -Z, np.abs(Y)
        elif f == 4: sc, tc, ma = X, -Y, np.abs(Z)
        else: sc, tc, ma = -X, -Y, np.abs(Z)
        u = np.clip(((sc / ma + 1) * 0.5 * n).astype(int), 0, n - 1); v = np.clip(((tc / ma + 1) * 0.5 * n).astype(int), 0, n - 1)
        out[m] = faces[f][v, u]
    return out

def sphere_map(src, lobe=11.0):
    """the cube at src as the engine's sphere map: (N, N, 4) uint8 (b112: a function - gvmlib.Builder.envmap makes a
    model's own reflection picture with it; lobe = the prefilter's cosine power)"""
    faces = cube_faces(src)
    # b68: the PC samples this cube at mip (1 - C.a) x g_CubeMapLodLevelCount (WeaponMaterial ps_3_0; C.a = 0.4 on 36 of 65
    # shader maps, 0.53 on 14, 0.6 on 9: mip 4.8 / 3.8 / 3.2 of the 128 face = a ~20 degree blur) at g_CubeMapBrightness 1.0,
    # no curve. So: the cube prefiltered with a cos^11 lobe (half power at 20 degrees), raw. (b65-b67: the sharp cube x^1.8 x 1.5
    # - with b62's normalised masks a contrasty mirror on every gun, the M4A1 washed tan / silver in the tutorial next to the
    # PC's near-black one.)
    F = 32
    fs = [np.asarray(Image.fromarray((f * 255).astype(np.uint8)).resize((F, F), Image.BOX), np.float32) / 255 for f in faces]
    # the cube's texel directions (D3D layout, as sample()) and solid angles
    g = (np.arange(F) + 0.5) / F * 2 - 1; tc, sc = np.meshgrid(g, g, indexing='ij')
    D = []; C = []; W = []
    for f in range(6):
        if f == 0: d = np.stack([np.ones_like(sc), -tc, -sc], -1)
        elif f == 1: d = np.stack([-np.ones_like(sc), -tc, sc], -1)
        elif f == 2: d = np.stack([sc, np.ones_like(sc), tc], -1)
        elif f == 3: d = np.stack([sc, -np.ones_like(sc), -tc], -1)
        elif f == 4: d = np.stack([sc, -tc, np.ones_like(sc)], -1)
        else: d = np.stack([-sc, -tc, -np.ones_like(sc)], -1)
        l = np.linalg.norm(d, axis=-1); D.append((d / l[..., None]).reshape(-1, 3)); C.append(fs[f].reshape(-1, 3)); W.append((1 / l ** 3).reshape(-1))
    D = np.concatenate(D); C = np.concatenate(C); W = np.concatenate(W)
    M = 64
    t, s = np.mgrid[0:M, 0:M].astype(np.float32); s = (s + 0.5) / M * 2 - 1; t = 1 - (t + 0.5) / M * 2
    rr = s * s + t * t; inside = rr <= 1; nz = np.sqrt(np.clip(1 - rr, 0, 1))
    r = np.stack([2 * nz * s, 2 * nz * t, 2 * nz * nz - 1], -1)
    r[~inside] = np.array([0, 0, -1], np.float32)
    q = np.stack([r[..., 0], r[..., 1], -r[..., 2]], -1).reshape(-1, 3)   # (as sample(): the cube is read along (rx, ry, -rz))
    img = np.zeros((len(q), 3), np.float32)
    for i in range(0, len(q), 512):
        w = np.clip(q[i:i + 512] @ D.T, 0, None) ** lobe * W
        img[i:i + 512] = (w @ C) / w.sum(1, keepdims=True)
    img = img.reshape(M, M, 3)
    im = Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)).resize((N, N), Image.BICUBIC)
    a = np.asarray(im.convert('RGBA')).copy(); a[..., 3] = 255
    return a

if __name__ == '__main__':
    OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DATA')
    SRC = gitspaths.dirp('ANIMOUT', 'weapon', 'Textures') + 'CUBE.dds'
    a = sphere_map(SRC)
    os.makedirs(OUT, exist_ok=True); p = os.path.join(OUT, 'env_ref.gtx')
    with open(p, 'wb') as f: f.write(b'GTX1' + struct.pack('<HHBBHI', N, N, 0, 0, 0, a.nbytes) + a.tobytes())
    print('env_ref.gtx', os.path.getsize(p), 'bytes; mean RGB', np.round(a[..., :3].reshape(-1, 3).mean(0), 1), 'from', SRC)
