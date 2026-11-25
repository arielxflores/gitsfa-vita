# Software renderer for SWF characters (shapes with solid/gradient/bitmap fills, strokes, sprites with masks, text)
import numpy as np, re
from swf import *
SS=4
def mcompose(A,B):   # A o B, matrices (a,b,c,d,tx,ty): x'=a x + c y + tx, y'=b x + d y + ty
    a,b,c,d,tx,ty=A; e,f,g,h,ux,uy=B
    return (a*e+c*f, b*e+d*f, a*g+c*h, b*g+d*h, a*ux+c*uy+tx, b*ux+d*uy+ty)
def minv(A):
    a,b,c,d,tx,ty=A; det=a*d-b*c
    if abs(det)<1e-12: det=1e-12
    ia,ib,ic,id_=d/det,-b/det,-c/det,a/det
    return (ia,ib,ic,id_,-(ia*tx+ic*ty),-(ib*tx+id_*ty))
def mapply(A,x,y): a,b,c,d,tx,ty=A; return a*x+c*y+tx, b*x+d*y+ty
I2=(1,0,0,1,0,0)
def cxcompose(A,B):  # apply B then A
    (ma,aa),(mb,ab)=A,B
    return ([ma[i]*mb[i] for i in range(4)],[ma[i]*ab[i]+aa[i] for i in range(4)])
CX0=([1,1,1,1],[0,0,0,0])
class Canvas:
    def __init__(s,W,H): s.W,s.H=W,H; s.px=np.zeros((H,W,4),np.float32)
    def comp(s,x0,y0,src):   # src premultiplied (h,w,4) in 0..1
        h,w=src.shape[:2]; d=s.px[y0:y0+h,x0:x0+w]; d[:]=src+d*(1-src[...,3:4])
def coverage(segs,x0,y0,W,H):
    """even-odd coverage of segments (n,4) in canvas px within box -> (H,W) float"""
    HS,WS=H*SS,W*SS; diff=np.zeros((HS,WS+1),np.int32)
    for xa,ya,xb,yb in segs:
        if ya==yb: continue
        if ya>yb: xa,ya,xb,yb=xb,yb,xa,ya
        j0=int(np.ceil((ya-y0)*SS-0.5)); j1=int(np.ceil((yb-y0)*SS-0.5))
        j0=max(j0,0); j1=min(j1,HS)
        if j0>=j1: continue
        ys=y0+(np.arange(j0,j1)+0.5)/SS; xs=xa+(ys-ya)*(xb-xa)/(yb-ya)
        xi=np.clip(np.ceil((xs-x0)*SS-0.5).astype(np.int64),0,WS)
        np.add.at(diff,(np.arange(j0,j1),xi),1)
    par=(np.cumsum(diff[:,:WS],axis=1)&1).astype(np.float32)
    return par.reshape(H,SS,W,SS).mean((1,3))
def bbox_of(pts,canvas):
    x0=int(np.floor(pts[:,0].min()))-1; x1=int(np.ceil(pts[:,0].max()))+1; y0=int(np.floor(pts[:,1].min()))-1; y1=int(np.ceil(pts[:,1].max()))+1
    x0=max(x0,0); y0=max(y0,0); x1=min(x1,canvas.W); y1=min(y1,canvas.H)
    return x0,y0,x1,y1
def sample_bitmap(img,u,v,repeat,smooth=True):
    h,w=img.shape[:2]; f=img.astype(np.float32)/255.0
    u=u-0.5; v=v-0.5
    if repeat: u=np.mod(u,w); v=np.mod(v,h)
    else: u=np.clip(u,0,w-1); v=np.clip(v,0,h-1)
    x0=np.floor(u).astype(int); y0=np.floor(v).astype(int); fx=(u-x0)[...,None]; fy=(v-y0)[...,None]
    x1=(x0+1)%w if repeat else np.minimum(x0+1,w-1); y1=(y0+1)%h if repeat else np.minimum(y0+1,h-1); x0%=w; y0%=h
    c=f[y0,x0]*(1-fx)*(1-fy)+f[y0,x1]*fx*(1-fy)+f[y1,x0]*(1-fx)*fy+f[y1,x1]*fx*fy
    return c
def grad_color(g,t):
    recs=g['recs']; r=np.array([x[0] for x in recs],np.float32)/255.0; c=np.array([x[1] for x in recs],np.float32)/255.0
    t=np.clip(t,0,1); out=np.zeros(t.shape+(4,),np.float32)
    for k in range(4): out[...,k]=np.interp(t,r,c[:,k])
    return out
class R:
    def __init__(s,ch,font=None): s.ch=ch; s.font=font; s.frame_of={}
    def draw(s,cid,M,cx,cv,frame=0):
        c=s.ch.get(cid)
        if c is None: return
        k=c['kind']
        if k=='shape': s.shape(c,M,cx,cv)
        elif k=='sprite': s.sprite(cid,c,M,cx,cv,s.frame_of.get(cid,frame))
        elif k=='text': s.text(c,M,cx,cv)
    def sprite(s,cid,c,M,cx,cv,frame):
        if not c['frames']: return
        dl=c['frames'][min(frame,len(c['frames'])-1)]
        clips=[]   # (until_depth, mask)
        for dep in sorted(dl):
            p=dl[dep]
            if p.get('visible',1)==0: continue
            clips=[x for x in clips if dep<=x[0]]
            Mm=mcompose(M,p.get('m',I2)); cc=cxcompose(cx,p['cx']) if 'cx' in p else cx
            if 'clip' in p:
                mc=Canvas(cv.W,cv.H); s.draw(p.get('id'),Mm,CX0,mc,0); clips.append((p['clip'],mc.px[...,3:4].copy())); continue
            if clips:
                tmp=Canvas(cv.W,cv.H); s.draw(p.get('id'),Mm,cc,tmp,0)
                m=np.ones_like(clips[0][1])
                for _,mk in clips: m=m*mk
                cv.px[:]=tmp.px*m+cv.px*(1-tmp.px[...,3:4]*m)
            else: s.draw(p.get('id'),Mm,cc,cv,0)
    def fillpix(s,st,M,cx,x0,y0,W,H):
        ys,xs=np.mgrid[y0:y0+H,x0:x0+W]+0.5
        if st['t']=='solid': col=np.broadcast_to(np.array(st['c'],np.float32)/255.0,(H,W,4)).copy()
        elif st['t']=='bmp':
            b=s.ch.get(st['id'])
            if b is None or b['kind']!='bitmap': col=np.zeros((H,W,4),np.float32)
            else:
                a,bb,c,d,tx,ty=st['m']; FM=(a/20,bb/20,c/20,d/20,tx,ty); inv=minv(mcompose(M,FM))
                u,v=mapply(inv,xs,ys); col=sample_bitmap(b['img'],u,v,st.get('repeat',False))
        else:
            inv=minv(mcompose(M,st['m'])); gx,gy=mapply(inv,xs,ys)
            t=(gx+819.2)/1638.4 if st['t']=='lin' else np.sqrt(gx*gx+gy*gy)/819.2
            col=grad_color(st['g'],t)
        mul,add=cx; col=col*np.array(mul,np.float32)+np.array(add,np.float32)/255.0
        return np.clip(col,0,1)
    def shape(s,c,M,cx,cv):
        E=c['edges']
        if not E: return
        pts=np.array([[e[0],e[1],e[2],e[3]] for e in E],np.float64)
        a,b,cc,d,tx,ty=M
        X0=a*pts[:,0]+cc*pts[:,1]+tx; Y0=b*pts[:,0]+d*pts[:,1]+ty; X1=a*pts[:,2]+cc*pts[:,3]+tx; Y1=b*pts[:,2]+d*pts[:,3]+ty
        T=np.stack([X0,Y0,X1,Y1],1)
        keys={}
        for i,e in enumerate(E):
            for f in (e[4],e[5]):
                if f: keys.setdefault(f,[]).append(i)
        for key in sorted(keys,key=lambda k:(k[0],k[1])):
            idx=keys[key]; segs=T[idx]; P=np.concatenate([segs[:,:2],segs[:,2:]])
            x0,y0,x1,y1=bbox_of(P,cv)
            if x1<=x0 or y1<=y0: continue
            cov=coverage(segs,x0,y0,x1-x0,y1-y0)
            if cov.max()<=0: continue
            st=c['fills'][key[0]][key[1]-1]
            col=s.fillpix(st,M,cx,x0,y0,x1-x0,y1-y0); al=col[...,3:4]*cov[...,None]
            cv.comp(x0,y0,np.concatenate([col[...,:3]*al,al],2))
        # strokes
        lk={}
        for i,e in enumerate(E):
            if e[6]: lk.setdefault(e[6],[]).append(i)
        sc=np.sqrt(abs(a*d-b*cc))
        for key,idx in lk.items():
            ls=c['lines'][key[0]][key[1]-1]; w=max(ls['w']*sc,1.0)/2
            quads=[]
            for i in idx:
                xa,ya,xb,yb=T[i]; dx,dy=xb-xa,yb-ya; l=np.hypot(dx,dy)
                if l<1e-6: continue
                nx,ny=-dy/l*w,dx/l*w; ex,ey=dx/l*w*0.5,dy/l*w*0.5
                q=[(xa-ex+nx,ya-ey+ny),(xb+ex+nx,yb+ey+ny),(xb+ex-nx,yb+ey-ny),(xa-ex-nx,ya-ey-ny)]
                quads.append(q)
            if not quads: continue
            P=np.array([p for q in quads for p in q]); x0,y0,x1,y1=bbox_of(P,cv)
            if x1<=x0 or y1<=y0: continue
            cov=np.zeros((y1-y0,x1-x0),np.float32)
            for q in quads:
                segs=np.array([[q[k][0],q[k][1],q[(k+1)%4][0],q[(k+1)%4][1]] for k in range(4)])
                cov=np.maximum(cov,coverage(segs,x0,y0,x1-x0,y1-y0))
            mul,add=cx; col=np.clip(np.array(ls['c'],np.float32)/255.0*np.array(mul,np.float32)+np.array(add,np.float32)/255.0,0,1)
            al=col[3]*cov[...,None]; cv.comp(x0,y0,np.concatenate([np.broadcast_to(col[:3],al.shape[:2]+(3,))*al,al],2))
    def text(s,t,M,cx,cv,override=None):
        txt=override if override is not None else t.get('text','')
        if t.get('html'): txt=re.sub(r'<[^>]+>','',txt).replace('&amp;','&').replace('&lt;','<').replace('&gt;','>')
        if not txt or not s.font: return
        f=s.font; size=t.get('size',12); sc=size/1024.0; x0b,x1b,y0b,y1b=t['bounds']
        lines=txt.replace('\r','\n').split('\n'); col=t.get('color',[255,255,255,255])
        for li,line in enumerate(lines):
            wpx=sum(f['adv'][f['codes'][ord(ch)]]*sc if ord(ch) in f['codes'] else size*0.5 for ch in line)
            al=t.get('align',0); x=x0b+2+t.get('lm',0)
            if al==1: x=x1b-2-wpx
            elif al==2: x=(x0b+x1b)/2-wpx/2
            y=y0b+2+f['asc']*sc+li*size*1.15
            edges=[]
            for ch in line:
                g=f['codes'].get(ord(ch))
                if g is None: x+=size*0.5; continue
                for e in f['glyphs'][g]: edges.append((x+e[0]*sc,y+e[1]*sc,x+e[2]*sc,y+e[3]*sc,(0,1),0,0))
                x+=f['adv'][g]*sc
            if edges: s.shape(dict(edges=edges,fills=[[dict(t='solid',c=col)]],lines=[[]]),M,cx,cv)
