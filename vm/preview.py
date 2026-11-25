# Software preview of vm_c30.gvm using the same math the Vita engine will use.
import sys,struct,numpy as np
from PIL import Image
import etcpak
def read(path):
    d=open(path,'rb').read();o=4
    nt,nb,npal,nm,nc=struct.unpack_from('<5I',d,o);o+=20
    texs=[]
    for i in range(nt):
        w,h,fmt,nmp=struct.unpack_from('<HHBB',d,o);o+=6;mm=[]
        for m in range(nmp):
            sz=struct.unpack_from('<I',d,o)[0];o+=4;mm.append(d[o:o+sz]);o+=sz
        texs.append((w,h,fmt,mm))
    bones=[d[o+32*i:o+32*i+32].split(b'\0')[0].decode() for i in range(nb)];o+=32*nb
    pal=[]
    for i in range(npal):
        s=struct.unpack_from('<i',d,o)[0];S=np.frombuffer(d,'<f4',12,o+4).reshape(3,4);o+=52;pal.append((s,S))
    meshes=[]
    dt=np.dtype([('p','<f4',3),('n','i1',4),('uv','<f4',2),('bi','u1',4),('bw','u1',4)])
    for i in range(nm):
        tex,bl,nv,ni=struct.unpack_from('<iiII',d,o);o+=16
        v=np.frombuffer(d,dt,nv,o);o+=nv*dt.itemsize
        I=np.frombuffer(d,'<u2',ni,o).astype(int);o+=ni*2+(2 if ni%2 else 0)
        meshes.append(dict(tex=tex,blend=bl&0xff,base=(bl>>8)&0xff,flags=bl>>16,v=v,I=I))   # (base: the mesh's palette window, bits 8-15 of the blend word)
    clips={}
    for i in range(nc):
        name=d[o:o+16].split(b'\0')[0].decode();nf,loop,fps=struct.unpack_from('<IIf',d,o+16);o+=28
        data=np.frombuffer(d,'<f4',nf*nb*7,o).reshape(nf,nb,7);o+=data.nbytes;clips[name]=(loop,fps,data)
    na=struct.unpack_from('<I',d,o)[0];o+=4;att={}
    for i in range(na): att[d[o:o+16].split(b'\0')[0].decode()]=np.frombuffer(d,'<f4',3,o+16);o+=28
    return dict(texs=texs,bones=bones,pal=pal,meshes=meshes,clips=clips,att=att)
def decode_dxt(w,h,fmt,data):
    bs=8 if fmt==1 else 16;nb=(w//4)*(h//4)
    B=np.frombuffer(data,'u1',nb*bs).reshape(nb,bs)
    cb=B[:,-8:]
    c0=cb[:,0].astype(int)|(cb[:,1].astype(int)<<8);c1=cb[:,2].astype(int)|(cb[:,3].astype(int)<<8)
    def rgb(c): return np.stack([((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31],1).astype(float)
    p0,p1=rgb(c0),rgb(c1)
    four=(c0>c1)|(fmt!=1)
    p2=np.where(four[:,None],(2*p0+p1)/3,(p0+p1)/2);p3=np.where(four[:,None],(p0+2*p1)/3,0)
    pal=np.stack([p0,p1,p2,p3],1)
    idx=cb[:,4:8].astype(np.uint32);bits=idx[:,0]|(idx[:,1]<<8)|(idx[:,2]<<16)|(idx[:,3]<<24)
    sel=np.stack([(bits>>(2*i))&3 for i in range(16)],1)
    col=np.take_along_axis(pal,sel[:,:,None].repeat(3,2),1)
    a=np.full((nb,16),255.0)
    if fmt==1: a=np.where((~four[:,None])&(sel==3),0,255.0)
    else:
        ab=B[:,:8];a0=ab[:,0].astype(float);a1=ab[:,1].astype(float)
        ai=np.zeros((nb,),np.uint64)
        for k in range(6): ai|=ab[:,2+k].astype(np.uint64)<<np.uint64(8*k)
        asel=np.stack([((ai>>np.uint64(3*i))&np.uint64(7)).astype(int) for i in range(16)],1)
        ap=np.zeros((nb,8))
        ap[:,0]=a0;ap[:,1]=a1
        big=a0>a1
        for k in range(2,8): ap[:,k]=np.where(big,((8-k)*a0+(k-1)*a1)/7,0)
        for k in range(2,6): ap[:,k]=np.where(big,ap[:,k],((6-k)*a0+(k-1)*a1)/5)
        ap[:,6]=np.where(big,ap[:,6],0);ap[:,7]=np.where(big,ap[:,7],255)
        a=np.take_along_axis(ap,asel,1)
    rgba=np.concatenate([col,a[:,:,None]],2).reshape(h//4,w//4,4,4,4).transpose(0,2,1,3,4).reshape(h,w,4)
    return rgba
def qmat(q):
    x,y,z,w=q
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
def palette_mats(G,frame):
    out=[]
    for slot,S in G['pal']:
        q=frame[slot,:4];p=frame[slot,4:]
        B=np.eye(4);B[:3,:3]=qmat(q/np.linalg.norm(q));B[:3,3]=p
        S4=np.eye(4);S4[:3,:]=S
        out.append(B@S4)
    return np.array(out)
def render(G,frame,path,fovy=62,W=480,H=272,offset=(0,0,0)):
    M=palette_mats(G,frame)
    f=1/np.tan(np.radians(fovy/2));asp=W/H
    img=np.zeros((H,W,3));img[:]=(0.45,0.5,0.56);zb=np.full((H,W),1e9)
    texc={}
    L=np.array([0.3,0.8,0.5]);L/=np.linalg.norm(L)
    for m in sorted(G['meshes'],key=lambda m:m['blend']):
        v=m['v'];P=np.c_[v['p'],np.ones(len(v))]
        bw=v['bw']/255.0;acc=np.zeros((len(v),4));nacc=np.zeros((len(v),3))
        for j in range(4):
            Mj=M[v['bi'][:,j]+m.get('base',0)]
            acc+=bw[:,j:j+1]*np.einsum('nij,nj->ni',Mj,P)
            nacc+=bw[:,j:j+1]*np.einsum('nij,nj->ni',Mj[:,:3,:3],v['n'][:,:3]/127.0)
        X=acc[:,:3]+np.array(offset)
        nacc/=np.linalg.norm(nacc,axis=1,keepdims=True)+1e-9
        shade=0.45+0.55*np.clip(nacc@L,0,1)
        if m['tex'] not in texc:
            w,h,fmt,mm=G['texs'][m['tex']];texc[m['tex']]=decode_dxt(w,h,fmt,mm[0])/255.0
        T=texc[m['tex']];th,tw=T.shape[:2]
        z=-X[:,2];sx=(X[:,0]/z*f/asp*0.5+0.5)*W;sy=(0.5-X[:,1]/z*f*0.5)*H
        tri=m['I'].reshape(-1,3)
        for a,b,c in tri:
            if z[a]<2 or z[b]<2 or z[c]<2: continue
            x0=int(max(0,np.floor(min(sx[a],sx[b],sx[c]))));x1=int(min(W-1,np.ceil(max(sx[a],sx[b],sx[c]))))
            y0=int(max(0,np.floor(min(sy[a],sy[b],sy[c]))));y1=int(min(H-1,np.ceil(max(sy[a],sy[b],sy[c]))))
            if x0>x1 or y0>y1: continue
            xs,ys=np.meshgrid(np.arange(x0,x1+1)+0.5,np.arange(y0,y1+1)+0.5)
            d=(sy[b]-sy[c])*(sx[a]-sx[c])+(sx[c]-sx[b])*(sy[a]-sy[c])
            if abs(d)<1e-9: continue
            l0=((sy[b]-sy[c])*(xs-sx[c])+(sx[c]-sx[b])*(ys-sy[c]))/d
            l1=((sy[c]-sy[a])*(xs-sx[c])+(sx[a]-sx[c])*(ys-sy[c]))/d
            l2=1-l0-l1
            ins=(l0>=0)&(l1>=0)&(l2>=0)
            if not ins.any(): continue
            iz=l0/z[a]+l1/z[b]+l2/z[c];zz=1/iz
            yy,xx=np.where(ins);yy+=y0;xx+=x0;zz=zz[ins]
            ok=zz<zb[yy,xx]
            if m['blend']==2: ok[:]=True
            if not ok.any(): continue
            yy,xx=yy[ok],xx[ok];zz=zz[ok]
            w0=(l0[ins][ok]/z[a])*zz;w1=(l1[ins][ok]/z[b])*zz;w2=(l2[ins][ok]/z[c])*zz
            uv=w0[:,None]*v['uv'][a]+w1[:,None]*v['uv'][b]+w2[:,None]*v['uv'][c]
            tu=(np.mod(uv[:,0],1)*tw).astype(int)%tw;tv=(np.mod(uv[:,1],1)*th).astype(int)%th
            col=T[tv,tu]
            sh=w0*shade[a]+w1*shade[b]+w2*shade[c]
            if m['blend']==2: img[yy,xx]=np.clip(img[yy,xx]+col[:,:3]*col[:,3:4],0,1);continue
            img[yy,xx]=col[:,:3]*sh[:,None];zb[yy,xx]=zz
    Image.fromarray((np.clip(img,0,1)*255).astype('u1')).save(path)
if __name__=='__main__':
    G=read(sys.argv[1]);clip=sys.argv[2];fr=int(sys.argv[3]);out=sys.argv[4]
    render(G,G['clips'][clip][2][fr],out)
