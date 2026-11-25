# GVM1 reader + pose sampling + CPU skinning (numpy ports of engine/src/model.h: model_load, model_sample, model_palette,
# skin_mesh) for the host renders of tools/vision.
import struct, io, numpy as np
from PIL import Image

def dds_rgba(w, h, data, fmt):
    four = b'DXT1' if fmt == 1 else b'DXT5'
    hdr = b'DDS ' + struct.pack('<IIIIIII', 124, 0x1007, h, w, len(data), 0, 1) + b'\0' * 44 + struct.pack('<II4sIIIII', 32, 4, four, 0, 0, 0, 0, 0) + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
    return np.asarray(Image.open(io.BytesIO(hdr + data)).convert('RGBA'))

class GVM:
    def __init__(s, path):
        d = open(path, 'rb').read(); o = 4
        assert d[:4] == b'GVM1'
        s.ntex, s.nb, s.npal, s.nmesh, s.nclip = struct.unpack_from('<5I', d, o); o += 20
        s.tex = []
        for i in range(s.ntex):
            w, h, fmt, nmp = struct.unpack_from('<HHBB', d, o); o += 6; lv = []
            for m in range(nmp):
                sz = struct.unpack_from('<I', d, o)[0]; o += 4; lv.append(d[o:o + sz]); o += sz
            s.tex.append(dds_rgba(w, h, lv[0], fmt))
        s.bone = [d[o + 32 * i:o + 32 * i + 32].split(b'\0')[0].decode() for i in range(s.nb)]; o += 32 * s.nb
        s.pal_slot = []; s.pal_s2b = []
        for i in range(s.npal):
            s.pal_slot.append(struct.unpack_from('<i', d, o)[0]); s.pal_s2b.append(np.array(struct.unpack_from('<12f', d, o + 4)).reshape(3, 4)); o += 52
        s.mesh = []
        for i in range(s.nmesh):
            tex, blend, nv, ni = struct.unpack_from('<iiii', d, o); o += 16
            V = d[o:o + nv * 32]; o += nv * 32
            I = np.frombuffer(d, '<u2', ni, o).copy(); o += ni * 2 + (2 if ni & 1 else 0)
            a = np.frombuffer(V, np.uint8).reshape(nv, 32)
            pos = a[:, 0:12].copy().view('<f4').reshape(nv, 3)
            nrm = a[:, 12:16].copy().view(np.int8).astype('f4')[:, :3] / 127.0
            uv = a[:, 16:24].copy().view('<f4').reshape(nv, 2)
            bi = a[:, 24:28].astype(int); bw = a[:, 28:32].astype('f4') / 255.0
            pbase = (blend >> 8) & 0xff; blend &= 0xff; lods = (blend >> 4) & 7   # model.h: palette window, chr.h: LOD bits
            s.mesh.append(dict(tex=tex, blend=blend & 15, lods=lods, pbase=pbase, nv=nv, ni=ni, pos=pos, nrm=nrm, uv=uv, bi=bi + pbase, bw=bw, raw2=a[:, 29].copy(), I=I))
        s.clip = []
        for i in range(s.nclip):
            nm = d[o:o + 16].split(b'\0')[0].decode(); nf, loop, fps = struct.unpack_from('<iif', d, o + 16); o += 28
            data = np.frombuffer(d, '<f4', nf * s.nb * 7, o).reshape(nf, s.nb, 7).copy(); o += nf * s.nb * 28
            s.clip.append(dict(name=nm, nf=nf, loop=loop, fps=fps, data=data))
        s.att = {}
        if o + 4 <= len(d):
            na = struct.unpack_from('<I', d, o)[0]; o += 4
            for i in range(min(na, 8)):
                nm = d[o:o + 16].split(b'\0')[0].decode(); s.att[nm] = struct.unpack_from('<3f', d, o + 16); o += 28
    def find_clip(s, n):
        for i, c in enumerate(s.clip):
            if c['name'] == n: return i
        return -1
    def sample(s, ci, t):
        c = s.clip[ci]; n = c['nf']; fr = t * c['fps']
        if c['loop']: L = n - 1; fr = fr % L if L > 0 else 0
        else: fr = min(max(fr, 0), n - 1)
        i0 = int(fr); i0 = min(i0, max(n - 2, 0)); i1 = min(i0 + 1, n - 1); a = min(fr - i0, 1.0)
        A, B = c['data'][i0], c['data'][i1]
        d = (A[:, :4] * B[:, :4]).sum(1, keepdims=True); sb = np.where(d < 0, -a, a)
        q = A[:, :4] * (1 - a) + B[:, :4] * sb; q /= np.linalg.norm(q, axis=1, keepdims=True)
        p = A[:, 4:] + (B[:, 4:] - A[:, 4:]) * a
        return q, p
    def palette(s, q, p):
        out = np.zeros((s.npal, 3, 4))
        for e in range(s.npal):
            b = s.pal_slot[e]; x, y, z, w = q[b]
            R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
            S = s.pal_s2b[e]; o = R @ S; o[:, 3] += p[b]; out[e] = o
        return out
    def skin(s, pal):
        """model-space positions and normals per mesh (skin_mesh: all weights for positions, first bone for normals)"""
        out = []
        for m in s.mesh:
            P = np.c_[m['pos'], np.ones(m['nv'])]
            m['bi'] = np.minimum(m['bi'], s.npal - 1)
            single = m['raw2'] == 0
            pos = np.zeros((m['nv'], 3))
            for j in range(4):
                R = pal[m['bi'][:, j]]                     # nv x 3 x 4
                w = np.where(single, 1.0 if j == 0 else 0.0, m['bw'][:, j])
                pos += w[:, None] * np.einsum('nij,nj->ni', R, P)
            R0 = pal[m['bi'][:, 0]][:, :, :3]
            nrm = np.einsum('nij,nj->ni', R0, m['nrm'])
            out.append((pos.astype('f4'), nrm.astype('f4')))
        return out
