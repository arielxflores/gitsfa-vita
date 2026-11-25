# Host previews of tk_blue.gvm / tk_red.gvm (vm/preview.py rasteriser, same skinning maths as the engine).
# usage: python3 preview_tank.py outdir [clip frame yaw pitch ...]
import sys, os, numpy as np
sys.path.insert(0, '/root/work/tank/vm'); sys.path.insert(0, '/root/work/tank/tools/skills')
import preview as PV, preview_gvm
D = '/root/work/tank/build/data/'
def load(tag):
    G = PV.read(D + 'tk_%s.gvm' % tag)
    if not G['clips']: G['clips'] = PV.read(D + 'tk_blue.gvm')['clips']
    return G
def shot(G, out, clip='idle', frame=0, yaw=30, pitch=15, dist=650, W=480, H=360):
    preview_gvm.render(G, out, yaw, pitch, dist, clip, frame, W, H, target=(0, 0, 90))
if __name__ == '__main__':
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    from PIL import Image
    for tag in ('blue', 'red'):
        G = load(tag); tiles = []
        for clip, fr, yaw in (('idle', 0, 30), ('idle', 0, 160), ('walk_f', 10, 30), ('run', 8, 90), ('fire_loop', 5, 20), ('destroy', 83, 30), ('stun_loop', 40, 30), ('dance', 30, 30)):
            fr = min(fr, G['clips'][clip][2].shape[0] - 1); p = out + '/%s_%s_%d.png' % (tag, clip, yaw); shot(G, p, clip, fr, yaw); tiles.append(Image.open(p))
        sh = Image.new('RGB', (480 * 4, 360 * 2))
        for i, t in enumerate(tiles): sh.paste(t, ((i % 4) * 480, (i // 4) * 360))
        sh.save(out + '/sheet_%s.png' % tag); print(out + '/sheet_%s.png' % tag)
