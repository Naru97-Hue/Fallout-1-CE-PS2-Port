#ifndef FALLOUT_PLIB_GNW_PS2SCANLINES_H_
#define FALLOUT_PLIB_GNW_PS2SCANLINES_H_

// Where the scanline bands go, in TV (framebuffer) pixels. Kept apart from
// the GS code so tools/run_smoke.sh can check it on a PC.
//
// Progressive modes (480p, 720p): one dark band per pair of picture lines,
// a picture line tall.
// Interlaced modes (480i, 1080i): the TV shows even framebuffer lines in one
// field and odd lines in the next, so darkening every other line would make
// one whole field dark - a 30 Hz flicker. Instead the bands are two lines
// tall every four, which gives both fields the same pattern.
//
// emit(y0, y1) is called for each band [y0, y1). Returns the band count.
template <typename Emit>
static inline int ps2scanlines_bands(int fbHeight, float scaleY, bool interlaced, Emit emit)
{
    int count = 0;
    if (interlaced) {
        for (int y = 2; y < fbHeight; y += 4) {
            emit((float)y, (float)(y + 2 < fbHeight ? y + 2 : fbHeight));
            count++;
        }
        return count;
    }
    // Whole TV lines only, so the result doesn't depend on how the GS
    // rounds sprite edges.
    int pictureLines = (int)(fbHeight / scaleY + 0.5f);
    for (int row = 1; row < pictureLines; row += 2) {
        int y0 = (int)(row * scaleY + 0.5f);
        int y1 = (int)((row + 1) * scaleY + 0.5f);
        if (y1 <= y0) {
            y1 = y0 + 1;
        }
        if (y1 > fbHeight) {
            y1 = fbHeight;
        }
        if (y0 < y1) {
            emit((float)y0, (float)y1);
            count++;
        }
    }
    return count;
}

#endif /* FALLOUT_PLIB_GNW_PS2SCANLINES_H_ */
