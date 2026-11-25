# b104: what the special-maneuver records (smf.txt, make_smf.py) mean, checked against the maps' player collision.
# Prints, per map: each CLIMB's floor above the path's end (the hang) just beyond the face - the floor is 1.75-2.1 m above
# it, the face quad's top is not the floor - and each VAULT's arc as a cubic Bezier of its four points next to the highest
# surface under it. engine/src/player.h smf_start / smf_at follow these readings.
# usage: python3 smf_check.py <folder of UNPACKED .gcol (tools/glz: glz d)> <smf.txt>
import sys
GCOL, SMF = sys.argv[1], sys.argv[2]
import sys, struct, glob, os, numpy as np, collections
def load(st):
    d_=open('%s/%s.gcol'%(GCOL,st),'rb').read(); mg,ver,nv,nt=struct.unpack_from('<4sIII',d_,0)
    P=np.frombuffer(d_,'<f4',nv*3,16).reshape(-1,3); I=np.frombuffer(d_,'<u4',nt*3,16+nv*12).reshape(-1,3); o=16+nv*12+nt*12; ns=struct.unpack_from('<I',d_,o)[0]; o+=4+ns*24
    fl_=np.frombuffer(d_,'u1',nt,o)>>4; I=I[(fl_&1)>0]; A=P[I[:,0]];B=P[I[:,1]];C=P[I[:,2]]
    return A,B,C,np.minimum(np.minimum(A,B),C),np.maximum(np.maximum(A,B),C)
def floors(M,x,y,z0,z1):
    A,B,C,lo,hi=M; m=(lo[:,0]<=x)&(hi[:,0]>=x)&(lo[:,1]<=y)&(hi[:,1]>=y)&(hi[:,2]>=z0)&(lo[:,2]<=z1); out=[]
    for a,b,c in zip(A[m],B[m],C[m]):
        d=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(d)<1e-6: continue
        l0=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/d; l1=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/d; l2=1-l0-l1
        if l0<-1e-4 or l1<-1e-4 or l2<-1e-4: continue
        z=l0*a[2]+l1*b[2]+l2*c[2]; n=np.cross(b-a,c-a); nz=abs(n[2])/(np.linalg.norm(n)+1e-9)
        if z0<=z<=z1 and nz>0.7: out.append(float(z))
    return sorted(set(round(z) for z in out))
maps={}; rows=[]; vrows=[]
for l in open(SMF):
    if l[0]=='#': continue
    st=l.split()[0]
    if st not in maps:
        try: maps[st]=load(st)
        except Exception as e: maps[st]=None
    M=maps[st]
    if M is None: continue
    v=[float(x) for x in l.split()[1:]]; Pp=np.array(v[9:21]).reshape(4,3); F=np.array(v[21:33]).reshape(4,3); d=Pp[3,:2]-Pp[0,:2]; hl=np.linalg.norm(d); d=d/max(hl,1e-6); ftop=F[:,2].max()
    if ftop>Pp[3,2]+60 and hl<120:
        a,b=F[0],F[1]
        if abs(F[1,2]-F[0,2])>50: b=F[3]
        t=(b-a)[:2]; tl=np.linalg.norm(t); n=np.array([-t[1],t[0]])/tl if tl>1 else d
        if n@d<0: n=-n
        e=Pp[3,:2]; onp=e-n*((e-a[:2])@n); best=None
        for s in (45,60,75,90,110,130,150):
            x,y=onp+n*s; fl=[z for z in floors(M,x,y,Pp[3,2]+60,ftop+60)]
            if fl: best=(s,fl); break
        rows.append((st,round(float(Pp[3,2]-Pp[0,2])),round(float(ftop-Pp[3,2])),best and best[0],best and [z-round(float(Pp[3,2])) for z in best[1]]))
    else:
        # vault: Bezier vs through-the-points peak, obstacle top under the path
        ts=np.linspace(0,1,41); Bz=np.array([(1-t)**3*Pp[0]+3*(1-t)**2*t*Pp[1]+3*(1-t)*t*t*Pp[2]+t**3*Pp[3] for t in ts]); clear=[]
        for q in Bz[2:-2:2]:
            fl=floors(M,q[0],q[1],Pp[0,2]-500,q[2]+250); under=[z for z in fl if z<=q[2]+40]
            if under: clear.append(q[2]-max(under))
        vrows.append((st,round(float(hl)),round(float(Bz[:,2].max()-Pp[0,2])),round(float(max(Pp[1,2],Pp[2,2])-Pp[0,2])),round(min(clear)) if clear else None, round(float(F[:,2].min()-Pp[0,2])), round(float(ftop-Pp[0,2]))))
print('CLIMBS: map, hang feet above start, face top above the hang, floor found at s cm beyond: heights above the hang feet')
for r in rows: print('  ',r)
print('VAULTS: map, across, Bezier peak, control-point peak, least clearance of the Bezier (feet) over the surface under it, face bottom, face top')
for r in vrows: print('  ',r)
print('VAULT OBSTACLES: map, across, start->end dz, obstacle top above start (highest player-blocking surface under the path between 15% and 85%), its place along, Bezier z there, control points z+, face bottom/top')
import collections
C=collections.Counter()
for l in open(SMF):
    if l[0]=='#': continue
    st=l.split()[0]; M=maps.get(st)
    if M is None: continue
    v=[float(x) for x in l.split()[1:]]; Pp=np.array(v[9:21]).reshape(4,3); F=np.array(v[21:33]).reshape(4,3); d=Pp[3,:2]-Pp[0,:2]; hl=np.linalg.norm(d); ftop=F[:,2].max()
    if ftop>Pp[3,2]+60 and hl<120: continue
    best=(-999,0,0)
    for t in np.linspace(0.12,0.88,20):
        q=(1-t)**3*Pp[0]+3*(1-t)**2*t*Pp[1]+3*(1-t)*t*t*Pp[2]+t**3*Pp[3]
        fl=[z for z in floors(M,q[0],q[1],min(Pp[0,2],Pp[3,2])-50,Pp[0,2]+200)]
        if fl and max(fl)-Pp[0,2]>best[0]: best=(max(fl)-Pp[0,2],t,q[2]-Pp[0,2])
    row=(st,round(float(hl)),round(float(Pp[3,2]-Pp[0,2])),round(float(best[0])),round(best[1],2),round(float(best[2])),tuple(round(float(z-Pp[0,2])) for z in Pp[1:3,2]),round(float(F[:,2].min()-Pp[0,2])),round(float(ftop-Pp[0,2])))
    C[row[1:]]+=1
for r,n in sorted(C.items(),key=lambda x:-x[1])[:40]: print('  x%d'%n,r)
