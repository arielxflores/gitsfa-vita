import struct,numpy as np
def spawns(path):
    d=open(path,'rb').read();mg,ver,nv,ni=struct.unpack_from('<4sIII',d,0);o=16+nv*12+ni*12
    n=struct.unpack_from('<I',d,o)[0];o+=4;out=[]
    for i in range(n):
        gi,t,x,y,z,yaw=struct.unpack_from('<ii4f',d,o);o+=24;out.append((gi,t,x,y,z,yaw))
    return out
def tris(path):
    d=open(path,'rb').read();mg,ver,nv,ni=struct.unpack_from('<4sIII',d,0)
    P=np.frombuffer(d,'<f4',nv*3,16).reshape(-1,3);I=np.frombuffer(d,'<u4',ni*3,16+nv*12).reshape(-1,3);return P,I
