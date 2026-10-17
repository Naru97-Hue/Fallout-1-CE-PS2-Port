"""Extracts files from Fallout 1 .DAT archives (used by convert_movies.py)."""
import struct, sys, os
def lzss(data, outsize):
    out = bytearray(); i = 0
    while i < len(data) and len(out) < outsize:
        n = struct.unpack('>h', data[i:i+2])[0]; i += 2
        if n == 0: break
        if n < 0:
            out += data[i:i-n]; i += -n; continue
        end = i + n
        ring = bytearray(b' ' * 4096); r = 4078
        while i < end:
            flags = data[i]; i += 1
            for b in range(8):
                if i >= end: break
                if flags & (1 << b):
                    c = data[i]; i += 1; out.append(c); ring[r] = c; r = (r + 1) & 4095
                else:
                    o = data[i] | ((data[i+1] & 0xF0) << 4); l = (data[i+1] & 0x0F) + 3; i += 2
                    for k in range(l):
                        c = ring[(o + k) & 4095]; out.append(c); ring[r] = c; r = (r + 1) & 4095
    return bytes(out)
def entries(f):
    dc, = struct.unpack('>I', f.read(4)); f.read(12)
    dirs = [f.read(f.read(1)[0]).decode('latin1') for _ in range(dc)]
    for d in dirs:
        fc, = struct.unpack('>I', f.read(4)); f.read(12)
        for _ in range(fc):
            name = f.read(f.read(1)[0]).decode('latin1')
            attr, off, size, packed = struct.unpack('>IIII', f.read(16))
            yield (d + '\\' + name), attr, off, size, packed


def extract(dat, pattern, outdir, ext=""):
    """Extracts files whose path contains pattern (and ends with ext);
    returns the extracted file names."""
    pattern = pattern.upper()
    names = []
    with open(dat, 'rb') as f:
        for path, attr, off, size, packed in list(entries(f)):
            up = path.upper()
            if pattern not in up or not up.endswith(ext.upper()):
                continue
            f.seek(off)
            raw = f.read(packed if attr == 0x40 else size)
            data = lzss(raw, size) if attr == 0x40 else raw
            name = path.split('\\')[-1].lower()
            with open(os.path.join(outdir, name), 'wb') as out:
                out.write(data)
            names.append(name)
    return names


if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("usage: f1dat.py <file.DAT> <path pattern> <output folder>")
        sys.exit(1)
    for n in extract(sys.argv[1], sys.argv[2], sys.argv[3]):
        print(n)
