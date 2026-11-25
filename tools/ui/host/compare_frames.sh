#!/bin/bash
# Side-by-side sheets from footage frames already extracted (left: the frame, right: our host render of the matching state over
# the frame blurred). usage: compare_frames.sh <out_dir> scene=/path/frame.jpg ...   (needs run.sh's captures in out_dir/cap)
# e.g. the round-2 footage frames: work/modes/r2/ts_660.jpg (Train Station switching sides) -> dem_switch
set -e
H=$(cd "$(dirname "$0")" && pwd); O=$1; shift
mkdir -p $O/cmp
for pair in "$@"; do n=${pair%%=*}; f=${pair#*=}; [ -f "$O/cap/$n.cap" ] || { echo "no capture $n"; continue; }
  python3 -c "from PIL import Image, ImageFilter; Image.open('$f').convert('RGB').resize((960,544)).filter(ImageFilter.GaussianBlur(14)).save('$O/cmp/${n}_bg.jpg')"
  python3 $H/raster_hud.py $O/cap/$n.cap $O/cap/$n.tex $O/cmp/${n}_ours.png $O/cmp/${n}_bg.jpg
  python3 -c "
from PIL import Image
a=Image.open('$f').convert('RGB').resize((960,544)); b=Image.open('$O/cmp/${n}_ours.png').convert('RGB')
S=Image.new('RGB',(1928,544),(255,0,255)); S.paste(a,(0,0)); S.paste(b,(968,0)); S.save('$O/cmp/${n}_full.jpg', quality=88)"
done
ls $O/cmp
