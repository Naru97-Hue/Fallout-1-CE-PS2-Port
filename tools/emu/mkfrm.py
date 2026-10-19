import struct, sys
# Synthetic FRM: one direction, one w x h frame with a checker pattern.
w, h = int(sys.argv[2]), int(sys.argv[3])
px = bytes(((x // 4 + y // 4) % 2) * 200 + 10 for y in range(h) for x in range(w))
frame = struct.pack('>HHIhh', w, h, w * h, 0, 0) + px
head = struct.pack('>IHHH', 4, 10, 0, 1) + struct.pack('>6h', *[0]*6) + struct.pack('>6h', *[0]*6) + struct.pack('>6I', *[0]*6) + struct.pack('>I', len(frame))
open(sys.argv[1], 'wb').write(head + frame)
