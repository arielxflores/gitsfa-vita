#!/usr/bin/env python3
# b116 (test tool): a second player for a room of the game, played by a computer - so that an INTERNET (or LAN) room can be
# tested with one Vita. It speaks the game's own protocol (engine/src/net_lobby.h, net_game.h): finds the room, joins it,
# says READY, and in the match walks in a slow circle (30 inputs a second), acknowledges the host's snapshots and events and
# measures what arrives. It does not draw or simulate anything: the host does (the match is host-authoritative).
#   peer.py join <ROOM-CODE | a.b.c.d[:port]> --version b115 [--name PC] [--seconds 200] [--find 12]
#           [--lag MS] [--jitter MS] [--loss PERCENT] [--cut SECONDS]     (a worse line, both directions; --cut: 0.5 s lost every N s)
#           [--matches N]  [--bad-from K]     (stay for N matches; the worse line only from match K+1 on: a clean one first)
#           [--show-address]                                              (addresses are not printed without it)
# Exit: 0 a match was played to its end, 2 no room answered, 3 refused, 4 the connection was lost, 5 the time ran out.
import argparse, heapq, random, select, socket, struct, sys, time
from roomcode import decode

NP_DISC, NP_ROOM, NP_JOIN, NP_WELCOME, NP_LOBBY, NP_LSTATE, NP_BYE, NP_INPUT, NP_SNAP = range(1, 10)
NR = ['LOBBY', 'STARTING', 'MATCH', 'RESULTS']
NW = ['OK', 'DIFFERENT BUILD', 'ROOM FULL', 'MATCH IN PROGRESS', 'ROOM CLOSED']
MODES = ['TEAM DEATHMATCH', 'DEMOLITION', 'TERMINAL CONQUEST', 'GHOST ASSAULT']

def fnv(s):
    h = 2166136261
    for c in s.encode(): h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h
def hdr(t, room): return b'GN' + bytes([1, t]) + struct.pack('<I', room)
def cstr(b): return b.split(b'\0')[0].decode('ascii', 'replace')
def fixed(s, n): return s.encode('ascii', 'replace')[:n - 1].ljust(n, b'\0')

class Line:   # the socket, with a worse line on request
    def __init__(self, a):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); self.s.bind(('0.0.0.0', 0)); self.s.setblocking(False)
        self.lag, self.jit, self.loss, self.cut = a.lag / 1000.0, a.jitter / 1000.0, a.loss / 100.0, a.cut
        self.out, self.inq, self.n = [], [], 0; self.t0 = time.time(); self.dropped = 0; self.on = True
    def bad(self, now):
        if not self.on: return False
        if self.cut > 0 and (now - self.t0) % self.cut > self.cut - 0.5: return True
        return random.random() < self.loss
    def due(self): return time.time() + (self.lag + random.random() * self.jit if self.on else 0.0)
    def send(self, to, d):
        now = time.time()
        if self.bad(now): self.dropped += 1; return
        self.n += 1; heapq.heappush(self.out, (self.due(), self.n, to, d))
    def pump(self, wait):
        now = time.time(); nxt = min([q[0][0] for q in (self.out, self.inq) if q] + [now + wait])
        r, _, _ = select.select([self.s], [], [], max(0.0, nxt - now)); now = time.time()
        while self.out and self.out[0][0] <= now:
            _, _, to, d = heapq.heappop(self.out)
            try: self.s.sendto(d, to)
            except OSError: pass
        if r:
            for _ in range(64):
                try: d, frm = self.s.recvfrom(2048)
                except (BlockingIOError, ConnectionError): break
                if self.bad(now): self.dropped += 1; continue
                self.n += 1; heapq.heappush(self.inq, (self.due() if (self.on and (self.lag or self.jit)) else now, self.n, frm, d))
        got = []
        while self.inq and self.inq[0][0] <= now: _, _, frm, d = heapq.heappop(self.inq); got.append((frm, d))
        return got

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('role', choices=['join']); ap.add_argument('target')
    ap.add_argument('--version', default='b123'); ap.add_argument('--name', default='COMPUTER'); ap.add_argument('--seconds', type=float, default=200)
    ap.add_argument('--find', type=float, default=12); ap.add_argument('--lag', type=float, default=0); ap.add_argument('--jitter', type=float, default=0)
    ap.add_argument('--loss', type=float, default=0); ap.add_argument('--cut', type=float, default=0); ap.add_argument('--show-address', action='store_true')
    ap.add_argument('--matches', type=int, default=1); ap.add_argument('--bad-from', type=int, default=0)
    a = ap.parse_args()
    t = a.target if '.' in a.target else decode(a.target)
    if not t: print('this is not a room code (a letter is wrong or missing)'); return 2
    ip, _, port = t.partition(':'); host = (ip, int(port or 47750)); shown = '%s:%d' % host if a.show_address else 'the host'
    build = fnv('%s/p1' % a.version) | 1; L = Line(a); T0 = time.time(); el = lambda: time.time() - T0
    worse = bool(a.lag or a.jitter or a.loss or a.cut); L.on = worse and a.bad_from <= 0
    def say(s): print('%6.1f  %s' % (el(), s)); sys.stdout.flush()
    say('looking for a room at %s (this side: build %s%s)' % (shown, a.version, ', line: +%g ms, jitter %g ms, %g%% lost%s' % (a.lag, a.jitter, a.loss, ', 0.5 s cut every %g s' % a.cut if a.cut else '') + (' - from match %d on' % (a.bad_from + 1) if a.bad_from > 0 else '') if worse else ''))
    phase, room, nonce = 'find', 0, random.getrandbits(32) | 1
    t_disc = t_join = t_ls = t_in = 0.0; host_rx = time.time(); state = last_state = 0; my_slot = -1; joined_at = 0; slots = []
    seq = 0; cmds = []; snap_last = 0xFFFF; ev_last = 0; yaw = 0.0
    M = None; played = 0   # the match's figures
    def match_begin(): return dict(t0=time.time(), last=0.0, snaps=0, bytes=0, tx=0, txb=0, gaps100=0, gaps250=0, gap=0.0, rtt=[], ev=0, lost=0, first=None, prev=None, late=None)
    def match_line(tag):
        d = max(0.001, (M['last'] or time.time()) - M['t0']); r = sorted(M['rtt'])
        say('%s: %d snapshots in %.0f s (%.1f a second; the host sends 30), %d never arrived, longest pause %.0f ms, pauses over 100 ms: %d, over 250 ms: %d; round trip %s; in %.1f kB/s, out %.1f kB/s; %d events' % (
            tag, M['snaps'], d, M['snaps'] / d, M['lost'], M['gap'] * 1000, M['gaps100'], M['gaps250'],
            'median %.0f ms, worst %.0f ms' % (r[len(r) // 2], r[-1]) if r else 'not measured', M['bytes'] / d / 1000, M['txb'] / d / 1000, M['ev']))
    t_report = 0.0; rc = 5; t_over = 0.0
    while el() < a.seconds:
        now = time.time()
        if phase == 'find':
            if el() > a.find: say('NO ANSWER in %.0f s: nothing at that address answers on that port' % a.find); return 2
            if now - t_disc > 1.0: t_disc = now; L.send(host, hdr(NP_DISC, 0) + struct.pack('<I', build))
        elif phase == 'join' and now - t_join > 0.5:
            t_join = now; L.send(host, hdr(NP_JOIN, room) + struct.pack('<II', build, nonce) + fixed(a.name, 20) + bytes([0, 0, 8]))
        elif phase == 'room':
            # b123 (the user's internet test: the Vita loaded Urban for 8.3 s after the countdown and sent nothing meanwhile; the
            # game's own client waits, the host waits 15 s for its players): 20 s while the match starts, 6 s otherwise
            quiet = 20 if state == 1 or (state == 2 and (M is None or not M['snaps'])) else 6
            if now - host_rx > quiet: say('CONNECTION LOST: nothing from the host for %d s (state %s)' % (quiet, NR[state])); rc = 4; break
            if now - t_ls > 0.25:
                t_ls = now; ready = 1 if now - joined_at > 2 else 0
                L.send(host, hdr(NP_LSTATE, room) + bytes([ready, 255, 0, 0, 8, 1 if state == 2 else 0]))
            if state == 2 and now - t_in >= 0.0333 - 0.002:
                dt = min(0.1, now - t_in) if t_in else 0.0333; t_in = now; seq = (seq + 1) & 0xFFFF; yaw = (yaw + dt * 0.5) % 6.2831853
                cmds.append(struct.pack('<HHbbBHhBBB', seq, max(1, min(10000, int(dt * 100000 + 0.5))), 0, 100, 0, int(yaw * 65536 / 6.2831853) & 0xFFFF, 0, 64, 0, 0)); cmds = cmds[-4:]
                pk = hdr(NP_INPUT, room) + struct.pack('<IHHB', int((now - T0) * 1000) & 0xFFFFFFFF, snap_last, ev_last, len(cmds)) + b''.join(cmds) + b'\0'
                L.send(host, pk); M['tx'] += 1; M['txb'] += len(pk) + 28
                if now - t_report > 20: t_report = now; match_line('so far')
        for frm, d in L.pump(0.005):
            if len(d) < 8 or d[:2] != b'GN' or d[2] != 1 or frm[0] != host[0]: continue
            ty = d[3]; rm = struct.unpack_from('<I', d, 4)[0]; b = d[8:]; host_rx = time.time()
            if ty == NP_ROOM and phase == 'find' and len(b) >= 46:
                hb = struct.unpack_from('<I', b, 0)[0]; ver = cstr(b[4:16]); st = b[16]; mode, mp, team, bots = b[17], b[18], b[19], b[20]; hum, mx = b[24], b[25]; nm = cstr(b[26:46])
                say('THE ROOM ANSWERED after %.1f s: "%s", %s, %d v %d, %d of %d players, build %s, %s' % (el(), nm, MODES[mode] if mode < len(MODES) else 'mode %d' % mode, team, team, hum, mx, ver, NR[st] if st < 4 else st))
                if hb != build: say('REFUSED: the room runs build %s, this side says %s (give --version %s)' % (ver, a.version, ver)); return 3
                room = rm; phase = 'join'; host = frm
            elif rm != room: continue
            elif ty == NP_WELCOME and phase == 'join' and len(b) >= 6:
                n, res, slot = struct.unpack_from('<IBB', b, 0)
                if n != nonce: continue
                if res != 0: say('REFUSED: %s' % (NW[res] if res < len(NW) else res)); return 3
                phase = 'room'; my_slot = slot; joined_at = time.time(); say('JOINED: place %d (GHOST %s)' % (slot, 'AB'[slot & 1]))
            elif ty == NP_LOBBY and phase == 'room' and len(b) >= 17:
                state = b[2]; ns = b[16]; sl = []
                for k in range(ns):
                    o = 17 + k * 22
                    if o + 22 > len(b): break
                    if b[o] & 1: sl.append('%s%s%s' % (cstr(b[o + 6:o + 22]), ' (host)' if b[o] & 8 else '', ' READY' if b[o] & 2 else ''))
                if sl != slots: slots = sl; say('in the room: ' + ', '.join(sl))
                if state != last_state:
                    say('the room: %s' % NR[state])
                    if state == 2:
                        M = match_begin(); t_in = 0; cmds = []; snap_last = 0xFFFF; ev_last = 0; t_report = time.time()
                        if worse and not L.on and played >= a.bad_from: L.on = True; L.t0 = time.time(); say('from here on the line is made worse on purpose')
                    if last_state == 2 and M: match_line('THE MATCH IS OVER'); played += 1; t_over = time.time()
                    if state == 0 and played >= a.matches: rc = 0
                    last_state = state
            elif ty == NP_BYE and phase == 'room':
                say('the host closed the room'); rc = 0 if played >= a.matches else 4 if not played else 5; phase = 'gone'
            elif ty == NP_SNAP and phase == 'room' and M is not None and len(b) >= 22:
                sq, base, hms, cl, il, echo, age, n, en = struct.unpack_from('<HHIHHIHHH', b, 0); now = time.time()
                if M['first'] is None: M['first'] = sq; say('IN THE MATCH: the first snapshot arrived (%d bytes of state)' % n)
                elif M['prev'] is not None:
                    step = (sq - M['prev']) & 0xFFFF
                    if 0 < step < 0x8000: M['lost'] += step - 1
                    g = now - M['last']; M['gap'] = max(M['gap'], g); M['gaps100'] += g > 0.1; M['gaps250'] += g > 0.25
                if M['prev'] is None or 0 < ((sq - M['prev']) & 0xFFFF) < 0x8000: M['prev'] = sq; snap_last = sq
                M['last'] = now; M['snaps'] += 1; M['bytes'] += len(d) + 28
                if echo: M['rtt'].append(max(0.0, ((int((now - T0) * 1000) - echo) & 0xFFFFFFFF) - age))
                o = 22 + en
                if o < len(b):
                    ne = b[o]; o += 1
                    for _ in range(ne):
                        if o + 3 > len(b): break
                        es, ln = struct.unpack_from('<HB', b, o); o += 3 + ln
                        if es == ((ev_last + 1) & 0xFFFF): ev_last = es; M['ev'] += 1
        if phase == 'gone': break
        if rc == 0 and phase == 'room' and state == 0: break
        if phase == 'room' and played >= a.matches and state != 2 and time.time() - t_over > 12: rc = 0; say('leaving the room after the results'); break   # (the host may start another match: we have played ours)
    if phase == 'room': L.send(host, hdr(NP_BYE, room) + b'\0'); L.pump(0.05); L.pump(0.05)
    if M is not None and state == 2: match_line('stopped during the match')
    if L.dropped: say('(this side dropped %d packets on purpose)' % L.dropped)
    say('RESULT: %s' % ('%d match%s played to the end' % (played, '' if played == 1 else 'es') if rc == 0 else 'the connection was lost' if rc == 4 else 'stopped after %d of %d matches (%s)' % (played, a.matches, 'in the match' if state == 2 else 'in the room, no match started' if phase == 'room' else phase)))
    return rc
if __name__ == '__main__': sys.exit(main())
