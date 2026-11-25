# Demuxes a CRI Sofdec2 .usm (the game's skill videos: ui.dat ui\GFx_DA\Data\rsc\video\skill\*.usm) into its raw video
# elementary stream (MPEG-1 in this game, unencrypted) + audio streams. ffmpeg reads the result: ffmpeg -r 24 -i x.m1v ...
# usage: python3 usm_demux.py in.usm out.m1v
import struct, sys
def demux(p, outv):
    d = open(p, 'rb').read(); i = 0; v = bytearray(); a = {}
    while i + 8 <= len(d):
        sig = d[i:i + 4]; sz = struct.unpack('>I', d[i + 4:i + 8])[0]
        off = d[i + 9]; pad = struct.unpack('>H', d[i + 10:i + 12])[0]; ch = d[i + 12]; typ = d[i + 15] & 3
        pay = d[i + 8 + off:i + 8 + sz - pad]
        if typ == 0:                       # 0 = data (1 header, 2 section end, 3 seek info)
            if sig == b'@SFV': v += pay
            elif sig == b'@SFA': a.setdefault(ch, bytearray()).extend(pay)
        i += 8 + sz
    open(outv, 'wb').write(v)
    for ch, b in a.items(): open(outv + '.a%d' % ch, 'wb').write(b)
    return len(v), {k: len(b) for k, b in a.items()}
if __name__ == '__main__': print(demux(sys.argv[1], sys.argv[2]))
