#!/usr/bin/env bash
# net (stage 1-2): two or more game instances on this machine talking over UDP on 127.0.0.1 (the LAN backend) or the PDP shim
# (HG_NET_PDP=1: the ad-hoc backend's code over loopback). Each instance gets its own out folder with a netauto.txt line
# (engine/src/net_auto.h) and runs on the real clock (HG_REALTIME=1), without audio.
#   nettest.sh <out dir> <seconds> "<netauto line for instance 0>" "<line for instance 1>" ...
#   env: MODE (build: plain | asan, default plain), HG_NET_LOSS (% dropped), HG_NET_LAG (ms), NET_VERSION_1 (instance 1's netversion.txt),
#        NET_STUN / NET_IGD (b108: net_stun.txt / net_igd.txt of every instance; "host inet ..." / "join inet code=<room code>")
# Then: grep "net:" <out>/i*/boot.log
set -e
H="$(cd "$(dirname "$0")" && pwd)"; MODE="${MODE:-plain}"; B="$H/out/gitsfa_$MODE"
[ -x "$B" ] || nice -n 10 bash "$H/build.sh" "$MODE"
O="$1"; T="$2"; shift 2; rm -rf "$O"; mkdir -p "$O"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=0}"
i=0
for line in "$@"; do
	d="$O/i$i"; mkdir -p "$d"; echo "$line" > "$d/netauto.txt"; [ "${VERBOSE:-1}" = 1 ] && touch "$d/verbose"   # (the per-second fps / bots lines)
	v="NET_VERSION_$i"; [ -n "${!v:-}" ] && echo "${!v}" > "$d/netversion.txt"
	[ -n "${NET_STUN:-}" ] && echo "$NET_STUN" > "$d/net_stun.txt"; [ -n "${NET_IGD:-}" ] && echo "$NET_IGD" > "$d/net_igd.txt"   # (b108: the internet room's STUN server / router description, tools/net/fake_router.py)
	( cd "$d"; HG_OUT="$d" HG_REALTIME=1 HG_AUDIO_OFF=1 HG_NET_NAME="P$i" HG_STATUS_EVERY=1800 HG_MAX_FRAMES=$((T * 70)) nice -n 5 timeout "$T" "$B" > run.out 2> run.err; echo "instance $i exit $?" >> "$O/exits.txt" ) &
	sleep "${STAGGER:-1}"; i=$((i + 1))
done
wait
cat "$O/exits.txt"
for d in "$O"/i*; do echo "== $d: $(grep -c . "$d/boot.log" 2>/dev/null) log lines; $(grep -c 'runtime error\|ERROR: AddressSanitizer' "$d/run.err" 2>/dev/null) sanitizer reports"; grep -E "^net: (host|client)|netauto:|net: (match|everyone|joined|found|hosting|START|player|leaving|the host)|stopping" "$d/boot.log" | head -${SHOW:-12}; done
