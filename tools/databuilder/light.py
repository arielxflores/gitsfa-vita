# databuilder: <map>.glgt for every map: the map's light columns (tools/make_light.py, from world_lmap.nif + lightmaps) +
# the actor light and the fog (tools/glgt_actor.py: light.ini [ACTOR], tutorial / tutorial2 have none; World.ini fog).
# usage: light.py OUT_MAPS_DIR
import os, sys, subprocess, shutil
H = os.path.dirname(os.path.abspath(__file__)); T = os.path.join(H, '..'); sys.path.insert(0, T)
import gitspaths
from build_data import MAPS
out = sys.argv[1]; raw = gitspaths.path('TMP', 'glgt'); os.makedirs(raw, exist_ok=True)
for stem, fix in MAPS:
    d = gitspaths.path('MAPSRC', fix); pk = os.path.join(raw, stem + '.pkl')
    subprocess.run([sys.executable, os.path.join(T, 'export_map.py'), os.path.join(d, 'world_lmap.nif'), pk], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, os.path.join(T, 'make_light.py'), d, pk, os.path.join(raw, stem + '.glgt')], check=True)
    os.remove(pk)
subprocess.run([sys.executable, os.path.join(T, 'glgt_actor.py'), raw, out] + [s for s, _ in MAPS], check=True)   # + ACTR, FOGP trailers
