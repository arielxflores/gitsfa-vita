# Full material dump for GITS:FA world NIFs (Gamebryo 30.2): inherited properties, texture slots, extra data, streams
import sys,struct,os
sys.path.insert(0,'/root/gits/tools')
from nifmesh import load,S,node,mesh,stream,avobj
SLOTS=['base','dark','detail','gloss','glow','bump','normal','parallax','decal0','decal1','decal2','decal3']
def texprop(h,po):
    d=h['d'];B=h['blocks']
    name,ne=struct.unpack_from('<II',d,po);o=po+8+4*ne+4
    flags,count=struct.unpack_from('<HI',d,o);o+=6;out={}
    for i in range(count):
        has=d[o];o+=1
        if not has: continue
        src,mf,an,ht=struct.unpack_from('<iHHB',d,o);o+=9
        tt=None
        if ht: tt=struct.unpack_from('<5fI2f',d,o);o+=32
        nm=SLOTS[i] if i<len(SLOTS) else 'slot%d'%i
        fn=None
        if 0<=src<len(B) and B[src][0]=='NiSourceTexture': fn=S(h,struct.unpack_from('<I',d,B[src][1]+13)[0])
        out[nm]=dict(src=src,file=fn,uvset=mf&0xff,filter=(mf>>8)&15,clamp=(mf>>12)&15,tt=tt)
    ns=struct.unpack_from('<I',d,o)[0];o+=4
    for i in range(ns):
        has=d[o];o+=1
        if not has: continue
        src,mf,an,ht=struct.unpack_from('<iHHB',d,o);o+=9
        tt=None
        if ht: tt=struct.unpack_from('<5fI2f',d,o);o+=32
        mid=struct.unpack_from('<I',d,o)[0];o+=4
        fn=None
        if 0<=src<len(B) and B[src][0]=='NiSourceTexture': fn=S(h,struct.unpack_from('<I',d,B[src][1]+13)[0])
        out['shader%d'%mid]=dict(src=src,file=fn,uvset=mf&0xff,clamp=(mf>>12)&15,tt=tt)
    out['_flags']=flags
    return out
def extra(h,k):
    d=h['d'];t,o,s=h['blocks'][k];nm=S(h,struct.unpack_from('<I',d,o)[0])
    if t=='NiStringExtraData': v=S(h,struct.unpack_from('<I',d,o+4)[0])
    elif t=='NiFloatExtraData': v=struct.unpack_from('<f',d,o+4)[0]
    elif t=='NiBooleanExtraData': v=d[o+4]
    elif t=='NiIntegerExtraData': v=struct.unpack_from('<i',d,o+4)[0]
    else: v=t
    return nm,v
def prop(h,k):
    d=h['d'];t,o,s=h['blocks'][k]
    if t=='NiTexturingProperty': return t,texprop(h,o)
    ne=struct.unpack_from('<I',d,o+4)[0];q=o+8+4*ne+4
    if t=='NiAlphaProperty':
        f,th=struct.unpack_from('<HB',d,q);return t,dict(blend=f&1,src=(f>>1)&15,dst=(f>>5)&15,test=(f>>9)&1,func=(f>>10)&7,nosort=(f>>13)&1,thr=th,raw=f)
    if t in('NiZBufferProperty','NiVertexColorProperty','NiSpecularProperty','NiShadeProperty','NiWireframeProperty','NiDitherProperty'):
        f=struct.unpack_from('<H',d,q)[0]
        if t=='NiZBufferProperty': return t,dict(test=f&1,write=(f>>1)&1,func=(f>>2)&15,raw=f)
        if t=='NiVertexColorProperty': return t,dict(light=(f>>3)&1,src=(f>>4)&3,raw=f)
        return t,dict(raw=f)
    if t=='NiStencilProperty':
        f=struct.unpack_from('<H',d,q)[0];return t,dict(raw=f,draw=(f>>10)&3,bytes=d[q:o+s].hex())
    if t=='NiMaterialProperty':
        v=struct.unpack_from('<14f',d,q) if s-(q-o)>=56 else None
        return t,dict(raw=d[q:o+s].hex(),v=v)
    return t,dict(raw=d[q:o+s].hex())
def walk_all(path):
    h=load(path);d=h['d'];B=h['blocks'];out=[]
    def walk(k,props,xtra,path_):
        t,o,s=B[k]
        if t=='NiNode': a=node(d,o)
        elif t=='NiMesh': a=mesh(d,o)
        else: return
        P=dict(props)
        for p in a['props']:
            if 0<=p<len(B):
                pt,pv=prop(h,p);P[pt]=(p,pv)
        X=dict(xtra)
        for e in a['extra']:
            if 0<=e<len(B): nm,v=extra(h,e);X[nm]=v
        nm=S(h,a['name'])
        if t=='NiNode':
            for c in a['children']: walk(c,P,{},path_+'/'+str(nm))
            return
        ownx={}
        for e in a['extra']:
            if 0<=e<len(B): n_,v=extra(h,e);ownx[n_]=v
        streams=[]
        for st in a['streams']:
            ds=stream(d,B[st['ref']][1]);streams.append((S(h,st['comps'][0][0]),st['comps'][0][1],hex(ds['fmt'][0]),len(ds['data'])))
        out.append(dict(k=k,name=nm,path=path_,props=P,extra=ownx,streams=streams,own=[B[p][0] for p in a['props'] if 0<=p<len(B)]))
    walk(0,{},{},'')
    return h,out
if __name__=='__main__':
    h,ms=walk_all(sys.argv[1])
    for i,m in enumerate(ms):
        if len(sys.argv)>2 and sys.argv[2] not in str(m['name']): continue
        print(i,m['k'],m['name'],m['path'][-60:])
        for pt,(pk,pv) in m['props'].items():
            if pt=='NiTexturingProperty':
                print('   TEX',{k:(v['file'],v['uvset'],v['clamp'],v['tt']) if isinstance(v,dict) else v for k,v in pv.items()})
            else: print('  ',pt,pk,pv)
        print('   X',m['extra']);print('   S',m['streams'])
