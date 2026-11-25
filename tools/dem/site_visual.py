# Which visible world_lmap.nif meshes overlap each Demolition target (to see whether the target object is part of the map art).
import sys,os,numpy as np,pickle
sys.path.insert(0,'/root/gits/tools');sys.path.insert(0,os.path.dirname(__file__))
import export_map,ssf_dem
from site_check import target_mesh,MAPS,RAW
for st,fx in MAPS.items():
    pk='/root/gits/raw/dem/check/%s_lmap.pkl'%st
    if os.path.exists(pk): m=pickle.load(open(pk,'rb'))
    else:
        f=[x for x in os.listdir('/root/gits/maps/'+fx) if x.lower()=='world_lmap.nif'][0]
        m=export_map.run('/root/gits/maps/%s/%s'%(fx,f),pk)
    r=ssf_dem.load(RAW+'/map/FixedMap/%s/SpawnSystem.ssf'%fx)
    for n,v in r['targets']:
        P,_=target_mesh(n,v);lo,hi=P.min(0)-20,P.max(0)+20
        hits=[]
        for x in m:
            Q=x['P'];ins=np.all((Q>=lo)&(Q<=hi),axis=1)
            if ins.mean()>0.5 and len(Q)>8: hits.append((x['name'],os.path.basename(x['tex'] or '-'),len(Q),round(float(ins.mean()),2)))
        print(st,n,'target bbox',lo.round(),hi.round());[print('   ',h) for h in hits[:12]]
