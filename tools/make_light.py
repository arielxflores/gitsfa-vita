# <name>.glgt: light under foot for characters and the first-person weapon, sampled from the map's own lightmaps.
# A 2D grid of columns (CELL cm); each column keeps up to NF floor surfaces (height + lightmap colour * 2, as the map shader).
# usage: make_light.py MAPDIR PKL OUT
import sys,os,struct,numpy as np
from PIL import Image
MAPDIR,PKL,OUT=sys.argv[1],sys.argv[2],sys.argv[3]
import pickle
CELL=150.0; NF=6
m=pickle.load(open(PKL,'rb'))
LM={}
def lmimg(p):
    if p not in LM:
        f=MAPDIR+'/'+os.path.splitext(p.replace('\\','/'))[0]+'.png'
        LM[p]=np.asarray(Image.open(f).convert('RGB')).astype(np.float32)/255 if os.path.exists(f) else None
    return LM[p]
allP=np.concatenate([x['P'] for x in m]); x0,y0=allP[:,0].min()-CELL,allP[:,1].min()-CELL
gw=int((allP[:,0].max()-x0)/CELL)+2; gh=int((allP[:,1].max()-y0)/CELL)+2
cols=[[] for _ in range(gw*gh)]
for x in m:
    if not x['lm'] or x['UV1'] is None: continue
    img=lmimg(x['lm'])
    if img is None: continue
    H,W=img.shape[:2]
    P=x['P'];UV=x['UV1'];I=x['I'].reshape(-1,3)
    A,B,C=P[I[:,0]],P[I[:,1]],P[I[:,2]]
    n=np.cross(B-A,C-A); ln=np.linalg.norm(n,axis=1); ok=(ln>1e-6)&(n[:,2]>0.6*ln)   # floors (facing up)
    for k in np.where(ok)[0]:
        a,b,c=A[k],B[k],C[k]
        cx0=int(np.floor((min(a[0],b[0],c[0])-x0)/CELL)); cx1=int(np.floor((max(a[0],b[0],c[0])-x0)/CELL))
        cy0=int(np.floor((min(a[1],b[1],c[1])-y0)/CELL)); cy1=int(np.floor((max(a[1],b[1],c[1])-y0)/CELL))
        d=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
        if abs(d)<1e-6: continue
        for gy in range(cy0,cy1+1):
            for gx in range(cx0,cx1+1):
                px,py=x0+(gx+0.5)*CELL,y0+(gy+0.5)*CELL
                u=((px-a[0])*(c[1]-a[1])-(py-a[1])*(c[0]-a[0]))/d; v=((b[0]-a[0])*(py-a[1])-(b[1]-a[1])*(px-a[0]))/d
                if u<-0.02 or v<-0.02 or u+v>1.02: continue
                z=a[2]+u*(b[2]-a[2])+v*(c[2]-a[2]); i0,i1,i2=I[k]
                t=UV[i0]+u*(UV[i1]-UV[i0])+v*(UV[i2]-UV[i0])
                tx=min(W-1,max(0,int(t[0]%1.0*W))); ty=min(H-1,max(0,int(t[1]%1.0*H)))
                # average a few texels around the point (lightmap texels are coarse; avoid seams)
                r=img[max(0,ty-2):ty+3,max(0,tx-2):tx+3].reshape(-1,3).mean(0)*2
                cols[gy*gw+gx].append((float(z),r))
out=bytearray(b'GLGT'+struct.pack('<ffffII',x0,y0,CELL,0,gw,gh)); nonempty=0
for c in cols:
    c.sort(key=lambda e:-e[0]); keep=[]
    for z,r in c:
        if not keep or abs(keep[-1][0]-z)>120: keep.append((z,r))   # distinct floors 1.2 m+ apart (top first)
        if len(keep)==NF: break
    nonempty+=bool(keep)
    out+=struct.pack('<B',len(keep))
    for z,r in keep: out+=struct.pack('<h3B',int(max(-32768,min(32767,round(z/2)))),*[int(max(0,min(255,round(v*100)))) for v in r])   # z in 2 cm units, colour *100
open(OUT,'wb').write(out)
print(os.path.basename(OUT),'grid',gw,'x',gh,'columns with floor',nonempty,'bytes',len(out))
