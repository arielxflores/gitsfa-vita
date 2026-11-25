import pickle,re,numpy as np
allev,defs=pickle.load(open('/root/gits/tools/audio/ref/fev.pkl','rb'))
def env_at(pts,x):
    xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
    if x<=xs[0]: return ys[0]
    for i in range(1,len(xs)):
        if x<=xs[i]: 
            t=(x-xs[i-1])/max(1e-9,xs[i]-xs[i-1]); return ys[i-1]+t*(ys[i]-ys[i-1])
    return ys[-1]
def wavname(fn): return fn.split('/')[-1].lower().replace('.wav','').replace(' ','_')
def layer_gain(e,l,s,x=0.0):
    g=e['vol']*s['vol']
    for en in l['envs']:
        if en['dsp']: continue
        if en['flags'] in (0x804,) and en['a2']==0: g*=env_at(en['pts'],x)
        elif en['flags'] in (0xc,0x24): g*=env_at(en['pts'],x)
    return g
rows=[]
for nm,e in allev.items():
    for l in e['layers']:
        for s in l['sis']:
            d=defs[s['sd']]
            g=layer_gain(e,l,s,0.0)
            for w in d[2]:
                if w[0]=='wav': rows.append((wavname(w[2]),nm,round(g,3),d[0]))
pickle.dump(rows,open('/root/gits/tools/audio/ref/wave_gain.pkl','wb'))
if __name__=='__main__':
    import sys
    for pat in sys.argv[1:]:
        seen=set()
        for r in rows:
            if re.search(pat,r[0]) and (r[0].rsplit('_',1)[0],r[1]) not in seen:
                seen.add((r[0].rsplit('_',1)[0],r[1])); print('%-45s %-50s %.3f'%r[:3])
