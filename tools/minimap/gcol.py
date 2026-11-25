import numpy as np, struct
def load(path):
    d=open(path,'rb').read()
    mg=d[:4]; ver,nv,nt=struct.unpack_from('<III',d,4); o=16
    V=np.frombuffer(d,'<f4',nv*3,o).reshape(-1,3); o+=nv*12
    I=np.frombuffer(d,'<u4',nt*3,o).reshape(-1,3); o+=nt*12
    ns=struct.unpack_from('<I',d,o)[0]; o+=4
    S=np.frombuffer(d,dtype=[('gi','<i4'),('type','<i4'),('x','<f4'),('y','<f4'),('z','<f4'),('yaw','<f4')],count=ns,offset=o); o+=ns*24
    M=np.frombuffer(d,'u1',nt,o) if ver>=2 else np.zeros(nt,'u1')
    return V,I,S,M
