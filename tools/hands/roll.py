import sys,pickle;sys.path.insert(0,'/root/gits/vm')
import preview as p, numpy as np
from gvmlib import qmat, mat2q
from PIL import Image,ImageDraw
D=pickle.load(open('/tmp/claude-0/hv/frames.pkl','rb'));S=D['skill'];RN=S['RN']
RC=np.array([[-1.0,0,0],[0,0,1],[0,1,0]]);CAM=np.array([0,0,156.93])
G=p.read('/tmp/claude-0/hv/out/vm_hands.gvm');B=G['bones'];idx=[RN.index(b) for b in B]
def axis_rot(u,th):
    u=u/np.linalg.norm(u);K=np.array([[0,-u[2],u[1]],[u[2],0,-u[0]],[-u[1],u[0],0]]);return np.eye(3)+np.sin(th)*K+(1-np.cos(th))*K@K
def frame(f,roll,sh=(0,0,0)):
    W=[qmat(S['W'][f][b]) for b in range(len(RN))];P=[x.copy() for x in S['P'][f]]
    iF=RN.index('Bip01_R_Forearm');W[iF]=W[iF]@axis_rot(np.array([1.,0,0]),np.radians(roll));P[iF]=P[iF]+W[iF]@np.array(sh,float)
    fr=np.zeros((len(B),7),'f4')
    for s,b in enumerate(idx): fr[s,:4]=mat2q(RC@W[b]);fr[s,4:]=RC@(P[b]-CAM)
    return fr
f=int(sys.argv[1]);rolls=[float(x) for x in sys.argv[4].split(',')];sh=[float(x) for x in sys.argv[5].split(',')] if len(sys.argv)>5 else (0,0,0)
ref=Image.open(sys.argv[2]).convert('RGB').resize((960,540)).crop((400,220,960,540));ims=[ref]
for r in rolls:
    p.render(G,frame(f,r,sh),'/tmp/claude-0/hv/_r.png',fovy=49.75,W=960,H=540,offset=(-1.2,2.3,0));im=Image.open('/tmp/claude-0/hv/_r.png').convert('RGB').crop((400,220,960,540)).point(lambda v:int(255*(v/255)**0.5));ImageDraw.Draw(im).text((8,8),'roll %+.0f'%r,fill=(255,255,0));ims.append(im)
c=Image.new('RGB',(1120,320*((len(ims)+1)//2)))
for i,x in enumerate(ims): c.paste(x,(i%2*560,i//2*320))
c.save(sys.argv[3],quality=92)
