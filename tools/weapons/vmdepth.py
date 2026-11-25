# Near-plane check: the viewmodel is drawn with near = 2 cm (main.c persp(PV, ..., 2, 400)); with the hip / ADS offset
# of weapons.txt, the closest weapon vertex (meshes >= 2) and arm vertex per clip, and how much of the weapon is cut.
# usage: python3 vmdepth.py vm_a.gvm ...
import sys,os;H=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,H+'/../../vm');sys.path.insert(0,H)
import preview,vmgl,vmstrip,numpy as np
for path in sys.argv[1:]:
    G=preview.read(path);sh=os.path.basename(path)[3:-4];T=vmstrip.wtab(sh);out=[]
    for c,(loop,fps,D) in G['clips'].items():
        a=1.0 if c in ('ads_idle','fire_ads','walk') else 0.0;off=vmstrip.proj(T,a)[1]
        worst=(1e9,0)
        for f in range(0,D.shape[0],max(1,D.shape[0]//10)):
            for i,m in enumerate(G['meshes']):
                if i<2 or len(m['v'])==0: continue
                X,_=vmgl.skin(G,D[f],i);z=-(X[:,2]+off[2]);fov=vmstrip.proj(T,a)[0];t=np.tan(np.radians(fov)/2)
                vis=(z>0.01)&(np.abs(X[:,1]+off[1])<z*t*1.05)&(np.abs(X[:,0]+off[0])<z*t*960/544*1.05)   # inside the view cone
                if not vis.any(): continue
                cut=float(((z<2.0)&vis).sum()/max(1,vis.sum()));zm=float(z[vis].min())
                if zm<worst[0]: worst=(zm,cut)
        out.append((worst[0],c,worst[1]))
    out.sort();print('%-10s'%sh,'nearest visible weapon vertex:','  '.join('%s %.1fcm%s'%(c,z,' CUT %.0f%%'%(100*k) if k>0 else '') for z,c,k in out[:4]))
