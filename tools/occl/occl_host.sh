#!/usr/bin/env bash
# b60: builds and runs the occlusion culling check (occl_host.c: engine/src/occl.h against a 960x544 picture of every
# triangle, and what it saves), on every map or the ones named.
#   occl_host.sh [maps folder (default /root/gits/build/maps)] [views a map (default 300)] [map ...]
#   OCV=1: a line for every chunk dropped that has pixels in the truth
set -e
H="$(cd "$(dirname "$0")" && pwd)"; M="${1:-/root/gits/build/maps}"; N="${2:-300}"; shift $(( $# < 2 ? $# : 2 ))
T="${TMPDIR:-/tmp}/occl_build"; mkdir -p "$T"
gcc -O2 -w -I"$H/../world_host" -DWORLDH="\"$H/../../engine/src/world.h\"" "$H/occl_host.c" -o "$T/occl_host" -lm
"$T/occl_host" "${M%/}/" "$N" "$@"
