# Terminal Conquest data in a map's SpawnSystem.ssf (format 0x01337af5, ver 18; maps with TTM data: Underground,
# Berutalube, RuinedTokyo). Layout found by inspection (tools/tank/TTM_NOTES.md):
#   0x1c4c  int nterm, int start (id of the terminal that is active first)
#   then per terminal:  int id (3, 6, 9 ...)  char name[16] ("terminal&B")  3 x rec  int link[2]  42 x rec
#   rec = (x, y, z, rx, ry, rz) float32 (cm, radians); unused records all-zero
#   the 3 recs: [0] terminal device, [1] [2] the two Think Tank call-in / side points (see notes)
#   link[2]: ids of the neighbouring terminals (0 = none) -> the chain A - B - C (- D - E)
#   42 recs = 21 spawns of list 0 (west / team A side) + 21 of list 1, used while this terminal is the active one
import struct,sys
def load(path):
    d=open(path,'rb').read(); o=0x1c4c
    n,start=struct.unpack_from('<2i',d,o); o+=8
    if not 0<n<=8: return None
    T=[]
    for k in range(n):
        tid=struct.unpack_from('<i',d,o)[0]; o+=4
        name=d[o:o+16].split(b'\0')[0].decode('latin1'); o+=16
        pts=[struct.unpack_from('<6f',d,o+24*j) for j in range(3)]; o+=72
        link=struct.unpack_from('<2i',d,o); o+=8
        sp=[struct.unpack_from('<6f',d,o+24*j) for j in range(42)]; o+=42*24
        T.append(dict(id=tid,name=name,pts=pts,link=link,sp=[[r for r in sp[:21] if any(abs(v)>1e-6 for v in r)],[r for r in sp[21:] if any(abs(v)>1e-6 for v in r)]]))
    return dict(start=start,term=T)
if __name__=='__main__':
    r=load(sys.argv[1]); print('start',r['start'])
    for t in r['term']:
        print(t['id'],t['name'],'links',t['link'])
        for p in t['pts']: print('   pt',[round(v,1) for v in p[:3]],round(p[5],3))
        for s in (0,1):
            xs=[r[0] for r in t['sp'][s]];ys=[r[1] for r in t['sp'][s]]
            print('   spawns',s,len(t['sp'][s]),'x %.0f..%.0f y %.0f..%.0f'%(min(xs),max(xs),min(ys),max(ys)) if xs else '')
