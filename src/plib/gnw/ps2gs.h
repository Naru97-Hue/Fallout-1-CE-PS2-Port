#ifndef FALLOUT_PLIB_GNW_PS2GS_H_
#define FALLOUT_PLIB_GNW_PS2GS_H_

#ifdef __PS2__

#include <SDL.h>

namespace fallout {

// Stage A of the GS renderer: the EE keeps drawing Fallout's 8-bit frame, the
// GS applies the palette (indexed T8 texture + CLUT) and scales it to the TV.

// Region of the 8-bit screen that changed.
void ps2gs_mark_dirty(int x, int y, int width, int height);

// The palette changed (fade, colour cycling, new palette). The range form
// tells which entries changed.
void ps2gs_mark_palette();
void ps2gs_mark_palette_range(int start, int count);

// Draws the frame with the GS. Returns false if the direct path isn't
// available (caller falls back to the SDL texture path).
bool ps2gs_present(SDL_Renderer* renderer, SDL_Surface* screen8, int screenWidth, int screenHeight);

// Stage D: brightness of the whole picture, 0 (black) .. 0x80 (normal), used
// for palette fades to/from black instead of changing the palette.
void ps2gs_set_fade(int level);
bool ps2gs_fades_supported();

// True when the GS draws the mouse cursor (it's then never composited into
// the 8-bit screen).
bool ps2gs_cursor_enabled();

// Queues an image upload to GS memory (psm GS_PSM_T8 or GS_PSM_CT32) at
// (dx, dy) of the buffer at tbp. mem must stay untouched until the frame's
// render queue has run (SDL_RenderPresent).
void ps2gs_upload(void* mem, int width, int height, int psm, unsigned int tbp, int tbw, int dx, int dy);

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2GS_H_ */
