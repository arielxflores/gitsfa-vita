"""QSA decoder with corrected animated-rotation dequantisation.

Fix (vs qsa.decode_block): an animated rotation channel is NOT mean+residual
added component-wise.  Each channel stores a coarse per-block "mean" rotation
(8-bit per component over [-1,1], tan-quarter-angle vector) and a fine residual
rotation (set-quantised, also a tan-quarter-angle vector).  The two are
composed as quaternions:

    m   = -1 + 2*mean/255                     (per component)
    r   = setMin + s/(2^bits-1)*(setMax-setMin)
    q   = tqa(m) (x) tqa(r)                    (Hamilton product, mean on the left)
    v   = q.xyz / (1 + q.w)   (q canonicalised to w >= 0)  -> stored tqa vector

where tqa(v) = (2v, 1-|v|^2)/(1+|v|^2).  Positions stay additive.
Everything else (header, constants, block layout, bit packing) is as in qsa.py.
"""
import struct, numpy as np
from qsa import u16list, hdr, getbits, r4, const_tracks, block_offsets


def tqa(v):
    v = np.asarray(v, float)
    s = (v * v).sum(-1, keepdims=True)
    return np.concatenate([2 * v, 1 - s], -1) / (1 + s)


def itqa(q):
    """quaternion (x,y,z,w) -> tan-quarter-angle vector, choosing w>=0."""
    q = np.asarray(q, float)
    q = np.where(q[..., 3:4] < 0, -q, q)
    return q[..., :3] / (1 + q[..., 3:4])


def qmul(a, b):
    ax, ay, az, aw = np.moveaxis(a, -1, 0)
    bx, by, bz, bw = np.moveaxis(b, -1, 0)
    return np.stack([aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw,
                     aw * bw - ax * bx - ay * by - az * bz], -1)


def _raw(R, S, data, stride, ns):
    """returns mean bytes (n,3), residual values (ns,n,3)"""
    n = len(R)
    mean = np.array([[r[3 + k] for k in range(3)] for r in R], float).reshape(n, 3)
    res = np.zeros((ns, n, 3))
    for f in range(ns):
        pos = f * stride * 8
        for c, r in enumerate(R):
            for k in range(3):
                bits = r[k]; si = r[6 + k]
                s = getbits(data, pos, bits); pos += bits
                t = s / ((1 << bits) - 1) if bits else 0.0
                smin, smax = S[si][k], S[si][3 + k]
                res[f, c, k] = smin + t * (smax - smin)
    return mean, res


def decode_block(h, b, qmode='compose'):
    """qmode='compose' (correct) or 'add' (old behaviour, for comparison).
    Returns dict(start, ns, P, Q, Qq, H): P positions (ns,nP,3); Q rotations as
    tan-quarter-angle vectors (ns,nQ,3); Qq the same as quaternions (ns,nQ,4)."""
    d = h['d']
    H = struct.unpack_from('<20H', d, b)
    size, nch, start, ns, nP, nQ = H[:6]
    pB, qB = H[9], H[10]
    nPS, nQS = H[15], H[16]
    gp = np.frombuffer(d, '<f4', 6, b + 0x28)
    o = b + 0x9c
    PS = np.frombuffer(d, '<f4', 6 * nPS, o).reshape(-1, 6); o += 24 * nPS
    PR = [d[o + 9 * i:o + 9 * i + 9] for i in range(nP)]; o += 9 * r4(nP)
    pdata = d[o:o + pB * ns + 8]; o += pB * ns; o = (o + 3) & ~3
    QS = np.frombuffer(d, '<f4', 6 * nQS, o).reshape(-1, 6); o += 24 * nQS
    QR = [d[o + 9 * i:o + 9 * i + 9] for i in range(nQ)]; o += 9 * r4(nQ)
    qdata = d[o:o + qB * ns + 8]

    if nP:
        pm, pr = _raw(PR, PS, pdata, pB, ns)
        gmin, gmax = gp[:3].astype(float), gp[3:].astype(float)
        P = gmin + pm / 255 * (gmax - gmin) + pr
    else:
        P = np.zeros((ns, 0, 3))

    if nQ:
        qm, qr = _raw(QR, QS, qdata, qB, ns)
        mv = -1 + 2 * qm / 255
        if qmode == 'add':
            Q = mv + qr
            Qq = tqa(Q)
        else:
            Qq = qmul(np.broadcast_to(tqa(mv), qr.shape[:-1] + (4,)), tqa(qr))
            Qq = np.where(Qq[..., 3:4] < 0, -Qq, Qq)
            Q = itqa(Qq)
    else:
        Q = np.zeros((ns, 0, 3)); Qq = np.zeros((ns, 0, 4))
    return dict(start=start, ns=ns, P=P, Q=Q, Qq=Qq, H=H)
