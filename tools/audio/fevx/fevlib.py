# FMOD Designer 4.44 sac.fev reader (LGCY chunk). Event anchors come from the GUIDs in sac.h.
import struct,re,pickle,os,sys
sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import gitspaths
FMOD=gitspaths.dirp('FMOD')   # Data/audio/Fmod_Build of the install (databuilder: no fixed paths)
d=open(FMOD+"sac.fev",'rb').read()
L=d.find(b'LGCY')+8; Lsz=struct.unpack_from('<I',d,L-4)[0]; G=d[L:L+Lsz]
o=d.find(b'STRR'); sz=struct.unpack_from('<I',d,o+4)[0]; s=d[o+8:o+8+sz]
n=struct.unpack_from('<I',s,0)[0]; offs=struct.unpack_from('<%dI'%n,s,4); base=4+4*n
strs=[s[base+x:s.index(b'\0',base+x)].decode('latin1') for x in offs]
def guid_bytes(g):
    g=g.strip('{}'); a,b,c,dd,e=g.split('-')
    return struct.pack('<IHH',int(a,16),int(b,16),int(c,16))+bytes.fromhex(dd+e)
def events():
    h=open(FMOD+'sac.h',encoding='latin1').read()
    ev=[]
    for m in re.finditer(r'EVENTGUID_SAC_(\w+) = "(\{[0-9a-f-]+\})"',h):
        gb=guid_bytes(m.group(2)); p=G.find(gb)
        if p<0: print('missing',m.group(1)); continue
        ev.append((p-8,m.group(1)))
    ev.sort(); return ev
def u32(p): return struct.unpack_from('<I',G,p)[0]
def i32(p): return struct.unpack_from('<i',G,p)[0]
def f32(p): return struct.unpack_from('<f',G,p)[0]
def u16(p): return struct.unpack_from('<H',G,p)[0]
e=d.find(b'EPRP'); esz=struct.unpack_from('<I',d,e+4)[0]; E=d[e+8:e+8+esz]
npts=struct.unpack_from('<I',E,0)[0]
PTS=[struct.unpack_from('<ffI',E,4+12*i) for i in range(npts)]
def rd_str(p):
    n=u32(p); return G[p+4:p+4+n-1].decode('latin1'), p+4+n
def parse_event(a):
    """complex event (type 8) at a -> dict, end offset"""
    ev={'type':u32(a),'name':strs[u32(a+4)],'off':a}
    hdr=[u32(a+i) for i in range(0,168,4)]
    ev['vol']=f32(a+24); ev['pitch']=f32(a+28); ev['pitch_rand']=f32(a+32); ev['vol_rand']=f32(a+36)
    ev['w40']=u32(a+40); ev['w44']=u32(a+44); ev['w48']=u32(a+48); ev['mode']=u32(a+52)
    ev['f56']=f32(a+56); ev['f60']=f32(a+60); ev['f64']=f32(a+64); ev['f68']=f32(a+68); ev['w72']=u32(a+72)
    ev['spk']=[f32(a+76+4*i) for i in range(8)]
    ev['f108']=[f32(a+108),f32(a+112),f32(a+116)]; ev['w120']=u32(a+120); ev['f124']=f32(a+124)
    ev['rev']=(f32(a+128),f32(a+132)); ev['w136']=u32(a+136); ev['fade']=(u32(a+140),u32(a+144))
    ev['spawn']=(f32(a+148),f32(a+152)); ev['f156']=f32(a+156); ev['w160']=(u32(a+160),u32(a+164))
    p=a+168; nl=u32(p); p+=4; layers=[]
    for li in range(nl):
        fl=u16(p); pr=struct.unpack_from('<h',G,p+2)[0]; cp=u16(p+4); nsi=u16(p+6); nen=u16(p+8); p+=10
        sis=[]
        for k in range(nsi):
            sd=u16(p); vals=struct.unpack_from('<ffIIiIfffffffII',G,p+2)
            # start,len,startmode,loopmode,loopcount,autopitch,apref,apmin,finetune,vol,xin,xout,xintype,xouttype  (one spare)
            sis.append({'sd':sd,'start':vals[0],'len':vals[1],'smode':vals[2],'lmode':vals[3],'lcount':vals[4],'vol':vals[9],'raw':vals}); p+=58
        envs=[]
        for k in range(nen):
            a0=i32(p); dsp,p=rd_str(p+4); a2=u32(p); fl2=u32(p+4); a4=u32(p+8); np_=u32(p+12); p+=16
            pts=[PTS[u32(p+4*j)] for j in range(np_)]; p+=4*np_
            t0=u32(p); t1=u32(p+4); p+=8
            envs.append({'a0':a0,'dsp':dsp,'a2':a2,'flags':fl2,'a4':a4,'pts':pts,'t':(t0,t1)})
        layers.append({'flags':fl,'prio':pr,'cparam':cp,'sis':sis,'envs':envs})
    ev['layers']=layers
    npar=u32(p); p+=4; params=[]
    for k in range(npar):
        w=struct.unpack_from('<IfffIIII',G,p); p+=32
        sus=[f32(p+4*j) for j in range(w[7])]; p+=4*w[7]
        params.append({'name':strs[w[0]],'vel':w[1],'min':w[2],'max':w[3],'flags':w[4],'w5':w[5],'nenv':w[6],'sustain':sus})
    ev['params']=params
    nup=u32(p); p+=4; ups={}
    for k in range(nup):
        nm,p=rd_str(p); t=u32(p); p+=4
        if t==0: ups[nm]=i32(p); p+=4
        elif t==1: ups[nm]=f32(p); p+=4
        else: v,p=rd_str(p); ups[nm]=v
    ev['props']=ups
    ev['one']=u32(p); p+=4
    ev['cat'],p=rd_str(p)
    return ev,p
def sounddefs():
    # templates: count + 0x4c-byte records follow the last event; defs: count, then name idx, template idx, nwaves, waves
    p=G.find(b'fmod_resouce/')
    # walk back: wave = type u32, weight u32, strlen u32, string; def = name u32, tmpl u32, nw u32
    p0=p-4-4-4-12   # start of first def (name,tmpl,nw,type,weight,len)
    n=u32(p0-4); q=p0; defs=[]
    for i in range(n):
        nm=u32(q); tm=u32(q+4); nw=u32(q+8); q+=12; ws=[]
        for k in range(nw):
            t=u32(q); wt=u32(q+4); q+=8
            if t==0:
                fn,q=rd_str(q); bank=u32(q); idx=u32(q+4); ms=u32(q+8); q+=12; ws.append(('wav',wt,fn,bank,idx,ms))
            elif t==1:   # oscillator
                ws.append(('osc',wt,u32(q),f32(q+4))); q+=8
            elif t==2:   # don't play / silence
                ws.append(('null',wt)); 
            elif t==3: ws.append(('prog',wt))
            else: raise Exception('wave type %d at %x'%(t,q))
        defs.append((strs[nm] if nm<len(strs) else nm,tm,ws))
    return defs,q
def rd_props(p):
    n=u32(p); p+=4; ups={}
    for k in range(n):
        nm,p=rd_str(p); t=u32(p); p+=4
        if t==0: ups[nm]=i32(p); p+=4
        elif t==1: ups[nm]=f32(p); p+=4
        else: v,p=rd_str(p); ups[nm]=v
    return ups,p
def walk_groups():
    """-> list of (path, event dict) in file order, and end offset"""
    p=0x83e; ng=u32(p); p+=4; out=[]
    def grp(p,path):
        nm=strs[u32(p)]; p+=4
        props,p=rd_props(p)
        nsub=u32(p); nev=u32(p+4); p+=8
        for i in range(nsub): p=grp(p,path+nm+'/')
        for i in range(nev):
            t=u32(p)
            if t!=8: raise Exception('event type %d at %x in %s'%(t,p,path+nm))
            e,p=parse_event(p); out.append((path+nm+'/'+e['name'],e))
        return p
    for i in range(ng): p=grp(p,'')
    return out,p

# b107: the project's reverb definitions (FMOD Designer "Reverbs" tab: one per ambience zone, named like the ambience beds:
# shooting_range, train_station_outside, urban_inside ...). In LGCY: u32 count, then per definition a length-prefixed name
# and 132 bytes of properties; the first 48 are the I3DL2 set read here (levels in millibels, times in seconds).
#   -> [(name, dict(room, room_hf, decay, decay_hf, refl, refl_delay, reverb, reverb_delay, diffusion, density, hf_ref))]
def reverb_defs():
    import re
    def rec(p):
        if p+4>len(G): return None
        n=u32(p)
        if not 2<=n<=48 or p+4+n+132>len(G) or G[p+4+n-1]!=0 or not re.fullmatch(rb'[A-Za-z0-9_ ]+',G[p+4:p+4+n-1]): return None
        q=p+4+n; room,room_hf=struct.unpack_from('<ii',G,q); _,dec,dhf=struct.unpack_from('<fff',G,q+8)
        refl=i32(q+20); rd=f32(q+24); rev=i32(q+28); rvd,diff,dens,hf=struct.unpack_from('<ffff',G,q+32)
        if not (0.05<=dec<=30 and 0<=dhf<=4 and 0<=diff<=100 and 0<=dens<=100 and 20<=hf<=24000 and -10000<=room<=0 and -10000<=refl<=2000 and -10000<=rev<=2000 and 0<=rd<=1 and 0<=rvd<=1): return None
        lf=f32(q+48); lfref=f32(q+52)
        return (G[p+4:p+4+n-1].decode(),dict(room_lf=lf,lf_ref=lfref,room=room,room_hf=room_hf,decay=dec,decay_hf=dhf,refl=refl,refl_delay=rd,reverb=rev,reverb_delay=rvd,diffusion=diff,density=dens,hf_ref=hf)),q+132
    best=[]
    p=G.find(b'shooting_range\0')
    starts=range(4,len(G)-80) if p<0 else range(max(4,p-40000),p)
    for s in starts:
        n=u32(s-4)
        if not 1<=n<=1000: continue
        out=[];q=s
        for _ in range(n):
            r=rec(q)
            if r is None: break
            out.append(r[0]);q=r[1]
        if len(out)==n and n>len(best): best=out
    return best
