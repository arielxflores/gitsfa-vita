#!/usr/bin/env bash
# databuilder check (b47): the fog each map gets in the engine, from the old compiled-in MAPS[] table (git REV's main.c) and
# from the new <map>.glgt FOGP trailers (engine/src/light.h), compared bit for bit.   usage: fogcheck.sh MAPS_DIR [REV]
set -e
H="$(cd "$(dirname "$0")" && pwd)"; R="$H/../../.."; M="${1:?usage: fogcheck.sh MAPS_DIR [REV]}"; REV="${2:-86dacd1}"
T=$(mktemp -d "${TMPDIR:-/tmp}/fogchk.XXXXXX"); trap 'rm -rf "$T"' EXIT
git -C "$R" show "$REV:engine/src/main.c" | awk '/^typedef struct \{ const char \*stem, \*name; float fog/{p=1} p{print} p&&/^\};/{exit}' > "$T/before.inc"
cc -O0 -w -I"$R/engine/src" -DBEFORE="\"$T/before.inc\"" -o "$T/before" "$H/fogcheck.c" -lm
cc -O0 -w -I"$R/engine/src" -o "$T/after" "$H/fogcheck.c" -lm
"$T/before" > "$T/before.txt"; "$T/after" "${M%/}/" > "$T/after.txt" || true
if cmp -s "$T/before.txt" "$T/after.txt"; then echo "fog check: all $(wc -l < "$T/after.txt") maps identical (bit for bit) to the old MAPS[] table"; cat "$T/after.txt"
else echo "fog check FAILED:"; diff "$T/before.txt" "$T/after.txt"; exit 1; fi
