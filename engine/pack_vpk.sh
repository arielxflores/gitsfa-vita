#!/usr/bin/env bash
# Package the VPK with the two-line bubble name: "GITS: SAC" above "First Assault".
# usage: pack_vpk.sh BUILD   (run from the engine folder after make; writes b/gitsfa-BUILD.vpk)
set -euo pipefail
B="$1"; cd "$(dirname "$0")"; export PATH="${VITASDK:-/usr/local/vitasdk}/bin:$PATH"
vita-mksfoex -s TITLE_ID=GITS00001 -s APP_VER=01.00 -s STITLE="$(printf 'GITS: SAC\nFirst Assault')" "GITS: SAC First Assault" b/param.sfo
args=()
for f in sce_sys/icon0.png sce_sys/pic0.png $(find sce_sys/livearea -type f | sort); do [[ -f "$f" ]] && args+=(-a "$f=$f"); done   # b52: pic0 = the boot screen (the LiveArea background art, no logo)
[[ -s shbank.bin ]] && args+=(-a "shbank.bin=shbank.bin")   # render b45: the offline shader bank (app0:shbank.bin; tools/shbank)
rm -f "b/gitsfa-$B.vpk"
vita-pack-vpk -s b/param.sfo -b b/eboot.bin "${args[@]}" "b/gitsfa-$B.vpk"
echo "packed b/gitsfa-$B.vpk"
