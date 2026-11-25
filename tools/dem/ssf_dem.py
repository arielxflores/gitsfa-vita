# Demolition data in a map's SpawnSystem.ssf (format 0x01337af5, ver 12; same layout in every map file of 19348 bytes):
#   0x180c  attacker spawn list : 21 slots x (x,y,z, rx,ry, yaw) float32, unused slots all-zero
#   0x1a04  defender spawn list : 21 slots, same record
#   0x1bfc  2 target objects    : (x,y,z, rx,ry,rz) float32 + char name[16]  ("BOMB_<id>_A", "BOMB_<id>_B" ->
#           worldEnv/EnvBomb/<name>.nif, the target's collision mesh; A first, B second)
# Which list attacks: list 0 is the far one from both targets on every DEM map (checked), so 0 = attack, 1 = defense.
import struct,sys
def load(path):
    d=open(path,'rb').read()
    if len(d)<0x1c4c: return None
    lists=[]
    for base in (0x180c,0x1a04):
        L=[struct.unpack_from('<6f',d,base+24*j) for j in range(21)]
        lists.append([r for r in L if any(abs(v)>1e-6 for v in r)])
    objs=[]
    for j in range(2):
        o=0x1bfc+40*j;v=struct.unpack_from('<6f',d,o);n=d[o+24:o+40].split(b'\0')[0].decode()
        if n: objs.append((n,v))
    return dict(attack=lists[0],defense=lists[1],targets=objs)
if __name__=='__main__':
    r=load(sys.argv[1])
    for k in ('attack','defense'): print(k,len(r[k]),[tuple(round(x,1) for x in s) for s in r[k]])
    for n,v in r['targets']: print(n,[round(x,2) for x in v])
