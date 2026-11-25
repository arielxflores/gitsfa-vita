import numpy as np,struct,sys,os,wave
rev=np.array([int('{:08b}'.format(i)[::-1],2) for i in range(256)],'u1')
KEY=b'%lAn2{Pi*Lhw3T}@7*!kV=?qS$@iNlJ'
FREQ={1:8000,2:11000,3:11025,4:16000,5:22050,6:24000,7:32000,8:44100,9:48000}
def load(path):
    c=rev[np.frombuffer(open(path,'rb').read(),'u1')]
    k=np.frombuffer(KEY*(len(c)//31+1),'u1')[:len(c)];return (c^k).tobytes()
def parse(d):
    ver,ns,shs,nts,ds,mode=struct.unpack_from('<6I',d,4);hdr=60 if ver==1 else 64
    o=hdr;smp=[]
    for i in range(ns):
        v=struct.unpack_from('<Q',d,o)[0];o+=8
        nxt=v&1;fi=(v>>1)&0xF;ch=((v>>5)&3)+1;doff=((v>>7)&0x7FFFFFF)<<5;nsamp=(v>>34)&0x3FFFFFFF
        freq=FREQ.get(fi,44100)
        while nxt:
            c=struct.unpack_from('<I',d,o)[0];o+=4;nxt=c&1;size=(c>>1)&0xFFFFFF;typ=(c>>25)&0x7F
            if typ==2: freq=struct.unpack_from('<I',d,o)[0]
            if typ==1: ch=d[o]
            o+=size
        smp.append(dict(freq=freq,ch=ch,off=doff,n=nsamp))
    names=d[hdr+shs:hdr+shs+nts];base=hdr+shs+nts
    for i,s in enumerate(smp):
        no=struct.unpack_from('<I',names,4*i)[0];s['name']=names[no:names.index(b'\0',no)].decode('latin1')
        end=smp[i+1]['off'] if i+1<ns else ds
        s['data']=d[base+s['off']:base+end]
    return mode,smp
STEP=[7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
IDX=[-1,-1,-1,-1,2,4,6,8]*2
def ima_xbox(data,ch,n):
    # Xbox IMA: per block 0x24 bytes per channel; header s16 sample, u8 index, pad; 8 interleaved u32 words per channel
    out=[[] for _ in range(ch)];bs=36*ch;p=0
    while p+bs<=len(data):
        hist=[];idx=[]
        for c in range(ch):
            s,i=struct.unpack_from('<hb',data,p+4*c);hist.append(s);idx.append(max(0,min(i,88)));out[c].append(s)
        q=p+4*ch;cnt=[0]*ch
        for w in range(8):
            for c in range(ch):
                word=data[q:q+4];q+=4
                for b in word:
                    for nib in (b&15,b>>4):
                        cnt[c]+=1
                        if cnt[c]>63: continue   # 64 samples per block: header sample + 63 nibbles (last nibble is padding)
                        st=STEP[idx[c]];diff=st>>3
                        if nib&1:diff+=st>>2
                        if nib&2:diff+=st>>1
                        if nib&4:diff+=st
                        if nib&8:diff=-diff
                        hist[c]=max(-32768,min(32767,hist[c]+diff));idx[c]=max(0,min(88,idx[c]+IDX[nib]))
                        out[c].append(hist[c])
        p+=bs
    a=np.array(out,'i2').T[:n] if out[0] else np.zeros((0,ch),'i2')
    return a
if __name__=='__main__':
    mode,smp=parse(load(sys.argv[1]))
    pat=sys.argv[2] if len(sys.argv)>2 else ''
    for s in smp:
        if pat and pat not in s['name'].lower(): continue
        print(s['name'],s['freq'],s['ch'],s['n'],len(s['data']), 'expected bytes',(s['n']+64)//65*36*s['ch'],(s['n']+63)//64*36*s['ch'])

def decode(s,mode):
    """Decode one sample. Only IMA ADPCM (codec 7) is decoded here; MPEG banks (music, voice, foley)
    must go through vgmstream. Refusing anything else stops a wrong codec from turning into noise."""
    if mode!=7: raise ValueError('codec %d is not IMA ADPCM: decode %s with vgmstream'%(mode,s['name']))
    return ima_xbox(s['data'],s['ch'],s['n'])
