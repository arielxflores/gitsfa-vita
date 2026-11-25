import sys, re; sys.path.insert(0,'/root/gits/tools/ui')
import numpy as np, hudmv as M, render3
from PIL import Image, ImageDraw
SWF='/root/gits/ui2/ui/GFx_DA/%s.swf'
def movie(name):
    return M.Movie(SWF % name)
def tree(mv, path, depth=0, maxd=4, out=None):
    out = out if out is not None else []
    kids = mv.children(path); seen = {}
    for nm, cid in kids:
        if re.match(r'd\d+$', nm) or re.match(r'(icon|no|zero|record|slot|item|list)(\d+)$', nm):
            seen[cid] = seen.get(cid, 0) + 1
            if seen[cid] > 1:
                if seen[cid] == 2: out.append('  ' * depth + '  ... more of %s' % cid)
                continue
        mm = re.match(r'(icon|no|zero|record|slot|item|list)(\d+)$', nm)
        c = mv.ch.get(cid, {}); kind = c.get('kind')
        info = ''
        if kind == 'text': info = repr(re.sub('<[^>]+>', '', c.get('text', ''))[:40]) + ' b=%s sz=%s' % (c.get('bounds'), c.get('size'))
        if kind == 'sprite': info = 'fr=%d' % len(c.get('frames', []))
        if kind == 'bitmap': info = 'bmp %s' % (c['img'].shape,)
        out.append('  ' * depth + '%s %s %s %s %s' % (nm, cid, kind, mv.sym.get(cid) or '', info))
        if kind == 'sprite' and depth < maxd and not (mm and int(mm.group(2)) > 1): tree(mv, path + '.' + nm, depth + 1, maxd, out)
    return out
def roots(mv):
    return [(p.get('name') or 'd%d' % d, p.get('id'), mv.sym.get(p.get('id')), [round(x, 1) for x in p.get('m', (1, 0, 0, 1, 0, 0))]) for d, p in sorted(mv.ROOT[0].items())]
def shot(mv, out, names=None, hide=None, texts=None, frames=None, scale=0.6, bg=(0.1, 0.12, 0.15)):
    M.set_stage(1600, 906.6666667, 0.6)
    names = names or [p.get('name') for d, p in sorted(mv.ROOT[0].items()) if p.get('name')]
    R = mv.renderer(hide, frames, texts); R.out_scale_m = np.diag([scale, scale, 1.0])
    cv = render3.Canvas(int(round(1600 * scale)), int(round(906.67 * scale)))
    for n in names:
        try:
            cid, Mx = mv.root_item(n); R.draw(cid, Mx, render3.CX0, cv, n)
        except Exception as e: print('ERR', n, e)
    a = cv.px[..., 3:4]; img = np.clip(cv.px[..., :3] + np.array(bg) * (1 - a), 0, 1)
    Image.fromarray((img * 255).astype(np.uint8)).save(out)
if __name__ == '__main__':
    mv = movie(sys.argv[1])
    for r in roots(mv): print(r)
    if len(sys.argv) > 2:
        print('\n'.join(tree(mv, sys.argv[2], 0, int(sys.argv[3]) if len(sys.argv) > 3 else 3)))
