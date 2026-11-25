# Render InGameUI.swf subtrees flat (each 'iconN' state on its own) into a labelled sheet, to pick HUD parts.
# usage: swf_peek.py out.png path [path...]   (path ending in '/*' = one image per iconN child)
import sys,re,numpy as np
sys.path.insert(0,'/root/gits/tools/ui')
import hudmv as M
from PIL import Image,ImageDraw
mv=M.dmovie() if __import__("os").environ.get("MOVIE")=="D" else M.main()
def flat(path,hide=None,scale=1.0):
    img,loc=mv.render_flat(path,scale,hide=hide)
    return img
def only(parent,k):
    pat=re.compile(re.escape(parent)+r'\.icon(\d+)(\.|$)')
    return lambda p: bool(pat.match(p)) and int(pat.match(p).group(1))!=k
tiles=[]
for arg in sys.argv[2:]:
    if arg.endswith('/*'):
        p=arg[:-2];kids=[n for n,_ in mv.children(p) if re.fullmatch(r'icon\d+',n)]
        for n in kids:
            k=int(n[4:]);im=flat(p,hide=only(p,k));tiles.append(('%s:%s'%(p.split('.')[-1],n),im))
    else: tiles.append((arg,flat(arg)))
W=0;rows=[]
for lab,im in tiles:
    if im is None: im=np.zeros((10,10,4),np.float32)
    a=im;bg=np.ones_like(a[...,:3])*np.array([0.2,0.25,0.3]);rgb=a[...,:3]+bg*(1-a[...,3:4])
    I=Image.fromarray((np.clip(rgb,0,1)*255).astype(np.uint8));rows.append((lab,I));W=max(W,I.size[0]+260)
H=sum(max(I.size[1],16)+6 for _,I in rows)
S=Image.new('RGB',(min(W,1600),H),(0,0,0));d=ImageDraw.Draw(S);y=0
for lab,I in rows:
    d.text((2,y),lab[-40:],fill=(255,255,0));S.paste(I,(260,y));y+=max(I.size[1],16)+6
S.save(sys.argv[1])
