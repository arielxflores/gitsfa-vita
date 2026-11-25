# Host renderer for vm_<short>.gvm (headless OpenGL through moderngl/EGL): the engine's viewmodel maths and shading
# (camera-space bone poses, palette skinning, sh = 0.40 + 0.75 * sat(n.l), opaque pass then blended meshes), with an
# optional background picture (a footage frame) so renders can be laid over / next to the original.
# Blend modes as in model.h + weapon.h: 0 opaque, 1 alpha blend (lens vignettes, reticles), 2 additive (dots, glows).
# usage (module): G=vmgl.load(path); img=vmgl.render(G,clip,t,fovy,offset,bg=None,W=960,H=544)
import sys,os,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import preview,moderngl
from PIL import Image
_ctx=None
def ctx():
    global _ctx
    if _ctx is None: _ctx=moderngl.create_standalone_context(backend='egl')
    return _ctx
VS='''#version 330
in vec3 pos; in vec3 nrm; in vec2 uv;
uniform mat4 mvp; uniform vec3 ldir; out vec2 t0; out float sh;
void main(){ gl_Position=mvp*vec4(pos,1.0); t0=uv; sh=0.40+0.75*clamp(dot(normalize(nrm+vec3(0,0,0.0001)),ldir),0.0,1.0); }'''
FS='''#version 330
in vec2 t0; in float sh; uniform sampler2D dif; uniform vec4 tint; uniform int mode; out vec4 o;
void main(){ vec4 c=texture(dif,t0); if(mode==3) o=vec4(c.rgb*tint.rgb,c.a*tint.a); else o=vec4(c.rgb*sh*tint.rgb,c.a*tint.a); }'''
# lens glass (blend bit 7; engine: wpn_vm.h WVM_LENS_VS / WVM_LENS_FS, same maths): camera-space position s and normal n
# come skinned; v = eye -> vertex including the K offset; sphere-ish env lookup scrolled by the camera yaw / pitch
LVS='''#version 330
in vec3 pos; in vec3 nrm; in vec2 uv;
uniform mat4 mvp; uniform vec4 lp; uniform vec4 ko; uniform vec4 lc; out vec2 t0; out vec2 te; out float fr; out vec2 te2; out vec3 nd; out vec3 vv;
void main(){ vec3 n=normalize(nrm+vec3(0,0,0.0001)); vec3 v=normalize(pos+ko.xyz); vec3 r=reflect(v,n);
  float c=clamp(-dot(v,n),0.0,1.0); fr=1.0-c; fr=0.6+0.4*fr*fr;
  te=vec2(r.x*0.14+lp.y,0.5-r.y*0.2+lp.z);
  vec3 n2=normalize(n+(pos-lc.xyz)*lc.w); vec3 r2=reflect(v,n2); te2=vec2(r2.x*0.14+lp.y,0.5-r2.y*0.2+lp.z); nd=n2; vv=v;
  gl_Position=mvp*vec4(pos,1.0); t0=uv; }'''
# b66 glass (wpn_vm.h WVM_GLASS_*): the closed lens at hip is curved glass (domed normal, fresnel, contrasty panorama tinted by the
# coating, a soft glint); aimed / the holo window: the b65 formula (fo). glass=0 in render(): b65's lens exactly (test switch oldlens)
LFS='''#version 330
in vec2 t0; in vec2 te; in float fr; in vec2 te2; in vec3 nd; in vec3 vv; uniform sampler2D dif; uniform sampler2D env; uniform vec4 tint; uniform vec4 coat; uniform vec4 lk; uniform vec4 gl; uniform int glass; out vec4 o;
void main(){ vec4 b=texture(dif,t0); vec3 e=texture(env,te).rgb;
  vec2 d=t0*2.0-1.0; float rr=clamp(dot(d,d),0.0,1.0);
  float a=mix(coat.a,mix(clamp(b.a*2.0-1.0,0.0,1.0),b.a,lk.y),lk.x); vec3 bc=b.rgb*mix(1.0,lk.y,lk.x); vec3 rf=e*coat.rgb*(fr*mix(lk.w+(1.0-lk.w)*rr,rr*rr*lk.z,lk.x*(1.0-lk.y)));
  float ao=clamp(a+dot(rf,vec3(0.3,0.5,0.2)),0.0,1.0); vec3 fo=(bc*a+rf)/max(ao,0.002);
  if(glass==0){ o=vec4(fo*tint.rgb,ao*tint.a); return; }
  vec3 n=normalize(nd), v=normalize(vv); float cs=1.0-clamp(-dot(v,n),0.0,1.0); float c2=cs*cs; float F=0.04+0.96*c2*c2*cs;
  vec3 e2=texture(env,te2).rgb; e2=e2*e2*2.45; float g=pow(clamp(dot(n,normalize(gl.xyz-v)),0.0,1.0),28.0)*gl.w;
  vec3 R=e2*mix(vec3(1.0),coat.rgb,0.5)*(F*0.6)+vec3(g); float A0=coat.a*0.3;   // b82: see-through glass at hip (wpn_vm.h)
  float Ah=clamp(A0+0.55*smoothstep(0.55,1.0,rr)+dot(R,vec3(0.3,0.5,0.2)),0.0,1.0); vec3 cl=(fo*(0.4*A0)+R)/max(Ah,0.002);
  vec3 fc=mix(cl,fo,lk.x); float fa=mix(Ah,ao,lk.x); o=vec4(fc*tint.rgb,fa*tint.a); }'''
BGVS='''#version 330
in vec2 p; out vec2 t; void main(){ gl_Position=vec4(p,0,1); t=vec2(p.x*0.5+0.5,0.5-p.y*0.5); }'''
BGFS='''#version 330
in vec2 t; uniform sampler2D bg; out vec4 o; void main(){ o=texture(bg,t); }'''
def load(path):
    G=preview.read(path);c=ctx();G['gltex']=[]
    for w,h,fmt,mm in G['texs']:
        rgba=(preview.decode_dxt(w,h,fmt,mm[0])).clip(0,255).astype('u1')
        t=c.texture((w,h),4,rgba.tobytes());t.build_mipmaps();t.filter=(moderngl.LINEAR_MIPMAP_LINEAR,moderngl.LINEAR);t.repeat_x=t.repeat_y=True
        for k in range(1,len(mm)):   # the file's own mip levels (as the engine uploads them), not GL-made ones
            wk,hk=max(1,w>>k),max(1,h>>k);t.write(preview.decode_dxt(wk,hk,fmt,mm[k]).clip(0,255).astype('u1').tobytes(),level=k)
        G['gltex'].append(t)
    G['prog']=c.program(vertex_shader=VS,fragment_shader=FS);G['bgprog']=c.program(vertex_shader=BGVS,fragment_shader=BGFS)
    G['lprog']=c.program(vertex_shader=LVS,fragment_shader=LFS)
    return G
def persp(fovy,asp,n,f):
    t=1/np.tan(np.radians(fovy)/2);m=np.zeros((4,4));m[0,0]=t/asp;m[1,1]=t;m[2,2]=(f+n)/(n-f);m[2,3]=2*f*n/(n-f);m[3,2]=-1;return m
def pose(G,clip,t):
    """bone poses (nb,7) of a clip at time t (seconds, clamped / looped as the engine does)"""
    loop,fps,data=G['clips'][clip];n=data.shape[0];fr=t*fps
    fr=fr%(n-1) if loop and n>1 else min(max(fr,0),n-1)
    i0=min(int(fr),max(n-2,0));i1=min(i0+1,n-1);a=min(fr-i0,1.0)
    A=data[i0];B=data[i1];d=(A[:,:4]*B[:,:4]).sum(1,keepdims=True);Bq=np.where(d<0,-B[:,:4],B[:,:4])
    q=A[:,:4]*(1-a)+Bq*a;q/=np.linalg.norm(q,axis=1,keepdims=True);p=A[:,4:]*(1-a)+B[:,4:]*a
    return np.c_[q,p]
def skin(G,fr,mi):
    M=preview.palette_mats(G,fr);m=G['meshes'][mi];v=m['v'];P=np.c_[v['p'],np.ones(len(v))];bw=v['bw']/255.0
    acc=np.zeros((len(v),4));nacc=np.zeros((len(v),3))
    for j in range(4):
        Mj=M[v['bi'][:,j]+m.get('base',0)];acc+=bw[:,j:j+1]*np.einsum('nij,nj->ni',Mj,P)
        if j<2: nacc+=bw[:,j:j+1]*np.einsum('nij,nj->ni',Mj[:,:3,:3],v['n'][:,:3]/127.0)
    return acc[:,:3],nacc
def bone_xform(fr,b,v):
    q=fr[b,:4];return preview.qmat(q/np.linalg.norm(q))@np.asarray(v,float)+fr[b,4:]
def lens_params(G,ads,lkz=0.6):
    """the engine's per-model lens uniforms (wpn_vm.h wvm_lens_draw): coat, lk"""
    a=G['att'];k1=float(a['lensenv'][1]) if 'lensenv' in a else 1.0;kind=1 if k1>=1 else 0
    coat=tuple(a['lenscoat'])+(1.0 if kind else k1,) if 'lenscoat' in a else (0.5,0.5,0.5,1.0)
    op=1.0 if kind==1 else min(1.0,max(0.0,(ads-0.38)*8.4))   # tube lens: closed at hip, opens at ads 0.38-0.5 (engine: wpn_vm.h)
    return coat,(op,1.0 if kind==1 else 0.0,lkz,float(a['lensenv'][2]) if 'lensenv' in a else 0.3)
def render(G,fr,fovy,offset,bg=None,W=960,H=544,tint=(1,1,1,1),hide=(),only=None,clear=(0.45,0.5,0.56),extra=None,kick=0.0,ads=None,yaw=0.0,pitch=0.0,glass=1):
    """fr: pose (nb,7) or (clip,t). offset: camera-space viewmodel offset (engine K translation). bg: PIL image (stretched).
    ads: aiming amount (blend-3 meshes, the lens caps, are drawn only below 0.5); None = guess from the clip name.
    hide: mesh indices not drawn; extra(fbo,P): callback drawing more after the model (overlays)."""
    if ads is None: ads=1.0 if isinstance(fr,tuple) and fr[0].startswith(('ads','fire_ads')) else 0.0
    if isinstance(fr,tuple): fr=pose(G,*fr)
    c=ctx();fbo=c.simple_framebuffer((W,H),samples=0);fbo.use();fbo.clear(*clear,1.0,depth=1.0)
    if bg is not None:
        im=bg.convert('RGBA').resize((W,H),Image.LANCZOS);t=c.texture((W,H),4,im.tobytes())
        vb=c.buffer(np.array([-1,-1,1,-1,-1,1,1,1],'f4').tobytes());va=c.vertex_array(G['bgprog'],[(vb,'2f','p')])
        c.disable(moderngl.DEPTH_TEST);t.use(0);va.render(moderngl.TRIANGLE_STRIP);t.release()
    P=persp(fovy,W/H,2,400);K=np.eye(4);K[:3,3]=offset
    ka=kick*0.015;Kr=np.eye(4);Kr[1,1]=Kr[2,2]=np.cos(ka);Kr[2,1]=np.sin(ka);Kr[1,2]=-np.sin(ka)
    MVP=(P@K@Kr).astype('f4')
    pr=G['prog'];pr['mvp'].write(MVP.T.tobytes());pr['ldir'].value=(-0.35,0.75,0.56);pr['tint'].value=tuple(tint)
    c.enable(moderngl.DEPTH_TEST);c.disable(moderngl.CULL_FACE)
    B=lambda m:m['blend']&15
    L=lambda m:bool(m['blend']&128 and 'lensenv' in G['att'])
    order=[i for i,m in enumerate(G['meshes']) if B(m)==0 or (B(m)==3 and ads<0.5)]+[i for i,m in enumerate(G['meshes']) if B(m)==1 and not L(m)]+[i for i,m in enumerate(G['meshes']) if B(m)==2]+[i for i,m in enumerate(G['meshes']) if L(m)]
    lp=G['lprog'];lp['mvp'].write(MVP.T.tobytes());lp['tint'].value=tuple(tint);coat,lk=lens_params(G,ads);lp['coat'].value=coat;lp['lk'].value=lk
    lp['lp'].value=(0.0,yaw/(2*np.pi),max(-0.15,min(0.15,-pitch*0.25)),0.0);lp['ko'].value=tuple(offset)+(0.0,);lp['dif'].value=0;lp['env'].value=1
    L=np.array([0.15,1.0,-0.25]);L/=np.linalg.norm(L);lp['gl'].value=tuple(L)+(0.5,);lp['glass'].value=int(glass)   # (b66 glint: the viewmodel's highlight direction)
    for i in order:
        if i in hide or (only is not None and i not in only): continue
        m=G['meshes'][i]
        if len(m['I'])==0: continue
        X,N=skin(G,fr,i)
        vb=c.buffer(np.c_[X,N,m['v']['uv']].astype('f4').tobytes());ib=c.buffer(m['I'].astype('u4').tobytes())
        lens=m['blend']&128 and 'lensenv' in G['att']
        va=c.vertex_array(lp if lens else pr,[(vb,'3f 3f 2f','pos','nrm','uv')],ib)
        if not lens: pr['mode'].value=0
        else: G['gltex'][int(G['att']['lensenv'][0])].use(1);ctr=X.mean(0);R=float(np.sqrt(((X-ctr)**2).sum(1)).max());lp['lc'].value=tuple(ctr)+(2.0/max(R,1e-4),)   # (b66: the dome)
        if B(m) in (0,3): c.disable(moderngl.BLEND);fbo.depth_mask=True
        elif B(m)==1 or lens: c.enable(moderngl.BLEND);c.blend_func=(moderngl.SRC_ALPHA,moderngl.ONE_MINUS_SRC_ALPHA);fbo.depth_mask=False
        else: c.enable(moderngl.BLEND);c.blend_func=(moderngl.SRC_ALPHA,moderngl.ONE);fbo.depth_mask=False
        G['gltex'][m['tex']].use(0);va.render(moderngl.TRIANGLES);vb.release();ib.release();va.release()
        fbo.depth_mask=True
    c.disable(moderngl.BLEND)
    if extra: extra(c,fbo,P@K)
    img=Image.frombytes('RGBA',(W,H),fbo.read(components=4)).transpose(Image.FLIP_TOP_BOTTOM).convert('RGB')
    fbo.release();return img
def project(PK,p,W=960,H=544):
    q=PK@np.r_[p,1.0];return ((q[0]/q[3]+1)*W/2,(1-q[1]/q[3])*H/2)
