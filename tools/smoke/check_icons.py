#!/usr/bin/env python3
"""Validates icon.sys and the 3D icon against the published PS2 formats."""
import struct
import sys

d = sys.argv[1]
sys_data = open(f"{d}/icon.sys", "rb").read()
assert len(sys_data) == 964, "icon.sys must be 964 bytes"
assert sys_data[:4] == b"PS2D", "bad icon.sys magic"
icon_name = sys_data[0x104:0x144].split(b"\0")[0].decode()
assert icon_name, "no icon named"

ico = open(f"{d}/{icon_name}", "rb").read()
magic, shapes, tex_type, _, verts = struct.unpack_from("<IIIII", ico, 0)
assert magic == 0x10000 and shapes == 1 and tex_type == 7, "bad icon header"
assert verts % 3 == 0 and verts > 0, "vertex count must be whole triangles"
off = 20 + verts * (8 * shapes + 16)
tag, _, _, _, frames = struct.unpack_from("<IIfII", ico, off)
assert tag == 1, "bad animation header"
off += 20
for _ in range(frames):
    _, keys, _, _ = struct.unpack_from("<IIII", ico, off)
    off += 16 + 8 * (keys - 1)
assert len(ico) == off + 128 * 128 * 2, "texture size mismatch"
print(f"OK: icon.sys + {icon_name} ({verts} vertices, {len(ico)} bytes)")
