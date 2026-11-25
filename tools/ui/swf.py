# Minimal SWF / Scaleform GFX reader (tags, bitmaps, shapes, sprites) for porting the original HUD art and layout.
import zlib,struct
class Bits:
    def __init__(s,d,o=0): s.d=d; s.p=o*8
    def ub(s,n):
        v=0
        for _ in range(n):
            v=(v<<1)|((s.d[s.p>>3]>>(7-(s.p&7)))&1); s.p+=1
        return v
    def sb(s,n):
        if n==0: return 0
        v=s.ub(n); return v-(1<<n) if v&(1<<(n-1)) else v
    def fb(s,n): return s.sb(n)/65536.0
    def align(s): s.p=(s.p+7)&~7
    def byte(s): s.align(); return s.p>>3
def rect(b):
    n=b.ub(5); return [b.sb(n) for _ in range(4)]  # xmin xmax ymin ymax (twips)
def matrix(b):
    b.align(); sx=sy=1.0; r0=r1=0.0
    if b.ub(1): n=b.ub(5); sx=b.fb(n); sy=b.fb(n)
    if b.ub(1): n=b.ub(5); r0=b.fb(n); r1=b.fb(n)
    n=b.ub(5); tx=b.sb(n); ty=b.sb(n)
    return (sx,r0,r1,sy,tx/20.0,ty/20.0)   # a b c d tx ty (pixels)
def cxform(b,alpha):
    b.align(); ha=b.ub(1); hm=b.ub(1); n=b.ub(4); k=4 if alpha else 3
    mul=[b.sb(n)/256.0 for _ in range(k)] if hm else [1.0]*k
    add=[b.sb(n) for _ in range(k)] if ha else [0]*k
    if not alpha: mul.append(1.0); add.append(0)
    return (mul,add)
def load(path):
    raw=open(path,'rb').read(); sig=raw[:3]; ver=raw[3]
    body=zlib.decompress(raw[8:]) if sig in (b'CWS',b'CFX') else raw[8:]
    b=Bits(body); fr=rect(b); o=b.byte(); rate,count=struct.unpack_from('<HH',body,o); o+=4
    tags=[]
    while o<len(body):
        cl=struct.unpack_from('<H',body,o)[0]; o+=2; code=cl>>6; ln=cl&63
        if ln==63: ln=struct.unpack_from('<I',body,o)[0]; o+=4
        tags.append((code,body[o:o+ln])); o+=ln
        if code==0: break
    return dict(sig=sig,ver=ver,frame=[v/20 for v in fr],rate=rate/256,count=count,tags=tags)
def cstr(d,o):
    e=d.index(b'\0',o); return d[o:e].decode('latin1'),e+1
TAGN={0:'End',1:'ShowFrame',2:'DefineShape',4:'PlaceObject',5:'RemoveObject',6:'DefineBits',9:'SetBackgroundColor',10:'DefineFont',11:'DefineText',12:'DoAction',
 20:'DefineBitsLossless',21:'DefineBitsJPEG2',22:'DefineShape2',26:'PlaceObject2',28:'RemoveObject2',32:'DefineShape3',33:'DefineText2',35:'DefineBitsJPEG3',36:'DefineBitsLossless2',
 37:'DefineEditText',39:'DefineSprite',43:'FrameLabel',48:'DefineFont2',56:'ExportAssets',57:'ImportAssets',59:'DoInitAction',62:'DefineFontInfo2',69:'FileAttributes',70:'PlaceObject3',
 73:'DefineFontAlignZones',74:'CSMTextSettings',75:'DefineFont3',76:'SymbolClass',77:'Metadata',78:'DefineScalingGrid',82:'DoABC',83:'DefineShape4',84:'DefineMorphShape2',86:'DefineSceneAndFrameLabelData',88:'DefineFontName',
 1000:'GFX_ExporterInfo',1001:'GFX_FontTextureInfo',1002:'GFX_DefineExternalGradient',1003:'GFX_DefineGradientMap',1004:'GFX_DefineCompactedFont',1005:'GFX_DefineExternalSound',1006:'GFX_DefineExternalStreamSound',1007:'GFX_DefineSubImage',1008:'GFX_DefineExternalImage2',1009:'GFX_DefineExternalImage'}

def place(code,d):
    """PlaceObject2/3 -> dict"""
    b=Bits(d); o=0; f=d[0]; f2=0; o=1
    if code==70: f2=d[1]; o=2
    depth=struct.unpack_from('<H',d,o)[0]; o+=2; r=dict(depth=depth,move=bool(f&1))
    if code==70 and (f2&8 or (f2&16 and f&2)):
        cls,o=cstr(d,o); r['cls']=cls
    if f&2: r['id']=struct.unpack_from('<H',d,o)[0]; o+=2
    b=Bits(d,o)
    if f&4: r['m']=matrix(b)
    if f&8: r['cx']=cxform(b,True)
    o=b.byte()
    if f&16: r['ratio']=struct.unpack_from('<H',d,o)[0]; o+=2
    if f&32: r['name'],o=cstr(d,o)
    if f&64: r['clip']=struct.unpack_from('<H',d,o)[0]; o+=2
    if code==70:
        if f2&1: o=skip_filters(d,o,r)
        if f2&2: r['blend']=d[o]; o+=1
        if f2&4: o+=1
        if f2&32: r['visible']=d[o]; o+=1
    return r
def skip_filters(d,o,r):
    n=d[o]; o+=1; fl=[]
    SZ={0:23,1:9,2:27,3:23,5:None,6:80,7:None}
    for _ in range(n):
        t=d[o]; o+=1; fl.append(t)
        if t==4 or t==7:   # gradient glow / bevel
            nc=d[o]; o+=1+nc*5+19
        elif t==5:   # convolution
            mx,my=d[o],d[o+1]; o+=2+8+4*mx*my+4+1
        else: o+=SZ[t]
    r['filters']=fl; return o
def sprite(d):
    sid,nf=struct.unpack_from('<HH',d,0); o=4; tags=[]
    while o<len(d):
        cl=struct.unpack_from('<H',d,o)[0]; o+=2; code=cl>>6; ln=cl&63
        if ln==63: ln=struct.unpack_from('<I',d,o)[0]; o+=4
        tags.append((code,d[o:o+ln])); o+=ln
        if code==0: break
    return sid,nf,tags
def symbols(S):
    out={}
    for code,d in S['tags']:
        if code==76:
            n=struct.unpack_from('<H',d,0)[0]; o=2
            for _ in range(n):
                cid=struct.unpack_from('<H',d,o)[0]; o+=2; name,o=cstr(d,o); out[cid]=name
    return out
def frames(tags):
    """timeline -> list of display lists (depth -> placement dict) per frame, and frame labels"""
    disp={}; out=[]; labels={}
    for code,d in tags:
        if code in (26,70):
            p=place(code,d)
            if p['move'] and p['depth'] in disp:
                q=dict(disp[p['depth']]); q.update({k:v for k,v in p.items() if k!='move'})
                if 'id' in p and not p['move']: q=p
                disp[p['depth']]=q
            else: disp[p['depth']]=p
        elif code==28: disp.pop(struct.unpack_from('<H',d,0)[0],None)
        elif code==5: disp.pop(struct.unpack_from('<H',d,2)[0],None)
        elif code==43: labels[len(out)]=cstr(d,0)[0]
        elif code==1: out.append({k:dict(v) for k,v in disp.items()})
    return out,labels

# ---------------------------------------------------------------- characters
import numpy as np
def rgb(d,o,alpha):
    if alpha: return list(d[o:o+4]),o+4
    return list(d[o:o+3])+[255],o+3
def grad_rec(b,d,shape_ver,focal):
    b.align(); o=b.p>>3
    hdr=d[o]; o+=1; n=hdr&15; recs=[]
    for _ in range(n):
        r=d[o]; o+=1; c,o=rgb(d,o,shape_ver>=3); recs.append((r,c))
    f=0.0
    if focal: f=struct.unpack_from('<h',d,o)[0]/256.0; o+=2
    b.p=o*8; return dict(spread=hdr>>6,interp=(hdr>>4)&3,recs=recs,focal=f)
def fillstyles(b,d,ver):
    b.align(); o=b.p>>3; n=d[o]; o+=1
    if n==255 and ver>=2: n=struct.unpack_from('<H',d,o)[0]; o+=2
    out=[]
    for _ in range(n):
        t=d[o]; o+=1
        if t==0: c,o=rgb(d,o,ver>=3); out.append(dict(t='solid',c=c))
        elif t in (0x10,0x12,0x13):
            b.p=o*8; m=matrix(b); g=grad_rec(b,d,ver,t==0x13); o=b.p>>3
            out.append(dict(t='lin' if t==0x10 else 'rad',m=m,g=g))
        elif t in (0x40,0x41,0x42,0x43):
            bid=struct.unpack_from('<H',d,o)[0]; o+=2; b.p=o*8; m=matrix(b); o=b.byte()
            out.append(dict(t='bmp',id=bid,m=m,repeat=t in (0x40,0x42),smooth=t in (0x40,0x41)))
        else: raise ValueError('fill %x'%t)
    b.p=o*8; return out
def linestyles(b,d,ver):
    b.align(); o=b.p>>3; n=d[o]; o+=1
    if n==255: n=struct.unpack_from('<H',d,o)[0]; o+=2
    out=[]
    for _ in range(n):
        w=struct.unpack_from('<H',d,o)[0]; o+=2
        if ver==4:
            f1,f2=d[o],d[o+1]; o+=2; join=(f1>>4)&3
            if join==2: o+=2
            hasfill=f1&8
            if hasfill:
                b.p=o*8; fs=fillstyles_one(b,d,ver); o=b.p>>3; c=fs.get('c',[255,255,255,255])
            else: c,o=rgb(d,o,True)
        else: c,o=rgb(d,o,ver>=3)
        out.append(dict(w=w/20.0,c=c))
    b.p=o*8; return out
def fillstyles_one(b,d,ver):
    o=b.p>>3; t=d[o]; o+=1
    if t==0: c,o=rgb(d,o,True); b.p=o*8; return dict(t='solid',c=c)
    if t in (0x10,0x12,0x13): b.p=o*8; m=matrix(b); g=grad_rec(b,d,ver,t==0x13); return dict(t='lin',m=m,g=g,c=g['recs'][0][1])
    bid=struct.unpack_from('<H',d,o)[0]; o+=2; b.p=o*8; m=matrix(b); b.align(); return dict(t='bmp',id=bid,m=m)
def shape_records(b,d,ver,fills,lines,nfb,nlb,flatten=8):
    """-> list of edges (x0,y0,x1,y1,fill0_id,fill1_id,line_id) in pixels; style ids index into style lists (list of lists)"""
    fill_sets=[fills]; line_sets=[lines]; edges=[]; x=y=0.0; f0=f1=ln=0; fs=0
    while True:
        if b.ub(1)==0:
            fl=b.ub(5)
            if fl==0: break
            if fl&1: n=b.ub(5); x=b.sb(n)/20.0; y=b.sb(n)/20.0
            if fl&2: f0=b.ub(nfb); f0=f0 and (fs,f0)
            if fl&4: f1=b.ub(nfb); f1=f1 and (fs,f1)
            if fl&8: ln=b.ub(nlb); ln=ln and (fs,ln)
            if fl&16:
                fills=fillstyles(b,d,ver); lines=linestyles(b,d,ver); fill_sets.append(fills); line_sets.append(lines); fs=len(fill_sets)-1
                nfb=b.ub(4); nlb=b.ub(4); f0=f1=ln=0
        else:
            if b.ub(1):   # straight
                n=b.ub(4)+2
                if b.ub(1): dx=b.sb(n)/20.0; dy=b.sb(n)/20.0
                elif b.ub(1): dx=0.0; dy=b.sb(n)/20.0
                else: dx=b.sb(n)/20.0; dy=0.0
                edges.append((x,y,x+dx,y+dy,f0,f1,ln)); x+=dx; y+=dy
            else:
                n=b.ub(4)+2; cx=x+b.sb(n)/20.0; cy=y+b.sb(n)/20.0; ax=cx+b.sb(n)/20.0; ay=cy+b.sb(n)/20.0
                px,py=x,y
                for k in range(1,flatten+1):
                    t=k/flatten; qx=(1-t)**2*x+2*(1-t)*t*cx+t*t*ax; qy=(1-t)**2*y+2*(1-t)*t*cy+t*t*ay
                    edges.append((px,py,qx,qy,f0,f1,ln)); px,py=qx,qy
                x,y=ax,ay
    return edges,fill_sets,line_sets
# databuilder: DefineShape4's EdgeBounds RECT is byte-aligned. The old read (False) misplaces the fill styles, so most
# DefineShape4 shapes fail to parse and are left out; the HUD/menu converters' reference outputs were made that way, so they
# keep it. The logo renderer (tools/tutorial/logos_render.py) sets it True.
SHAPE4_EDGE_RECT_ALIGNED=False
def define_shape(code,d):
    ver={2:1,22:2,32:3,83:4}[code]; sid=struct.unpack_from('<H',d,0)[0]; b=Bits(d,2); bounds=rect(b)
    if ver==4:
        if SHAPE4_EDGE_RECT_ALIGNED: b.align(); rect(b); b.align(); b.p+=8   # EdgeBounds RECT starts on a byte, then 1 flag byte
        else: rect(b); b.align(); b.p+=8
    fills=fillstyles(b,d,ver); lines=linestyles(b,d,ver); nfb=b.ub(4); nlb=b.ub(4)
    edges,fsets,lsets=shape_records(b,d,ver,fills,lines,nfb,nlb)
    return sid,dict(kind='shape',bounds=[v/20 for v in bounds],edges=edges,fills=fsets,lines=lsets)
def define_bits_lossless(code,d):
    cid,fmt,w,h=struct.unpack_from('<HBHH',d,0); o=7; alpha=code==36
    if fmt==3:
        nc=d[o]+1; o+=1; raw=zlib.decompress(d[o:]); cs=4 if alpha else 3
        pal=np.frombuffer(raw[:nc*cs],np.uint8).reshape(nc,cs); stride=(w+3)&~3
        idx=np.frombuffer(raw[nc*cs:nc*cs+stride*h],np.uint8).reshape(h,stride)[:,:w]
        img=pal[idx]
        if not alpha: img=np.concatenate([img,np.full((h,w,1),255,np.uint8)],2)
        else:   # palette alpha is premultiplied
            a=img[...,3:4].astype(np.float32); img=np.concatenate([np.where(a>0,img[...,:3]*255.0/np.maximum(a,1),0).clip(0,255).astype(np.uint8),img[...,3:4]],2)
    elif fmt==5:
        raw=np.frombuffer(zlib.decompress(d[o:]),np.uint8).reshape(h,w,4)
        a=raw[...,0:1].astype(np.float32) if alpha else np.full((h,w,1),255.0)
        c=raw[...,1:4].astype(np.float32)
        if alpha: c=np.where(a>0,c*255.0/np.maximum(a,1),0)
        img=np.concatenate([c.clip(0,255),a],2).astype(np.uint8)
    else: raise ValueError('lossless fmt %d'%fmt)
    return cid,dict(kind='bitmap',img=img)
def define_edit_text(d):
    cid=struct.unpack_from('<H',d,0)[0]; b=Bits(d,2); r=rect(b); o=b.byte()
    f1,f2=d[o],d[o+1]; o+=2; t=dict(kind='text',bounds=[v/20 for v in r])
    if f1&1:  # hasFont
        t['font'],t['size']=struct.unpack_from('<HH',d,o); o+=4; t['size']/=20.0
    elif f2&0x80: cls,o=cstr(d,o); t['size']=struct.unpack_from('<H',d,o)[0]/20.0; o+=2
    if f1&4: t['color']=list(d[o:o+4]); o+=4
    if f1&2: o+=2
    if f2&0x20: t['align'],lm,rm,ind,lead=struct.unpack_from('<BHHhh',d,o); o+=9; t['lm']=lm/20; t['rm']=rm/20
    t['var'],o=cstr(d,o)
    if f1&0x80: t['text'],o=cstr(d,o)
    t['html']=bool(f2&2); t['multiline']=bool(f1&0x20); return cid,t
def define_font3(d):
    fid,flags,lang,nl=struct.unpack_from('<HBBB',d,0); name=d[5:5+nl].decode('latin1').strip('\0'); o=5+nl
    ng=struct.unpack_from('<H',d,o)[0]; o+=2; wide=flags&8
    offs=struct.unpack_from('<%d%s'%(ng,'I' if wide else 'H'),d,o); base=o; o+=ng*(4 if wide else 2)
    ctoff=struct.unpack_from('<I' if wide else '<H',d,o)[0]
    glyphs=[]
    for i in range(ng):
        s=base+offs[i]; b=Bits(d,s); nfb=b.ub(4); nlb=b.ub(4)
        e,_,_=shape_records(b,d,3,[dict(t='solid',c=[255,255,255,255])],[],nfb,nlb,4); glyphs.append(e)
    o=base+ctoff; codes=struct.unpack_from('<%dH'%ng,d,o); o+=2*ng
    asc=desc=lead=0; adv=[1024*20]*ng
    if flags&0x80: asc,desc,lead=struct.unpack_from('<Hhh',d,o); o+=6; adv=struct.unpack_from('<%dh'%ng,d,o)
    return fid,dict(kind='font',name=name,glyphs=glyphs,codes={c:i for i,c in enumerate(codes)},adv=[a/20.0 for a in adv],asc=asc/20.0,desc=desc/20.0)
def characters(S):
    ch={}
    for code,d in S['tags']:
        try:
            if code in (2,22,32,83): k,v=define_shape(code,d); ch[k]=v
            elif code in (20,36): k,v=define_bits_lossless(code,d); ch[k]=v
            elif code==39: sid,nf,t=sprite(d); fr,lab=frames(t); ch[sid]=dict(kind='sprite',frames=fr,labels=lab,nf=nf)
            elif code==37: k,v=define_edit_text(d); ch[k]=v
            elif code==75: k,v=define_font3(d); ch[k]=v
            elif code==78: cid=struct.unpack_from('<H',d,0)[0]; ch.setdefault('grid',{})[cid]=[v/20 for v in rect(Bits(d,2))]
        except Exception as e: print('tag',code,'error',e)
    return ch
