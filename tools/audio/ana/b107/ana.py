import numpy as np,wave,sys,glob
from scipy import signal
def rd(p):
    w=wave.open(p);d=np.frombuffer(w.readframes(w.getnframes()),'<i2').reshape(-1,w.getnchannels()).astype(np.float32)/32768;return d.mean(1),w.getframerate()
V,sr=rd('/tmp/claude-0/sr/showcase.wav')
def bands(x,sr=48000):
    out={}
    for nm,(lo,hi) in dict(low=(60,400),mid=(400,2500),high=(2500,10000)).items():
        b=signal.butter(4,[lo,hi],btype='band',fs=sr,output='sos');out[nm]=signal.sosfilt(b,x)
    out['all']=x;return out
def edc(x,sr=48000,hop=480):
    m=len(x)//hop;return 10*np.log10((x[:m*hop].reshape(m,hop)**2).mean(1)+1e-12)
def onset(x,sr=48000):
    e=edc(x,sr,48);pk=e.max();i=int(np.argmax(e>pk-20));return i*48
def shot(t,dur=2.6):
    s=V[int((t-0.3)*sr):int((t+dur)*sr)];o=onset(s[:int(0.8*sr)]);return s[o:o+int(dur*sr)-int(0.8*sr)]
if __name__=='__main__':
    ts=[float(a) for a in sys.argv[1].split(',')]
    S=[shot(t) for t in ts]
    refs={p[:-4]:rd(p)[0] for p in sorted(glob.glob('sr_*.wav'))}
    for k in refs: o=onset(refs[k]);refs[k]=refs[k][o:]
    def spec(x): 
        f,P=signal.welch(x[:int(0.25*48000)],48000,nperseg=2048);return np.log10(P[2:400]+1e-12)
    for t,s in zip(ts,S):
        sc=sorted((float(np.corrcoef(spec(s),spec(r))[0,1]),k) for k,r in refs.items())[::-1]
        print(t,'peak %.2f'%abs(s).max(),' '.join('%s %.3f'%(k[3:],c) for c,k in sc[:5]))
