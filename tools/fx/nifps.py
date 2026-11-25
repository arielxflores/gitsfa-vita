# Gamebryo 30.2 particle-effect NIF reader (NiPS* particle systems, billboard plane meshes,
# alpha/flip/transform controllers). Layouts follow the public niftools nif.xml description.
import struct,sys,json
sys.path.insert(0,__import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)),'..','lib'))
from nifhdr import hdr

class R:
    def __init__(s,d,o): s.d=d;s.o=o
    def u(s,f):
        v=struct.unpack_from('<'+f,s.d,s.o);s.o+=struct.calcsize('<'+f);return v if len(v)>1 else v[0]
    def arr(s,f,n):
        v=struct.unpack_from('<%d%s'%(n,f),s.d,s.o);s.o+=struct.calcsize('<%d%s'%(n,f));return list(v)

NONE=0xffffffff
def load(path):
    d=open(path,'rb').read();h=hdr(d);h['d']=d;return h

def S(h,i): return None if i==NONE else h['strs'][i]

def objnet(r):
    name=r.u('I');ne=r.u('I');ex=r.arr('I',ne);ctrl=r.u('I');return dict(name=name,extra=ex,ctrl=ctrl)
def avobj(r):
    a=objnet(r);a['flags']=r.u('H');a['t']=r.arr('f',3);a['r']=r.arr('f',9);a['s']=r.u('f')
    np_=r.u('I');a['props']=r.arr('I',np_);a['coll']=r.u('I');return a
def keygroup(r,vf,vn=1):
    n=r.u('I')
    if n==0: return dict(it=0,keys=[])
    it=r.u('I');ks=[]
    for _ in range(n):
        t=r.u('f');v=r.arr(vf,vn)
        k=dict(t=t,v=v if vn>1 else v[0])
        if it==2: k['fwd']=r.arr(vf,vn);k['bwd']=r.arr(vf,vn)
        elif it==3: k['tbc']=r.arr('f',3)
        ks.append(k)
    return dict(it=it,keys=ks)
def timectl(r):
    c=dict(next=r.u('I'),flags=r.u('H'),freq=r.u('f'),phase=r.u('f'),start=r.u('f'),stop=r.u('f'),target=r.u('I'))
    return c
def meshmod(r):
    a=r.u('I');r.arr('H',a);b=r.u('I');r.arr('H',b)

def parse_block(h,k):
    t,o,sz=h['blocks'][k];d=h['d'];r=R(d,o);out=dict(type=t)
    try:
        if t in('NiNode','NiBillboardNode','NiSortAdjustNode'):
            out.update(avobj(r));nc=r.u('I');out['children']=[c for c in r.arr('I',nc) if c!=NONE]
            ne=r.u('I');r.arr('I',ne)
            if t=='NiBillboardNode': out['bbmode']=r.u('H')
            if t=='NiSortAdjustNode': out['sortmode']=r.u('I')
        elif t in('NiMesh','NiPSParticleSystem'):
            out.update(avobj(r));nm=r.u('I');r.arr('I',2*nm);r.u('I');r.u('B')
            out['prim']=r.u('I');out['nsub']=r.u('H');r.u('B');out['bound']=r.arr('f',4)
            ns=r.u('I');st=[]
            for _ in range(ns):
                ref=r.u('I');r.u('B');nsm=r.u('H');r.arr('H',nsm);nc=r.u('I');comps=[r.arr('I',2) for _ in range(nc)]
                st.append(dict(ref=ref,comps=comps))
            out['streams']=st;nmod=r.u('I');out['mods']=r.arr('I',nmod)
            if t=='NiPSParticleSystem':
                out['sim']=r.u('I');out['gen']=r.u('I');ne=r.u('I');out['emitters']=r.arr('I',ne)
                nsp=r.u('I');out['spawners']=r.arr('I',nsp);out['death_spawner']=r.u('I');out['maxp']=r.u('I')
                out['has_col'],out['has_rot'],out['has_rotax'],out['has_anim'],out['world']=r.arr('B',5)
                out['normal_method']=r.u('I');out['normal_dir']=r.arr('f',3);out['up_method']=r.u('I');out['up_dir']=r.arr('f',3)
                out['living_spawner']=r.u('I');nk=r.u('B');out['spawn_rate_keys']=[r.arr('f',2) for _ in range(nk)];out['prerpi']=r.u('B')
        elif t=='NiPSSimulator':
            meshmod(r);n=r.u('I');out['steps']=r.arr('I',n)
        elif t=='NiPSSimulatorGeneralStep':
            n=r.u('B');out['size_keys']=[r.arr('f',2)[::-1] for _ in range(n)];out['size_loop']=r.u('I')
            n=r.u('B');out['color_keys']=[(lambda c:(r.u('f'),c))(r.arr('B',4)) for _ in range(n)];out['color_loop']=r.u('I')
            n=r.u('B');out['rot_keys']=[(lambda q:(r.u('f'),q))(r.arr('f',4)) for _ in range(n)];out['rot_loop']=r.u('I')
            out['grow'],out['shrink']=r.arr('f',2);out['grow_gen'],out['shrink_gen']=r.arr('H',2)
        elif t=='NiPSSimulatorForcesStep':
            n=r.u('I');out['forces']=r.arr('I',n)
        elif t=='NiPSSimulatorCollidersStep':
            n=r.u('I');out['colliders']=r.arr('I',n)
        elif t in('NiPSGravityForce','NiPSDragForce','NiPSBombForce'):
            out['name']=r.u('I');out['ftype']=r.u('I');out['active']=r.u('B')
            if t=='NiPSGravityForce':
                out['axis']=r.arr('f',3);out['decay']=r.u('f');out['strength']=r.u('f');out['force_type']=r.u('I');out['turb']=r.u('f');out['turb_scale']=r.u('f');out['obj']=r.u('I')
            elif t=='NiPSDragForce':
                out['axis']=r.arr('f',3);out['pct']=r.u('f');out['range']=r.u('f');out['falloff']=r.u('f');out['obj']=r.u('I')
            else:
                out['axis']=r.arr('f',3);out['decay']=r.u('f');out['dv']=r.u('f');out['decay_type']=r.u('I');out['sym']=r.u('I');out['obj']=r.u('I')
        elif t in('NiPSAlignedQuadGenerator',):
            meshmod(r)
            for f in['scale_amt_u','scale_lim_u','scale_rest_u','scale_amt_v','scale_lim_v','scale_rest_v','center_u','center_v']: out[f]=r.u('f')
            out['uv_scroll']=r.u('B');out['frames_x'],out['frames_y']=r.arr('H',2);out['pingpong']=r.u('B')
            out['init_frame']=r.u('H');out['init_frame_var']=r.u('f');out['nframes']=r.u('H');out['nframes_var']=r.u('f');out['t0'],out['t1']=r.arr('f',2)
        elif t in('NiPSFacingQuadGenerator',):
            meshmod(r)
        elif t.startswith('NiPS') and t.endswith('Emitter'):
            out['name']=r.u('I');out['speed'],out['speed_var'],out['speed_flip']=r.arr('f',3)
            out['decl'],out['decl_var'],out['planar'],out['planar_var']=r.arr('f',4)
            out['size'],out['size_var'],out['life'],out['life_var']=r.arr('f',4)
            out['rot'],out['rot_var'],out['rot_speed'],out['rot_speed_var']=r.arr('f',4)
            out['rot_axis']=r.arr('f',3);out['rand_rot_sign'],out['rand_rot_axis']=r.arr('B',2)
            if t=='NiPSMeshEmitter':
                n=r.u('I');out['meshes']=r.arr('I',n);out['emitter_obj']=r.u('I');out['emit_from']=r.u('I');out['vel_type']=r.u('I')
            else:
                out['emitter_obj']=r.u('I')
                if t=='NiPSBoxEmitter': out['dims']=r.arr('f',3)
                elif t=='NiPSSphereEmitter': out['radius']=r.u('f')
                elif t=='NiPSCylinderEmitter': out['radius'],out['height']=r.arr('f',2)
                elif t=='NiPSTorusEmitter': out['radius'],out['sradius']=r.arr('f',2)
        elif t in('NiPSEmitParticlesCtlr','NiPSForceActiveCtlr','NiAlphaController','NiFlipController','NiTransformController','NiPSEmitterSpeedCtlr','NiPSEmitterLifeSpanCtlr','NiPSEmitterSizeCtlr','NiPSEmitterDeclinationCtlr','NiPSEmitterPlanarAngleCtlr','NiPSEmitterRadiusCtlr','NiPSEmitterDeclinationVarCtlr','NiPSEmitterPlanarAngleVarCtlr','NiPSEmitterRotSpeedCtlr','NiPSEmitterRotAngleCtlr','NiPSGravityStrengthCtlr','NiMaterialColorController','NiTextureTransformController','NiVisController'):
            out.update(timectl(r));out['interp']=r.u('I')
            if t=='NiPSEmitParticlesCtlr': out['emitter']=r.u('I');out['active_interp']=r.u('I')
            elif t.startswith('NiPSEmitter') or t=='NiPSForceActiveCtlr' or t=='NiPSGravityStrengthCtlr': out['tname']=r.u('I')
            elif t=='NiFlipController': out['slot']=r.u('I');n=r.u('I');out['sources']=r.arr('I',n)
            elif t=='NiMaterialColorController': out['target_color']=r.u('H')
            elif t=='NiTextureTransformController': out['shader_map']=r.u('B');out['slot']=r.u('I');out['op']=r.u('I')
        elif t=='NiFloatInterpolator': out['value']=r.u('f');out['data']=r.u('I')
        elif t in('NiBoolInterpolator','NiBoolTimelineInterpolator'): out['value']=r.u('B');out['data']=r.u('I')
        elif t=='NiPoint3Interpolator': out['value']=r.arr('f',3);out['data']=r.u('I')
        elif t=='NiFloatData': out.update(keygroup(r,'f'))
        elif t=='NiBoolData': out.update(keygroup(r,'B'))
        elif t=='NiPosData': out.update(keygroup(r,'f',3))
        elif t=='NiTransformInterpolator': out['tr']=r.arr('f',3);out['q']=r.arr('f',4);out['sc']=r.u('f');out['data']=r.u('I')
        elif t=='NiTransformData':
            n=r.u('I')
            if n:
                rt=r.u('I');out['rot_type']=rt
                if rt==4:
                    out['xyz']=[keygroup(r,'f') for _ in range(3)]
                else:
                    ks=[]
                    for _ in range(n):
                        tt=r.u('f');q=r.arr('f',4)
                        if rt==3: r.arr('f',3)
                        ks.append((tt,q))
                    out['rot']=ks
            out['trans']=keygroup(r,'f',3);out['scale']=keygroup(r,'f')
        elif t=='NiTexturingProperty':
            out.update(objnet(r));out['tflags']=r.u('H');out['count']=r.u('I');hb=r.u('B')
            if hb: out['base']=r.u('I');out['base_flags']=r.u('H')
        elif t=='NiSourceTexture':
            out.update(objnet(r));r.u('B');out['file']=S(h,r.u('I'))
        elif t=='NiAlphaProperty':
            out.update(objnet(r));f=r.u('H');out['aflags']=f;out['thresh']=r.u('B')
            BL=['ONE','ZERO','SRC_COLOR','INV_SRC_COLOR','DST_COLOR','INV_DST_COLOR','SRC_ALPHA','INV_SRC_ALPHA','DST_ALPHA','INV_DST_ALPHA','SAT']
            out['blend']=(f&1,BL[(f>>1)&15] if ((f>>1)&15)<11 else (f>>1)&15,BL[(f>>5)&15] if ((f>>5)&15)<11 else (f>>5)&15);out['test']=(f>>9)&1
        elif t=='NiMaterialProperty':
            out.update(objnet(r));out['amb']=r.arr('f',3);out['dif']=r.arr('f',3);out['spec']=r.arr('f',3);out['emis']=r.arr('f',3);out['gloss']=r.u('f');out['alpha']=r.u('f')
        elif t=='NiZBufferProperty' or t=='NiVertexColorProperty':
            out.update(objnet(r));out['pflags']=r.u('H')
        elif t=='NiIntegerExtraData': out['name']=S(h,r.u('I'));out['val']=r.u('i')
        elif t=='NiFloatExtraData': out['name']=S(h,r.u('I'));out['val']=r.u('f')
        elif t=='NiBooleanExtraData': out['name']=S(h,r.u('I'));out['val']=r.u('B')
        elif t=='NiStringExtraData': out['name']=S(h,r.u('I'));out['val']=S(h,r.u('I'))
        elif t=='NiPSBoundUpdater': out['skip']=r.u('H')
        else: out['raw']=d[o:o+sz].hex()
    except Exception as e:
        out['err']=repr(e)
    if 'name' in out and isinstance(out['name'],int): out['name']=S(h,out['name'])
    if 'tname' in out: out['tname']=S(h,out['tname'])
    if 'emitter' in out and t=='NiPSEmitParticlesCtlr': out['emitter']=S(h,out['emitter'])
    out['_used']=r.o-o;out['_size']=sz
    return out

def parse(path):
    h=load(path);B=[parse_block(h,k) for k in range(len(h['blocks']))];return h,B

def stream_pos(h,B,mk):
    """positions (world-ish, local to mesh) of an NiMesh block"""
    import numpy as np
    m=B[mk];d=h['d']
    for st in m['streams']:
        t,o,sz=h['blocks'][st['ref']]
        r=R(d,o);size,clone,nr=r.arr('I',3);r.arr('I',2*nr);nc=r.u('I');fm=r.arr('I',nc)
        sizes={0x30437:12,0x20436:8,0x10215:2,0x10425:4,0x40110:4,0x40401:4,0x10413:2}
        stride=sum(sizes.get(f,12) for f in fm)
        if stride==0 or size%stride: continue
        raw=np.frombuffer(d[r.o:r.o+size],'u1').reshape(-1,stride);off=0;outd={}
        for (sem,idx),f in zip(st['comps'],fm):
            nm=S(h,sem);s_=sizes.get(f,12)
            col=raw[:,off:off+s_].copy();off+=s_
            if nm=='POSITION': outd['P']=col.view('<f4').reshape(-1,3)
            if nm=='TEXCOORD': outd['UV']=col.view('<f4').reshape(-1,2)
            if nm=='INDEX': outd['I']=col.view('<u2' if s_==2 else '<u4').ravel()
        yield outd

if __name__=='__main__':
    for p in sys.argv[1:]:
        h,B=parse(p);print('=====',p)
        for k,b in enumerate(B):
            bb={x:y for x,y in b.items() if x not in('r','coll','flags','extra','ctrl') or y not in([],NONE,0)}
            if b['_used']!=b['_size'] and 'raw' not in b: bb['MISMATCH']=1
            print(k,json.dumps(bb,default=str)[:600])
