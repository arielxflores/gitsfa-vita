# Before / after sheet for one map: footage frames (when given) | our render with the shipped files | with the new files,
# at the same player-like views (calib_dist.views). usage:
#   ba_sheet.py STEM OUT.jpg OLD_GMAP OLD_GLUT NEW_GMAP NEW_GLUT [FRAMES_DIR]     (env: N=rows)
import sys, os, glob, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render import Renderer
from calib_dist import views, frame_ok
from PIL import Image, ImageDraw
MAPS = '/root/gits/build/maps'; W, H = 320, 180
def main(stem, out, og, ol, ng, nl, fdir=None, n=5):
    V = views(stem, n, seed=11); gs = '%s/%s.gsky' % (MAPS, stem); gs = gs if os.path.exists(gs) else None; cols = []
    if fdir:
        fs = sorted(glob.glob(fdir + '/*.jpg')); fr = []
        for f in fs:
            a = np.asarray(Image.open(f).convert('RGB').resize((W, H), Image.BOX))
            if frame_ok(a.astype(np.float32) / 255): fr.append(a)
        cols.append(('footage (other places)', [fr[i] for i in np.linspace(0, len(fr) - 1, n).astype(int)]))
    for tag, gm, gl in (('before', og, ol), ('after', ng, nl)):
        R = Renderer(W=W, H=H, ss=2); R.load(stem, gmap=gm, glut=gl, gmode='lut', gsky=gs)
        cols.append(('%s: %s' % (stem, tag), [np.asarray(R.shot(*v, fov=59)) for v in V])); R.ctx.release()
    S = np.vstack([np.hstack([c[1][i] for c in cols]) for i in range(n)]); im = Image.fromarray(S); d = ImageDraw.Draw(im)
    for k, (t, _) in enumerate(cols): d.text((k * W + 4, 3), t, fill=(255, 255, 0))
    im.save(out, quality=88)
if __name__ == '__main__':
    a = sys.argv[1:]; main(*a[:6], fdir=a[6] if len(a) > 6 else None, n=int(os.environ.get('N', 5)))
