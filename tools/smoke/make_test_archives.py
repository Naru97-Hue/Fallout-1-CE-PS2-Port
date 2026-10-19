#!/usr/bin/env python3
"""Writes a test .ZIP and Fallout 1 .DAT for modarc_test.cc."""
import struct, sys, zipfile

out = sys.argv[1]
with zipfile.ZipFile(f"{out}/TEST.ZIP", "w") as z:
    z.writestr("MyMod/MOD.TXT", "Test\nme\nhello")
    z.writestr("MyMod/data/art/critters/test.frm", "deflated critter art, " * 2 + "deflated critter art", zipfile.ZIP_DEFLATED)
    z.writestr("MyMod/data/text/english/game/stored.msg", "stored", zipfile.ZIP_STORED)

# DAT: one stored file, one chunked-LZSS file (an LZSS block of 16 literals
# + a back-reference, then a raw block).
lit = b"ABCDEFGH"
lz = bytes([0xFF]) + lit + bytes([0xFF]) + lit  # 16 literals
raw = b"rawtail"
chunked = struct.pack(">H", len(lz)) + lz + struct.pack(">H", 0x8000 | len(raw)) + raw
files = {"MAPS": [("TEST.MAP", 0x20, b"plain stored map", None)],
         "SCRIPTS": [("LZSS.INT", 0x40, chunked, 16 + len(raw))]}
dirs = list(files)
head = struct.pack(">IIII", len(dirs), 0, 0, 0) + b"".join(bytes([len(d)]) + d.encode() for d in dirs)
index_size = sum(16 + sum(1 + len(n) + 16 for n, *_ in files[d]) for d in dirs)
offset = len(head) + index_size
index = b""; data = b""
for d in dirs:
    index += struct.pack(">IIII", len(files[d]), 0, 16, 0)
    for name, attr, payload, size in files[d]:
        index += bytes([len(name)]) + name.encode() + struct.pack(">IIII", attr, offset + len(data), size or len(payload), len(payload) if attr == 0x40 else 0)
        data += payload
open(f"{out}/TEST.DAT", "wb").write(head + index + data)
