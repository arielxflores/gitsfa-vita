#!/bin/bash
# builds and runs the tut_ui.h host test, rasterises the captures (tools/menu/raster.py); backgrounds: $BG/<name>.jpg if present
# OUT (default /tmp/claude-0/tut) gets cap/ and png/; DATA: the build/data folder (tut_ui.gtx, menu packs)
set -e
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../../.." && pwd); D=${OUT:-/tmp/claude-0/tut}; mkdir -p $D/cap $D/png
gcc -O1 -w -I$R/tools/menu/host -o $D/ttest $H/test.c -lm
rm -rf $D/cap/*; $D/ttest ${DATA:-$R/build/data/} $D/cap
for c in $D/cap/*.cap; do n=$(basename $c .cap); python3 $R/tools/menu/raster.py $c $D/cap/$n.tex $D/png/$n.png ${BG:-/nonexistent}/$n.jpg & done; wait
ls $D/png
