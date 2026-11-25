#!/usr/bin/env bash
# render b45: compiles the build's shader domain (enum.sh) with the Vita's own compiler (SceShaccCg, run by the emulator
# harness on the user's libshacccg.suprx) and packs the offline shader bank engine/shbank.bin (pack_vpk.sh puts it into the
# VPK as app0:shbank.bin). The compiler itself is never shipped, only its GXP output.
#   gen.sh <vpk of this build> [instance (default 5)]     env: KEEP (keep the scratch folder)
# Steps: enum.sh -> shbank_src.bin into ux0:data/gitsfa (run.sh EXTRA_DATA) with the shbank_gen test switch: the game compiles
# every source, writes ux0:data/gitsfa/shbank_gxp.bin and stops (shcache.h shb_generate) -> pack.py.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); VPK=$(readlink -f "$1"); I=${2:-5}
T=$(mktemp -d "${TMPDIR:-/tmp}/shbank.XXXXXX"); trap '[[ -n ${KEEP:-} ]] || rm -rf "$T"' EXIT
bash "$H/enum.sh" "$T/enum"
mkdir -p "$T/data"; cp "$T/enum/shbank_src.bin" "$T/data/"
EMU=/root/work/emu; FS=$EMU/v3k/inst$I/portable/fs; [[ $I == 1 ]] && FS=$EMU/v3k/build/bin/portable/fs
rm -f "$FS/ux0/data/gitsfa/shbank_gxp.bin"
EXTRA_DATA="$T/data" MARKERS="shbank_gen" INSTANCE=$I SHOT_EVERY=120 HANG_STOP=90 nice -n 15 bash $EMU/run.sh "$VPK" "${SECS:-900}" | tail -3
G="$FS/ux0/data/gitsfa/shbank_gxp.bin"; [[ -s $G ]] || { echo "shbank gen: no $G (see the run's boot.log)"; rm -f "$FS/ux0/data/gitsfa/shbank_gen" "$FS/ux0/data/gitsfa/shbank_src.bin"; exit 1; }
cp "$G" "$T/gxp.bin"
rm -f "$G" "$FS/ux0/data/gitsfa/shbank_gen" "$FS/ux0/data/gitsfa/shbank_src.bin"   # (run.sh clears only its own switches: the next run must start normally)
python3 -I "$H/pack.py" "$T/gxp.bin" "$T/enum/shbank_keys.txt" "$H/../../engine/shbank.bin"
cp "$T/enum/shbank_keys.txt" "$H/../../engine/shbank_keys.txt"
