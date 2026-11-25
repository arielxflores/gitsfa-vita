#!/bin/bash
# Side-by-side sheets: footage frame (left) | the host HUD render over the same frame (right), full frame + zoomed regions.
# usage: compare.sh [data_dir] [out_dir]   (needs run.sh's captures in out_dir/cap; footage under /mnt/user-data/uploads/gits sac fa videos)
set -e
H=$(cd "$(dirname "$0")" && pwd); D=${1:-$H/../../../build/data/}; O=${2:-/tmp/claude-0/hudhost}
V="/mnt/user-data/uploads/gits sac fa videos"
declare -A VID=( [cmp_tdm]="GitS SAC - First Assault Online - TDM Neon Ruins - Motoko gameplay.mp4" [cmp_em]="GitS SAC - First Assault Online - Elimination Urban City - Motoko gameplay.mp4" [cmp_dem]="GitS SAC - First Assault Online - Demolition Headquaters - Motoko 22kdr gameplay.mp4" )
declare -A T=( [cmp_tdm]=200 [cmp_em]=100 [cmp_dem]=236 )
mkdir -p $O/cmp
for n in cmp_tdm cmp_em cmp_dem; do
  ffmpeg -v error -y -ss ${T[$n]} -i "$V/${VID[$n]}" -frames:v 1 -q:v 2 $O/cmp/$n.jpg
  python3 -c "from PIL import Image, ImageFilter; Image.open('$O/cmp/$n.jpg').convert('RGB').resize((960,544)).filter(ImageFilter.GaussianBlur(14)).save('$O/cmp/${n}_bg.jpg')"   # (the footage's own HUD blurred away)
  python3 $H/raster_hud.py $O/cap/$n.cap $O/cap/$n.tex $O/cmp/${n}_ours.png $O/cmp/${n}_bg.jpg
  python3 -c "
from PIL import Image
a=Image.open('$O/cmp/$n.jpg').convert('RGB').resize((960,544)); b=Image.open('$O/cmp/${n}_ours.png').convert('RGB')
S=Image.new('RGB',(1928,544),(255,0,255)); S.paste(a,(0,0)); S.paste(b,(968,0)); S.save('$O/cmp/${n}_full.jpg', quality=88)"
  python3 $H/sidebyside.py $O/cmp/${n}_zoom.png $O/cmp/$n.jpg $O/cmp/${n}_ours.png
done
ls $O/cmp
