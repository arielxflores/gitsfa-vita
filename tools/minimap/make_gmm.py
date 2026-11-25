# Per-map minimap texture for the engine: <stem>.gmm = 'GMM1', w, h, nmip, world->texel affine (6 floats), DXT5 mips
import struct, numpy as np, etcpak, os, sys
from PIL import Image
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
D = gitspaths.dirp('MINIMAP')                       # minimap.txt + minimap/<img>.png
O = gitspaths.dirp('DATA') if len(sys.argv) < 2 else sys.argv[1].rstrip('/') + '/'
for line in open(D + 'minimap.txt'):
    if line.startswith('#') or not line.strip(): continue
    p = line.split()
    if p[1] == 'none': continue
    stem, img = p[0], p[1]; a, b, c, d, e, f = map(float, p[2:8])
    im = Image.open(D + 'minimap/%s.png' % img).convert('RGBA'); W, H = im.size; k = W / 600.0
    aff = [a * k, b * k, c * k, d * k, e * k, f * k]
    mips = []; cur = im; w, h = W, H
    while w >= 64:
        px = np.asarray(cur, np.uint8).copy(); px[..., :3] = (px[..., :3].astype(np.float32) * px[..., 3:4] / 255).astype(np.uint8)   # premultiplied
        mips.append(etcpak.compress_to_dxt5(np.ascontiguousarray(px).tobytes(), w, h))
        w //= 2; h //= 2; cur = cur.resize((w, h), Image.BOX)
    with open(O + stem + '.gmm', 'wb') as fo:
        fo.write(b'GMM1' + struct.pack('<III', W, H, len(mips)) + struct.pack('<6f', *aff))
        for m in mips: fo.write(struct.pack('<I', len(m))); fo.write(m)
    print(stem, img, os.path.getsize(O + stem + '.gmm'))
