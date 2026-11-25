# host-side .gsky loader/renderer (mirrors world.h's sky pass)
import struct,numpy as np,moderngl,math
import gmapio
SVS='''#version 330
in vec3 pos; in vec2 uv; uniform mat4 mvp; uniform float far; uniform vec2 scroll;
out vec2 t0;
void main(){ vec4 p=mvp*vec4(pos,1.0); if (far>0.5) p.z=p.w*0.99999; gl_Position=p; t0=uv+scroll; }'''
SFS='''#version 330
in vec2 t0; uniform sampler2D tex; uniform sampler2D lut; uniform vec4 tint; uniform vec4 fogc; uniform float fog; uniform float grade; uniform vec4 gk[10];
out vec4 o;
void main(){ vec4 c=texture(tex,t0)*tint; vec3 fc=mix(c.rgb,fogc.rgb,fog); vec3 gc=fc;
 if (grade>1.5){ vec3 x=clamp(fc,0.0,1.0); vec3 y=x*x; vec3 z=y*x;
   gc=gk[9].xyz+gk[0].xyz*x.x+gk[1].xyz*x.y+gk[2].xyz*x.z+gk[3].xyz*y.x+gk[4].xyz*y.y+gk[5].xyz*y.z+gk[6].xyz*z.x+gk[7].xyz*z.y+gk[8].xyz*z.z; }
 else if (grade>0.5){ vec3 q=clamp(fc,0.0,1.0)*15.0; float b=min(floor(q.z),14.0); float f=q.z-b;
   vec2 u=vec2((q.x+0.5+b*16.0)/256.0,(q.y+0.5)/16.0); gc=mix(texture(lut,u).rgb,texture(lut,u+vec2(0.0625,0.0)).rgb,f);}
 o=vec4(gc,c.a); }'''
class Sky:
    def __init__(s,R,path,t=0.0):
        ctx=R.ctx;s.prog=ctx.program(vertex_shader=SVS,fragment_shader=SFS);s.t=t
        d=open(path,'rb').read();mg,ver,nl,nt=struct.unpack_from('<4sIII',d,0);o=16;s.texs=[]
        for i in range(nt):
            w,h,fmt,n=struct.unpack_from('<HHBB',d,o);o+=6;lv=[]
            for k in range(n): sz=struct.unpack_from('<I',d,o)[0];o+=4;lv.append(d[o:o+sz]);o+=sz
            tx=ctx.texture((w,h),4,gmapio.dds_decode(w,h,lv[0],fmt).tobytes())
            if n>1: tx.build_mipmaps(0,n-1)
            for k in range(1,n): tx.write(gmapio.dds_decode(max(1,w>>k),max(1,h>>k),lv[k],fmt).tobytes(),level=k)
            tx.filter=(moderngl.LINEAR_MIPMAP_LINEAR if n>1 else moderngl.LINEAR,moderngl.LINEAR);tx.repeat_x=tx.repeat_y=True;s.texs.append(tx)
        s.layers=[]
        for i in range(nl):
            tex,flags=struct.unpack_from('<iI',d,o);o+=8;tint=struct.unpack_from('<4f',d,o);o+=16;su,sv,rot,fog=struct.unpack_from('<4f',d,o);o+=16
            nv,ni=struct.unpack_from('<II',d,o);o+=8
            V=np.frombuffer(d,'<f4',nv*5,o).reshape(nv,5);o+=nv*20;I=np.frombuffer(d,'<u2',ni,o).astype('u4');o+=ni*2+(2 if ni%2 else 0)
            vbo=ctx.buffer(V.astype('f4').tobytes());ibo=ctx.buffer(I.tobytes())
            s.layers.append(dict(tex=tex,flags=flags,tint=tint,scroll=(su,sv),rot=rot,fog=fog,vao=ctx.vertex_array(s.prog,[(vbo,'3f 2f','pos','uv')],ibo)))
    def draw(s,R,P,V,cam,world=False):
        ctx=R.ctx;pr=s.prog;pr['tex'].value=0;pr['lut'].value=2;R.lut.use(2);pr['grade'].value=R.grade;pr['fogc'].value=R.fog;pr['gk'].write(R.gk.astype('f4').tobytes())
        ctx.depth_mask=False;ctx.disable(moderngl.CULL_FACE)
        for L in s.layers:
            far=bool(L['flags']&4)
            if far==world: continue
            M=V.copy()
            if far: M[:3,3]=0          # centred on the camera
            a=L['rot']*s.t;Rz=np.eye(4);Rz[0,0]=Rz[1,1]=math.cos(a);Rz[0,1]=-math.sin(a);Rz[1,0]=math.sin(a)
            pr['mvp'].write((P@M@Rz).T.astype('f4').tobytes());pr['far'].value=1.0 if far else 0.0
            pr['scroll'].value=(L['scroll'][0]*s.t,L['scroll'][1]*s.t);pr['tint'].value=tuple(L['tint']);pr['fog'].value=L['fog']
            if L['flags']&1: ctx.enable(moderngl.BLEND);ctx.blend_func=(moderngl.SRC_ALPHA,moderngl.ONE)
            elif L['flags']&2: ctx.enable(moderngl.BLEND);ctx.blend_func=(moderngl.SRC_ALPHA,moderngl.ONE_MINUS_SRC_ALPHA)
            else: ctx.disable(moderngl.BLEND)
            pr['grade'].value=R.grade if not (L['flags']&3) else 0.0
            s.texs[L['tex']].use(0);L['vao'].render()
        ctx.disable(moderngl.BLEND);ctx.depth_mask=True;ctx.enable(moderngl.CULL_FACE)
