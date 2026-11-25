#!/usr/bin/env bash
# The tree to publish: git archive of HEAD without the export-ignore paths of .gitattributes (game data, converted data,
# footage, renders), then a check that nothing game-like is left (data files, images outside the LiveArea art, pickles,
# sound / video). usage: public_tree.sh OUT_DIR
set -e
O="${1:?usage: public_tree.sh OUT_DIR}"; R="$(cd "$(dirname "$0")/../.." && pwd)"
rm -rf "$O"; mkdir -p "$O"; git -C "$R" archive --format=tar HEAD | tar -x -C "$O"
bad=$(cd "$O" && find . -type f \( -iname '*.g[a-z][a-z]' -o -iname '*.gab' -o -iname '*.dds' -o -iname '*.tga' -o -iname '*.nif' -o -iname '*.pkl' \
      -o -iname '*.npy' -o -iname '*.sdf' -o -iname '*.ssf' -o -iname '*.wav' -o -iname '*.mp4' -o -iname '*.jpg' -o \( -iname '*.png' -a ! -path './engine/*' \) \) \
      ! -name '*.gitattributes' | sort)
if [ -n "$bad" ]; then echo "NOT CLEAN:"; echo "$bad"; exit 1; fi
echo "public tree in $O: $(find "$O" -type f | wc -l) files, $(du -sh "$O" | cut -f1)"
