import sys,pickle;sys.path.insert(0,'/root/gits/vm')
import preview as p, numpy as np
from gvmlib import qmat, mat2q
from PIL import Image,ImageDraw
D=pickle.load(open('/tmp/claude-0/hv/frames.pkl','rb'));S=D['skill'];RN=S['RN']
RC=np.array([[-1.0,0,0],[0,0,1],[0,1,0]]);CAM=np.array([0,0,156.93])
G=p.read('/tmp/claude-0/hv/out/vm_hands.gvm');B=G['bones'];idx=[RN.index(b) for b in B]
def axis_rot(u,th):
    u=np.array(u,float);u=u/np.linalg.norm(u);K=np.array([[0,-u[2],u[1]],[u[2],0,-u[0]],[-u[1],u[0],0]]);return np.eye(3)+np.sin(th)*K+(1-np.cos(th))*K@K
def frame(f,roll,sh=(0,0,0),yaw=0,pit=0):
    W=[qmat(S['W'][f][b]) for b in range(len(RN))];P=[x.copy() for x in S['P'][f]]
    iF=RN.index('Bip01_R_Forearm');iH=RN.index('Bip01_R_Hand')
    Rw=W[iF]@axis_rot([0,0,1],np.radians(yaw))@axis_rot([0,1,0],np.radians(pit))@axis_rot([1,0,0],np.radians(roll))
    # keep the wrist end fixed when yaw/pitch change the direction
    wrist=P[iF]+W[iF]@np.array([21.5,0,0]);W[iF]=Rw;P[iF]=wrist-Rw@np.array([21.5,0,0])+Rw@np.array(sh,float)
    fr=np.zeros((len(B),7),'f4')
    for s,b in enumerate(idx): fr[s,:4]=mat2q(RC@W[b]);fr[s,4:]=RC@(P[b]-CAM)
    return fr
def overlay(f,ref,roll=0,sh=(0,0,0),yaw=0,pit=0,off=(-1.2,2.3,0),fov=49.75):
    p.render(G,frame(f,roll,sh,yaw,pit),'/tmp/claude-0/hv/_r.png',fovy=fov,W=960,H=540,offset=off);im=Image.open('/tmp/claude-0/hv/_r.png').convert('RGB')
    a=np.asarray(im).astype(int);g=a.sum(2);ed=(np.abs(np.diff(g,axis=0,prepend=g[:1]))+np.abs(np.diff(g,axis=1,prepend=g[:,:1])))>60
    b=np.asarray(ref).copy();b[ed]=(60,255,60);return Image.fromarray(b),im
if __name__=='__main__':
    f=int(sys.argv[1]);ref=Image.open(sys.argv[2]).convert('RGB').resize((960,540));out=sys.argv[3]
    cases=[eval('dict(%s)'%c) for c in sys.argv[4:]];ims=[]
    for c in cases:
        o,_=overlay(f,ref,**c);o=o.crop((400,220,960,540));ImageDraw.Draw(o).text((8,8),str(c),fill=(255,255,0));ims.append(o)
    cv=Image.new('RGB',(1120,320*((len(ims)+1)//2)))
    for i,x in enumerate(ims): cv.paste(x,(i%2*560,i//2*320))
    cv.save(out,quality=92)
