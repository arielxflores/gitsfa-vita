import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from menulib import *
from PIL import Image, ImageDraw
def to_img(px,bg=(70,45,45)):
    a=px[...,3:4]; return Image.fromarray(np.clip(px[...,:3]*255+np.array(bg,np.float32)*(1-a),0,255).astype(np.uint8))
def sheet(mv,base,out,sc=0.6,hide=None,frames=None,cols=4,maxw=480):
    tiles=[]
    for k,_ in mv.children(base):
        try: img,loc=mv.render_flat(base+'.'+k,sc,hide=hide,frames=frames)
        except Exception as e: print('ERR',k,e); continue
        if img is None: continue
        t=to_img(img); 
        if t.size[0]>maxw: t=t.resize((maxw,max(1,int(t.size[1]*maxw/t.size[0]))))
        w,h=t.size; tt=Image.new('RGB',(max(w,140),h+14),(40,40,40)); tt.paste(t,(0,14)); ImageDraw.Draw(tt).text((2,1),'%s %s'%(k,[round(v) for v in loc]),fill=(255,255,0)); tiles.append(tt)
    if not tiles: print('nothing'); return
    W=max(t.size[0] for t in tiles); rows=(len(tiles)+cols-1)//cols; Hh=max(t.size[1] for t in tiles)
    im=Image.new('RGB',(W*cols,Hh*rows),(20,20,20))
    for i,t in enumerate(tiles): im.paste(t,((i%cols)*W,(i//cols)*Hh))
    im.save(out); print(out, im.size)
if __name__=='__main__':
    mv=movie(sys.argv[1]); sheet(mv,sys.argv[2],sys.argv[3],float(sys.argv[4]) if len(sys.argv)>4 else 0.6)
