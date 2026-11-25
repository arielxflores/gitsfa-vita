#!/usr/bin/env bash
# render b45: the build's shader program domain, for the offline shader bank (engine/src/shcache.h). The host game
# (tools/hostgame, plain build) starts with the shbank_enum test switch under every switch set that changes a shader source
# (colour grading forced to each mode and left to its compile test, half precision on / off, the packed fog on / off; the
# test run's probe sets on), and their sources are merged, one per key (merge.py). Every shader the game compiles is
# compiled at start-up (no later compiles), so the start-ups cover what a build can bind.
#   enum.sh <out dir>  ->  <out>/shbank_src.bin and <out>/shbank_keys.txt     env: SRC (engine/src), FRAMES (600)
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); mkdir -p "$1"; O=$(cd "$1" && pwd); E=${SRC:-$H/../../engine/src}
mkdir -p "$O/hg"
SRC=$E nice -n 15 bash "$H/../hostgame/build.sh" plain "$O/hg/gitsfa_enum" > "$O/hg/build.log" 2>&1 || { echo "shbank enum: the host game did not build (log: $O/hg/build.log)"; exit 1; }
rm -rf "$O/run"; ins=()
for g in auto 0 1 2 3 4 5; do for v in half nofogpack nohalf; do
	D="$O/run/$g-$v"; mkdir -p "$D"; touch "$D/autoplay" "$D/shbank_enum"
	[[ $g != auto ]] && echo "$g" > "$D/grade.txt"; [[ $v != half ]] && touch "$D/$v"
	(cd "$D" && HG_OUT="$D" HG_MAX_FRAMES="${FRAMES:-600}" nice -n 15 "$O/hg/gitsfa_enum" > run.out 2> run.err) || true
	[[ -s "$D/shbank_src.bin" ]] || { echo "shbank enum: start-up $g-$v compiled nothing (see $D/boot.log)"; exit 1; }
	ins+=("$D/shbank_src.bin")
done; done
for x in oldlens; do   # b66: test switches that swap a program (oldlens: b65's lens glass, the A/B for the glass lens), once each
	D="$O/run/auto-$x"; mkdir -p "$D"; touch "$D/autoplay" "$D/shbank_enum" "$D/$x"
	(cd "$D" && HG_OUT="$D" HG_MAX_FRAMES="${FRAMES:-600}" nice -n 15 "$O/hg/gitsfa_enum" > run.out 2> run.err) || true
	[[ -s "$D/shbank_src.bin" ]] || { echo "shbank enum: start-up auto-$x compiled nothing (see $D/boot.log)"; exit 1; }
	ins+=("$D/shbank_src.bin")
done
python3 -I "$H/merge.py" "$O/shbank_src.bin" "$O/shbank_keys.txt" "${ins[@]}"
rm -rf "$O/run" "$O/hg"/*.o
