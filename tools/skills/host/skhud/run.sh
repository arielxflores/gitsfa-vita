#!/bin/bash
# skills.h HUD prompts on the host: engine headers + the ui agent's recording GL stand-in -> captures -> PNGs (raster_hud.py)
set -e
H=$(cd "$(dirname "$0")" && pwd); SRC=${1:-$H/../../../../engine/src}; D=${2:-$H/../../../../build/data/}; O=${3:-${TMPDIR:-/tmp}/skhud}
B=$O/build; rm -rf $B; mkdir -p $B $O/cap $O/png; rm -rf $O/cap/*
for h in player.h sprite.h fx.h model.h skin.h jobs.h light.h hud.h hud_game.h grade.h skills.h; do cp "$SRC/$h" $B/; done
cp $H/../../../ui/host/vitaGL.h $B/vitaGL_rec.h; printf "#pragma once\n#include \"vitaGL_rec.h\"\n#include \"vitaGL_extra.h\"\n" > $B/vitaGL.h; cp $H/vitaGL_extra.h $B/; cp $H/../sktest/audio.h $B/; cp $H/skhud.c $B/
python3 $H/../sktest/gen_names.py "$SRC/audio.h" "$D/audio_manifest.txt" "$B/names_gen.h" > /dev/null
gcc -O1 -w -I$B -o $B/skhud $B/skhud.c -lm
(cd $B && ./skhud $D $O/cap)
for c in $O/cap/*.cap; do n=$(basename $c .cap); python3 $H/../../../ui/host/raster_hud.py $c $O/cap/$n.tex $O/png/$n.png & done; wait
ls $O/png
