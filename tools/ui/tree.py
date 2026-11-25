import hudlib,re,sys
def rec(path,depth,maxd):
    try: k=hudlib.children(path)
    except Exception as e: print('  '*depth,'ERR',e); return
    icons=[nm for nm,_ in k if re.match(r'(icon|no|zero)\d+$',nm)]
    shown=0
    for nm,cid in k:
        if re.match(r'(icon|no|zero)\d+$',nm):
            shown+=1
            if shown>1: continue
            nm2=nm+' (+%d more %s)'%(len(icons)-1,'...')
        else: nm2=nm
        c=hudlib.ch.get(cid,{}); kind=c.get('kind')
        info=''
        if kind=='text': info=repr(re.sub('<[^>]+>','',c.get('text',''))[:30])
        if kind=='sprite': info='fr=%d'%len(c.get('frames',[]))
        print('  '*depth+nm2,cid,kind,hudlib.sym.get(cid) or '',info)
        if kind=='sprite' and depth<maxd and not nm2.startswith('icon'): rec(path+'.'+nm,depth+1,maxd)
rec(sys.argv[1],0,int(sys.argv[2]) if len(sys.argv)>2 else 5)
