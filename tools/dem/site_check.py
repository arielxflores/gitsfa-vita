# Top-down + side check of each Demolition target placed into its map: map collision (build/maps/<stem>.gcol) around the
# target, target collision mesh (EnvBomb/<name>.nif placed with the SpawnSystem.ssf transform) in yellow.
import sys,os,numpy as np,matplotlib;matplotlib.use('Agg');import matplotlib.pyplot as plt
sys.path.insert(0,'/root/gits/tools');sys.path.insert(0,'/root/gits/tools/minimap');sys.path.insert(0,os.path.dirname(__file__))
import export_col,gcol,ssf_dem
RAW='/root/gits/raw/dem/dem_x'
MAPS={'downtown':'Fix_Downtown','ecenter':'Fix_Ecenter','pss9':'Fix_PSS9','trainstation':'Fix_Trainstation','zioplant':'Fix_Zioplant'}
def rotz(a):
    c,s=np.cos(a),np.sin(a);return np.array([[c,-s,0],[s,c,0],[0,0,1]])
def target_mesh(name,v):
    P=[];I=[];o=0
    for n,p,i in export_col.run(RAW+'/worldEnv/EnvBomb/%s.nif'%name):
        P.append(p);I.append(i.reshape(-1,3)+o);o+=len(p)
    P=np.concatenate(P);I=np.concatenate(I)
    R=rotz(v[5])  # rx,ry are 0 on every DEM map
    return P@R.T+np.array(v[:3]),I
if __name__=='__main__':
    out=sys.argv[1]
    fig,axs=plt.subplots(5,4,figsize=(24,30))
    for row,(st,fx) in enumerate(MAPS.items()):
        V,I,S,M=gcol.load('/root/gits/build/maps/%s.gcol'%st)
        r=ssf_dem.load(RAW+'/map/FixedMap/%s/SpawnSystem.ssf'%fx)
        for col,(n,v) in enumerate(r['targets']):
            P,TI=target_mesh(n,v);c=(P.min(0)+P.max(0))/2;R=700
            T=V[I];cen=T.mean(1)
            sel=(np.abs(cen[:,0]-c[0])<R)&(np.abs(cen[:,1]-c[1])<R)&(cen[:,2]>c[2]-400)&(cen[:,2]<c[2]+400)
            T=T[sel];nz=np.abs(np.cross(T[:,1]-T[:,0],T[:,2]-T[:,0]))[:,2];ln=np.linalg.norm(np.cross(T[:,1]-T[:,0],T[:,2]-T[:,0]),axis=1)+1e-9
            ax=axs[row,col*2]
            for t,w in zip(T,nz/ln):
                ax.fill(t[:,0],t[:,1],color=(0.3,0.3,0.6) if w>0.7 else (0.8,0.8,0.8),alpha=0.25 if w>0.7 else 0.6,lw=0)
            for t in P[TI]: ax.fill(t[:,0],t[:,1],color=(1,0.85,0),alpha=0.5,lw=0)
            ax.plot(v[0],v[1],'r+',ms=12);ax.set_aspect('equal');ax.set_xlim(c[0]-R,c[0]+R);ax.set_ylim(c[1]-R,c[1]+R)
            ax.set_title('%s %s top (z %.0f..%.0f)'%(st,n,P[:,2].min(),P[:,2].max()))
            ax=axs[row,col*2+1]   # side view along x (looking +y)
            sel2=np.abs(T.mean(1)[:,1]-c[1])<250
            for t in T[sel2]: ax.fill(t[:,0],t[:,2],color=(0.6,0.6,0.6),alpha=0.3,lw=0)
            for t in P[TI]: ax.fill(t[:,0],t[:,2],color=(1,0.85,0),alpha=0.5,lw=0)
            ax.set_aspect('equal');ax.set_xlim(c[0]-R,c[0]+R);ax.set_ylim(c[2]-300,c[2]+400);ax.set_title('side x/z, slab |y-c|<250')
    plt.tight_layout();plt.savefig(out,dpi=50)
