from common import *
import re,sys
sr=8000
meta=pickle.load(open(PCM+'sac_bank06/meta.pkl','rb'))
names=[k.lower().replace('.wav','') for k in meta]
sel=[n for n in names if re.match(r'(d_aramaki|d_op|k_|d_[a-z]+_)',n)]
for vid in sys.argv[1:]:
    s0,a=load_ref(vid); m=resample_poly(a.mean(1),80,221).astype('f4')
    hits=[]
    for n in sel:
        x=load_snd(n,sr); tp=x[:int(1.2*sr)]
        if len(tp)<int(0.3*sr): continue
        c=ncc(m,tp)
        # local energy floor to avoid silent-region artefacts
        for i in np.nonzero(c>0.45)[0]:
            if c[i]==c[max(0,i-sr):i+sr].max():
                seg=m[i:i+len(tp)]
                if np.sqrt((seg**2).mean())<1e-3: continue
                g=(seg*tp).sum()/(tp*tp).sum()
                hits.append((i/sr,n,round(float(c[i]),2),round(float(20*np.log10(abs(g)+1e-9)),1)))
    hits.sort()
    with open('/tmp/claude-0/-home-claude/bd5c4ae5-d28e-519d-bb5f-d5d54181e990/scratchpad/voices_%s.txt'%vid,'w') as f:
        for h in hits: f.write('%8.2f %-40s ncc %.2f gain %+.1f dB\n'%h)
    print(vid,len(hits),flush=True)
