# Host renderer for .gmap maps, matching the Vita engine's world shading (world.h):
#   diffuse x lightmap x2 (+ glow) -> fog (exp, from MAPS[] / world.ini) -> 16^3 colour-grading LUT
# usage as module: R=Renderer(); R.load('downtown'); R.shot(pos,yaw,pitch,'out.png')
import sys,os,struct,json,math,numpy as np,moderngl
from PIL import Image
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)));sys.path.insert(0,'/root/gits/tools')
import gmapio
from uvrebase import rebase
FOG=json.load(open('/root/gits/tools/fog.json'))
BUILD='/root/gits/build'
VS='''#version 330
in vec3 pos; in vec2 uv0; in vec2 uv1; in vec4 col; in vec3 nrm;
uniform mat4 mvp; uniform vec4 fogp; uniform float skymode; uniform vec3 eye; uniform mat3 vrot; uniform float envk;
out vec2 t0; out vec2 t1; out float fog; out vec4 vc; out vec3 ev; out vec4 fv;
void main(){ vec4 p=mvp*vec4(pos,1.0); gl_Position=p; t0=uv0; t1=uv1; vc=col;
 vec3 v=normalize(pos-eye); vec3 r=reflect(v,normalize(nrm+vec3(0,0,1e-5))); vec3 rv=vrot*r;   // b67: the view-space reflection -> sphere map
 float em=2.0*sqrt(rv.x*rv.x+rv.y*rv.y+(rv.z+1.0)*(rv.z+1.0))+1e-5; ev=vec3(rv.x/em+0.5,0.5-rv.y/em,envk);
 fv=vec4(pos-eye,dot(pos-eye,nrm));   // b67: Fresnel per pixel (|dot| / length: per vertex it went wrong on big floor triangles)
 fog = skymode>0.5 ? fogp.w : fogp.z*(1.0-exp(-max(p.w-fogp.x,0.0)*fogp.y)); }'''
FS='''#version 330
in vec2 t0; in vec2 t1; in float fog; in vec4 vc; in vec3 ev; in vec4 fv;
uniform sampler2D dif; uniform sampler2D lm; uniform sampler2D lut; uniform sampler2D glw; uniform sampler2D envt; uniform float envocc; uniform float envdif; uniform float envfres;
uniform vec4 fogc; uniform float texbias; uniform float lmon; uniform float hastex; uniform int mode; uniform float ref; uniform float glowk; uniform vec4 tint; uniform float usevc; uniform float grade; uniform vec4 gk[10]; uniform float lmscale; uniform int idmode; uniform int mid;
out vec4 o;
void main(){
 vec4 c = hastex>0.5 ? texture(dif,t0,texbias) : vec4(1.0,0.0,1.0,1.0);
 if (usevc>0.5) c*=vc;
 c*=tint;
 if (mode>=1 && c.a<ref) discard;
 vec3 l = lmon>0.5 ? texture(lm,t1,texbias).rgb*lmscale : vec3(1.0);
 vec3 fc = c.rgb*l;
 if (glowk>0.0) fc += texture(glw,t0).rgb*glowk;
 if (ev.z>0.0) { float cs=1.0-min(abs(fv.w)/max(length(fv.xyz),1e-3),1.0), c2=cs*cs; float ef = envfres>0.0 ? envfres+(1.0-envfres)*c2*c2*cs : 1.0; float eo = envocc>0.5 ? clamp(dot(l,vec3(0.333)),0.0,1.0) : 1.0; float ed = envdif>0.5 ? clamp(2.0*dot(c.rgb,vec3(0.333)),0.0,1.0) : 1.0; fc += texture(envt,ev.xy).rgb*ev.z*eo*ed*ef; }   // b67 (envocc / envdif: experiments)
 fc = mix(fc,fogc.rgb,fog);
 vec3 gc = fc;
 if (grade>1.5){ vec3 x=clamp(fc,0.0,1.0); vec3 y=x*x; vec3 z=y*x;
   gc=gk[9].xyz+gk[0].xyz*x.x+gk[1].xyz*x.y+gk[2].xyz*x.z+gk[3].xyz*y.x+gk[4].xyz*y.y+gk[5].xyz*y.z+gk[6].xyz*z.x+gk[7].xyz*z.y+gk[8].xyz*z.z; }
 else if (grade>0.5){ vec3 q=clamp(fc,0.0,1.0)*15.0; float b=min(floor(q.z),14.0); float f=q.z-b;
   vec2 u=vec2((q.x+0.5+b*16.0)/256.0,(q.y+0.5)/16.0); gc=mix(texture(lut,u).rgb,texture(lut,u+vec2(0.0625,0.0)).rgb,f);} 
 o=vec4(gc, mode==2 ? c.a : 1.0);
 if (idmode==1){ if (mode==2 && c.a<0.3) discard; o=vec4(float(mid&255)/255.0,float((mid>>8)&255)/255.0,1.0,1.0);} }'''
def persp(fovy,asp,n,f):
    t=1/math.tan(fovy/2);m=np.zeros((4,4));m[0,0]=t/asp;m[1,1]=t;m[2,2]=(f+n)/(n-f);m[2,3]=2*f*n/(n-f);m[3,2]=-1;return m
def view(p,yaw,pitch):
    cp,sp,cy,sy=math.cos(pitch),math.sin(pitch),math.cos(yaw),math.sin(yaw)
    F=np.array([cy*cp,sy*cp,sp]);R=np.array([sy,-cy,0.0]);U=np.cross(R,F)
    m=np.eye(4);m[0,:3]=R;m[1,:3]=U;m[2,:3]=-F;m[0,3]=-R@p;m[1,3]=-U@p;m[2,3]=F@p;return m
class Renderer:
    def __init__(s,W=960,H=544,ss=2):
        s.ctx=moderngl.create_standalone_context(backend='egl');s.W,s.H,s.ss=W,H,ss
        s.prog=s.ctx.program(vertex_shader=VS,fragment_shader=FS)
        s.fbo=s.ctx.framebuffer([s.ctx.renderbuffer((W*ss,H*ss))],s.ctx.depth_renderbuffer((W*ss,H*ss)))
        s.white=s.ctx.texture((1,1),4,b'\xff\xff\xff\xff')
        s.sky=None;s.glow_on=True
    def load(s,stem,gmap=None,lut=True,sky=None,gsky=None,glut=None,gmode='lut'):
        s.stem=stem;g=gmapio.read(gmap or '%s/maps/%s.gmap'%(BUILD,stem))
        s.ver=g['ver'];s.texs=[]
        for t in g['tex']:
            w,h=t['w'],t['h'];tx=s.ctx.texture((w,h),4,gmapio.dds_decode(w,h,t['levels'][0],t['fmt']).tobytes())
            if len(t['levels'])>1: tx.build_mipmaps(0,len(t['levels'])-1)
            for k in range(1,len(t['levels'])):
                tx.write(gmapio.dds_decode(max(1,w>>k),max(1,h>>k),t['levels'][k],t['fmt']).tobytes(),level=k)
            tx.filter=(moderngl.LINEAR_MIPMAP_LINEAR if len(t['levels'])>1 else moderngl.LINEAR,moderngl.LINEAR);tx.repeat_x=tx.repeat_y=True
            tx.anisotropy=getattr(s,'aniso',1.0)   # (b69 A/B: the device has none so far)
            s.texs.append(tx)
        s.meshes=[]
        for m in g['mesh']:
            V=m['V'];I=m['I'].astype(np.int64)
            nrm=np.zeros((len(V),3),'f4')
            if m['flags']&64: nrm=V[:,-1].copy().view(np.int8).reshape(-1,4)[:,:3].astype('f4')/127;V=V[:,:-1]   # b67: the env slot
            if V.shape[1]==8: col=V[:,7].copy().view(np.uint8).reshape(-1,4).astype('f4')/255;V=V[:,:7]
            else: col=np.ones((len(V),4),'f4')
            if not (m['flags']&16):
                V2,I2=rebase(np.c_[V,col,nrm],I);V,col,nrm,I=V2[:,:7],V2[:,7:11],V2[:,11:14],I2
            buf=np.c_[V[:,:7],col,nrm].astype('f4')
            vbo=s.ctx.buffer(buf.tobytes());ibo=s.ctx.buffer(np.asarray(I,'u4').tobytes())
            vao=s.ctx.vertex_array(s.prog,[(vbo,'3f 2f 2f 4f 3f','pos','uv0','uv1','col','nrm')],ibo)
            c=(np.array(m['mn'])+np.array(m['mx']))/2
            s.meshes.append(dict(m,vao=vao,V=None,I=None,cen=c))
        fc,fp=FOG.get(stem,[[0.55,0.6,0.66],[1000,0.0001,0.5]])
        s.fog=(fc[0],fc[1],fc[2],1.0);s.fogp=(fp[0],fp[1],fp[2],0.0)
        lp=glut or '%s/data/%s.glut'%(BUILD,stem)
        s.gk=np.zeros((10,4),'f4');s.gk[0,0]=s.gk[1,1]=s.gk[2,2]=1
        if lut and os.path.exists(lp):
            raw=open(lp,'rb').read();a=np.frombuffer(raw[:12288],np.uint8).reshape(16,256,3);s.grade=1.0
            if gmode in ('poly','affine') and raw[12288:12292]==b'GFIT':
                k=np.frombuffer(raw,'<f4',80,12296).reshape(2,10,4);s.gk=k[0 if gmode=='poly' else 1].copy();s.grade=2.0
        else:
            a=np.zeros((16,256,3),np.uint8);x=np.arange(256);y=np.arange(16)[:,None]
            a[...,0]=(x%16)*17;a[...,1]=y*17;a[...,2]=(x//16)*17;s.grade=0.0
        s.lut=s.ctx.texture((256,16),3,np.ascontiguousarray(a).tobytes());s.lut.filter=(moderngl.LINEAR,moderngl.LINEAR);s.lut.repeat_x=s.lut.repeat_y=False
        s.sky=sky
        if gsky and os.path.exists(gsky):
            import sky as _sky; s.sky=_sky.Sky(s,gsky)
    def draw(s,MVP,lmon=True,only=None,hl=None,cam=None,P=None,Vm=None):
        Pr=s.prog;Pr['mvp'].write(MVP.T.astype('f4').tobytes());Pr['fogp'].value=s.fogp;Pr['fogc'].value=s.fog;Pr['grade'].value=s.grade;Pr['gk'].write(s.gk.astype('f4').tobytes())
        Pr['texbias'].value=float(getattr(s,'texbias',0.0));Pr['dif'].value=0;Pr['lm'].value=1;Pr['lut'].value=2;Pr['glw'].value=3;s.lut.use(2);Pr['lmscale'].value=2.0;Pr['skymode'].value=0.0
        Pr['envt'].value=4;Pr['envocc'].value=float(getattr(s,'envocc',1.0));Pr['envdif'].value=float(getattr(s,'envdif',0.0));Pr['envfres'].value=float(getattr(s,'envfres',0.1))   # (the engine's b67: lightmap shade, Fresnel F0 0.1 per pixel)
        if cam is not None and Vm is not None: Pr['eye'].value=tuple(float(x) for x in cam);Pr['vrot'].write(np.ascontiguousarray(Vm[:3,:3].T).astype('f4').tobytes())
        ctx=s.ctx;n=0
        op=[i for i,m in enumerate(s.meshes) if m['mode']!=2]
        bl=[i for i,m in enumerate(s.meshes) if m['mode']==2]
        if cam is not None: bl.sort(key=lambda i:-float(((s.meshes[i]['cen']-cam)**2).sum()))
        def one(i):
            m=s.meshes[i]
            if only is not None and i not in only: return 0
            if m['mode']==2:
                ctx.enable(moderngl.BLEND)
                ctx.blend_func=(moderngl.SRC_ALPHA,moderngl.ONE) if m['flags']&8 else (moderngl.SRC_ALPHA,moderngl.ONE_MINUS_SRC_ALPHA)
            else: ctx.disable(moderngl.BLEND)
            if m['two']: ctx.disable(moderngl.CULL_FACE)
            else: ctx.enable(moderngl.CULL_FACE)
            ctx.depth_mask=bool(m['zwrite'])
            t=m['tex'];Pr['hastex'].value=1.0 if t>=0 else 0.0
            (s.texs[t] if t>=0 else s.white).use(0)
            lo=lmon and m['lm']>=0;Pr['lmon'].value=1.0 if lo else 0.0
            if lo: s.texs[m['lm']].use(1)
            Pr['mode'].value=int(m['mode']);Pr['ref'].value=float(m['ref']) if m['mode']==2 or s.ver>=2 else 0.5;Pr['tint'].value=tuple(m['tint']);Pr['usevc'].value=float(m.get('vcol',0))
            g=m.get('glow',-1);Pr['glowk'].value=float(m['glowk']) if g>=0 and s.glow_on else 0.0
            if g>=0: s.texs[g].use(3)
            e=m.get('env',-1);Pr['envk'].value=float(m.get('envk',0.0))*getattr(s,'envscale',1.0) if e>=0 and not (getattr(s,'envnoglow',0) and g>=0) and not (getattr(s,'envnotest',0) and m['mode']!=0) else 0.0   # b67
            if e>=0: s.texs[e].use(4)
            if hl is not None and i in hl: Pr['tint'].value=(1,0,0,1)
            Pr['mid'].value=i;Pr['idmode'].value=1 if getattr(s,'idmode',0) else 0
            m['vao'].render();return 1
        for i in op: n+=one(i)
        ctx.disable(moderngl.BLEND);ctx.depth_mask=True;ctx.enable(moderngl.CULL_FACE)
        if s.sky is not None and P is not None and not getattr(s,'idmode',0): s.sky.draw(s,P,Vm,cam);s.sky.draw(s,P,Vm,cam,world=True);Pr['mvp'].write(MVP.T.astype('f4').tobytes())
        for i in bl: n+=one(i)
        ctx.depth_mask=True;ctx.disable(moderngl.BLEND)
        return n
    def shot(s,pos,yaw,pitch,out=None,fov=59,lmon=True,only=None,hl=None,raw=False):
        ctx=s.ctx;s.fbo.use();ctx.viewport=(0,0,s.W*s.ss,s.H*s.ss)
        s.fbo.clear(*s.fog[:3],1.0,depth=1.0)
        ctx.enable(moderngl.DEPTH_TEST);ctx.depth_func='<='
        ctx.front_face='ccw';ctx.cull_face='back'
        P=persp(math.radians(fov),960/544,10,12000);V=view(np.array(pos,float),yaw,pitch);MVP=P@V
        s.draw(MVP,lmon,only,hl,cam=np.array(pos,float),P=P,Vm=V)
        im=Image.frombytes('RGBA',(s.W*s.ss,s.H*s.ss),s.fbo.read(components=4)).transpose(Image.FLIP_TOP_BOTTOM).convert('RGB')
        if s.ss>1 and not raw: im=im.resize((s.W,s.H),Image.BOX)
        if out: im.save(out)
        return im
if __name__=='__main__':
    stem=sys.argv[1];x,y,z,yaw,pitch=map(float,sys.argv[2:7]);out=sys.argv[7]
    R=Renderer();R.load(stem);R.shot((x,y,z),math.radians(yaw),math.radians(pitch),out)

def pick(R,pos,yaw,pitch,pts,fov=59):
    """mesh index under each (x,y) screen point (960x544 coords)"""
    R.idmode=1
    im=R.shot(pos,yaw,pitch,fov=fov,lmon=False,raw=True);R.idmode=0
    a=np.asarray(im).astype(int)[::R.ss,::R.ss];out=[]
    for x,y in pts:
        p=a[y,x]; out.append(p[0]+p[1]*256 if p[2]==255 else -1)
    return out,a
