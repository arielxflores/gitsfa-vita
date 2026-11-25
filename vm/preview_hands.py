# the hands model with the bomb device as the engine draws them (engine/src/hands.h): frames of a clip side by side.
# usage: [OFS=4.5,5.4,0] python3 preview_hands.py <clip> <frames, comma separated> <fovy> <out.png> [columns]   (b99; plant: 60 deg, gesture: 49.75)
import sys,os,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import preview as pv
from PIL import Image
D=os.environ.get('GITSFA_DATA','/root/gits/build/data');G=pv.read(D+'/vm_hands.gvm');Bm=pv.read(D+'/dem_bomb_1p.gvm')
F=np.diag([-1.0,-1,1])
def merged(line=True):
    H=dict(G);H['texs']=G['texs']+Bm['texs'];n=len(G['pal'])
    S0=np.zeros((3,4));S0[:,:3]=F;S1=np.eye(4)[:3]
    H['pal']=list(G['pal'])+[(G['bones'].index('Bip01_L_Hand'),S0),(G['bones'].index(os.environ.get('LB','Bip01_R_Hand')),S0 if os.environ.get('LB') else S1)]
    ms=list(G['meshes'])
    for k,m in enumerate(Bm['meshes']):
        if k==1 and not line: continue
        v=m['v'].copy();v['bi'][:]=0;v['bw'][:]=0;v['bw'][:,0]=255
        ms.append(dict(tex=m['tex']+len(G['texs']),blend=0,base=n+k,flags=0,v=v,I=m['I']))
    H['meshes']=ms;return H
clip=sys.argv[1];frames=[int(x) for x in sys.argv[2].split(',')];fov=float(sys.argv[3]);cols=int(sys.argv[5]) if len(sys.argv)>5 else len(frames)
H=merged() if clip.startswith('install_start') else G;tiles=[]
for f in frames:
    fr=G['clips'][clip][2];f=min(f,len(fr)-1);p='/tmp/_pvhands.png'
    pv.render(H,fr[f],p,fovy=fov,offset=tuple(float(x) for x in os.environ.get('OFS','0,0,0').split(',')),W=400,H=226);tiles.append(np.asarray(Image.open(p).convert('RGB')))
while len(tiles)%cols: tiles.append(np.zeros_like(tiles[0]))
Image.fromarray(np.concatenate([np.concatenate(tiles[i:i+cols],1) for i in range(0,len(tiles),cols)],0)).save(sys.argv[4]);print('ok')
