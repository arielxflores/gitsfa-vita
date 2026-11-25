#!/bin/bash
# builds the packs into $MENU_WORK/data, runs the host test and rasterises every capture to PNG
# usage: run_host.sh [-n] [names...]   (-n: keep the packs, only rebuild the test and the captures; names: rasterise only
# these captures - the machine is shared, rasterising all ~60 takes minutes). Ends with the legibility check of every capture.
set -e
T=$(cd "$(dirname "$0")" && pwd); export MENU_WORK=${MENU_WORK:-/tmp/claude-0/menu-ui}; D=$MENU_WORK; mkdir -p $D/data $D/cap $D/png
cd $D
if [[ "$1" != "-n" ]]; then python3 $T/make_ovl.py $D/data/ovl.ghud 2>&1 | grep -v "tag 83" | tail -2; python3 $T/make_menu.py $D/data/menu.ghud 2>&1 | grep -v "tag 83" | tail -2; fi
cp -u $T/../../build/data/mload_*.gtx $D/data/ 2>/dev/null || true; [[ -f $D/data/mthumb.gtx ]] || python3 $T/make_fe_tex.py $D/data/
[[ -f $D/bg.jpg ]] || cp /tmp/claude-0/menu/bg.jpg $D/bg.jpg 2>/dev/null || true
gcc -O1 -w -I$T/host -DMORE_TESTS -DMU_AUDIT -o $D/mtest $T/host/test.c -lm
A=("$@"); [[ "${A[0]:-}" == "-n" ]] && A=("${A[@]:1}"); [[ ${#A[@]} -gt 0 && -z "${MENU_ONLY:-}" ]] && export MENU_ONLY=$(IFS=,; echo "${A[*]}")   # (names: only those captures are written)
rm -rf $D/cap/*; $D/mtest $D/data/ $D/cap
[[ "$1" == "-n" ]] && shift
L=("$@"); [[ ${#L[@]} -eq 0 ]] && L=($(cd $D/cap && ls *.cap | sed 's/\.cap$//'))
for n in "${L[@]}"; do case $n in fe_*|res_*|tour_*) B="";; *) B=$D/bg.jpg;; esac; python3 $T/raster.py $D/cap/$n.cap $D/cap/$n.tex $D/png/$n.png $B & while (( $(jobs -r | wc -l) >= 2 )); do sleep 0.2; done; done; wait
python3 $T/check_legibility.py $D/cap | grep -E "FAIL|^ |captures fail"
