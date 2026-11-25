#!/bin/bash
# builds and runs the weapon HUD host test; PNGs (over a footage frame when given) in /tmp/claude-0/wpnhud/png
set -e
H=$(cd "$(dirname "$0")" && pwd); D=/tmp/claude-0/wpnhud; mkdir -p $D/png; rm -rf $D/*.cap $D/*.tex
gcc -O1 -w -I$H/../menu/host -o $D/whud $H/hud_host.c -lm
$D/whud $H/../../build/data/ $D
for c in $D/*.cap; do n=$(basename $c .cap); python3 $H/../menu/raster.py $c $D/$n.tex $D/png/$n.png $1; done
ls $D/png
