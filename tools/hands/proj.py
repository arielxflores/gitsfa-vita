import sys,pickle;sys.path.insert(0,'/root/gits/vm')
import numpy as np
from gvmlib import qmat, mat2q
D=pickle.load(open('/tmp/claude-0/hv/frames.pkl','rb'));S=D['skill'];RN=S['RN']
RC=np.array([[-1.0,0,0],[0,0,1],[0,1,0]]);CAM=np.array([0,0,156.93])
def rx(d): c,s=np.cos(np.radians(d)),np.sin(np.radians(d));return np.array([[1,0,0],[0,c,-s],[0,s,c]])
def P2(X,fov,off):
    X=X+np.array(off);z=-X[2];ft=1/np.tan(np.radians(fov/2));return np.array([(X[0]/z*ft/(960/540)*0.5+0.5)*960,(0.5-X[1]/z*ft*0.5)*540])
def bone(f,n): i=RN.index(n);return RC@qmat(S['W'][f][i]),RC@(S['P'][f][i]-CAM)
if __name__=='__main__':
    fov=56.7;off=(-2.3,1.4,0)
    for f in (20,24,28):
        R,t=bone(f,'Bip01_R_Forearm');Rh,th=bone(f,'Bip01_R_Hand')
        for deg in (49,55):
            G=rx(deg);gt=np.array([0,-0.954,0])
            dev=lambda p:R@(G@np.array(p)+gt)+t
            print(f,deg,'ring-y',P2(dev((22.8,-2.02,-7.24)),fov,off).round(0),'ring+y',P2(dev((22.8,2.02,-7.24)),fov,off).round(0),'holo(dev space)',P2(dev((19.4,-6.2,0.5)),fov,off).round(0),'glowring(dev)',P2(dev((19.25,-5.41,0.25)),fov,off).round(0),'holo(forearm space)',P2(R@np.array((19.4,-6.2,0.5))+t,fov,off).round(0),'wrist',P2(th,fov,off).round(0))
