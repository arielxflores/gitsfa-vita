# side-by-side sheet: footage frame | our host render at the located camera (locate.py json)
# usage: pairs.py STEM LOC.json OUT.jpg [glut] [gmap] [gsky]
import sys, os, math, json, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render import Renderer
from PIL import Image, ImageDraw
MAPS = '/root/gits/build/maps'
def sheet(stem, loc, out, glut=None, gmap=None, gsky=None, W=480, H=270, gmode='poly', minscore=0.0, R=None):
    if R is None:
        R = Renderer(W=W, H=H, ss=2); gs = gsky or '%s/%s.gsky' % (MAPS, stem)
        R.load(stem, gmap=gmap, glut=glut, gsky=gs if os.path.exists(gs) else None, gmode=gmode)
    items = [(f, v) for f, v in loc.items() if v['score'] >= minscore]
    S = Image.new('RGB', (W * 2, H * len(items)), (0, 0, 0))
    for i, (f, v) in enumerate(items):
        a = Image.open(f).convert('RGB').resize((W, H), Image.BOX)
        b = R.shot(v['pos'], math.radians(v['yaw']), math.radians(v['pitch']), fov=59).resize((W, H), Image.BOX)
        S.paste(a, (0, i * H)); S.paste(b, (W, i * H)); d = ImageDraw.Draw(S)
        d.text((4, i * H + 4), '%s  footage' % os.path.basename(f), fill=(255, 255, 0))
        d.text((W + 4, i * H + 4), 'ours (%s)  match %.2f  %.0f %.0f %.0f yaw %.0f pitch %.0f' % (stem, v['score'], *v['pos'], v['yaw'], v['pitch']), fill=(255, 255, 0))
    S.save(out, quality=88); return R
if __name__ == '__main__':
    stem, lp, out = sys.argv[1:4]; a = sys.argv[4:] + [None] * 3
    sheet(stem, json.load(open(lp)), out, glut=a[0], gmap=a[1], gsky=a[2])
