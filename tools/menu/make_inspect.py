# b89: the armory's INSPECT view (menu_fe.h / main.c wpn_inspect): where each first-person gun sits in its own model so
# the menu can turn it about its middle. For every gun of weapons.txt: the gun's meshes (all but the two arm meshes the
# first-person models start with) posed on the first frame of their 'idle' clip, in the model's camera space (x right,
# y up, z back: the barrel points along -z). Writes one line per gun:
#   short  cx cy cz  hx hy hz
# c = the middle of the gun's box, h = its half sizes (cm). The engine frames the gun from these and turns it about c.
#   python3 make_inspect.py DATA_DIR [OUT]   (OUT: DATA_DIR/wpn_inspect.txt)
import os, sys, numpy as np
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(H, '..', '..', 'vm'))
import preview
def posed(G, frame, meshes):
    M = preview.palette_mats(G, frame); out = []
    for m in meshes:
        v = m['v']; P = np.c_[v['p'], np.ones(len(v))]; bw = v['bw'] / 255.0; acc = np.zeros((len(v), 4))
        for j in range(4): acc += bw[:, j:j + 1] * np.einsum('nij,nj->ni', M[v['bi'][:, j] + m.get('base', 0)], P)
        used = np.unique(m['I']); out.append(acc[used, :3])
    return np.concatenate(out)
def main():
    d = sys.argv[1].rstrip('/\\') + '/'; out = sys.argv[2] if len(sys.argv) > 2 else d + 'wpn_inspect.txt'
    guns = [l.split('|')[0].strip() for l in open(d + 'weapons.txt') if l.strip() and not l.startswith('#')]
    lines = ['# wpn_inspect.txt (tools/menu/make_inspect.py): short cx cy cz hx hy hz - the gun (no arms) on its idle pose, camera space, cm']
    for g in guns:
        p = d + 'vm_%s.gvm' % g
        if not os.path.exists(p): continue
        G = preview.read(p); cl = G['clips'].get('idle')
        if cl is None or len(G['meshes']) < 3: continue
        X = posed(G, cl[2][0], G['meshes'][2:])
        lo, hi = np.percentile(X, 0.2, axis=0), np.percentile(X, 99.8, axis=0)   # (a stray vertex does not widen the frame)
        c, h = (lo + hi) / 2, (hi - lo) / 2
        lines.append('%s %.2f %.2f %.2f %.2f %.2f %.2f' % ((g,) + tuple(c) + tuple(h)))
    open(out, 'w').write('\n'.join(lines) + '\n'); print('\n'.join(lines))
if __name__ == '__main__': main()
