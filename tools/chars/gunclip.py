# b105: where the gun in an operator's hand passes through his own arms / body in a pose of the out-of-game bank (the
# operative page's choice pose): python3 gunclip.py <bank.gab> <clip> <w3_gun.gvm>   -> per frame, how many of the gun's
# vertices lie inside each limb (a capsule round the bone) and how deep
import sys, os, numpy as np
H_ = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(H_, '..', 'anim')); sys.path.insert(0, os.path.join(H_, '..', '..', 'vm'))
import gabview as g, preview
G = g.load(sys.argv[1]); d, bones, par, clips = G; clip = sys.argv[2]; W3 = preview.read(sys.argv[3])
V = np.concatenate([m['v']['p'] for m in W3['meshes'] if (m['blend'] & 15) == 0]); V = V[::3]
print('bones', [b for b in bones if 'Fore' in b or 'Arm' in b or 'Hand' in b]); print('gun', sys.argv[3], len(V), 'verts (every third), box', V.min(0).round(1), V.max(0).round(1))
LIMBS = [('L upper arm', 'Bip01_L_UpperArm', 'Bip01_L_Forearm', 4.6), ('L forearm', 'Bip01_L_Forearm', 'Bip01_L_Hand', 3.8), ('R upper arm', 'Bip01_R_UpperArm', 'Bip01_R_Forearm', 4.6),
         ('R forearm', 'Bip01_R_Forearm', 'Bip01_R_Hand', 3.8), ('chest', 'Bip01_Spine1', 'Bip01_Neck', 11.0), ('head', 'Bip01_Head', None, 9.0)]
def seg_d(X, A, B):
    ab = B - A; t = np.clip(((X - A) @ ab) / max(1e-9, ab @ ab), 0, 1); return np.linalg.norm(X - (A + t[:, None] * ab), axis=1), t
nf = clips[clip][0]; gi = bones.index('Grip'); fs = range(0, nf, max(1, int(sys.argv[4]) if len(sys.argv) > 4 else 2))
for fr in fs:
    P, R = g.pose(G, clip, fr); X = V @ R[gi].T + P[gi]; out = []
    for nm, a, b, r in LIMBS:
        if a not in bones or (b and b not in bones): continue
        A = P[bones.index(a)]; B = P[bones.index(b)] if b else A + R[bones.index(a)] @ np.array([12.0, 0, 0])
        if nm == 'R forearm': B = A + (B - A) * 0.8   # (the hand holds the gun: not the wrist's end)
        dd, t = seg_d(X, A, B); ins = dd < r
        if ins.sum() > 3: out.append('%s %d verts, %.1f cm deep' % (nm, ins.sum(), (r - dd[ins]).max()))
    print('%3d %.2fs' % (fr, fr / 30.0), '; '.join(out) if out else '-')
