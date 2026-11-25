import struct,numpy as np
def load_nmf(path):
    d=open(path,'rb').read()
    ver,a,b,ntri=struct.unpack_from('<4I',d,0)
    T=[];o=16
    for i in range(ntri):
        tid=struct.unpack_from('<I',d,o+4)[0]
        V=np.array(struct.unpack_from('<9f',d,o+8)).reshape(3,3)
        vi=struct.unpack_from('<4H',d,o+44)
        cen=np.array(struct.unpack_from('<3f',d,o+52))
        flag=struct.unpack_from('<3I',d,o+64)
        nb=[]
        for e in range(3):
            q=o+76+28*e;p=np.array(struct.unpack_from('<6f',d,q)).reshape(2,3);n=struct.unpack_from('<i',d,q+24)[0]
            nb.append((n,p))
        T.append(dict(id=tid,V=V,vi=vi,cen=cen,flag=flag,nb=nb));o+=160
    return T
if __name__=='__main__':
    import sys;T=load_nmf(sys.argv[1])
    print(len(T),'tris; flags',set(t['flag'] for t in T),'vi[3] values',sorted(set(t['vi'][3] for t in T))[:20])
    nn=[sum(1 for n,p in t['nb'] if n>0) for t in T];print('neighbor counts',np.bincount(nn))
    # check neighbor symmetry
    ids={t['id']:t for t in T};asym=0
    for t in T:
        for n,p in t['nb']:
            if n>0 and not any(m==t['id'] for m,_ in ids[n]['nb']): asym+=1
    print('asym links',asym)
    allV=np.concatenate([t['V'] for t in T]);print('bounds',allV.min(0),allV.max(0))
