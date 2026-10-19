import struct, sys
# Synthetic GNW font: 256 glyphs, 8x8, each a hollow box (test data only).
num, h = 256, 8
info = b''.join(struct.pack('<ii', 6, i * h) for i in range(num))
glyph = bytes([0x7C, 0x44, 0x44, 0x44, 0x44, 0x44, 0x7C, 0x00])
open(sys.argv[1], 'wb').write(struct.pack('<iiiii', num, h, 1, 0, 0) + info + glyph * num)
