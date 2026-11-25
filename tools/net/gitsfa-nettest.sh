#!/usr/bin/env bash
# Ghost in the Shell: First Assault (Vita port): a test of the INTERNET rooms with ONE Vita - this computer plays the second
# player (nettest/peer.py next to this script: the game's own protocol, no picture).
#
#   bash <the project folder>/gitsfa-nettest.sh b115
#
# The build must be installed already (gitsfa-test.sh / gitsfa-check.sh do that). Needs `serve` in the other Ubuntu tab.
# What happens (about 8 minutes, nothing to do on the Vita). Two rounds, the game started for each:
#   1. the Vita opens an INTERNET room by itself (MULTIPLAYER > INTERNET > host), asks its router for the port and shows
#      its ROOM CODE;
#   2. this computer reads the code from the Vita's log and joins with it, as a friend would: the code leads to your
#      router's internet side and back in to the Vita. Many routers refuse that turn-around from inside their own Wi-Fi;
#      then the computer joins by the Vita's address in your Wi-Fi instead, and the script says which of the two it was;
#   3. a Team Deathmatch of 2 minutes on Urban, 2 v 2 with bots: round 1 over the line as it is, round 2 over a line
#      made worse on purpose (80 ms later, 30 ms of jitter, 3% lost, half a second lost every 30 s);
#   4. the room closes (a port the router opened is given back) and the game quits.
# Results: results/<build>-net<N>/ (peer-<round>.txt = the computer's side, boot-<round>.log = the Vita's, screenshots
# every 10 s). The files in that folder hold your internet address; what this script prints does not.
# ROUNDS=1: the first round only.   ADDRESS=a.b.c.d: join there (skips the room code).
set -uo pipefail
BO_WIN="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LABEL="${1:-}"
if [[ -z "${LABEL}" ]]; then newest="$(ls -t "${BO_WIN}"/out/gitsfa-*.vpk 2>/dev/null | head -1)"; LABEL="$(basename "${newest}" .vpk)"; LABEL="${LABEL#gitsfa-}"; fi
[[ -n "${LABEL}" ]] || { echo "give the build, e.g.: bash gitsfa-nettest.sh b115"; exit 1; }
VAU="${VAU:-${HOME}/vita-agent-use}"; TITLE=GITS00001; VDIR=ux0:data/gitsfa; STATE="${HOME}/gitsfa/state"; PEER="${BO_WIN}/nettest/peer.py"
ROUNDS="${ROUNDS:-2}"
mkdir -p "${STATE}" "${BO_WIN}/results"
n=1; while [[ -e "${BO_WIN}/results/${LABEL}-net${n}" ]]; do n=$((n+1)); done
RES="${BO_WIN}/results/${LABEL}-net${n}"; mkdir -p "${RES}"
exec > >(tee -a "${RES}/test.log") 2>&1
cd "${VAU}" || { echo "no ${VAU}: set VAU=/path/to/vita-agent-use"; exit 1; }
va() { python3 client/vita_agent.py "$@"; }
ok() { grep -qE '"status": *"ok"|"complete": *true' <<<"$1"; }
hide() { sed -E 's/[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}(:[0-9]+)?/<address>/g; s/ROOM CODE [0-9A-Za-z]{4}-[0-9A-Za-z]{4}-[0-9A-Za-z]{3}/ROOM CODE <on the Vita>/g'; }
getlog() {   # this run's part of the Vita's log -> $1 (the game writes it in place: a marker ends this run's text)
  rm -f "$1"; va fs download "${VDIR}/boot.log" "$1" >/dev/null 2>&1; [[ -s "$1" ]] || return 1
  python3 -c "import sys; p=sys.argv[1]; d=open(p,'rb').read(); i=d.find(b'\\n@@@ END OF THIS RUN'); d=d[:i+1] if i>=0 else d.rstrip(b'\\n')+b'\\n'; open(p,'wb').write(d)" "$1" 2>/dev/null
}
echo "internet room test ${LABEL}-net${n} started $(date)"
[[ -f "${PEER}" ]] || { echo "STOPPED: ${PEER} is missing"; exit 1; }

echo "== checking the Vita"
r="$(va server status 2>&1)"; grep -qiE 'connected"?: *true' <<<"$r" || {
  echo "STOPPED: serve isn't connected. In the other tab run:"
  echo "  cd ~/vita-agent-use && python3 client/vita_agent.py session connect && python3 client/vita_agent.py serve"
  exit 1; }
SERVE_IP="$(grep -oE '\b(10|192\.168|172\.(1[6-9]|2[0-9]|3[01]))\.[0-9]{1,3}\.[0-9]{1,3}(\.[0-9]{1,3})?\b' <<<"$r" | grep -E '^([0-9]+\.){3}[0-9]+$' | head -1)"
[[ -z "${SERVE_IP}" ]] && SERVE_IP="$(find . -maxdepth 2 -type f \( -name '*.json' -o -name '*.toml' -o -name '*.ini' -o -name '*.cfg' \) -size -64k 2>/dev/null | xargs -r grep -hoE 'vita_ip"?[ :=]+"?[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' | head -1)"   # (the agent's own setting: used only when the Vita's log cannot be read while the game runs)
if va app running 2>/dev/null | grep -q "${TITLE}"; then echo "  the game is running: closing it first"; va app close ${TITLE} >/dev/null 2>&1; sleep 4; fi
for m in autoplay inettest; do ok "$(va fs stat "${VDIR}/${m}" 2>&1)" && { va fs trash "${VDIR}/${m}" --yes >/dev/null 2>&1; echo "  switch file ${m} taken away"; }; done
echo "host inet players=2 mode=0 map=11 team=2 bots=1 time=2 score=50 matches=1" > "${STATE}/netauto.txt"
va fs trash "${VDIR}/netauto.txt" --yes >/dev/null 2>&1; rm -f "${STATE}/netauto.state"
r="$(va fs upload "${STATE}/netauto.txt" "${VDIR}/netauto.txt" --transfer-state "${STATE}/netauto.state" 2>&1)"
ok "$(va fs stat "${VDIR}/netauto.txt" 2>&1)" || { echo "STOPPED: could not put the test's script on the Vita:"; echo "$r" | tail -2 | cut -c1-300; exit 1; }
: > "${STATE}/empty"; rm -f "${STATE}/marker-verbose.state"; va fs upload "${STATE}/empty" "${VDIR}/verbose" --transfer-state "${STATE}/marker-verbose.state" >/dev/null 2>&1   # (a frame rate line a second in the log)
cleanup() { va fs trash "${VDIR}/netauto.txt" --yes >/dev/null 2>&1; va fs trash "${VDIR}/verbose" --yes >/dev/null 2>&1; }
SHOTS=""; trap '[[ -n "${SHOTS}" ]] && kill ${SHOTS} 2>/dev/null; cleanup' EXIT
dumps() { va fs list ux0:data 2>/dev/null | grep -o '"[^"]*\.psp2dmp"' | sort -u; }
dumps > "${STATE}/net-dumps-before.txt"
TURN=""   # "no" once the room code's way did not answer: the next round goes to the Wi-Fi address at once
SHOTN=1; declare -a SUMMARY=()

round() {   # round <number> <what the line is> [peer.py's line options...]
  local K="$1" WHAT="$2"; shift 2; local LINE=("$@") CODE="" NOTE="" LAN="" L="${STATE}/netlog.txt" VIA="" RC=9 start el c B nd
  echo; echo "======== round ${K} of ${ROUNDS}: ${WHAT}"
  va fs trash "${VDIR}/netroom.txt" --yes >/dev/null 2>&1; : > "${STATE}/netroom-before.txt"   # (b123: the room file of an earlier launch away - its code is the same every time)
  echo "== launching: the Vita opens an INTERNET room by itself"
  r="$(va app launch ${TITLE} 2>&1)"; echo "$r" | tail -1 | cut -c1-160
  start=$(date +%s)
  ( i=${SHOTN}; while :; do sleep 10; va screen capture --output "${RES}/shot-$(printf %02d $i).jpg" >/dev/null 2>&1; i=$((i+1)); done ) & SHOTS=$!
  sleep 20
  while :; do
    el=$(( $(date +%s) - start ))
    # b123: the room code and the Vita's Wi-Fi address come from ux0:data/gitsfa/netroom.txt (256 bytes, written by the game when
    # its room opens). The log cannot be downloaded while the game runs: it changes between the 16 KB pieces of a download.
    R="${STATE}/netroom.txt"; rm -f "$R"
    if va fs download "${VDIR}/netroom.txt" "$R" >/dev/null 2>&1 && [[ -s "$R" ]] && ! cmp -s "$R" "${STATE}/netroom-before.txt"; then
      c="$(grep -a '^code ' "$R" | head -1 | cut -c6- | tr -d ' ')"; [[ "$c" == "-" ]] && c=""
      l="$(grep -a '^lan ' "$R" | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' | head -1)"; [[ -n "$l" ]] && LAN="$l"
      if [[ -n "$c" ]]; then CODE="$c"; NOTE="$(grep -a '^note ' "$R" | head -1 | cut -c6- | sed -E 's/^\(//; s/\) *$//; s/ +$//')"; break; fi
      (( el > 60 )) && grep -aq '^state open-no-code' "$R" && [[ -n "${LAN}" ]] && { echo "  the room is open but has no room code after ${el} s"; break; }
    fi
    (( el > 150 )) && { echo "  no room code from the Vita after ${el} s"; break; }
    va app running 2>/dev/null | grep -q "${TITLE}" || { sleep 3; va app running 2>/dev/null | grep -q "${TITLE}" || { echo "  the game is not running (${el} s)"; break; }; }
    sleep 5
  done
  [[ -s "$L" ]] && grep -a "net: internet room:" "$L" | tail -1 | sed -E "s/; router.s address.*//; s/^/  /" | cut -c1-200
  [[ -n "${CODE}" ]] && echo "  the Vita shows a room code. What it says about its port: ${NOTE}"
  [[ -z "${LAN}" ]] && LAN="${SERVE_IP}"
  run_peer() { python3 "${PEER}" join "$1" --version "${LABEL}" --seconds 240 --matches 1 --find "$2" "${LINE[@]}" > "${RES}/$3" 2>&1; }
  if [[ -n "${ADDRESS:-}" ]]; then
    echo "== joining at the address you gave"; VIA="the address given"; run_peer "${ADDRESS}" 15 "peer-${K}.txt"; RC=$?
  elif [[ -n "${CODE}" && "${TURN}" != no ]]; then
    echo "== joining with the room code (through your router's internet side), as a friend would"
    VIA="the room code"; run_peer "${CODE}" 10 "peer-${K}.txt"; RC=$?
    if (( RC == 2 )) && [[ -n "${LAN}" ]]; then
      TURN=no
      echo "  no answer that way. From inside your own Wi-Fi that usually means the router does not turn such a packet around"
      echo "  (it can still let one in from outside). Joining by the Vita's address in your Wi-Fi instead."
      mv "${RES}/peer-${K}.txt" "${RES}/peer-${K}-by-code.txt"; VIA="the Vita's address in your Wi-Fi"; run_peer "${LAN}" 15 "peer-${K}.txt"; RC=$?
    fi
  elif [[ -n "${LAN}" ]]; then
    echo "== joining by the Vita's address in your Wi-Fi$([[ -z "${CODE}" ]] && echo " (no room code)")"; VIA="the Vita's address in your Wi-Fi"; run_peer "${LAN}" 15 "peer-${K}.txt"; RC=$?
  else
    echo "== nothing to join: neither a room code nor the Vita's address is known"
  fi
  [[ -s "${RES}/peer-${K}.txt" ]] && { echo "-- the computer's side (joined by ${VIA}):"; grep -v "so far" "${RES}/peer-${K}.txt" | hide | cut -c1-330 | sed 's/^/   /'; }

  echo "== waiting for the Vita to close the room and quit"
  for i in $(seq 1 14); do va app running 2>/dev/null | grep -q "${TITLE}" || break; sleep 5; done
  kill ${SHOTS} 2>/dev/null; wait ${SHOTS} 2>/dev/null; SHOTS=""; SHOTN=$(( SHOTN + 40 ))
  va screen capture --output "${RES}/shot-end-${K}.jpg" >/dev/null 2>&1
  va app running 2>/dev/null | grep -q "${TITLE}" && { echo "  still running: closing it"; va app close ${TITLE} >/dev/null 2>&1; sleep 3; }
  B="${RES}/boot-${K}.log"; for try in 1 2 3; do getlog "$B" && break; sleep 3; done
  if [[ -s "$B" ]]; then
    echo "-- the Vita's side:"
    grep -a -m1 "GITS:FA Vita build" "$B" | sed 's/^/   /'
    grep -aE "^net: (internet room|hosting|.* joined from|.* is in the match|everyone is in|player .* left|refused|room closed)|^netauto: |^stopping" "$B" | hide | cut -c1-220 | sed 's/^/   /'
    python3 - "$B" <<'PY'
import re, sys
t = open(sys.argv[1], errors='replace').read(); ms, snaps, drops, n = [], 0, 0, 0
for m in re.finditer(r'^net: host ([0-9.]+) ms/frame main.*? (\d+) snapshots .*?ring drops (\d+)/(\d+)', t, re.M):
    n += 1; ms.append(float(m.group(1))); snaps += int(m.group(2)); drops += int(m.group(3)) + int(m.group(4))
if n: print('   the host side of the match: %d s logged, network code %.2f ms a frame on average (worst second %.2f), %.1f snapshots a second, packets dropped inside the game: %d' % (n, sum(ms) / n, max(ms), snaps / n, drops))
i = t.find('net: everyone is in'); fps = [float(x) for x in re.findall(r'^fps ([0-9.]+)', t[i:] if i >= 0 else '', re.M)]
if fps: print('   frame rate in the match (a line a second): average %.1f, lowest %.1f, seconds under 55: %d of %d' % (sum(fps) / len(fps), min(fps), sum(f < 55 for f in fps), len(fps)))
PY
    echo "   players the host dropped for silence: $(grep -ac 'left (timed out)' "$B")"
  else echo "  could not download the Vita's log"; fi
  dumps > "${STATE}/net-dumps-after.txt"; nd="$(comm -13 "${STATE}/net-dumps-before.txt" "${STATE}/net-dumps-after.txt")"
  if [[ -n "${nd}" ]]; then echo "   NEW CRASH DUMP on the Vita: ${nd}"; else echo "   no crash dump"; fi
  case "${RC}" in
    0) SUMMARY+=("round ${K} (${WHAT}): the match was played to the end, joined by ${VIA}");;
    2) SUMMARY+=("round ${K} (${WHAT}): the room did not answer the computer");;
    3) SUMMARY+=("round ${K} (${WHAT}): the room refused the computer");;
    4) SUMMARY+=("round ${K} (${WHAT}): the connection was lost");;
    *) SUMMARY+=("round ${K} (${WHAT}): did not finish");;
  esac
  [[ "${VIA}" == "the room code" && "${RC}" == 0 ]] && CODE_WAY=yes
  return "${RC}"
}
CODE_WAY=""
round 1 "the line as it is"; R1=$?
if (( ROUNDS >= 2 )); then
  if (( R1 == 0 )); then sleep 5; round 2 "a line made worse on purpose" --lag 80 --jitter 30 --loss 3 --cut 30
  else SUMMARY+=("round 2: not run (round 1 has to work first)"); fi
fi
echo; echo "RESULT"; for l in "${SUMMARY[@]}"; do echo "  ${l}"; done
if [[ "${CODE_WAY}" == yes ]]; then echo "  The room code led through your router's internet side to the Vita: the way a friend's Vita comes in is open."
elif (( R1 == 0 )); then echo "  The game's side works. Whether a packet from OUTSIDE reaches the Vita is what the port line above says; only a second place can prove it."; fi
echo "Results: ${RES}"
