# World effects a map places with its WorldEffectFx.wff (map.dat Fix_<map>\WorldEffectFx.wff -> effect.dat effect\<category>\<nif>):
# screens, signs, billboards, the shooting range's distance markers ... Baked into the map's .gmap as static meshes,
# posed as at t = 0 (visibility, alpha, colour, flipbook frame, texture offset and node transforms from their controllers).
#
# wff: 'f7783301', u32 count, count x 728-byte entries: nif name @0 (64 bytes), category @64 ("map" / "tutorial"),
#      position @584 (3 floats, cm)
# Left out: the Demolition bomb-site beams (DM*.nif, gio_bomb_*.nif: game-mode markers), fly.nif (no geometry),
# helicopter.nif (a flying animation; a frozen helicopter would look wrong), and the cyber-sky layers of the VR maps
# that make_sky.py already draws (radius > 2000 cm).
import sys,os,struct,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from nifmesh import load,S,node,mesh,stream
import map_visual as mv
import gitspaths
RAW=os.environ.get('GITS_MAPVIS_RAW') or gitspaths.root('MAPVIS')
SKIP=('dm.nif','dm_','gio_bomb','fly.nif','helicopter.nif')
SKIP_TEX=('succ_donut','bg_sunshine')   # 'target cleared' rings (an event effect); sun-shaft volumes (animated fades:
                                      # frozen at full strength they wash the whole room in a lavender haze)
def wff_entries(path):
    d=open(path,'rb').read();n=struct.unpack_from('<I',d,4)[0];out=[]
    for k in range(n):
        e=d[8+728*k:8+728*(k+1)]
        name=e[:64].split(b'\0')[0].decode('latin1');cat=e[64:128].split(b'\0')[0].decode('latin1')
        pos=struct.unpack_from('<3f',e,584);out.append((name,cat,pos))
    return out
# ---- controllers -> value at t = 0
def _keys_float(d,o):
    n,=struct.unpack_from('<I',d,o);o+=4
    if not n: return None,o
    kt,=struct.unpack_from('<I',d,o);o+=4;first=None
    sz={1:8,2:16,3:20,5:8}.get(kt,8)
    for i in range(n):
        t,v=struct.unpack_from('<ff',d,o)
        if first is None: first=v
        o+=sz
    return first,o
def _keys_vec3(d,o):
    n,=struct.unpack_from('<I',d,o);o+=4
    if not n: return None,o
    kt,=struct.unpack_from('<I',d,o);o+=4;first=None
    sz={1:16,2:40,3:28,5:16}.get(kt,16)
    for i in range(n):
        v=struct.unpack_from('<4f',d,o)
        if first is None: first=v[1:4]
        o+=sz
    return first,o
def _ctrl_chain(h,blk):
    d=h['d'];B=h['blocks'];t,o,s=B[blk];ne=struct.unpack_from('<I',d,o+4)[0];c=struct.unpack_from('<i',d,o+8+4*ne)[0];out=[]
    while 0<=c<len(B):
        out.append(c);c=struct.unpack_from('<i',d,B[c][1])[0]
    return out
def _interp_value(h,ip):
    """value at t=0 of a float / bool / transform interpolator"""
    d=h['d'];B=h['blocks']
    if not (0<=ip<len(B)): return None
    t,o,s=B[ip]
    if t=='NiFloatInterpolator':
        v,dr=struct.unpack_from('<fi',d,o)
        if 0<=dr<len(B) and B[dr][0]=='NiFloatData':
            fv,_=_keys_float(d,B[dr][1])
            if fv is not None: return fv
        return v if abs(v)<1e30 else None
    if t=='NiBoolInterpolator':
        v=d[o];dr=struct.unpack_from('<i',d,o+1)[0]
        if 0<=dr<len(B) and B[dr][0]=='NiBoolData':
            do=B[dr][1];n,=struct.unpack_from('<I',d,do)
            if n: return bool(d[do+12])
        return None if v>1 else bool(v)
    if t=='NiTransformInterpolator':
        tr=struct.unpack_from('<3f',d,o);q=struct.unpack_from('<4f',d,o+12);sc=struct.unpack_from('<f',d,o+28)[0];dr=struct.unpack_from('<i',d,o+32)[0]
        res={}
        if abs(tr[0])<1e30: res['t']=tr
        if abs(q[0])<1e30: res['q']=q
        if abs(sc)<1e30: res['s']=sc
        if 0<=dr<len(B) and B[dr][0]=='NiTransformData':
            do=B[dr][1];nr,=struct.unpack_from('<I',d,do);q2=do+4
            if nr:
                rt,=struct.unpack_from('<I',d,q2);q2+=4
                if rt==4:
                    eul=[]
                    for a in range(3): v,q2=_keys_float(d,q2);eul.append(v or 0.0)
                    res['eul']=eul
                else:
                    ksz={1:20,2:20,3:32,5:20}.get(rt,20)
                    res['q']=struct.unpack_from('<4f',d,q2+4);q2+=ksz*nr
            v,q2=_keys_vec3(d,q2)
            if v is not None: res['t']=v
            v,q2=_keys_float(d,q2)
            if v is not None: res['s']=v
        return res
    return None
def _quat(q):
    w,x,y,z=q;return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
def _eul(e):
    x,y,z=e;cx,sx,cy,sy,cz,sz=np.cos(x),np.sin(x),np.cos(y),np.sin(y),np.cos(z),np.sin(z)
    Rx=np.array([[1,0,0],[0,cx,-sx],[0,sx,cx]]);Ry=np.array([[cy,0,sy],[0,1,0],[-sy,0,cy]]);Rz=np.array([[cz,-sz,0],[sz,cz,0],[0,0,1]])
    return Rz@Ry@Rx
def posed_scene(path):
    """like map_visual.scene(), with each object's controllers applied at t = 0; invisible objects are dropped"""
    h=load(path);d=h['d'];B=h['blocks'];res=[]
    def ctrl_state(blk):
        st={}
        for c in _ctrl_chain(h,blk):
            ct,co,cs=B[c];ip=struct.unpack_from('<i',d,co+26)[0]
            v=_interp_value(h,ip)
            if ct=='NiVisController' and v is not None: st['vis']=v
            elif ct=='NiTransformController' and v: st['xf']=v
            elif ct=='NiAlphaController' and v is not None: st['alpha']=v
            elif ct=='NiMaterialColorController' and v is not None: st['color']=(struct.unpack_from('<H',d,co+30)[0],v)
            elif ct=='NiTextureTransformController' and v is not None:
                slot,op=struct.unpack_from('<II',d,co+31);st.setdefault('uvop',[]).append((slot,op,v))
            elif ct=='NiFlipController':
                slot,ns=struct.unpack_from('<II',d,co+30);src=struct.unpack_from('<%di'%ns,d,co+38) if ns else ()
                fr=int(v) if v is not None and v>=0 else 0
                if src: st['flip']=src[min(fr,len(src)-1)]
        return st
    def walk(k,M,props,pth):
        t,o,s=B[k]
        if t=='NiNode': a=node(d,o)
        elif t=='NiMesh': a=mesh(d,o)
        else: return
        cs=ctrl_state(k)
        if cs.get('vis') is False: return
        tr=np.array(a['t']);R=np.array(a['r']).reshape(3,3);sc=a['s']
        xf=cs.get('xf') or {}
        if 't' in xf: tr=np.array(xf['t'])
        if 'q' in xf: R=_quat(xf['q'])
        if 'eul' in xf: R=_eul(xf['eul'])
        if 's' in xf: sc=xf['s']
        L=np.eye(4);L[:3,:3]=R*sc;L[:3,3]=tr;W=M@L
        P=dict(props)
        for p in a['props']:
            if 0<=p<len(B):
                pt,pv=mv._prop(h,p);pv=dict(pv);pst=ctrl_state(p)
                if pt=='NiMaterialProperty':
                    if 'alpha' in pst: pv['alpha']=pst['alpha']
                    if 'color' in pst:
                        which,col=pst['color']
                        if isinstance(col,(int,float)): pass
                if pt=='NiTexturingProperty':
                    pv={k2:dict(v2) for k2,v2 in pv.items()}
                    if 'flip' in pst and 'base' in pv:
                        src=pst['flip']
                        if 0<=src<len(B) and B[src][0]=='NiSourceTexture': pv['base']['file']=S(h,struct.unpack_from('<I',d,B[src][1]+13)[0])
                    for slot,op,v in pst.get('uvop',[]):
                        if slot==0 and 'base' in pv:
                            off=list(pv['base'].get('off',(0.0,0.0)))
                            if op==0: off[0]=v
                            elif op==1: off[1]=v
                            pv['base']['off']=tuple(off)
                P[pt]=pv
        nm=S(h,a['name']) or ''
        if t=='NiNode':
            for c in a['children']: walk(c,W,P,pth+'/'+nm)
            return
        X={}
        for e in a['extra']:
            if 0<=e<len(B): k2,v=mv._extra(h,e);X[k2]=v
        res.append(dict(name=nm,path=pth+'/'+nm,props=P,extra=X,streams=mv._decode(h,a),W=W,nsub=a['nsub']))
    walk(0,np.eye(4),{},'')
    return res
_tex_index=None
def find_tex(name):
    global _tex_index
    if _tex_index is None:
        _tex_index={}
        for root,_,fs in os.walk(os.path.join(RAW,'effect')):
            for f in fs: _tex_index.setdefault(f.lower(),os.path.join(root,f))
        for root,_,fs in os.walk(os.path.join(RAW,'sky')):
            for f in fs: _tex_index.setdefault(f.lower(),os.path.join(root,f))
    return _tex_index.get(os.path.basename(name.replace('\\','/')).lower())
def effect_meshes(mapname,verbose=True):
    """[(scene mesh dict, offset xyz, nif name)] for the map's world effects that are baked into the .gmap"""
    w=os.path.join(RAW,'wff',mapname+'.wff')
    if not os.path.exists(w): return []
    out=[]
    for name,cat,pos in wff_entries(w):
        ln=name.lower()
        if any(ln.startswith(s) if s.endswith('_') else ln==s or ln.startswith(s) for s in SKIP):
            if verbose: print('  world fx %-28s left out'%name)
            continue
        p=os.path.join(RAW,'effect',cat.lower(),ln)
        if not os.path.exists(p): print('  world fx missing',p);continue
        sc=posed_scene(p);n0=len(out)
        for m in sc:
            P=m['streams'].get(('POSITION',0))
            if P is None: continue
            Pw=(np.c_[P[0],np.ones(len(P[0]))]@m['W'].T)[:,:3]
            if np.hypot(Pw[:,0],Pw[:,1]).max()>2000 and ln in ('tutorial_shooting_sky.nif','tutorial_bg.nif'): continue   # in the .gsky
            bt=(m['props'].get('NiTexturingProperty',{}).get('base',{}).get('file') or '').lower()
            if bt.startswith(SKIP_TEX): continue
            out.append((m,np.array(pos),name))
        if verbose: print('  world fx %-28s %3d meshes at (%.0f %.0f %.0f)'%(name,len(out)-n0,*pos))
    return out
