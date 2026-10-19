#!/usr/bin/env python3
"""Random LZSS streams through the original and the PS2 fast decoder: the
output must be byte-identical. usage: check_lzss.py <ref> <fast> <tmpdir>"""
import random, subprocess, sys
ref, fast, tmp = sys.argv[1:4]
random.seed(7)
for t in range(200):
    blocks, data = [], b""
    for _ in range(random.randint(1, 4)):
        blk = bytearray()
        for _ in range(random.randint(1, 200)):
            flags = random.randint(0, 255); blk.append(flags)
            for bit in range(8):
                blk += bytes([random.randint(0, 255)]) if flags >> bit & 1 else bytes([random.randint(0, 255), random.randint(0, 255)])
        blocks.append(len(blk)); data += bytes(blk)
    path = f"{tmp}/lzss.bin"
    open(path, "wb").write(data)
    args = [path] + [str(b) for b in blocks]
    if subprocess.run([ref] + args, capture_output=True).stdout != subprocess.run([fast] + args, capture_output=True).stdout:
        print(f"FAIL: LZSS fast path differs (case {t})"); sys.exit(1)
print("OK: LZSS fast path matches the original decoder (200 random streams)")
