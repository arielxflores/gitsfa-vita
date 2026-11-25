#!/bin/bash
# builds and runs the viewmodel draw host test (tools/weapons/vmdraw_host.c) on build/data; sheets (PNG) in $1 (default /tmp/wpn_vmdraw)
set -e
H=$(cd "$(dirname "$0")" && pwd); O=${1:-${TMPDIR:-/tmp}/wpn_vmdraw}; mkdir -p $O
gcc -O2 -w -I$H/sgl -o $O/vmdraw $H/vmdraw_host.c -lm
cd $H/../.. && $O/vmdraw build/data/ $O
python3 -c "
import glob,sys
from PIL import Image
for f in glob.glob(sys.argv[1]+'/vmdraw_*.ppm'): Image.open(f).save(f[:-4]+'.png')" $O
