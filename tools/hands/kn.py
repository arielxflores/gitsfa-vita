import sys,os,pickle
sys.argv=['x','knife','/tmp/claude-0/hv/outk'];sys.path.insert(0,'/root/gits/vm')
import make_vm_weapon as M, numpy as np
from gvmlib import qmat, mat2q
D=pickle.load(open('/tmp/claude-0/hv/frames.pkl','rb'));S=D['skill'];RN=S['RN']
print('M4A1 rig has:',[n for n in RN if 'Weapon' in n or 'Grip' in n])
i=RN.index('Weapon_R_forearm') if 'Weapon_R_forearm' in RN else -1;iF=RN.index('Bip01_R_Forearm')
if i>=0:
    for f in (0,10,20,30,40):
        Wf=qmat(S['W'][f][iF]);rel=Wf.T@qmat(S['W'][f][i]);t=Wf.T@(S['P'][f][i]-S['P'][f][iF]);print('skill f',f,'Weapon_R_forearm in forearm space q',np.round(mat2q(rel),3),'t',np.round(t,2))
os.makedirs('/tmp/claude-0/hv/outk',exist_ok=True);M.LASTFR={};M.build('knife')
fr,loop,RNk,cam=M.LASTFR['idle']
print('knife rig:',[n for n in RNk if 'Weapon' in n or 'Grip' in n or 'CW' in n])
iF=RNk.index('Bip01_R_Forearm')
for nm in ('Grip','Weapon_R_forearm','Weapon_R'):
    if nm in RNk:
        i=RNk.index(nm)
        for f in (0,len(fr)//2):
            P,W=fr[f];Wf=qmat(W[iF]);print('knife idle f',f,nm,'in forearm space q',np.round(mat2q(Wf.T@qmat(W[i])),3),'t',np.round(Wf.T@(P[i]-P[iF]),2))
pickle.dump(dict(P=np.array([f[0] for f in fr]),W=np.array([f[1] for f in fr]),RN=RNk),open('/tmp/claude-0/hv/knife_idle.pkl','wb'))
