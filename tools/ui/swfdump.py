import swf,sys
S=swf.load(sys.argv[1]); ch=swf.characters(S); sym=swf.symbols(S)
def show(cid,ind=0,maxd=3,fr=0):
    c=ch.get(cid)
    if c is None: print(' '*ind+'%d ?'%cid); return
    k=c['kind']
    if k=='sprite':
        print(' '*ind+'sprite %d %s frames %d labels %s'%(cid,sym.get(cid,''),len(c['frames']),dict(list(c['labels'].items())[:12])))
        if ind//2>=maxd or not c['frames']: return
        dl=c['frames'][min(fr,len(c['frames'])-1)]
        for dep in sorted(dl):
            p=dl[dep]; m=p.get('m',(1,0,0,1,0,0))
            print(' '*(ind+2)+'d%d %s id=%s name=%s m=(%.2f %.2f %.2f %.2f %.1f %.1f)%s%s'%(dep,'CLIP->%d'%p['clip'] if 'clip' in p else '',p.get('id'),p.get('name'),*m,' cx' if 'cx' in p else '',' f'+str(p.get('filters')) if p.get('filters') else ''))
            show(p.get('id'),ind+4,maxd)
    elif k=='shape':
        fills=[f['t']+('#%d'%f['id'] if f['t']=='bmp' else '') for fs in c['fills'] for f in fs]
        print(' '*ind+'shape %d bounds %s fills %s edges %d'%(cid,[round(v) for v in c['bounds']],fills,len(c['edges'])))
    elif k=='text': print(' '*ind+'text %d var=%s text=%r size=%s align=%s'%(cid,c.get('var'),c.get('text','')[:40],c.get('size'),c.get('align')))
    else: print(' '*ind+k,cid)
if __name__=='__main__':
    show(int(sys.argv[2]),0,int(sys.argv[3]) if len(sys.argv)>3 else 3,int(sys.argv[4]) if len(sys.argv)>4 else 0)
