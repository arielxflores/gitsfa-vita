# databuilder: reader for the game's .dat archives (map.dat, ui.dat, ...). They are zip files whose three record signatures
# are changed (local header DD 88 55 22, central directory 87 EB 2C 2A, end of central directory EE 99 66 33); entries are
# stored (method 0) or deflated (8), with zip64 extra fields. Our own reader (no zipfile patching): it walks the central
# directory, reads each entry's data after its local header, checks the CRC-32.
#   A = Archive(path); A.names() / A.entries (name -> Entry); A.read(name) -> bytes; A.extract(name, dst)
#   python3 datarc.py list <archive> [> listing.txt]        name, size, crc32 per entry (no data)
#   python3 datarc.py get <archive> <name> <out file>
import os, sys, struct, zlib
SIG_LOCAL, SIG_CENTRAL, SIG_END = b'\xdd\x88\x55\x22', b'\x87\xeb\x2c\x2a', b'\xee\x99\x66\x33'

class Entry:
    __slots__ = ('name', 'method', 'crc', 'csize', 'usize', 'offset')
    def __init__(s, name, method, crc, csize, usize, offset):
        s.name, s.method, s.crc, s.csize, s.usize, s.offset = name, method, crc, csize, usize, offset

def _zip64(extra, usize, csize, offset):
    """the zip64 extended information field (id 1): replaces the 0xFFFFFFFF values, in this order"""
    o = 0
    while o + 4 <= len(extra):
        hid, ln = struct.unpack_from('<HH', extra, o); body = extra[o + 4:o + 4 + ln]; o += 4 + ln
        if hid != 1: continue
        k = 0
        if usize == 0xFFFFFFFF and k + 8 <= len(body): usize = struct.unpack_from('<Q', body, k)[0]; k += 8
        if csize == 0xFFFFFFFF and k + 8 <= len(body): csize = struct.unpack_from('<Q', body, k)[0]; k += 8
        if offset == 0xFFFFFFFF and k + 8 <= len(body): offset = struct.unpack_from('<Q', body, k)[0]; k += 8
    return usize, csize, offset

class Archive:
    def __init__(s, path):
        s.path = path; s.f = open(path, 'rb'); s.f.seek(0, 2); n = s.f.tell()
        tail = min(n, 65536 + 22); s.f.seek(n - tail); t = s.f.read(tail); i = t.rfind(SIG_END)
        if i < 0: raise ValueError('%s: not a game archive (no end record)' % path)
        _, _, _, _, count, cd_size, cd_off, _ = struct.unpack_from('<IHHHHIIH', t, i)
        if count == 0xFFFF or cd_size == 0xFFFFFFFF or cd_off == 0xFFFFFFFF:
            # zip64: the locator is the 20 bytes before the end record, it points at the zip64 end record (layout as in the
            # zip format; their signatures are not checked, as the game may have changed them too)
            loc = t[i - 20:i]; z64 = struct.unpack_from('<Q', loc, 8)[0]
            s.f.seek(z64); r = s.f.read(56)
            count, cd_size, cd_off = struct.unpack_from('<QQQ', r, 32)
        s.f.seek(cd_off); cd = s.f.read(cd_size); o = 0; s.entries = {}; s.order = []
        for _ in range(count):
            if cd[o:o + 4] != SIG_CENTRAL: raise ValueError('%s: bad central directory record at %d' % (path, o))
            (_, _, _, flags, method, _, _, crc, csize, usize, nl, el, cl, _, _, _, off) = struct.unpack_from('<IHHHHHHIIIHHHHHII', cd, o)
            name = cd[o + 46:o + 46 + nl].decode('utf-8' if flags & 0x800 else 'cp437'); extra = cd[o + 46 + nl:o + 46 + nl + el]
            usize, csize, off = _zip64(extra, usize, csize, off)
            if flags & 1: raise ValueError('%s: %s is encrypted' % (path, name))
            e = Entry(name, method, crc, csize, usize, off); s.entries[name] = e; s.order.append(e); o += 46 + nl + el + cl
        s.lower = {k.lower().replace('\\', '/'): v for k, v in s.entries.items()}
    def names(s): return [e.name for e in s.order]
    def find(s, name):
        """an entry by its path, any letter case, / or \\"""
        return s.entries.get(name) or s.lower.get(name.lower().replace('\\', '/'))
    def read(s, e):
        if isinstance(e, str): e = s.find(e)
        s.f.seek(e.offset); h = s.f.read(30)
        if h[:4] != SIG_LOCAL: raise ValueError('%s: bad local header for %s' % (s.path, e.name))
        nl, el = struct.unpack_from('<HH', h, 26); s.f.seek(e.offset + 30 + nl + el); d = s.f.read(e.csize)
        if e.method == 8: d = zlib.decompress(d, -15)
        elif e.method != 0: raise ValueError('%s: %s: compression method %d' % (s.path, e.name, e.method))
        if len(d) != e.usize or zlib.crc32(d) != e.crc: raise ValueError('%s: %s: size / CRC mismatch' % (s.path, e.name))
        return d
    def extract(s, e, dst):
        d = s.read(e); os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
        tmp = dst + '.part'; open(tmp, 'wb').write(d); os.replace(tmp, dst); return len(d)

if __name__ == '__main__':
    cmd = sys.argv[1]; A = Archive(sys.argv[2])
    if cmd == 'list':
        for e in A.order: print('%d\t%08x\t%d\t%s' % (e.usize, e.crc, e.method, e.name))
    elif cmd == 'get': print(A.extract(A.find(sys.argv[3]), sys.argv[4]), 'bytes')
