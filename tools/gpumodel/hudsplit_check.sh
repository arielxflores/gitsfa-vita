#!/usr/bin/env bash
# render b40: the HUD split checked on the host. Builds every HUD capture harness twice, from a reference tree (a git
# revision: the HUD before the split) and from this clone, runs both on the same data and compares the captures with
# capdiff.py (same quads, order, textures, vertices; the draw count may grow). The clone's builds define HUD_SPLIT_CHECK:
# hud.h then counts the lean draws and any lean vertex with a gauge threshold (must be 0). Heavy steps at nice 15.
#   hudsplit_check.sh <work dir> [reference revision, default HEAD] [data dir, default /root/gits/build/data/]
# The work dir takes ~4 GB (the harnesses dump textures and PPMs per state): delete it afterwards.
# Harnesses: tools/ui/host (TDM / Elimination / Demolition HUD, scoreboard, killcam), tools/menu/host (front end, splashes,
# pause, loading screens), tools/tank/host (Think Tank / Terminal Conquest), tools/tutorial/host (training UI),
# tools/weapons/hud_host.c (weapon HUD), tools/skills/host/skhud (skill HUD).
set -uo pipefail
W=$1; REV=${2:-HEAD}; DATA=${3:-/root/gits/build/data/}
T=$(cd "$(dirname "$0")" && pwd); R=$(cd "$T/../.." && pwd)
mkdir -p "$W"; rm -rf "$W/ref" "$W/new" "$W/out"; mkdir -p "$W/ref" "$W/out"
(cd "$R" && git archive "$REV" engine/src tools/ui tools/menu tools/tank tools/tutorial tools/weapons tools/skills) | tar -x -C "$W/ref"
mkdir -p "$W/new"; cp -r "$R/engine" "$R/tools" "$W/new/" 2>/dev/null; rm -rf "$W/new/engine/b"
sed -i '1i #define HUD_SPLIT_CHECK 1' "$W/new/engine/src/hud.h"
for side in ref new; do
	S=$W/$side; O=$W/out/$side; mkdir -p $O/ui $O/menu $O/tut $O/wpn $O/tank $O/skhud
	echo "== $side"
	nice -n 15 gcc -O1 -w -I$S/tools/ui/host -o $O/hudtest $S/tools/ui/host/hud_host.c -lm && (cd $S && nice -n 15 $O/hudtest $DATA $O/ui > $O/ui.log 2>&1); tail -1 $O/ui.log
	nice -n 15 gcc -O1 -w -I$S/tools/menu/host -DMORE_TESTS -o $O/mtest $S/tools/menu/host/test.c -lm && (cd $S && nice -n 15 $O/mtest $DATA $O/menu > $O/menu.log 2>&1); tail -1 $O/menu.log
	nice -n 15 gcc -O1 -w -I$S/tools/menu/host -o $O/ttest $S/tools/tutorial/host/test.c -lm && (cd $S && nice -n 15 $O/ttest $DATA $O/tut > $O/tut.log 2>&1); tail -1 $O/tut.log
	nice -n 15 gcc -O1 -w -I$S/tools/menu/host -o $O/whud $S/tools/weapons/hud_host.c -lm && (cd $S && nice -n 15 $O/whud $DATA $O/wpn > $O/wpn.log 2>&1); tail -1 $O/wpn.log
	TMPDIR=$O nice -n 15 bash $S/tools/tank/host/run.sh $S/engine/src $DATA $O/tank > $O/tank.log 2>&1; tail -1 $O/tank.log
	sed -i 's/skills.h; do/skills.h sk_shield_geo.h; do/' $S/tools/skills/host/skhud/run.sh   # (its header list misses sk_shield_geo.h since perf b38)
	nice -n 15 bash $S/tools/skills/host/skhud/run.sh $S/engine/src $DATA $O/skhud > $O/skhud.log 2>&1; grep -i 'split check' $O/skhud.log | tail -1
done
for h in ui menu tut wpn tank skhud; do
	A=$W/out/ref/$h; B=$W/out/new/$h; [[ -d $A/cap ]] && A=$A/cap; [[ -d $B/cap ]] && B=$B/cap
	echo "== $h"; python3 "$T/capdiff.py" "$A" "$B" | tail -${CAPDIFF_LINES:-1}
done
for f in $W/out/new/*.log; do echo "$(basename $f .log): $(grep -ho 'hud split check:.*' $f | tail -1)"; done
