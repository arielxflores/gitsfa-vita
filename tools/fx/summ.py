import nifps,sys,numpy as np,json
NONE=nifps.NONE
def fmt(x):
    if isinstance(x,float): return round(x,3)
    if isinstance(x,(list,tuple)): return [fmt(y) for y in x]
    if isinstance(x,dict): return {k:fmt(v) for k,v in x.items()}
    return x
def summ(path):
    h,B=nifps.parse(path);out=[]
    def props(b):
        r={}
        for p in b.get('props',[]):
            q=B[p]
            if q['type']=='NiAlphaProperty': r['blend']=q['blend'][1]+'/'+q['blend'][2] if q['blend'][0] else 'opaque';r['atest']=q['test']
            if q['type']=='NiTexturingProperty':
                if 'base' in q: r['tex']=B[q['base']].get('file')
                if q['ctrl']!=NONE: r['flip']=ctl(q['ctrl'])
            if q['type']=='NiMaterialProperty':
                r['mat']=dict(emis=q['emis'],alpha=q['alpha'],dif=q['dif'])
                if q['ctrl']!=NONE: r['mat_ctl']=ctl(q['ctrl'])
            if q['type']=='NiZBufferProperty': r['z']=q['pflags']
        return r
    def ctl(c):
        L=[]
        while c!=NONE:
            q=B[c];e=dict(t=q['type'],start=q['start'],stop=q['stop'],flags=q['flags'])
            if q.get('tname'): e['tname']=q['tname']
            if q['type']=='NiFlipController': e['n']=len(q['sources']);e['files']=[B[s]['file'] for s in q['sources']][:2]
            i=q.get('interp',NONE)
            if i!=NONE:
                ib=B[i];e['ival']=ib.get('value')
                if ib.get('data',NONE)!=NONE:
                    db=B[ib['data']];e['keys']=[(k['t'],k['v']) for k in db.get('keys',[])] if 'keys' in db else {k:v for k,v in db.items() if k in('trans','scale','rot','xyz')}
                    if len(e['keys'])>8 and isinstance(e['keys'],list): e['keys']=e['keys'][:3]+['...',len(e['keys'])]+e['keys'][-2:]
            if q['type']=='NiPSEmitParticlesCtlr':
                ab=B[q['active_interp']];e['active']=[(k['t'],k['v']) for k in B[ab['data']]['keys']] if ab['data']!=NONE else ab['value']
            L.append(e);c=q['next']
        return L
    def walk(k,M,depth):
        b=B[k];t=b['type']
        if 'r' not in b: return
        L=np.eye(4);L[:3,:3]=np.array(b['r']).reshape(3,3)*b['s'];L[:3,3]=b['t'];W=M@L
        ext={B[e]['name']:B[e].get('val') for e in b.get('extra',[])}
        rec=dict(depth=depth,type=t,name=b['name'],pos=W[:3,3].tolist(),scale=float(np.linalg.norm(W[:3,0])),hidden=b['flags']&1,**props(b))
        if ext: rec['extra']=ext
        if b['ctrl']!=NONE and t!='NiPSParticleSystem': rec['ctl']=ctl(b['ctrl'])
        if t=='NiBillboardNode': rec['bb']=b['bbmode']
        if t=='NiMesh':
            for sd in nifps.stream_pos(h,B,k):
                if 'P' in sd: P=sd['P'];rec['ext_lo']=P.min(0).tolist();rec['ext_hi']=P.max(0).tolist();rec['nv']=len(P)
                if 'UV' in sd: rec['uv']=[sd['UV'].min(0).tolist(),sd['UV'].max(0).tolist()]
        if t=='NiPSParticleSystem':
            rec['ctl']=ctl(b['ctrl']) if b['ctrl']!=NONE else None
            rec['maxp']=b['maxp'];rec['world']=b['world'];rec['methods']=(b['normal_method'],b['up_method'],b['normal_dir'],b['up_dir'])
            for m in b['mods']:
                q=B[m]
                if q['type']=='NiPSAlignedQuadGenerator': rec['quad']={x:q[x] for x in('frames_x','frames_y','init_frame','init_frame_var','nframes','nframes_var','t0','t1','scale_amt_u','scale_amt_v','scale_lim_u','scale_lim_v','scale_rest_u','scale_rest_v','center_u','center_v','uv_scroll','pingpong')}
                elif q['type']=='NiPSFacingQuadGenerator': rec['quad']='facing'
                if q['type']=='NiPSSimulator':
                    for s in q['steps']:
                        st=B[s]
                        if st['type']=='NiPSSimulatorGeneralStep': rec['gen']={x:st[x] for x in('size_keys','size_loop','color_keys','color_loop','rot_keys','grow','shrink')}
                        if st['type']=='NiPSSimulatorForcesStep': rec['forces']=[{x:y for x,y in B[f].items() if x not in('_used','_size','range','falloff','obj')} for f in st['forces']]
                        if st['type']=='NiPSSimulatorCollidersStep' and st['colliders']: rec['colliders']=[B[c]['type'] for c in st['colliders']]
            rec['emitters']=[]
            for e in b['emitters']:
                q={x:y for x,y in B[e].items() if x not in('_used','_size')}
                if 'meshes' in q:
                    q['mesh_ext']=[]
                    for mk in q['meshes']:
                        for sd in nifps.stream_pos(h,B,mk):
                            if 'P' in sd: q['mesh_ext'].append((sd['P'].min(0).tolist(),sd['P'].max(0).tolist(),len(sd['P'])))
                rec['emitters'].append(q)
        out.append(rec)
        for c in b.get('children',[]): walk(c,W,depth+1)
    walk(0,np.eye(4),0);return out
if __name__=='__main__':
    for p in sys.argv[1:]:
        print('#####',p)
        for r in summ(p):
            d=r.pop('depth');print('  '*d+json.dumps(fmt(r),default=str))
