#!/usr/bin/env bash
# Build eboot.bin, making sure the code segment leaves room for vita-elf-create's module data, then pack BUILD.vpk.
# usage: build.sh BUILD
set -euo pipefail
B="$1"; cd "$(dirname "$0")"; export VITASDK="${VITASDK:-/usr/local/vitasdk}"; export PATH="$VITASDK/bin:$PATH"
cd b
REL=OFF; [[ "$B" == release* ]] && REL=ON   # render (b35): "bash build.sh release..." makes the publishing build (GITSFA_RELEASE)
cmake .. -DPAD=16 -DRELEASE=$REL >/dev/null
make gitsfa > make.log 2>&1 || { echo "BUILD FAILED (link)"; grep -E "error" make.log | head; exit 1; }
cp make.log make_compile.log; NW=$(grep -c "warning:" make_compile.log || true); echo "compile warnings: $NW (b/make_compile.log)"
# end of the R E segment and start of the next one: vita-elf-create writes its module data (SCE data) in the gap between them.
# b87: vitasdk 2026.08's needs 4276 bytes (the old SDK's under 4 KB). The next segment starts at the next 64 KB boundary
# after the code, so a gap under 4.5 KB is widened by padding the code just past that boundary; measured after each link
# (the pad's place in the image is the linker's), at most 4 tries
seg() { arm-vita-eabi-readelf -lW gitsfa | python3 -c "
import sys
L=[l.split() for l in sys.stdin if l.strip().startswith('LOAD')]
print(int(L[0][2],16)+int(L[0][5],16), int(L[1][2],16))"; }
PAD=16
for try_ in 1 2 3 4; do
  read -r END NEXT < <(seg); GAP=$((NEXT-END))
  echo "code segment ends 0x$(printf %x $END), next segment 0x$(printf %x $NEXT), gap $GAP (pad $PAD)"
  (( GAP >= 4608 )) && break
  PAD=$(( PAD + GAP + 64 )); cmake .. -DPAD=$PAD >/dev/null
  make gitsfa > make.log 2>&1 || { echo "BUILD FAILED (link)"; grep -E "error" make.log | head; exit 1; }
done
cmake .. -DPAD=$PAD >/dev/null
rm -f eboot.bin
make > make.log 2>&1 || true
if ! grep -qE "Built target eboot.bin-self" make.log; then echo "BUILD FAILED"; tail -20 make.log; exit 1; fi
[[ -s eboot.bin ]] || { echo "no eboot.bin"; exit 1; }
# render b45: every shader source of this build must be in the offline shader bank (tools/shbank/check.sh; SHBANK_CHECK=0 skips)
bash ../../tools/shbank/check.sh || { echo "BUILD FAILED: shader bank (tools/shbank/gen.sh <vpk> remakes it; SHBANK_CHECK=0 builds without the check)"; exit 1; }
cd .. && bash pack_vpk.sh "$B"
