# Reads a .ghud pack (hud.h format): images, nodes (local->screen homography), fields. Used by the legibility audit.
import struct, sys, numpy as np
def read(path):
    d = open(path, 'rb').read(); o = 0
    def u(fmt):
        nonlocal o; v = struct.unpack_from(fmt, d, o); o += struct.calcsize(fmt); return v
    assert d[:4] == b'GHUD'; o = 4; ver, = u('<I'); tw0, th0, tw1, th1 = u('<4I'); tw2, th2 = u('<2I') if ver >= 2 else (0, 0)   # (b92 v2: a third page)
    n, = u('<I'); imgs = {}
    for i in range(n):
        nm = d[o:o + 32].split(b'\0')[0].decode(); o += 32; tex, p0, p1, p2 = u('<4B'); x, y, w, h = u('<4H'); loc = u('<4f')
        imgs[nm] = dict(i=i, tex=tex, x=x, y=y, w=w, h=h, loc=loc)
    n, = u('<I'); nodes = {}
    for i in range(n):
        nm = d[o:o + 32].split(b'\0')[0].decode(); o += 32; H = np.array(u('<9f')).reshape(3, 3); mul = u('<4f'); add = u('<4f')
        nodes[nm] = dict(i=i, H=H, mul=mul, add=add)
    n, = u('<I'); fields = {}
    for i in range(n):
        nm = d[o:o + 32].split(b'\0')[0].decode(); o += 32; nd, = u('<i'); b = u('<4f'); size, = u('<f'); al, = u('<i'); col = u('<4B')
        fields[nm] = dict(node=nd, b=b, size=size, align=al, col=col)
    ng, asc, desc = struct.unpack_from('<Iff', d, o)
    return dict(imgs=imgs, nodes=nodes, fields=fields, asc=asc, desc=desc, tex=((tw0, th0), (tw1, th1)), tex2=(tw2, th2))
def proj(H, x, y):
    v = H @ np.array([x, y, 1.0]); return v[:2] / v[2]
if __name__ == '__main__':
    P = read(sys.argv[1]); pat = sys.argv[2] if len(sys.argv) > 2 else ''
    nn = {v['i']: k for k, v in P['nodes'].items()}
    print('images', len(P['imgs']), 'nodes', len(P['nodes']), 'fields', len(P['fields']), 'tex', P['tex'])
    for k, f in P['fields'].items():
        if pat in k:
            H = P['nodes'][nn[f['node']]]['H']; a = proj(H, f['b'][0], f['b'][2]); b = proj(H, f['b'][0], f['b'][2] + f['size'])
            print('field %-16s node %-14s size %5.1f -> %4.1f px  at %s' % (k, nn[f['node']], f['size'], np.linalg.norm(b - a), np.round(a)))
