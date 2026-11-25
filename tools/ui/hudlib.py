import render3,numpy as np,swf
from PIL import Image, ImageDraw
S,ch,sym,m3,font=render3.setup('/root/gits/ui/ui/GFx_DA/InGameUI.swf','/tmp/claude-0/ffdec/ingame_as/scripts','/root/gits/ui2/ui/GFx_DA/Data/Localization/fonts_en.swf')
ROOT,_=swf.frames(S['tags'])
STAGE={'W':1600,'H':900,'fw':1600,'Y':{}}
X={'killrecord':1600-6,'watchList':1600-17,'hpapMC':1600-8,'tkHPMC':1600-83,'model':1600-36,'tutMIMC':1600-12,'minimap':26,'donut':19,'intelMC':10,'transfer':240,'rmMC':800-18,'tkskillMC':85,'tccomaMC':800}
def root_item(name):
    for dep in sorted(ROOT[0]):
        p=ROOT[0][dep]
        if p.get('name')==name:
            m=list(p.get('m',(1,0,0,1,0,0)))
            W=STAGE['W']
            XS={'killrecord':W-6,'watchList':W-17,'hpapMC':W-8,'tkHPMC':W-83,'model':W-36,'tutMIMC':W-12,'minimap':26,'donut':19,'intelMC':10,'transfer':240,'rmMC':W//2-18,'tkskillMC':85,'tccomaMC':W//2}
            if name in XS: m[4]=XS[name]
            if name in STAGE['Y']: m[5]=STAGE['Y'][name]
            return p['id'],render3.m2(m)
def renderer(hide=None,frames=None,texts=None,scale=1.0):
    R=render3.R3(ch,font,sym,m3,hide=hide,center=(STAGE['W']//2,STAGE['H']//2),stage_w=STAGE['fw'])
    R.out_scale_m=np.diag([scale,scale,1.0]); R.frame_of=frames or {}; R.text_of=texts or {}
    return R
def draw_items(names,W=1600,H=900,scale=1.0,hide=None,frames=None,texts=None):
    R=renderer(hide,frames,texts,scale); cv=render3.Canvas(int(W*scale),int(H*scale))
    for n in names:
        cid,M=root_item(n); R.draw(cid,M,render3.CX0,cv,n)
    return cv.px
def to_img(px,bg=(60,70,75)):
    img=np.clip(px,0,1); a=img[...,3:4]; return Image.fromarray((img[...,:3]*255+np.array(bg,np.float32)*(1-a)).astype(np.uint8))
def children(path):
    """direct children (name, id) of a display path like 'hpapMC.hpapMC'"""
    parts=path.split('.'); cid,_=root_item(parts[0])
    for nm in parts[1:]:
        c=ch[cid]; dl=c['frames'][0]; nxt=None
        for dep in sorted(dl):
            if (dl[dep].get('name') or 'd%d'%dep)==nm: nxt=dl[dep]['id']; break
        cid=nxt
    c=ch[cid]; dl=c['frames'][0]
    return [((dl[d].get('name') or 'd%d'%d),dl[d].get('id')) for d in sorted(dl)]
def sheet_children(path,crop,out,scale=1.0):
    kids=children(path); tiles=[]
    for nm,cid in kids:
        full=path+'.'+nm
        hide=lambda p,full=full: not (p==full or p.startswith(full+'.') or full.startswith(p+'.') or p==path.split('.')[0])
        px=draw_items([path.split('.')[0]],hide=lambda p,full=full: (p.count('.')>=full.count('.') and not (p==full or p.startswith(full+'.'))) )
        im=to_img(px).crop(crop); d=ImageDraw.Draw(im); d.text((3,3),nm,fill=(255,255,0)); tiles.append(im)
    w,h=tiles[0].size; cols=6; rows=(len(tiles)+cols-1)//cols; s=Image.new('RGB',(w*cols,h*rows))
    for i,t in enumerate(tiles): s.paste(t,((i%cols)*w,(i//cols)*h))
    s.save(out); return kids
