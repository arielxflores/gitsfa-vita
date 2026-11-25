import sys,os,pickle
sys.argv=['x','hands','/tmp/claude-0/hv/out'];sys.path.insert(0,'/root/gits/vm')
import make_vm_weapon as M, numpy as np
from gvmlib import qmat, mat2q
os.makedirs('/tmp/claude-0/hv/out',exist_ok=True);M.build('hands')
out={}
for name in ('skill','idle'):
    fr,loop,RN,cam=M.LASTFR[name];iH=RN.index('Bip01_Head')
    out[name]=dict(P=np.array([f[0] for f in fr]),W=np.array([f[1] for f in fr]),RN=RN,cam=cam,iH=iH)
    P=out[name]['P'];W=out[name]['W']
    R0=qmat(W[0][iH])
    for i in range(0,len(fr),4):
        Rr=qmat(W[i][iH])@R0.T;ang=np.degrees(np.arccos(np.clip((np.trace(Rr)-1)/2,-1,1)))
        print(name,i,'head pos',np.round(P[i][iH],1),'head rot vs f0 %.1f deg'%ang,'axis',np.round(np.array([Rr[2,1]-Rr[1,2],Rr[0,2]-Rr[2,0],Rr[1,0]-Rr[0,1]])/(2*np.sin(np.radians(ang))+1e-9),2))
pickle.dump(out,open('/tmp/claude-0/hv/frames.pkl','wb'));print('RC',M.RC.tolist(),'CAM',M.CAM,'HEAD',M.HEAD)
