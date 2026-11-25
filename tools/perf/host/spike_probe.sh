#!/usr/bin/env bash
# builds spike_probe (engine headers + the bot_sim host stubs) and runs it under callgrind: instructions per probe call and the
# raycasts inside. usage: spike_probe.sh <map stem> [seed]   env: OUT (build dir), MAPS, NOCG=1 (plain run)
set -e
H="$(cd "$(dirname "$0")" && pwd)"; E="${SRC:-$H/../../../engine/src}"; C="$H/../../chars/host"; T="${OUT:-/tmp/spike_probe_build}"
mkdir -p "$T"; cp "$E"/player.h "$E"/model.h "$E"/skin.h "$E"/skin_gpu.h "$E"/nav.h "$E"/light.h "$E"/chr.h "$E"/bots.h "$E"/grade.h "$E"/jobs.h "$E"/cpuprof.h "$E"/elim.h "$T"/; cp "$C"/stub/audio.h "$C"/stub/sprite.h "$C"/vitaGL.h "$T"/
cp "$H/spike_probe.c" "$T"/; ${CC:-gcc} ${CFLAGS:--O2 -g} -w -I"$T" -o "$T/spike_probe" "$T/spike_probe.c" -lm -lpthread
D="$H/../../../build/data"; M="${MAPS:-/root/gits/build/maps}"
if [ -n "$NOCG" ]; then "$T/spike_probe" "$D" "$M" "$@"; exit; fi
valgrind --tool=callgrind --callgrind-out-file="$T/cg.$1" "$T/spike_probe" "$D" "$M" "$@" 2>/dev/null
callgrind_annotate --inclusive=yes --tree=calling "$T/cg.$1" 2>/dev/null | awk '
	/\* .*:probe_/ { split($0, a, ":probe_"); split(a[2], b, " "); cur = "probe_" b[1]; next }
	/^[0-9,]+ .*\* / { cur = "" }
	cur != "" && /> .*:(raycast|bot_los|nav_search_run|bot_spawn|elim_zone_at|bots_update)[ .]/ { print "   " cur ": " $0 }' | sed "s/ \[.*//; s/([0-9.]*%) //"
callgrind_annotate --inclusive=yes "$T/cg.$1" 2>/dev/null | grep -E ":probe_" | sed "s/ \[.*//"
