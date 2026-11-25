#!/usr/bin/env python3
# b108: an internet room takes packets from anyone who learns its address, so the host's packet handlers get hammered here.
# Joins a room on 127.0.0.1 as a player (so the in-room and in-match handlers are reached), then sends
#   - well-formed packets with hostile values (NaN / huge floats, out-of-range indices, wrong lengths),
#   - packets cut short or with random tails, random types, pure noise,
#   - the same from other source ports (strangers: join floods, inputs without a seat).
#   python3 fuzz.py [--port 47750] [--secs 60] [--seed 1]
# Run the host under the ASan build (MODE=asan tools/hostgame/nettest.sh ... "host inet players=2 ...") and look for sanitizer reports.
import socket, struct, random, time, argparse, math
ap = argparse.ArgumentParser(); ap.add_argument('--port', type=int, default=47750); ap.add_argument('--secs', type=float, default=60); ap.add_argument('--seed', type=int, default=1); ap.add_argument('--wait', type=float, default=40)
A = ap.parse_args(); R = random.Random(A.seed); HOST = ('127.0.0.1', A.port)
DISC, ROOM, JOIN, WELCOME, LOBBY, LSTATE, BYE, INPUT, SNAP = range(1, 10)
def hdr(t, room): return b'GN\x01' + bytes([t]) + struct.pack('<I', room)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(0.5)
room = build = None; t0 = time.time()
while room is None and time.time() - t0 < A.wait:
    s.sendto(hdr(DISC, 0) + struct.pack('<I', 0), HOST)
    try:
        d, _ = s.recvfrom(2048)
        if d[:3] == b'GN\x01' and d[3] == ROOM: room, build = struct.unpack_from('<II', d, 4)
    except (socket.timeout, ConnectionError): pass
if room is None: raise SystemExit('fuzz: no room answered on %s:%d' % HOST)
print('fuzz: room %08x build %08x' % (room, build), flush=True)
nonce = R.getrandbits(32)
def join(sock, nonce, name=b'FUZZ', chr_=3, prim=10, sec=8): sock.sendto(hdr(JOIN, room) + struct.pack('<II', build, nonce) + name.ljust(20, b'\0')[:20] + bytes([chr_ & 255, prim & 255, sec & 255]), HOST)
acked = [0, 0]   # the host's last command / event sequence of our seat (from its snapshots): the next event must be +1
def drain(sock):
    sock.setblocking(False); n = 0
    try:
        while True:
            d, _ = sock.recvfrom(4096); n += 1
            if sock is s and len(d) >= 20 and d[:3] == b'GN\x01' and d[3] == SNAP: acked[0], acked[1] = struct.unpack_from('<HH', d, 16)
    except (BlockingIOError, ConnectionError): pass
    return n
join(s, nonce); time.sleep(0.3)
BADF = [float('nan'), float('inf'), -float('inf'), 3.4e38, -3.4e38, 1e-40, 0.0, -0.0, 1e9, -1e9, 65536.0, 123.0]
def f(): return R.choice(BADF) if R.random() < 0.5 else R.uniform(-20000, 20000)
def u8(): return R.choice([0, 1, 2, 7, 8, 11, 12, 13, 31, 32, 63, 64, 127, 128, 200, 254, 255]) if R.random() < 0.6 else R.randrange(256)
def u16(): return R.choice([0, 1, 0x7FFF, 0x8000, 0xFFFF]) if R.random() < 0.4 else R.randrange(65536)
cseq = 0; eseq = 0
def event():
    t = R.choice([1, 2, 3, 4, 1, 1, 16, 17, 18, 19, 20, 21, 0, 255, R.randrange(256)])
    if t == 1:
        np_ = u8() if R.random() < 0.3 else R.randrange(1, 13); b = struct.pack('<IBB3f', R.getrandbits(32), u8() if R.random() < 0.5 else R.randrange(64), np_, f(), f(), f())
        b += b''.join(struct.pack('<HH', u16(), u16()) for _ in range(min(np_, 12) if R.random() < 0.8 else R.randrange(14)))
    elif t == 3: b = struct.pack('<BB3fHH', u8(), u8(), f(), f(), f(), u16(), u16())
    elif t == 2: b = struct.pack('<7f', f(), f(), f(), f(), f(), f(), R.choice([0.5, 1.0, 3.9, f()]))
    elif t == 4: b = struct.pack('<3fH', f(), f(), f(), u16())
    else: b = bytes(R.randrange(256) for _ in range(R.randrange(0, 40)))
    b = bytes([t]) + b
    if R.random() < 0.15: b = b[:R.randrange(1, len(b) + 1)]
    if R.random() < 0.1: b += bytes(R.randrange(256) for _ in range(R.randrange(1, 30)))
    return b[:255]
def inp():
    global cseq, eseq
    eseq = acked[1]; cseq = max(cseq, acked[0]) if abs(cseq - acked[0]) < 20000 else acked[0]
    nc = R.choice([0, 1, 1, 2, 4, 8, 9, 255]) if R.random() < 0.9 else R.randrange(256); b = struct.pack('<IHHB', R.getrandbits(32), u16(), u16(), nc)
    for _ in range(min(nc, 8)):
        cseq = (cseq + R.choice([1, 1, 1, 2, 0, 30000])) & 0xFFFF
        b += struct.pack('<HHbbBHhBBB', cseq, u16(), R.randrange(-128, 128), R.randrange(-128, 128), R.randrange(256), u16(), R.randrange(-32768, 32768), u8(), u8(), u8())
    ne = R.choice([0, 1, 1, 2, 3, 6]); b += bytes([ne if R.random() < 0.9 else u8()])
    for _ in range(ne):
        e = event(); eseq = (eseq + 1) & 0xFFFF if R.random() < 0.95 else R.randrange(65536)
        b += struct.pack('<HB', eseq, len(e) if R.random() < 0.9 else u8()) + e
    return b
def lstate(): return bytes([1 if R.random() < 0.8 else u8(), u8() if R.random() < 0.2 else 2, u8(), u8(), u8(), 1 if R.random() < 0.9 else u8()])
others = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(12)]
sent = 0; rx = 0; t0 = time.time(); last = 0
while time.time() - t0 < A.secs:
    now = time.time() - t0
    if now - last > 0.1:   # the seated player stays alive: ready, loaded
        last = now; join(s, nonce)   # (the seat again if the host dropped us; a repeat is answered with the same seat)
        s.sendto(hdr(LSTATE, room) + bytes([1, 2, 3, 10, 8, 1]), HOST); sent += 1
    for _ in range(20):
        k = R.random(); sock = s if R.random() < 0.8 else R.choice(others)
        if k < 0.55: p = hdr(INPUT, room) + inp()
        elif k < 0.65: p = hdr(LSTATE, room) + lstate()
        elif k < 0.72: p = hdr(R.choice([0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11]) if sock is s else R.randrange(0, 12) if R.random() < 0.8 else R.randrange(256), room if R.random() < 0.8 else R.getrandbits(32)) + bytes(R.randrange(256) for _ in range(R.randrange(0, 1400)))
        elif k < 0.78: p = bytes(R.randrange(256) for _ in range(R.randrange(0, 1500)))
        elif k < 0.84: p = hdr(JOIN, room) + struct.pack('<II', build if R.random() < 0.8 else R.getrandbits(32), R.getrandbits(32)) + bytes(R.randrange(256) for _ in range(R.randrange(0, 30)))
        elif k < 0.88: p = hdr(DISC, R.getrandbits(32)) + bytes(R.randrange(256) for _ in range(R.randrange(0, 8)))
        elif k < 0.92: p = hdr(SNAP, room) + bytes(R.randrange(256) for _ in range(R.randrange(0, 1200)))   # (a host must ignore these)
        elif k < 0.96: p = b'\x01\x01' + struct.pack('>HI', R.randrange(0, 1200), 0x2112A442) + bytes(R.randrange(256) for _ in range(R.randrange(0, 200)))   # a STUN-looking answer
        else: p = (hdr(INPUT, room) + inp())[:R.randrange(0, 40)]
        if sock is not s and R.random() < 0.02: join(sock, R.getrandbits(32), bytes(R.randrange(256) for _ in range(20)), u8(), u8(), u8())
        try: sock.sendto(p[:1472], HOST); sent += 1
        except OSError: pass
    for q in [s] + others: rx += drain(q)
    time.sleep(0.02)
print('fuzz: %d packets sent, %d received from the host in %.0f s' % (sent, rx, time.time() - t0))
