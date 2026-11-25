#!/usr/bin/env bash
# perf (b46): makes the chunk-split files (<map>.gchk, world.h) for every .gmap of a folder and checks each one (gchk.c).
# Run it whenever a .gmap changes (a stale .gchk is ignored mesh by mesh: those meshes are split at load as before).
# The files go beside the .gmap files (the default out folder is the maps folder: build/maps, then ux0:data/gitsfa/ with the maps).
#   gchk.sh [maps folder (default /root/gits/build/maps)] [out folder (default: the maps folder)] [map ...]
set -e
H="$(cd "$(dirname "$0")" && pwd)"; M="${1:-/root/gits/build/maps}"; O="${2:-$M}"; shift $(( $# < 2 ? $# : 2 ))
mkdir -p "$O"; T="${TMPDIR:-/tmp}/gchk_build"; mkdir -p "$T"
gcc -O2 -Wall -Wextra -Wno-unused-function -I"$H/../world_host" -DWORLDH="\"$H/../../engine/src/world.h\"" "$H/gchk.c" -o "$T/gchk" -lm
nice -n "${GCHK_NICE:-15}" "$T/gchk" "${M%/}/" "${O%/}/" "$@"
