import swf,render,sys,numpy as np
from PIL import Image
S=swf.load('/root/gits/ui/ui/GFx_DA/InGameUI.swf'); ch=swf.characters(S)
F=swf.load('/root/gits/ui2/ui/GFx_DA/Data/Localization/fonts_en.swf'); font=[v for v in swf.characters(F).values() if isinstance(v,dict) and v.get('kind')=='font'][0]
R=render.R(ch,font)
def shot(cid,out,ox=400,oy=300,W=800,H=600,scale=1.0,frame=0,bg=(40,60,70)):
    W,H=int(W),int(H); cv=render.Canvas(W,H); R.draw(cid,(scale,0,0,scale,ox,oy),render.CX0,cv,frame)
    img=np.clip(cv.px,0,1); a=img[...,3:4]; o=(img[...,:3]*255+np.array(bg,np.float32)*(1-a)).astype(np.uint8)
    nz=np.argwhere(a[...,0]>0.01); bb=(nz.min(0),nz.max(0)) if len(nz) else None
    Image.fromarray(o).save(out); return bb
if __name__=='__main__':
    cid=int(sys.argv[1]); print(shot(cid,sys.argv[2],*[float(x) for x in sys.argv[3:]]))
