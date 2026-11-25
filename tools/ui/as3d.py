# Matrix3D placements that the Flash HUD scripts apply to named children (fl.motion AnimatorFactory3D setup code)
import re,glob,os,numpy as np
def load(scripts_dir):
    out={}   # (class, child) -> 4x4 (column-major vec as Flash: indices 12,13,14 = translation)
    for f in glob.glob(scripts_dir+'/**/*.as',recursive=True):
        s=open(f,encoding='utf8',errors='ignore').read()
        m=re.search(r'class (\w+)',s)
        if not m: continue
        cls=m.group(1)
        for vec in set(re.findall(r'____motion_(\w+?)af\d+_mat3DVec__\[0\]',s)):
            vals=[0.0]*16
            for i,v in re.findall(r'____motion_%saf\d+_mat3DVec__\[(\d+)\] = ([-\d.e]+);'%re.escape(vec),s): vals[int(i)]=float(v)
            out[(cls,vec)]=np.array(vals).reshape(4,4)   # rows = Flash raw data order (column vectors stored consecutively)
    return out
if __name__=='__main__':
    import sys
    M=load(sys.argv[1]); print(len(M))
    for (c,n),v in sorted(M.items()):
        if len(sys.argv)>2 and sys.argv[2] not in c: continue
        r=v[:3,:3]; t=v[3,:3]
        ang=np.degrees(np.arccos(np.clip(np.trace(r)/np.cbrt(max(abs(np.linalg.det(r)),1e-9))/1.0*0.5-0.5,-1,1)))
        print(c,n,'t',t.round(1),'rot~%.1f deg'%ang, 'scale %.2f'%np.cbrt(abs(np.linalg.det(r))))
