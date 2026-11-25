#!/usr/bin/env bash
# Ghost in the Shell: First Assault (offline Vita port): the whole automatic check of one build, start to finish.
# It runs gitsfa-test.sh several times in a row, each time with a different focus (the game plays itself every time),
# then stops and prints a summary. Nothing to do on the Vita while it runs. About 3 hours for all twelve passes (b108: ONLY="9 12" is the short check of that build, about 6 minutes; pass 12 needs the Vita on Wi-Fi).
#
#   bash <the project folder>/gitsfa-check.sh b105
#
# With no argument it checks the newest build in out/. Needs `serve` running in the other Ubuntu tab.
# The first pass also sends any data files that changed (over Wi-Fi; gitsfa-usb.sh beforehand is faster).
# Stop: Ctrl+C. Resume from pass N: START=N bash .../gitsfa-check.sh b105     Only some passes: ONLY="1 3" bash ...
# Results: results/<build>-<n> per pass as usual, and the summary in results/<build>-check.txt.
set -u
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -n "${1:-}" ]]; then LABEL="$1"
else
  newest="$(ls -t "${DIR}"/out/gitsfa-*.vpk 2>/dev/null | head -1)"
  [[ -n "${newest}" ]] || { echo "STOPPED: no builds in ${DIR}/out"; exit 1; }
  LABEL="$(basename "${newest}" .vpk)"; LABEL="${LABEL#gitsfa-}"
fi
[[ -f "${DIR}/out/gitsfa-${LABEL}.vpk" ]] || { echo "STOPPED: ${DIR}/out/gitsfa-${LABEL}.vpk not found"; exit 1; }
SUM="${DIR}/results/${LABEL}-check.txt"
# (focus | TOURSTART | switch files)
PASSES=(
  "full tour: menus, all 17 maps, every mode, the end-of-match screens||"
  "operatives: every operative and skin on the OPERATIVE page (Paz's pose, Azuma's shield)||opview"
  "armory: INSPECT of every gun (barrels, muzzles, the UMP-45)||inspview"
  "Urban: the helicopters (camera follows them), skyline, sewer lids, ground|0|wobjview"
  "waiting room before every map: AUTO BALANCE and START IN||waitroom nomenutour"
  "knife: a knife strike every 2.5 s on every map||knifeview nomenutour"
  "hands: the planting and defusing hands every 14 s (the knife's housing on the forearm), skill gestures||handstest nomenutour"
  "vaults, climbs and ladders on every map||smftest nomenutour"
  "end of a match at once: the team line-up, TOP OPERATIVE (the top operative faces the camera), the result tabs||restest nomenutour"
  "shooting range first (reloads and shots in the range's echo), then the training programs|13|nomenutour"
  "every map again WITHOUT the echo (test switch noreverb): compare its sound mixer time with pass 1||noreverb nomenutour"
  "internet room (b108): the Vita opens a room on the INTERNET connection, asks your router to open the game's port, shows the room code, closes the room||inettest"
)
N=${#PASSES[@]}; START="${START:-1}"
echo "check of ${LABEL}: ${N} passes, started $(date '+%Y-%m-%d %H:%M')" | tee -a "${SUM}"
summarize() {   # summarize PASS_NO WHAT RESULT_DIR RC
  local res="$3" log="$3/boot.log" line
  {
    echo; echo "---- pass $1/${N}: $2"
    echo "     results: ${res}   (script exit code $4)"
    if [[ ! -s "${log}" ]]; then echo "     NO boot.log - the run did not get as far as the game (see ${res}/test.log)"
    else
      echo "     build line: $(grep -m1 'GITS:FA Vita build' "${log}" | tr -d '\r')"
      if grep -q "autoplay finished" "${log}"; then echo "     the scripted run FINISHED on its own"; else echo "     the scripted run did NOT reach its end (time limit, or it stopped early)"; fi
      echo "     maps toured: $(grep -c '^TOUR ' "${log}")   screenshots: $(ls "${res}"/shot-*.jpg 2>/dev/null | wc -l)"
      grep '^TOUR ' "${log}" | sed -E 's/^TOUR +([0-9]+\/[0-9]+) +([a-z0-9_]+).*(fps avg [0-9.]+ worst [0-9.]+).*/       \1 \2: \3/' | tr -d '\r'
      line="$(grep -m1 'tour finished' "${log}" | tr -d '\r')"; [[ -n "${line}" ]] && echo "     ${line}"
      line="$(grep -m1 '^reverb:' "${log}" | tr -d '\r')"; [[ -n "${line}" ]] && echo "     ${line}"   # (b107)
      line="$(grep -m1 '^weapon sounds:' "${log}" | tr -d '\r')"; [[ -n "${line}" ]] && echo "     ${line}"
      line="$(grep -o 'mix [0-9]* us' "${log}" | tail -1)"; [[ -n "${line}" ]] && echo "     sound mixer, slowest 21 ms block of the run: ${line}"
      line="$(grep -o 'audio: [0-9]* late grains' "${log}" | tail -1)"; [[ -n "${line}" ]] && echo "     ${line} (blocks of sound that came late)"
      if grep -q '^netauto: host test (inettest)' "${log}"; then   # (b108, pass 12; addresses are left out of this summary - the room code holds yours, it is in boot.log on this PC only)
        line="$(grep -m1 '^net: INTERNET up' "${log}" | tr -d '\r')"; echo "     ${line:-the network did not come up (is the Vita on Wi-Fi?)}"
        line="$(grep -m1 '^net: internet room:' "${log}" | tr -d '\r' | sed -E 's/; router.s address.*//')"; [[ -n "${line}" ]] && echo "     ${line}"
        line="$(grep -m1 '^inettest: ' "${log}" | tr -d '\r' | sed -E 's/ROOM CODE [0-9A-Z-]+ - /room code made - /')"; echo "     ${line:-no room code line: the room did not open}"
      fi
      line="$(grep -iE 'out of memory|could not load|: failed|FAILED' "${log}" | grep -vc '(0 failed')"; echo "     lines that mention a failed load / memory: ${line}"
    fi
    if ls "${res}"/*.psp2dmp >/dev/null 2>&1; then echo "     CRASH DUMP: $(ls "${res}"/*.psp2dmp | xargs -n1 basename | tr '\n' ' ')"; else echo "     no crash dump"; fi
  } | tee -a "${SUM}"
}
for ((p = START; p <= N; p++)); do
  if [[ -n "${ONLY:-}" ]] && ! grep -qw "${p}" <<<"${ONLY}"; then continue; fi
  IFS='|' read -r what ts mk <<< "${PASSES[$((p - 1))]}"
  echo; echo "================ pass ${p}/${N}: ${what} ($(date '+%H:%M')) ================"
  before="$(ls -d "${DIR}/results/${LABEL}"-[0-9]* 2>/dev/null | wc -l)"
  # a command the Vita never answered (after a game that quit by itself, say) blocks every later one until it is recovered:
  # recover before each pass, and once more + one retry when a pass stops on "An uncertain command is pending"
  ( cd "${VAU:-${HOME}/vita-agent-use}" 2>/dev/null && python3 client/vita_agent.py session recover > /dev/null 2>&1 )
  TOURSTART="${ts}" MARKERS="${mk}" MANUAL=0 bash "${DIR}/gitsfa-test.sh" "${LABEL}"
  rc=$?
  res="$(ls -dt "${DIR}/results/${LABEL}"-[0-9]* 2>/dev/null | head -1)"
  if [[ ! -s "${res}/boot.log" ]] && grep -q "uncertain command is pending" "${res}/test.log" 2>/dev/null; then
    echo "  the Vita had a command pending: recovering it, then this pass again"
    ( cd "${VAU:-${HOME}/vita-agent-use}" 2>/dev/null && python3 client/vita_agent.py session recover 2>&1 | tail -2 | cut -c1-200 ); sleep 8
    TOURSTART="${ts}" MARKERS="${mk}" MANUAL=0 bash "${DIR}/gitsfa-test.sh" "${LABEL}"
    rc=$?
    res="$(ls -dt "${DIR}/results/${LABEL}"-[0-9]* 2>/dev/null | head -1)"
  fi
  after="$(ls -d "${DIR}/results/${LABEL}"-[0-9]* 2>/dev/null | wc -l)"
  if (( after == before )) || [[ ! -s "${res}/boot.log" ]]; then
    summarize "${p}" "${what}" "${res:-none}" "${rc}"
    echo; echo "STOPPED at pass ${p}: the test did not run (read the lines above: is the Vita on, and serve running in the other tab?)."
    echo "When that is fixed, continue with:"
    echo "  START=${p} bash ${DIR}/gitsfa-check.sh ${LABEL}"
    exit 1
  fi
  summarize "${p}" "${what}" "${res}" "${rc}"
  sleep 5
done
echo | tee -a "${SUM}"; echo "check of ${LABEL} finished $(date '+%Y-%m-%d %H:%M'). Summary: ${SUM}" | tee -a "${SUM}"
echo "Tell Claude it's done."
