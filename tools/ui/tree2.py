import hudmv as M,re,sys
mv=M.main() if sys.argv[1]!='D' else M.dmovie()
def rec(path,depth,maxd):
    k=mv.children(path)
    shown={}
    for nm,cid in k:
        mm=re.match(r'(icon|no|zero|record|targetPoint)(\d+)$',nm)
        if mm:
            shown[mm.group(1)]=shown.get(mm.group(1),0)+1
            if shown[mm.group(1)]>1: continue
            nm2=nm+' (+%d)'%(sum(1 for n,_ in k if re.match(mm.group(1)+r'\d+$',n))-1)
        else: nm2=nm
        c=mv.ch.get(cid,{}); kind=c.get('kind')
        info=''
        if kind=='text': info=repr(re.sub('<[^>]+>','',c.get('text',''))[:30])+' b=%s'%c.get('bounds')
        if kind=='bitmap': info='bmp %s'%(c['img'].shape,)
        print('  '*depth+nm2,cid,kind,mv.sym.get(cid) or '',info)
        if kind=='sprite' and depth<maxd and not mm: rec(path+'.'+nm,depth+1,maxd)
rec(sys.argv[2],0,int(sys.argv[3]) if len(sys.argv)>3 else 5)
