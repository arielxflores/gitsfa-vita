# SWF renderer with Flash 3D (Matrix3D + perspective projection) for the HUD widgets
import numpy as np, re
from swf import *
import render as R2
from render import Canvas, coverage, sample_bitmap, grad_color, CX0, cxcompose
OVR={}   # display path -> 4x4 local adjustment (layout fixes to match the shipped game)
def m2(m): a,b,c,d,tx,ty=m; return np.array([[a,c,0,tx],[b,d,0,ty],[0,0,1,0],[0,0,0,1]],np.float64)
class R3:
    def __init__(s,ch,font=None,sym=None,mat3d=None,fov=50.0,center=(800,450),stage_w=1600,hide=None,show_only=None):
        s.ch=ch; s.font=font; s.sym=sym or {}; s.m3=mat3d or {}; s.f=(stage_w/2)/np.tan(np.radians(fov/2)); s.cx,s.cy=center
        s.P=np.array([[1,0,s.cx/s.f,0],[0,1,s.cy/s.f,0],[0,0,1/s.f,1]],np.float64)
        s.hide=hide or (lambda path:False); s.frame_of={}; s.text_of={}; s.out_scale=1.0; s.ovr=OVR
    def H(s,W):   # local plane (x,y,1) -> screen homogeneous
        return s.out_scale_m@(s.P@W)[:, [0,1,3]]
    def draw(s,cid,W,cx,cv,path='',cls_parent=None,frame=0):
        c=s.ch.get(cid)
        if c is None: return
        k=c['kind']
        if k=='shape': s.shape(c,W,cx,cv)
        elif k=='sprite': s.sprite(cid,c,W,cx,cv,path,s.frame_of.get(path,frame))
        elif k=='text': s.text(c,W,cx,cv,s.text_of.get(path))
    def child_matrix(s,cid_parent,p):
        cls=s.sym.get(cid_parent,''); cls=cls.split('.')[-1]
        nm=p.get('name')
        if nm and (cls,nm) in s.m3: return s.m3[(cls,nm)].T
        return m2(p.get('m',(1,0,0,1,0,0)))
    def sprite(s,cid,c,W,cx,cv,path,frame):
        if not c['frames']: return
        dl=c['frames'][min(frame,len(c['frames'])-1)]; clips=[]
        for dep in sorted(dl):
            p=dl[dep]; cp=path+'.'+(p.get('name') or 'd%d'%dep)
            if p.get('visible',1)==0 or s.hide(cp): continue
            clips=[x for x in clips if dep<=x[0]]
            Wm=W@s.child_matrix(cid,p)
            if cp in s.ovr: Wm=W@s.ovr[cp][0]@s.child_matrix(cid,p)@s.ovr[cp][1]
            cc=cxcompose(cx,p['cx']) if 'cx' in p else cx
            if 'clip' in p:
                mc=Canvas(cv.W,cv.H); s.draw(p.get('id'),Wm,CX0,mc,cp); clips.append((p['clip'],mc.px[...,3:4].copy())); continue
            if clips:
                tmp=Canvas(cv.W,cv.H); s.draw(p.get('id'),Wm,cc,tmp,cp)
                m=np.ones_like(clips[0][1])
                for _,mk in clips: m=m*mk
                cv.px[:]=tmp.px*m+cv.px*(1-tmp.px[...,3:4]*m)
            else: s.draw(p.get('id'),Wm,cc,cv,cp)
    def proj(s,H,x,y):
        X=H[0,0]*x+H[0,1]*y+H[0,2]; Y=H[1,0]*x+H[1,1]*y+H[1,2]; Wh=H[2,0]*x+H[2,1]*y+H[2,2]
        return X/Wh,Y/Wh
    def fillpix(s,st,H,cx,x0,y0,Wd,Ht):
        ys,xs=np.mgrid[y0:y0+Ht,x0:x0+Wd]+0.5
        if st['t']=='solid': col=np.broadcast_to(np.array(st['c'],np.float32)/255.0,(Ht,Wd,4)).copy()
        else:
            Hi=np.linalg.inv(H); lx,ly=s.proj(Hi,xs,ys)
            if st['t']=='bmp':
                b=s.ch.get(st['id'])
                if b is None or b['kind']!='bitmap': col=np.zeros((Ht,Wd,4),np.float32)
                else:
                    a,bb,c,d,tx,ty=st['m']; inv=R2.minv((a/20,bb/20,c/20,d/20,tx,ty)); u,v=R2.mapply(inv,lx,ly)
                    col=sample_bitmap(b['img'],u,v,st.get('repeat',False))
            else:
                inv=R2.minv(st['m']); gx,gy=R2.mapply(inv,lx,ly)
                t=(gx+819.2)/1638.4 if st['t']=='lin' else np.sqrt(gx*gx+gy*gy)/819.2
                col=grad_color(st['g'],t)
        mul,add=cx; col=col*np.array(mul,np.float32)+np.array(add,np.float32)/255.0
        return np.clip(col,0,1)
    def shape(s,c,W,cx,cv):
        E=c['edges']
        if not E: return
        H=s.H(W); pts=np.array([[e[0],e[1],e[2],e[3]] for e in E],np.float64)
        X0,Y0=s.proj(H,pts[:,0],pts[:,1]); X1,Y1=s.proj(H,pts[:,2],pts[:,3]); T=np.stack([X0,Y0,X1,Y1],1)
        keys={}
        for i,e in enumerate(E):
            for f in (e[4],e[5]):
                if f: keys.setdefault(f,[]).append(i)
        for key in sorted(keys):
            segs=T[keys[key]]; P=np.concatenate([segs[:,:2],segs[:,2:]])
            x0,y0,x1,y1=R2.bbox_of(P,cv)
            if x1<=x0 or y1<=y0: continue
            cov=coverage(segs,x0,y0,x1-x0,y1-y0)
            if cov.max()<=0: continue
            st=c['fills'][key[0]][key[1]-1]; col=s.fillpix(st,H,cx,x0,y0,x1-x0,y1-y0); al=col[...,3:4]*cov[...,None]
            cv.comp(x0,y0,np.concatenate([col[...,:3]*al,al],2))
        lk={}
        for i,e in enumerate(E):
            if e[6]: lk.setdefault(e[6],[]).append(i)
        A=H[:2,:2]; sc=np.sqrt(abs(np.linalg.det(A)))/max(abs(H[2,2]),1e-6)
        for key,idx in lk.items():
            ls=c['lines'][key[0]][key[1]-1]; w=max(ls['w']*sc,1.0)/2; quads=[]
            for i in idx:
                xa,ya,xb,yb=T[i]; dx,dy=xb-xa,yb-ya; l=np.hypot(dx,dy)
                if l<1e-6: continue
                nx,ny=-dy/l*w,dx/l*w; ex,ey=dx/l*w*0.5,dy/l*w*0.5
                quads.append([(xa-ex+nx,ya-ey+ny),(xb+ex+nx,yb+ey+ny),(xb+ex-nx,yb+ey-ny),(xa-ex-nx,ya-ey-ny)])
            if not quads: continue
            P=np.array([p for q in quads for p in q]); x0,y0,x1,y1=R2.bbox_of(P,cv)
            if x1<=x0 or y1<=y0: continue
            cov=np.zeros((y1-y0,x1-x0),np.float32)
            for q in quads: cov=np.maximum(cov,coverage(np.array([[q[k][0],q[k][1],q[(k+1)%4][0],q[(k+1)%4][1]] for k in range(4)]),x0,y0,x1-x0,y1-y0))
            mul,add=cx; col=np.clip(np.array(ls['c'],np.float32)/255.0*np.array(mul,np.float32)+np.array(add,np.float32)/255.0,0,1)
            al=col[3]*cov[...,None]; cv.comp(x0,y0,np.concatenate([np.broadcast_to(col[:3],al.shape[:2]+(3,))*al,al],2))
    def text(s,t,W,cx,cv,override=None):
        txt=override if override is not None else t.get('text','')
        if t.get('html'):
            m=re.search(r'color="#([0-9A-Fa-f]{6})"',txt)
            if m and 'color_html' not in t: t=dict(t); t['color']=[int(m.group(1)[i:i+2],16) for i in (0,2,4)]+[255]
            sz=re.search(r'size="(\d+)"',txt)
            if sz: t=dict(t); t['size']=float(sz.group(1))
            txt=re.sub(r'<[^>]+>','',txt).replace('&amp;','&').replace('&lt;','<').replace('&gt;','>')
        if not txt or not s.font: return
        f=s.font; size=t.get('size',12); sc=size/1024.0; x0b,x1b,y0b,y1b=t['bounds']
        col=t.get('color',[255,255,255,255])
        for li,line in enumerate(txt.replace('\r','\n').split('\n')):
            wpx=sum(f['adv'][f['codes'][ord(ch)]]*sc if ord(ch) in f['codes'] else size*0.5 for ch in line)
            al=t.get('align',0); x=x0b+2+t.get('lm',0)
            if al==1: x=x1b-2-wpx
            elif al==2: x=(x0b+x1b)/2-wpx/2
            y=y0b+2+f['asc']*sc+li*size*1.15; edges=[]
            for ch in line:
                g=f['codes'].get(ord(ch))
                if g is None: x+=size*0.5; continue
                for e in f['glyphs'][g]: edges.append((x+e[0]*sc,y+e[1]*sc,x+e[2]*sc,y+e[3]*sc,(0,1),0,0))
                x+=f['adv'][g]*sc
            if edges: s.shape(dict(edges=edges,fills=[[dict(t='solid',c=col)]],lines=[[]]),W,cx,cv)
def setup(swf_path,scripts,font_swf):
    import as3d
    S=load(swf_path); ch=characters(S); sym=symbols(S)
    F=load(font_swf); font=[v for v in characters(F).values() if isinstance(v,dict) and v.get('kind')=='font'][0]
    return S,ch,sym,as3d.load(scripts),font
