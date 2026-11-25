#!/bin/bash
# ui b42: the HUD overlap check. Builds the host HUD harnesses with the element tags (hudtag.h) - the in-match HUD
# (hud_host.c), the overlay combinations (combo.c: + skills.h, menu.h, hud_round.h), the Think Tank / Terminal Conquest HUD
# (tools/tank/host), the tutorial overlays (tools/tutorial/host), the weapon prompts (tools/weapons) and the skills' prompts
# (tools/skills/host/skhud) - runs them, and reports every pair of elements that overlap or come closer than 6 px
# (overlap.py). Terminal Conquest combinations: the tank harness's frames + combo.c's overlays (feed, commander, notices).
# usage: overlap.sh [out_dir]   env: DATA (build/data), BG (a background picture for the PNGs), PNG=0 (no pictures), ONLY (regex),
#   KEEP=1 (keep the captures)
set -e
H=$(cd "$(dirname "$0")" && pwd); R=$(cd "$H/../../.." && pwd); SRC=$R/engine/src; D=${DATA:-$R/build/data/}; O=${1:-/tmp/claude-0/hudover}
mkdir -p $O; B=$O/build; rm -rf $B; mkdir -p $B
for k in hud combo tank tut wpn sk; do rm -rf $O/$k; mkdir -p $O/$k; done
# engine headers + the recording stand-in (+ the skills harness's extras), the audio stub with the real sound names
cp $SRC/*.h $B/; cp $H/vitaGL.h $B/vitaGL_rec.h; cp $R/tools/skills/host/skhud/vitaGL_extra.h $B/; printf '#pragma once\n#include "vitaGL_rec.h"\n#include "vitaGL_extra.h"\n' > $B/vitaGL.h
cp $R/tools/skills/host/sktest/audio.h $B/; python3 $R/tools/skills/host/sktest/gen_names.py "$SRC/audio.h" "$D/audio_manifest.txt" "$B/names_gen.h" > /dev/null
cp $H/hudtag.h $H/combo.c $R/tools/skills/host/skhud/skhud.c $B/
echo "== build"
gcc -O1 -w -I$H -include $H/hudtag.h -o $B/hudtest $H/hud_host.c -lm
gcc -O1 -w -I$B -include $B/hudtag.h -o $B/combo $B/combo.c -lm
gcc -O1 -w -I$B -include $B/hudtag.h -o $B/skhud $B/skhud.c -lm
gcc -O1 -w -I$R/tools/menu/host -include $H/hudtag.h -o $B/tut $R/tools/tutorial/host/test.c -lm
gcc -O1 -w -I$R/tools/menu/host -include $H/hudtag.h -o $B/wpn $R/tools/weapons/hud_host.c -lm
TB=$B/tank; mkdir -p $TB
for h in glz.h texup.h alog.h player.h sprite.h fx.h fxtrim.h model.h skin.h skin_gpu.h jobs.h cpuprof.h grade.h light.h nav.h hud.h hud_game.h skills.h chr.h bots.h thinktank.h ttm.h vita_prompt.h sk_shield_geo.h; do cp "$SRC/$h" $TB/; done
cp $R/tools/tank/host/vitaGL.h $R/tools/tank/host/audio.h $R/tools/tank/host/tk_test.c $H/hudtag.h $TB/
python3 $R/tools/tank/host/gen_names.py "$SRC/audio.h" "$D/audio_manifest.txt" "$TB/names_gen.h"
cc -std=gnu11 -O1 -w -DDATA="\"$D\"" -I$TB -include $TB/hudtag.h $TB/tk_test.c -o $TB/tk_test -lm
echo "== run"
$B/hudtest $D $O/hud > /dev/null; (cd $B && ./combo $D $O/combo | grep claims; ./skhud $D $O/sk > /dev/null); $B/tut $D $O/tut > /dev/null; $B/wpn $D $O/wpn > /dev/null
(cd $TB && ./tk_test $O/tank > $O/tank.log 2>&1) || echo "(tank harness: see $O/tank.log)"
rm -f $O/tank/*.ppm
# Terminal Conquest frames + combo.c's overlays (the add-on's elements only: kill feed rows, commander, notices, timer warning)
M="$O/tank/t_live.cap+$O/combo/x_feed5_ttm.cap@feed $O/tank/t_live.cap+$O/combo/x_cm2.cap@commander $O/tank/t_live.cap+$O/combo/x_cm3.cap@commander
   $O/tank/t_overtime_final.cap+$O/combo/x_kill_t2_ttm.cap@notify,sk.t2ready $O/tank/t_ready.cap+$O/combo/x_feed5_ttm_band.cap@feed $O/tank/t_secured.cap+$O/combo/x_feed5_ttm_band.cap@feed
   $O/tank/t_live.cap+$O/combo/x_kill_ttm_live.cap@notify $O/tank/m_live.cap+$O/combo/x_feed5_ttm.cap@feed $O/tank/t_overtime_final.cap+$O/combo/x_feed5_ttm_band.cap@feed
   $O/tank/t_live.cap+$O/combo/x_start_ttm.cap@splash $O/tank/t_live.cap+$O/combo/x_start_ttm_goal.cap@splash"
echo "== overlap (min gap 6 px; rows of one list 2 px)"
A="$O/hud $O/combo $O/tank $O/tut $O/wpn $O/sk $M"
[ "${PNG:-1}" = 1 ] && P="--png $O/png" || P=""; mkdir -p $O/png
nice -n 15 python3 $H/overlap.py $P ${BG:+--bg $BG} ${ONLY:+--only "$ONLY"} --json $O/overlap.json $A | tee $O/overlap.txt
[ -n "${KEEP:-}" ] || rm -rf $O/hud $O/combo $O/tank $O/tut $O/wpn $O/sk $B   # (the captures' texture dumps take ~1.6 GB; KEEP=1 keeps them)
