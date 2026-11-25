#!/usr/bin/env bash
# b60: the light first-person meshes (vm_<gun>.gfp) for every gun of a data folder, then the check that they draw the same
# picture as the full models (fpcheck.c).
#   fp.sh [data folder (default /root/gits/build/data)] [out folder (default: the data folder)] [random views a gun for the check (default 2000; 0: no check)]
set -e
H="$(cd "$(dirname "$0")" && pwd)"; D="${1:-/root/gits/build/data}"; O="${2:-$D}"; N="${3:-2000}"
T="${TMPDIR:-/tmp}/fp_build"; mkdir -p "$T/vis" "$O"
python3 "$H/../databuilder/ctools.py" vmvis "$D" "$T/vis"
python3 "$H/make_fp.py" "$D" "$T/vis" "$O"
if [ "$N" != 0 ]; then gcc -O2 -w -I"$H/../weapons/sgl" -o "$T/fpcheck" "$H/fpcheck.c" -lm; "$T/fpcheck" "${D%/}/" "$O" "$N"; fi
