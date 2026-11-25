import ctypes,numpy as np,os
_H=os.path.dirname(os.path.abspath(__file__))
def _lib():   # databuilder: adpcm.dll on Windows (prebuilt, tools/databuilder/bin), libadpcm.so elsewhere (built from adpcm.c if missing)
    p=os.environ.get('GITSFA_ADPCM_LIB') or os.path.join(_H,'adpcm.dll' if os.name=='nt' else 'libadpcm.so')
    if not os.path.exists(p) and os.name!='nt':
        import subprocess;t=p+'.%d'%os.getpid();subprocess.run(['cc','-O2','-shared','-fPIC','-o',t,os.path.join(_H,'adpcm.c')],check=True);os.replace(t,p)
    return ctypes.CDLL(p)
L=_lib()
BF=1024
def bb(bf=BF): return 4+bf//2
def encode(a,bf=BF):
    """a: int16 (n,ch) -> bytes"""
    a=np.ascontiguousarray(a,dtype='<i2'); n,ch=a.shape; nblk=(n+bf-1)//bf
    out=np.zeros(nblk*ch*bb(bf),'u1')
    L.adpcm_encode(a.ctypes.data_as(ctypes.c_void_p),n,ch,bf,out.ctypes.data_as(ctypes.c_void_p))
    return out.tobytes(),nblk
def decode(data,nblk,ch,bf=BF):
    d=np.frombuffer(data,'u1').copy(); out=np.zeros((nblk*bf,ch),'<i2')
    L.adpcm_decode(d.ctypes.data_as(ctypes.c_void_p),nblk,ch,bf,out.ctypes.data_as(ctypes.c_void_p))
    return out
if __name__=='__main__':
    t=np.arange(48000)/32000.0
    x=(np.sin(2*np.pi*440*t)*12000+np.random.randn(len(t))*300).astype('i2')[:,None]
    x=np.concatenate([x,(x*0.5).astype('i2')],1)
    d,nb=encode(x); y=decode(d,nb,2)[:len(x)]
    e=(y.astype(float)-x)
    print('bytes',len(d),'ratio %.2f bits/sample'%(len(d)*8/x.size),'SNR %.1f dB'%(10*np.log10((x.astype(float)**2).mean()/(e**2).mean())))
