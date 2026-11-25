# stick figures of an animation bank's clips (chr_og_*.gab / chr_anim_*.gab), front view: right side red, left side blue, Grip yellow.
# usage: python3 gabview.py <bank.gab> <out.png> <name parts, comma separated> [0..1 = where in each clip]   (b99: finding the results poses)
import struct,sys,numpy as np
from PIL import Image,ImageDraw
def load(path):
    d=open(path,'rb').read();nb,nc,ns,nsl=struct.unpack_from('<4I',d,8);p=24
    bones=[];par=[]
    for i in range(nb): bones.append(d[p:p+32].split(b'\0')[0].decode());par.append(struct.unpack_from('<i',d,p+32)[0]);p+=36
    p+=16*nsl;p+=((16+2*nsl+3)&~3)*ns;hsz=68+8*nb;data=p+hsz*nc;clips={}
    for c in range(nc):
        nm=d[p:p+32].split(b'\0')[0].decode();nf,fl=struct.unpack_from('<HH',d,p+32);off,ln,nq=struct.unpack_from('<3I',d,p+56)
        rot=struct.unpack_from('<%dI'%nb,d,p+68);pos=struct.unpack_from('<%dI'%nb,d,p+68+4*nb)
        clips[nm]=(nf,data+off,nq,rot,pos);p+=hsz
    return d,bones,par,clips
def pose(G,name,fr):
    d,bones,par,clips=G;nf,off,nq,rot,pos=clips[name];fr=min(fr,nf-1);nb=len(bones);P=np.zeros((nb,3));R=[None]*nb
    for b in range(nb):
        r=rot[b];i=(r&0x7fffffff)+(fr if r>>31 else 0);q=np.array(struct.unpack_from('<4h',d,off+8*i),float)/32767;q/=np.linalg.norm(q)
        t=pos[b];j=(t&0x7fffffff)+(fr if t>>31 else 0);lp=np.array(struct.unpack_from('<3f',d,off+8*nq+12*j))
        x,y,z,w=q;M=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
        if par[b]<0: R[b]=M;P[b]=lp
        else: R[b]=R[par[b]]@M;P[b]=P[par[b]]+R[par[b]]@lp
    return P,R
def draw(G,names,out,fr=0.5,cols=6):
    d,bones,par,clips=G;W,H=170,250;n=len(names);rows=(n+cols-1)//cols
    im=Image.new('RGB',(W*cols,H*rows),(30,40,48));dr=ImageDraw.Draw(im)
    for k,nm in enumerate(names):
        nf=clips[nm][0];P,R=pose(G,nm,int(fr*(nf-1)));ox=(k%cols)*W+W//2;oy=(k//cols)*H+H-12
        # view from the front: camera at -y looking +y: screen x = world x, depth = y (nearer = smaller y)
        order=sorted(range(len(bones)),key=lambda b:-P[b][1])
        for b in order:
            if par[b]<0: continue
            a=P[par[b]];c=P[b];col=(230,90,90) if '_R_' in bones[b] else (90,160,240) if '_L_' in bones[b] else (220,220,220)
            if bones[b]=='Grip': col=(240,220,60)
            dr.line([ox+a[0]*1.25,oy-a[2]*1.25,ox+c[0]*1.25,oy-c[2]*1.25],fill=col,width=3)
        g=bones.index('Grip');Rg=R[g];tip=P[g]+Rg@np.array([40.0,0,0])
        h=bones.index('Bip01_Head');f=P[h]+R[h]@np.array([0,0,0.0])
        dr.text((k%cols*W+3,k//cols*H+2),'%s %d'%(nm[:22],nf),fill=(255,255,255))
        # facing: pelvis forward arrow (world -y = toward the camera)
        sp=bones.index('Bip01_Spine');fw=R[sp]@np.array([0,0,1.0])
        dr.text((k%cols*W+3,k//cols*H+14),'head y%.0f z%.0f'%(P[h][1],P[h][2]),fill=(160,200,160))
    im.save(out)
if __name__=='__main__':
    G=load(sys.argv[1]);names=[n for n in G[3] if any(n.startswith(p) or p in n for p in sys.argv[3].split(','))]
    draw(G,names,sys.argv[2],float(sys.argv[4]) if len(sys.argv)>4 else 0.5)
