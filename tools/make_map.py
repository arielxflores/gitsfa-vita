# One map -> <name>.gmap (visual + lightmaps, map_visual.py), <name>.gcol (player collision + spawns + materials), <name>.gnav (AI nav mesh).
# usage: make_map.py MAPDIR OUTDIR NAME [TEXMAX]
import sys,os,struct,numpy as np,etcpak,subprocess
from PIL import Image
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)));sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'lib'))
import gitspaths
import export_map,export_col
from uvrebase import rebase,split16
from nifmesh import *
MAPDIR,OUTD,NAME=sys.argv[1],sys.argv[2],sys.argv[3];TEXMAX=int(sys.argv[4]) if len(sys.argv)>4 else 256
# ---------- visual: <name>.gmap v2 with the NIF materials (alpha modes, glow, two-sided, neutral lightmaps) -- see map_visual.py
import map_visual
map_visual.build(MAPDIR,OUTD,NAME,TEXMAX)
# ---------- collision + spawns
# <name>.gcol v3: vertices, triangles, spawn points, then one byte per triangle = material | flags << 4
#   flags: 1 blocks the player (world_colp.nif: coc_player walls/floors and coc_jumper, the vaultable cover and window
#          frames of the "W + SPACE" moves), 2 hit by bullets / raycast() (coc_player and the world_colb.nif cob_* meshes),
#          4 climbable / vaultable cover (coc_jumper). The player only collides with world_colp; bullets with colb (+colp).
MAT={'coc_player':0,'cob_wood':1,'cob_wood1':1,'cob_metal':2,'cob_metal1':2,'cob_sand':3,'cob_glass':4,'cob_glass1':4,'cob_conc_glass':4,'cob_metal_glass':4,
     'cob_car':5,'cob_paperbox':6,'cob_paperbox1':6,'cob_soft':7,'cob_soft1':7,'cob_conc':8,'cob_conc1':8}
SKIP={'cob_none','cob_leaf','cob_leaf1','cob_conc_leaf','cob_wire','cob_wire1','cob_Wire1'}   # bullets pass through these
F_PLAYER,F_BULLET,F_CLIMB=1,2,4
def named_meshes(path):
    h=load(path);d=h['d'];B=h['blocks'];names=[]
    def walk(k,parent):
        t,o,s=B[k]
        if t=='NiNode':
            a=node(d,o)
            for c in a['children']: walk(c,S(h,a['name']))
        elif t=='NiMesh': names.append(parent)
    walk(0,'');return names
Ps=[];Is=[];Ms=[];off=0;skipped={}
for fn in ['world_colp.nif','world_colb.nif']:
    meshes=export_col.run(MAPDIR+'/'+fn);names=named_meshes(MAPDIR+'/'+fn)
    for (n,p,i),nm in zip(meshes,names):
        if fn=='world_colp.nif': fl=F_PLAYER|F_CLIMB if nm=='coc_jumper' else F_PLAYER|F_BULLET
        else:
            if nm not in MAT or nm in SKIP: skipped[nm]=skipped.get(nm,0)+len(i)//3;continue
            fl=F_BULLET
        I3=i.reshape(-1,3);T3=p[I3];keep=np.linalg.norm(np.cross(T3[:,1]-T3[:,0],T3[:,2]-T3[:,0]),axis=1)>1e-3   # drop degenerate triangles
        Ps.append(p);Is.append(I3[keep]+off);Ms.append(np.full(int(keep.sum()),(MAT.get(nm,0)&15)|(fl<<4),'u1'));off+=len(p)
P=np.concatenate(Ps).astype('<f4');I=np.concatenate(Is).astype('<u4');M=np.concatenate(Ms)
from ssf_scan import table,scan
# floor test against the player collision: the highest walkable triangle within 160 cm below (or 40 cm above) the point
PL=((M>>4)&F_PLAYER)>0
TA,TB,TC=P[I[PL,0]],P[I[PL,1]],P[I[PL,2]];NN=np.cross(TB-TA,TC-TA);NZ=NN[:,2]/(np.linalg.norm(NN,axis=1)+1e-9)
CELLS={}
for k,(a,b,c) in enumerate(zip(TA,TB,TC)):
    if abs(NZ[k])<0.64: continue
    x0,x1=int(min(a[0],b[0],c[0])//500),int(max(a[0],b[0],c[0])//500);y0,y1=int(min(a[1],b[1],c[1])//500),int(max(a[1],b[1],c[1])//500)
    if (x1-x0+1)*(y1-y0+1)>400: continue
    for gx in range(x0,x1+1):
        for gy in range(y0,y1+1): CELLS.setdefault((gx,gy),[]).append(k)
def floorz(x,y,z):
    best=None
    for k in CELLS.get((int(x//500),int(y//500)),[]):
        a,b,c=TA[k],TB[k],TC[k];d=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
        if abs(d)<1e-6: continue
        u=((x-a[0])*(c[1]-a[1])-(y-a[1])*(c[0]-a[0]))/d;v=((b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]))/d
        if u<0 or v<0 or u+v>1: continue
        zz=a[2]+u*(b[2]-a[2])+v*(c[2]-a[2])
        if -40<z-zz<160 and (best is None or zz>best): best=zz
    return best
def onfloor(x,y,z): return floorz(x,y,z) is not None
groups=[];src='none'
if os.path.exists(MAPDIR+'/spawnsystem.ssf'):
    d=open(MAPDIR+'/spawnsystem.ssf','rb').read();groups=table(d);src='table'
    # some tables hold more slots than real points (zeros, stray values): keep only points standing on the collision floor
    groups=[(t,[s for s in sl if s[0]!=0 and s[1]!=0 and onfloor(s[0],s[1],s[2])]) for t,sl in groups];groups=[g for g in groups if g[1]]
    if not groups: groups=scan(d,onfloor);src='scan'
sp=[(gi,t,s) for gi,(t,sl) in enumerate(groups) for s in sl]
sp=[(gi,t,(s[0],s[1],floorz(s[0],s[1],s[2])+2)+tuple(s[3:])) for gi,t,s in sp]   # stand each point on its floor (player.h also moves points out of walls at load)
# ---------- nav mesh (the game's own NaviMesh.Nmf; maps without one get a walkable grid mesh from tools/navgen.c)
from navparse import load_nmf
navf=MAPDIR+'/NaviMesh.Nmf' if os.path.exists(MAPDIR+'/NaviMesh.Nmf') else None
T=load_nmf(navf) if navf else []
TD=load_nmf(MAPDIR+'/DroneNaviMesh.nmf') if not T and os.path.exists(MAPDIR+'/DroneNaviMesh.nmf') else []
if not sp and (T or TD):   # no spawn points found: a few walkable spots (largest nav triangles, dropped onto the floor)
    c=sorted(T or TD,key=lambda t:-np.cross(t['V'][1]-t['V'][0],t['V'][2]-t['V'][0])[2])
    for t in c:
        x,y,z=t['cen'];zs=[zz for zz in np.arange(z-400,z+50,10) if onfloor(x,y,zz)]
        if zs: sp.append((0,2008,(x,y,floorz(x,y,min(zs))+2,0,0,0)))
        if len(sp)>=8: break
with open(OUTD+'/%s.gcol'%NAME,'wb') as f:
    f.write(b'GCOL'+struct.pack('<III',3,len(P),len(I)));f.write(P.tobytes());f.write(I.tobytes())
    f.write(struct.pack('<I',len(sp)))
    # SpawnSystem.ssf yaw faces along (-cos, -sin) (checked: Demolition attackers face their objective, TDM spawns face
    # into the map only after adding pi); the engine's forward is (cos yaw, sin yaw), so the .gcol stores yaw + pi
    for gi,t,s in sp: f.write(struct.pack('<ii4f',gi,t,s[0],s[1],s[2],(s[5]+np.pi+np.pi)%(2*np.pi)-np.pi))
    f.write(M.tobytes())
FL=M>>4
print(NAME,'gcol v3: tris',len(I),'(player %d, climbable %d, bullet %d)'%((FL&F_PLAYER>0).sum(),(FL&F_CLIMB>0).sum(),(FL&F_BULLET>0).sum()),'spawns',len(sp),'groups',len(groups),'from',src if groups else 'nav fallback','skipped',{k:v for k,v in skipped.items() if v>0 and k not in SKIP})
# the nav tools run the engine's own player physics (engine/src/player.h + nav.h) on the host
TOOLS=os.path.dirname(os.path.abspath(__file__));SRC=os.path.join(TOOLS,'..','engine','src')
def tool(name):
    exe=os.environ.get('GITSFA_EXE_'+name.upper()) or gitspaths.tmp('gits_%s'%name)   # (databuilder: a prebuilt tool on Windows)
    if os.environ.get('GITSFA_EXE_'+name.upper()): return exe
    if not os.path.exists(exe) or any(os.path.getmtime(x)>os.path.getmtime(exe) for x in (os.path.join(TOOLS,name+'.c'),os.path.join(SRC,'player.h'),os.path.join(SRC,'nav.h'))):
        tmp=exe+'.%d'%os.getpid()   # databuilder: built under its own name, then renamed (map steps run side by side)
        subprocess.run(['gcc','-O2','-w','-I',SRC,'-o',tmp,os.path.join(TOOLS,name+'.c'),'-lm'],check=True); os.replace(tmp,exe)
    return exe
if T:
    idx={t['id']:i for i,t in enumerate(T)}
    with open(OUTD+'/%s.gnav'%NAME,'wb') as f:
        f.write(b'GNAV'+struct.pack('<I',len(T)))
        for t in T:
            V=t['V'];nb=[-1,-1,-1]
            for n,p in t['nb']:
                if n<=0 or n not in idx: continue
                best=None
                for k in range(3):
                    a,b=V[k],V[(k+1)%3];e=min(np.abs(a-p[0]).max()+np.abs(b-p[1]).max(),np.abs(a-p[1]).max()+np.abs(b-p[0]).max())
                    if best is None or e<best[0]: best=(e,k)
                nb[best[1]]=idx[n]
            f.write(V.astype('<f4').tobytes());f.write(struct.pack('<3i',*nb))
    # which edges the player physics can actually cross (walk, jump, climb, vault): blocked-edge bits
    eb=gitspaths.tmp('edges_%s.bin'%NAME)
    r=subprocess.run([tool('navedges'),OUTD+'/%s.gcol'%NAME,OUTD+'/%s.gnav'%NAME,eb],capture_output=True,text=True)
    print(NAME,'gnav:',len(T),'tris from',os.path.basename(navf),'|',r.stdout.strip().split('\n')[-1])
    fl=open(eb,'rb').read()
    with open(OUTD+'/%s.gnav'%NAME,'ab') as f: f.write(b'FLG1'+fl)
else:   # no player-AI mesh (only the drone one): walkable grid from the player collision
    r=subprocess.run([tool('navgen'),OUTD+'/%s.gcol'%NAME,OUTD+'/%s.gnav'%NAME],capture_output=True,text=True)
    print(NAME,'gnav (navgen):',r.stdout.strip().split('\n')[-1])
