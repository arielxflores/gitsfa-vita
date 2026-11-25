# side view of a vm_<gun>.gvm's gun (no arms), in the Grip's frame, muzzle to the left as the PC item pictures
import sys,os,numpy as np
sys.path.insert(0,'/root/gits/vm')
import preview as p
from PIL import Image,ImageDraw
def gun_side(path,W=384,H=150,pad=6):
    G=p.read(path);B=G['bones'];ig=B.index('Grip');loop,fps,d=G['clips']['idle'];fr=d[0]
    M=p.palette_mats(G,fr);Rg=p.qmat(fr[ig,:4]/np.linalg.norm(fr[ig,:4]));pg=fr[ig,4:]
    tris=[]
    for m in G['meshes'][2:]:
        v=m['v'];P=np.c_[v['p'],np.ones(len(v))];bw=v['bw']/255.0;acc=np.zeros((len(v),4));nacc=np.zeros((len(v),3))
        for j in range(4):
            Mj=M[v['bi'][:,j].astype(int)+m['base']];acc+=bw[:,j:j+1]*np.einsum('nij,nj->ni',Mj,P);nacc+=bw[:,j:j+1]*np.einsum('nij,nj->ni',Mj[:,:3,:3],v['n'][:,:3]/127.0)
        X=(acc[:,:3]-pg)@Rg;N=nacc@Rg;N/=np.linalg.norm(N,axis=1,keepdims=True)+1e-9
        w,h,fmt,mm=G['texs'][m['tex']];T=p.decode_dxt(w,h,fmt,mm[0])
        I=m['I'].reshape(-1,3);uv=v['uv'][I].mean(1);col=T[np.clip((uv[:,1]%1*h).astype(int),0,h-1),np.clip((uv[:,0]%1*w).astype(int),0,w-1)]
        tris.append((X[I],N[I].mean(1),col,m['blend']))
    return tris
def draw(tris,W=384,H=150,pad=8,ax=(0,2),flip=(-1,-1),dep=1,box=None):
    allp=np.concatenate([t[0].reshape(-1,3) for t in tris]);a,b=ax
    mn=allp.min(0);mx=allp.max(0) 
    if box is None: box=(mn[a],mx[a],mn[b],mx[b])
    s=min((W-2*pad)/(box[1]-box[0]),(H-2*pad)/(box[3]-box[2]))
    cx=(box[0]+box[1])/2;cy=(box[2]+box[3])/2
    im=Image.new('RGB',(W*2,H*2),(52,44,46));dr=ImageDraw.Draw(im);items=[]
    for X,N,col,bl in tris:
        if bl: continue
        sx=(W/2+flip[0]*(X[:,:,a]-cx)*s)*2;sy=(H/2+flip[1]*(X[:,:,b]-cy)*s)*2;z=X[:,:,dep].mean(1)
        sh=0.35+0.65*np.abs(N[:,dep])
        for i in range(len(X)): items.append((z[i],sx[i],sy[i],tuple(int(min(255,c*sh[i]*1.5+18)) for c in col[i,:3])))
    return items,im,dr,box,s
def render(path,W=384,H=150,side=1):
    tris=gun_side(path)
    # which Grip axis is the barrel: the longest extent; up: second
    allp=np.concatenate([t[0].reshape(-1,3) for t in tris]);ext=allp.max(0)-allp.min(0);o=np.argsort(-ext);a,b,dep=int(o[0]),int(o[1]),int(o[2])
    items,im,dr,box,s=draw(tris,W,H,ax=(a,b),dep=dep)
    items.sort(key=lambda t:t[0]*side)
    for z,sx,sy,c in items: dr.polygon([(sx[0],sy[0]),(sx[1],sy[1]),(sx[2],sy[2])],fill=c)
    return im.resize((W,H),Image.LANCZOS),(a,b,dep,box,s)
if __name__=='__main__':
    im,info=render(sys.argv[1]);im.save(sys.argv[2]);print(info)
