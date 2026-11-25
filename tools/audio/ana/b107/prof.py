import numpy as np,sys
from scipy import signal
import wave
def rd(p):
    w=wave.open(p);d=np.frombuffer(w.readframes(w.getnframes()),'<i2').reshape(-1,w.getnchannels()).astype(np.float32)/32768;return d.mean(1),w.getframerate()
K=(4,8,12,20,30,40,60,80,100,140,190)
def prof(x,sr,at=None):
    b=signal.butter(4,300,btype='high',fs=sr,output='sos');x=signal.sosfilt(b,x)
    hop=sr//200;m=len(x)//hop;env=10*np.log10((x[:m*hop].reshape(m,hop)**2).mean(1)+1e-12)
    i=int(np.argmax(env)) if at is None else int(at*200)-10+int(np.argmax(env[int(at*200)-10:int(at*200)+10]))
    seg=env[i:i+200]-env[i];return env[i],i/200,[seg[k] for k in K if k<len(seg)]
if __name__=='__main__':
    for p in sys.argv[1:]:
        x,sr=rd(p);pk,t,s=prof(x,sr);print('%-28s pk %5.1f at %.2f | %s'%(p,pk,t,' '.join('%4.0f'%v for v in s)))
