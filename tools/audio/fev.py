# FMOD Designer 4.44 .fev: sound definitions (name -> waveforms: bank index, sample index, length) from the LGCY chunk
import struct,pickle,re
import os,sys;sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'));import gitspaths
F=gitspaths.path('FMOD','sac.fev')
def load():
    d=open(F,'rb').read()
    o=d.find(b'STRR'); sz=struct.unpack_from('<I',d,o+4)[0]; s=d[o+8:o+8+sz]
    n=struct.unpack_from('<I',s,0)[0]; offs=struct.unpack_from('<%dI'%n,s,4); base=4+4*n
    strs=[s[base+x:s.index(b'\0',base+x)].decode('latin1') for x in offs]
    return d,strs
def sounddefs():
    d,strs=load(); out={}
    # every waveform entry: type u32, weight u32, len u32, name, bank u32, index u32, ms u32
    for m in re.finditer(rb'fmod_resouce/[^\0]{1,200}\.wav\0',d):
        st=m.start(); ln=struct.unpack_from('<I',d,st-4)[0]
        if ln!=m.end()-st: continue
        bank,idx,ms=struct.unpack_from('<3I',d,m.end())
        wav=m.group()[:-1].decode('latin1')
        out.setdefault('waves',[]).append((st-12,wav,bank,idx,ms))
    # walk back from each wave to its sounddef header: header = name_idx u32, u32, nwaves u32 then waves
    waves=out['waves']; defs=[]; i=0
    pos2w={w[0]:k for k,w in enumerate(waves)}
    k=0
    while k<len(waves):
        p=waves[k][0]
        nm,unk,nw=struct.unpack_from('<3I',d,p-12)
        if nw>=1 and nm<len(strs) and k+nw<=len(waves):
            ws=waves[k:k+nw]; defs.append((strs[nm],[(w[1],w[2],w[3],w[4]) for w in ws])); k+=nw
        else:
            defs.append(('?',[(waves[k][1],waves[k][2],waves[k][3],waves[k][4])])); k+=1
    return defs,strs
if __name__=='__main__':
    defs,strs=sounddefs()
    print(len(defs),'sounddefs',sum(len(w) for _,w in defs),'waves', sum(1 for n,_ in defs if n=='?'),'unresolved')
    for n,w in defs[:5]: print(n,w[:3])
    banks=sorted(set(b for _,ws in defs for _,b,_,_ in ws)); print('bank ids',banks)
    pickle.dump(defs,open(gitspaths.tmp('sounddefs.pkl'),'wb'))
    i=strs.index('_el_jl_kl'); print(strs[:12])
