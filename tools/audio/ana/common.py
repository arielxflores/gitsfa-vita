import numpy as np, pickle, os, sys
from scipy.io import wavfile
from scipy.signal import resample_poly, fftconvolve
from fractions import Fraction
REF='/mnt/user-data/uploads/vita-agent-use/gitsfa/audioref/'
PCM='/root/fsbdec/pcm/'
def load_ref(name):
    sr,a=wavfile.read(REF+name+'.wav'); return sr,a.astype('f4')/32768
_meta={}
def bank_of(n):
    for b in range(10):
        bk='sac_bank%02d'%b
        if bk not in _meta: _meta[bk]=pickle.load(open(PCM+bk+'/meta.pkl','rb'))
        for k in _meta[bk]:
            if k.lower().replace(' ','_').replace('.wav','')==n.lower(): return bk,k,_meta[bk][k]
    return None
def load_snd(n,sr=22050,mono=True):
    r=bank_of(n)
    if r is None: raise KeyError(n)
    bk,k,(fr,ch,ns)=r
    a=np.load(PCM+bk+'/'+k+'.npy').astype('f4')/32768
    if mono and a.ndim==2: a=a.mean(1)
    if fr!=sr:
        f=Fraction(sr,fr).limit_denominator(1000); a=resample_poly(a,f.numerator,f.denominator,axis=0)
    return a
def env(x,sr,win=0.02):
    n=int(sr*win); m=len(x)//n
    return np.sqrt((x[:m*n].reshape(m,n)**2).mean(1)+1e-12)
def db(x): return 20*np.log10(np.maximum(x,1e-9))
def ncc(sig,tpl):
    """normalized cross-correlation of tpl over sig (mono), returns array len(sig)-len(tpl)+1"""
    tpl=tpl-tpl.mean(); tn=np.sqrt((tpl**2).sum())+1e-9
    num=fftconvolve(sig,tpl[::-1],mode='valid')
    s2=np.concatenate([[0],np.cumsum(sig.astype('f8')**2)])
    s1=np.concatenate([[0],np.cumsum(sig.astype('f8'))])
    L=len(tpl); e=s2[L:]-s2[:-L]-(s1[L:]-s1[:-L])**2/L
    return num/(tn*np.sqrt(np.maximum(e,1e-12)))
from scipy.signal import stft
def spec(x,sr,n=1024,hop=256,fmin=150,fmax=10000):
    f,t,Z=stft(x,sr,nperseg=n,noverlap=n-hop,boundary=None,padded=False)
    k=(f>=fmin)&(f<=fmax)
    # mel-ish compression: group bins into 48 log-spaced bands
    fb=f[k]; edges=np.geomspace(fmin,fmax,49); S=np.abs(Z[k])**2
    B=np.stack([S[(fb>=edges[i])&(fb<edges[i+1])].sum(0) for i in range(48)])
    return np.log(B+1e-10).astype('f4')   # bands x frames
def spec_match(Ssig,Stpl):
    """normalized correlation of template spectrogram (bands x T) slid over signal spectrogram; returns per-frame score"""
    T=Stpl.shape[1]; A=Stpl-Stpl.mean(); an=np.sqrt((A**2).sum())
    n=Ssig.shape[1]-T+1; out=np.zeros(n,'f4')
    # sliding: use convolution per band
    num=np.zeros(n); s1=np.zeros(n); s2=np.zeros(n)
    ones=np.ones(T)
    for b in range(Ssig.shape[0]):
        x=Ssig[b].astype('f8')
        num+=np.convolve(x,A[b][::-1],'valid')
        s1+=np.convolve(x,ones,'valid'); s2+=np.convolve(x*x,ones,'valid')
    N=T*Ssig.shape[0]; var=s2-s1*s1/N
    return num/(an*np.sqrt(np.maximum(var,1e-9)))
