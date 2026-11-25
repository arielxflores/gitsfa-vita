import pickle
evs,defs=pickle.load(open('/root/gits/tools/audio/ref/fev_paths.pkl','rb'))
META=[list(pickle.load(open('/root/fsbdec/pcm/sac_bank%02d/meta.pkl'%b,'rb')).items()) for b in range(10)]
def fsbname(bank,idx):
    k,(fr,ch,n)=META[bank][idx]; return k.lower().replace(' ','_').replace('.wav','')
def def_waves(sd):
    """[(fsb sample name, weight)] + null weight"""
    out=[]; nul=0
    for w in defs[sd][2]:
        if w[0]=='wav': out.append((fsbname(w[3],w[4]),w[1],w[3]))
        elif w[0]=='null': nul+=w[1]
    return out,nul
