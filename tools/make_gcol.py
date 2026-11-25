# Player collision for one map: the player mesh (world_colp "coc_player") plus every solid material mesh in world_colb
# (crates, railings, glass, cars...), except wires. Version 2 stores a material id per triangle.
import sys,struct,numpy as np,os;sys.path.insert(0,os.path.dirname(__file__))
import export_col
from nifmesh import *
MAT={'coc_player':0,'cob_wood':1,'cob_metal':2,'cob_metal1':2,'cob_sand':3,'cob_glass':4,'cob_glass1':4,'cob_car':5,'cob_paperbox':6,'cob_soft':7,'cob_conc':8}
def named_meshes(path):
    h=load(path);d=h['d'];B=h['blocks'];names=[]
    def walk(k,parent):
        t,o,s=B[k]
        if t=='NiNode':
            a=node(d,o)
            for c in a['children']: walk(c,S(h,a['name']))
        elif t=='NiMesh': names.append(parent)
    walk(0,'');return names
MAPDIR=sys.argv[1];OUT=sys.argv[2]
Ps=[];Is=[];Ms=[];off=0;skipped=[]
for f in ['world_colp.nif','world_colb.nif']:
    meshes=export_col.run(MAPDIR+'/'+f);names=named_meshes(MAPDIR+'/'+f)
    for (n,p,i),nm in zip(meshes,names):
        if nm not in MAT: skipped.append((nm,len(i)//3));continue
        Ps.append(p);Is.append(i.reshape(-1,3)+off);Ms.append(np.full(len(i)//3,MAT[nm],'u1'));off+=len(p)
P=np.concatenate(Ps).astype('<f4');I=np.concatenate(Is).astype('<u4');M=np.concatenate(Ms)
d=open(MAPDIR+'/SpawnSystem.ssf','rb').read();groups=[]
for k in range(32):
    o=0x40c+k*0x200
    if o+8>len(d): break
    cnt=struct.unpack_from('<i',d,o)[0];typ=struct.unpack_from('<f',d,o+4)[0]
    if not (0<cnt<=64) or typ<1000: continue
    groups.append((int(typ),[struct.unpack_from('<6f',d,o+8+24*j) for j in range(cnt)]))
with open(OUT,'wb') as f:
    f.write(b'GCOL'+struct.pack('<III',2,len(P),len(I)));f.write(P.tobytes());f.write(I.tobytes())
    sp=[(gi,t,s) for gi,(t,sl) in enumerate(groups) for s in sl]
    f.write(struct.pack('<I',len(sp)))
    for gi,t,s in sp: f.write(struct.pack('<ii4f',gi,t,s[0],s[1],s[2],s[5]))
    f.write(M.tobytes())
print('collision tris',len(I),'by material',np.bincount(M).tolist(),'skipped',skipped,'spawns',len(sp),'size',os.path.getsize(OUT))
