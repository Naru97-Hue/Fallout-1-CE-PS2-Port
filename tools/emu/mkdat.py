#!/usr/bin/env python3
"""Builds a synthetic MASTER.DAT (Fallout 1 format, stored entries) from the
files under a folder - test data only, no game content."""
import os, struct, sys
src, out = sys.argv[1], sys.argv[2]
files = {}
for root, _, names in os.walk(src):
    rel = os.path.relpath(root, src)
    d = '.' if rel == '.' else rel.replace('/', '\\').upper()
    for n in names:
        files.setdefault(d, []).append((n.upper(), open(os.path.join(root, n), 'rb').read()))
dirs = sorted(files)
head = struct.pack('>IIII', len(dirs), len(dirs), 0, 0) + b''.join(bytes([len(d)]) + d.encode() for d in dirs)
size = len(head) + sum(16 + sum(1 + len(n) + 16 for n, _ in files[d]) for d in dirs)
index, data = b'', b''
for d in dirs:
    entries = sorted(files[d])
    index += struct.pack('>IIII', len(entries), len(entries), 16, 0)
    for n, b in entries:
        index += bytes([len(n)]) + n.encode() + struct.pack('>IIII', 0x20, size + len(data), len(b), 0)
        data += b
open(out, 'wb').write(head + index + data)
print(f"{out}: {sum(len(v) for v in files.values())} files in {len(dirs)} folders")
