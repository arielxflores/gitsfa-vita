import sys,pickle;sys.path.insert(0,'/root/gits/vm')
import preview as p, numpy as np
from gvmlib import qmat, mat2q
from PIL import Image
D=pickle.load(open('/tmp/claude-0/hv/frames.pkl','rb'));S=D['skill'];RN=S['RN']
RC=np.array([[-1.0,0,0],[0,0,1],[0,1,0]]);CAM=np.array([0,0,156.93])
G=p.read('/tmp/claude-0/hv/out/vm_hands.gvm');B=G['bones'];idx=[RN.index(b) for b in B]
def axis_rot(u,th):
    u=u/np.linalg.norm(u);K=np.array([[0,-u[2],u[1]],[u[2],0,-u[0]],[-u[1],u[0],0]]);return np.eye(3)+np.sin(th)*K+(1-np.cos(th))*K@K
def twist_of(Rrel):   # twist angle of a rotation about local x
    q=mat2q(Rrel);return 2*np.arctan2(q[0],q[3])
def frame(f,frac):
    W=[qmat(S['W'][f][b]) for b in range(len(RN))];P=S['P'][f]
    if frac is not None:
        for s in 'LR':
            iT=RN.index('Bip01_%s_ForeTwist'%s);iH=RN.index('Bip01_%s_Hand'%s);iF=RN.index('Bip01_%s_Forearm'%s)
            rel=W[iF].T@W[iH];a=twist_of(rel);a=(a+np.pi)%(2*np.pi)-np.pi
            W[iT]=W[iF]@axis_rot(np.array([1.,0,0]),a*frac)
    fr=np.zeros((len(B),7),'f4')
    for s,b in enumerate(idx): fr[s,:4]=mat2q(RC@W[b]);fr[s,4:]=RC@(P[b]-CAM)
    return fr
if __name__=='__main__':
    f=int(sys.argv[1]);fracs=[None,0.0,0.5,0.75,1.0];ims=[]
    for fc in fracs:
        p.render(G,frame(f,fc),'/tmp/claude-0/hv/_r.png',fovy=49.75,W=960,H=540,offset=(-1.2,2.3,0));ims.append(Image.open('/tmp/claude-0/hv/_r.png').convert('RGB').crop((400,220,960,540)))
    ref=Image.open(sys.argv[2]).convert('RGB').resize((960,540)).crop((400,220,960,540))
    c=Image.new('RGB',(1120,960))
    for i,x in enumerate([ref]+ims): c.paste(x,(i%2*560,i//2*320))
    c.save(sys.argv[3],quality=92)
    for s in 'LR':
        iT=RN.index('Bip01_%s_ForeTwist'%s);iH=RN.index('Bip01_%s_Hand'%s);iF=RN.index('Bip01_%s_Forearm'%s);iU=RN.index('Bip01_%s_UpperArm'%s)
        W=[qmat(S['W'][f][b]) for b in range(len(RN))]
        print(s,'twist hand vs forearm %.0f deg; foretwist vs forearm: %s; foretwist vs upperarm %s'%(np.degrees(twist_of(W[iF].T@W[iH])),np.round(mat2q(W[iF].T@W[iT]),2),np.round(mat2q(W[iU].T@W[iT]),2)))
