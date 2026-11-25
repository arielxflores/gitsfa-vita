import numpy as np, sys, json, cv2
sys.path.insert(0, '/root/gits/tools/minimap')
import gcol, topdown, register as R
from PIL import Image, ImageDraw
def overlay(st, A, image, out):
    D = topdown.D; V, I, S, M = gcol.load(D + st + '.gcol'); N = topdown.load_nav(D + st + '.gnav'); W = topdown.wall_tris(V, I, S)
    a = np.asarray(Image.open(R.MAPDIR + image + '.dds').convert('RGBA')).astype(np.float32) / 255
    bg = np.array([0.12, 0.12, 0.22]); rgb = a[..., :3] * a[..., 3:4] + bg * (1 - a[..., 3:4])
    im = Image.fromarray((rgb * 255).astype(np.uint8)).resize((1200, 1200), Image.LANCZOS); d = ImageDraw.Draw(im)
    A = np.asarray(A) * 2
    for t in W[::max(1, len(W) // 20000)]:
        P = t[:, :2] @ A[:, :2].T + A[:, 2]; d.line([tuple(p) for p in P] + [tuple(P[0])], fill=(255, 140, 0))
    for t in N:
        P = t[:, :2] @ A[:, :2].T + A[:, 2]; d.line([tuple(p) for p in P] + [tuple(P[0])], fill=(60, 255, 90))
    for s in S:
        u, v = A[:, :2] @ [s['x'], s['y']] + A[:, 2]; d.ellipse([u - 4, v - 4, u + 4, v + 4], fill=(255, 40, 40))
    d.text((8, 8), '%s -> %s' % (st, image), fill=(255, 255, 0)); im.save(out)
if __name__ == '__main__':
    res = json.load(open(sys.argv[1]))
    for st, r in res.items(): overlay(st, r['A'], r['image'], '/tmp/claude-0/minimap/%s.png' % st)
