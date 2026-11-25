# before/after contact sheets per map: rows = views, columns = b25 data (v1, no sky) | new data (v2 + sky)
import sys,math,os,numpy as np,json
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from render import Renderer
from gcol import spawns
from PIL import Image,ImageDraw
OLD='/root/mapvis/old_b25';NEW=sys.argv[1];OUT=sys.argv[2];stems=sys.argv[3:]
EXTRA={   # (x, y, eye z, yaw deg, pitch deg, label): the spots of the b25 glitch screenshots
 'pss9':[(-1886,-4786,-203,107,0,'b25 shot-15: SECTOR B signs / floor line')],
 'headquarter':[(1983,-3280,582,0,-25,'b25 shot-09: 3rd-person view, floor gap'),(2343,-3280,554,200,-20,'HQ spawn area')],
 'underground':[(1430,675,245,0,-25,'b25 shot-37 area'),(0,0,0,0,0,'decal')],
 'port':[(-25,1494,152,-90,-10,'b25 shot-05 area'),(-25,1494,152,0,20,'port sky')],
 'downtown':[],
}
def pick_spawns(sp,n=4):
    P=np.array([[s[2],s[3]] for s in sp]);sel=[0]
    while len(sel)<min(n,len(sp)):
        d=np.min([np.hypot(*(P-P[i]).T) for i in sel],axis=0);sel.append(int(d.argmax()))
    return [sp[i] for i in sel]
FXMAP={'headquarter':'Headquarter','ecenter':'Ecenter','ghost_storage':'Ghost_Storage','underground':'Underground','urban':'Urban','tutorial':'Tutorial','shooting':'Shooting'}
def fx_view(stem):
    sys.path.insert(0,'/root/gits/tools');import world_fx
    if stem not in FXMAP: return []
    ms=world_fx.effect_meshes(FXMAP[stem],verbose=False)
    if not ms: return []
    best=None
    for m,off,src in ms:
        P=m['streams'][('POSITION',0)][0];Pw=(np.c_[P,np.ones(len(P))]@m['W'].T)[:,:3]+off;ext=np.ptp(Pw,axis=0)
        a=ext[0]*ext[2]+ext[1]*ext[2]
        if best is None or a>best[0]: best=(a,Pw.mean(0),ext)
    _,c,ext=best;nrm=np.array([1.0,0,0]) if ext[0]<ext[1] else np.array([0,1.0,0])
    out=[]
    for sgn in (1,-1):
        cam=c+nrm*sgn*700;cam[2]=c[2]+40;d=c-cam;yaw=math.degrees(math.atan2(d[1],d[0]));out.append((cam[0],cam[1],cam[2],yaw,-5,'world effect (screen / sign)'))
    return out
def views(stem):
    sp=spawns('/root/gits/build/maps/%s.gcol'%stem);V=[]
    for gi,t,x,y,z,yw in pick_spawns(sp):
        V.append((x,y,z+152,math.degrees(yw),-5,'spawn'));V.append((x,y,z+152,math.degrees(yw)+180,-5,'spawn, behind'))
    gi,t,x,y,z,yw=pick_spawns(sp,2)[-1];V.append((x,y,z+152,math.degrees(yw),30,'looking up'))
    for e in EXTRA.get(stem,[]): V.append(e)
    V+=fx_view(stem)
    return V
def decal_view(R,stem):
    # camera looking down at the no-lightmap text decals (the b25 confetti)
    import gmapio
    g=gmapio.read(NEW+'/%s.gmap'%stem)
    for m in g['mesh']:
        if m['tex']>=0 and m['mode']==2 and m['mn'][2]>0 and m['mx'][2]<60 and m['ni']>300:
            c=m['V'][len(m['V'])//2,:3];return (c[0]-250,c[1]-250,c[2]+200,45,-35,'floor text decals')
    return None
for stem in stems:
    vs=views(stem);RO=Renderer();RN=Renderer()
    RO.load(stem,gmap=OLD+'/%s.gmap'%stem)
    RN.load(stem,gmap=NEW+'/%s.gmap'%stem,gsky=NEW+'/%s.gsky'%stem)
    vs=[v if v[5]!='decal' else decal_view(RN,stem) for v in vs];vs=[v for v in vs if v]
    S=Image.new('RGB',(960,272*len(vs)+20),(20,20,20));d=ImageDraw.Draw(S)
    d.text((4,4),'%s   left: b25 data (gmap v1, no sky)   right: new data (gmap v2 + sky)'%stem,fill=(255,255,0))
    for i,(x,y,z,yaw,pitch,lab) in enumerate(vs):
        for j,R in enumerate((RO,RN)):
            im=R.shot((x,y,z),math.radians(yaw),math.radians(pitch)).resize((480,272),Image.BOX)
            S.paste(im,(j*480,20+i*272))
        d.text((4,24+i*272),'%s  (%.0f %.0f %.0f) yaw %.0f pitch %.0f'%(lab,x,y,z,yaw,pitch),fill=(255,255,0))
    S.save(OUT+'/%s.jpg'%stem,quality=88);print(stem,len(vs),'views',flush=True)
