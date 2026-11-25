# Software preview of a skill GVM1 (z up, cm) from an orbit camera, using vm/preview.py's reader / rasteriser.
# usage: python3 preview_gvm.py file.gvm out.png [yaw_deg] [pitch_deg] [dist_cm] [clip] [frame] [W] [H]
import sys, numpy as np
sys.path.insert(0, '/root/gits/vm')
import preview as PV

def view_matrix(yaw, pitch, dist, target):
    """camera orbiting target: yaw around z (0 = camera on -y looking +y, i.e. at the model's front), pitch up"""
    cy, sy, cp, sp = np.cos(yaw), np.sin(yaw), np.cos(pitch), np.sin(pitch)
    eye = np.array(target) + dist * np.array([sy * cp, -cy * cp, sp])
    f = np.array(target) - eye; f /= np.linalg.norm(f); r = np.cross(f, [0, 0, 1]); r /= np.linalg.norm(r); u = np.cross(r, f)
    V = np.eye(4); V[0, :3] = r; V[1, :3] = u; V[2, :3] = -f; V[:3, 3] = -V[:3, :3] @ eye
    return V

def render(G, out, yaw=30, pitch=20, dist=None, clip='rest', frame=0, W=480, H=360, target=None):
    allP = np.concatenate([m['v']['p'] for m in G['meshes']])
    lo, hi = allP.min(0), allP.max(0); c = (lo + hi) / 2 if target is None else np.array(target)
    if dist is None: dist = np.linalg.norm(hi - lo) * 1.3
    V = view_matrix(np.radians(yaw), np.radians(pitch), dist, c)
    data = G['clips'][clip][2][frame].copy()
    # pre-multiply every bone pose by the view: B' = V B
    from preview import qmat
    def m2q(m):
        t = np.trace(m)
        if t > 0: s = np.sqrt(t + 1) * 2; q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
        elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]: s = np.sqrt(1 + m[0, 0] - m[1, 1] - m[2, 2]) * 2; q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
        elif m[1, 1] > m[2, 2]: s = np.sqrt(1 + m[1, 1] - m[0, 0] - m[2, 2]) * 2; q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
        else: s = np.sqrt(1 + m[2, 2] - m[0, 0] - m[1, 1]) * 2; q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
        return np.array(q) / np.linalg.norm(q)
    for b in range(data.shape[0]):
        B = np.eye(4); B[:3, :3] = qmat(data[b, :4] / np.linalg.norm(data[b, :4])); B[:3, 3] = data[b, 4:]
        B = V @ B; data[b, :4] = m2q(B[:3, :3]); data[b, 4:] = B[:3, 3]
    PV.render(G, data, out, fovy=45, W=W, H=H)

if __name__ == '__main__':
    a = sys.argv[1:]
    G = PV.read(a[0])
    f = lambda i, d: float(a[i]) if len(a) > i else d
    render(G, a[1], f(2, 30), f(3, 20), float(a[4]) if len(a) > 4 and a[4] != '-' else None, a[5] if len(a) > 5 else 'rest', int(f(6, 0)), int(f(7, 480)), int(f(8, 360)))
