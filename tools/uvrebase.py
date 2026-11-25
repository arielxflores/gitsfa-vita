# Keep texture coordinates small. Large values (tiling offsets of 30..27000 in some maps) lose precision in the
# Vita's texture units and show up as speckle / mosaic noise. Shifting every triangle's UVs by a whole multiple of
# STEP leaves the picture unchanged (textures repeat) but keeps |uv| < STEP + the triangle's own span.
import numpy as np
STEP=8.0
def rebase(V,I,col=3,step=STEP):
    """V: (n,k) float32 vertices with uv at V[:,col:col+2]; I: flat or (m,3) indices. Returns (V2, I2 flat)."""
    tri=np.asarray(I).reshape(-1,3).astype(np.int64)
    uv=V[:,col:col+2]
    if len(tri)==0 or np.abs(uv).max()<=step: return V,tri.ravel()
    tu=uv[tri]; span=(tu.max(1)-tu.min(1)).max(1)
    sh=np.floor(tu.min(1)/step)*step                          # per-triangle shift
    # broken triangles (a texture repeated 64+ times across one triangle) would only ever show as noise:
    # give them their own vertices and a single texel (key -1 marks them)
    flat=span>64
    tag=np.where(flat,np.arange(len(tri)),-1)
    keys=np.c_[tri.ravel(),np.repeat(sh,3,axis=0),np.repeat(tag,3)]
    uniq,inv=np.unique(keys,axis=0,return_inverse=True)
    V2=V[uniq[:,0].astype(np.int64)].copy(); V2[:,col:col+2]-=uniq[:,1:3].astype(V.dtype)
    fl=uniq[:,3]>=0
    if fl.any():
        V2[fl,col:col+2]=np.mod(V2[fl,col:col+2],1.0)       # all three corners land in one tile...
        T=inv.reshape(-1,3); ft=np.where(flat)[0]
        for k in ft: V2[T[k],col:col+2]=V2[T[k],col:col+2].mean(0)   # ...and on one point
    return V2,inv.ravel()
def split16(V,I,maxv=65535,chunk=20000):
    """yield (V,I) pieces with u16 indices"""
    tri=np.asarray(I).reshape(-1,3)
    if len(V)<=maxv: yield V,tri.ravel(); return
    for k in range(0,len(tri),chunk):
        sub=tri[k:k+chunk];u,inv=np.unique(sub.ravel(),return_inverse=True);yield V[u],inv
