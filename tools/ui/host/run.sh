#!/bin/bash
# Host renders of the in-match HUD (engine code + recording GL stand-in), each state over a footage frame.
# usage: run.sh [data_dir] [out_dir]   (data_dir needs hud.ghud, ovl.ghud, menu.ghud, urban_em.gmm)
set -e
H=$(cd "$(dirname "$0")" && pwd); D=${1:-$H/../../../build/data/}; O=${2:-/tmp/claude-0/hudhost}
mkdir -p $O/cap $O/png; rm -rf $O/cap/*
gcc -O1 -w $HOSTCFLAGS -I$H -include $H/hudtag.h -o $O/hudtest $H/hud_host.c -lm   # ui b42: element tags (hudtag.h) for overlap.py
$O/hudtest $D $O/cap
BG=${BG:-}
for c in $O/cap/*.cap; do n=$(basename $c .cap); python3 $H/raster_hud.py $c $O/cap/$n.tex $O/png/$n.png $BG & while [ $(jobs -r | wc -l) -ge ${JOBS:-3} ]; do wait -n; done; done; wait   # (bounded: the rasteriser is memory hungry)
ls $O/png
