# databuilder: the radar textures <stem>.gmm (tools/minimap/make_gmm.py). Its inputs, the 512 x 512 radar PNGs, were made on
# the PC by a step that was not kept; here they are the game's ui\map\<img>.dds (600 x 600) scaled with Pillow's Lanczos (as
# tools/elim/make_gelim.py does for the Elimination radars): the closest of the scalers tried (2.6 % of the pixels differ by
# more than 1/255 in shown colour, 0.06 % by more than 8/255). Icons.png (512 already) is the DDS as it is.
# The world -> image registration (params/minimap.txt) is ours.     usage: radar.py OUT_DATA_DIR
import os, sys, shutil, subprocess
from PIL import Image
H = os.path.dirname(os.path.abspath(__file__)); T = os.path.join(H, '..'); sys.path.insert(0, T)
import gitspaths
mm = gitspaths.root('MINIMAP'); out = sys.argv[1]
if not os.path.exists(os.path.join(mm, 'minimap.txt')):   # (the dev container has the original PNGs: used as they are)
    os.makedirs(os.path.join(mm, 'minimap'), exist_ok=True)
    imgs = ['Icons']
    for ln in open(os.path.join(H, 'params', 'minimap.txt')):
        p = ln.split()
        if p and not ln.startswith('#') and p[1] != 'none': imgs.append(p[1])
    for im in imgs:
        s = Image.open(gitspaths.path('UI', 'ui', 'map', im + '.dds')).convert('RGBA')
        if s.size != (512, 512): s = s.resize((512, 512), Image.LANCZOS)
        s.save(os.path.join(mm, 'minimap', im + '.png'))
    shutil.copyfile(os.path.join(H, 'params', 'minimap.txt'), os.path.join(mm, 'minimap.txt'))
subprocess.run([sys.executable, os.path.join(T, 'minimap', 'make_gmm.py'), out], check=True)
