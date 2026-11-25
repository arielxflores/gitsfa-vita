# Find where game sounds occur in a footage soundtrack (normalized cross-correlation, FFT).
# usage: audmatch.py footage.npy(mono f32 @22050) pattern [pattern...]  -> prints time, score per hit
# Templates come from the decoded FMOD banks (/root/fsbdec/pcm/<bank>/<name>.npy), rate from build/data/audio_manifest.txt.
import sys,glob,os,re,numpy as np
from scipy.signal import resample_poly
SR=22050;MAXT=4.0
x=np.load(sys.argv[1]).astype(np.float32);N=1<<int(np.ceil(np.log2(len(x)+int(SR*MAXT))))
X=np.fft.rfft(x,N);c2=np.concatenate([[0],np.cumsum(x.astype(np.float64)**2)])
names=[]
for p in sys.argv[2:]:
    for f in sorted(glob.glob('/root/fsbdec/pcm/*/*.npy')):
        n=os.path.basename(f)[:-4]
        if re.fullmatch(p,n,re.I): names.append((n,f))
for n,f in names:
    a=np.load(f).astype(np.float32)
    a=a.mean(1) if a.ndim>1 else a
    r=44100  # decoded bank PCM in /root/fsbdec is always 44.1 kHz
    h=resample_poly(a,SR,r)[:int(SR*MAXT)]/32768;h=h-h.mean();L=len(h);hn=np.linalg.norm(h)
    if hn<1e-6: continue
    num=np.fft.irfft(X*np.conj(np.fft.rfft(h,N)),N)[:len(x)-L]
    en=np.sqrt(np.maximum(c2[L:len(x)]-c2[:len(x)-L],1e-12))[:len(num)]
    c=num/(hn*en);c[en<1e-3]=0
    # peaks: local maxima above threshold, 1 s apart
    th=float(os.environ.get('TH','0.35'));hits=[]
    idx=np.where(c>th)[0]
    for i in idx:
        if hits and i-hits[-1][0]<SR:
            if c[i]>hits[-1][1]: hits[-1]=(i,c[i])
            continue
        hits.append((i,c[i]))
    print('%-40s %5.2fs r%5d  '%(n,len(a)/r,r)+' '.join('%d:%05.2f(%.2f)'%(i/SR//60,i/SR%60,s) for i,s in hits[:40]))
