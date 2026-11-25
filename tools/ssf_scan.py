# Spawn points from SpawnSystem.ssf. The fixed group table at 0x40c is used when present (team deathmatch maps);
# other modes keep their points in typed lists further on, so we scan for runs of (x,y,z,0,0,yaw) records that sit on the
# nav mesh / collision floor.
import struct,numpy as np
def table(d):
    groups=[]
    for k in range(32):
        o=0x40c+k*0x200
        if o+8>len(d): break
        cnt=struct.unpack_from('<i',d,o)[0];typ=struct.unpack_from('<f',d,o+4)[0]
        if not (0<cnt<=64) or typ<1000: continue
        groups.append((int(typ),[struct.unpack_from('<6f',d,o+8+24*j) for j in range(cnt)]))
    return groups
def scan(d,onfloor):
    recs={}
    for o in range(0,len(d)-24,4):
        x,y,z,a,b,yaw=struct.unpack_from('<6f',d,o)
        if not all(np.isfinite([x,y,z,a,b,yaw])): continue
        if abs(a)>1e-3 or abs(b)>1e-3 or abs(yaw)>6.3 or abs(x)>80000 or abs(y)>80000 or abs(z)>30000: continue
        if abs(x)<1 and abs(y)<1: continue
        if onfloor(x,y,z): recs[o]=(x,y,z,0,0,yaw)
    # runs of records 24 bytes apart
    runs=[];cur=[]
    for o in sorted(recs):
        if cur and o==cur[-1]+24: cur.append(o)
        else:
            if len(cur)>=2: runs.append(cur)
            cur=[o]
    if len(cur)>=2: runs.append(cur)
    return [(2008,[recs[o] for o in r]) for r in runs]
