# NiTexturingProperty: map slots with their source texture and UV set (Gamebryo 20.x layout as used by GITS:FA)
import struct
SLOTS=['base','dark','detail','gloss','glow','bump','normal','parallax','decal0']
def texprop(d,po):
    o=po+12; flags,count=struct.unpack_from('<HI',d,o); o+=6; out={}
    for i in range(min(count,9)):
        has=d[o]; o+=1
        if not has: continue
        src,mf,an,ht=struct.unpack_from('<IHHB',d,o); o+=9
        if ht: o+=32
        name=SLOTS[i]
        if name=='bump': o+=4+4+16
        if name=='parallax': o+=4
        out[name]=dict(src=src,uvset=mf&0xff,filter=(mf>>8)&15,clamp=(mf>>12)&15,at=po)
    return out
