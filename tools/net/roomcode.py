#!/usr/bin/env python3
# b108: the room code of engine/src/net_nat.h (nat_code / nat_decode), for tests:  roomcode.py 203.0.113.7:47750  |  roomcode.py K7QF-2M9X-AB3
import sys
B32 = '0123456789ABCDEFGHJKMNPQRSTVWXYZ'
def chk(v):
    h = 5
    for i in range(6): h = (h * 31 + ((v >> (8 * i)) & 255) + 7) & 0xFFFF
    return (h ^ (h >> 5) ^ (h >> 10)) & 31
def encode(ip, port):
    a = [int(x) for x in ip.split('.')]; v = 0
    for x in a: v = v << 8 | x
    v = v << 16 | port; c = ''.join(B32[(v >> (5 * (9 - i))) & 31] for i in range(10)) + B32[chk(v)]
    return '%s-%s-%s' % (c[:4], c[4:8], c[8:])
def decode(s):
    t = s.upper().replace('-', '').replace(' ', '').replace('O', '0').replace('I', '1').replace('L', '1').replace('U', 'V')
    if len(t) != 11 or any(ch not in B32 for ch in t): return None
    v = 0
    for ch in t[:10]: v = v << 5 | B32.index(ch)
    if B32.index(t[10]) != chk(v): return None
    return '%d.%d.%d.%d:%d' % (v >> 40 & 255, v >> 32 & 255, v >> 24 & 255, v >> 16 & 255, v & 0xFFFF)
if __name__ == '__main__':
    a = sys.argv[1]; print(encode(a.split(':')[0], int(a.split(':')[1])) if '.' in a else decode(a))
