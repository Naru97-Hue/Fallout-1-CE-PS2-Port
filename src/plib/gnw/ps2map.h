#ifndef FALLOUT_PLIB_GNW_PS2MAP_H_
#define FALLOUT_PLIB_GNW_PS2MAP_H_

#ifdef __PS2__

#include <SDL.h>

#include "plib/gnw/rect.h"

struct gsGlobal;

namespace fallout {

// GS renderer, stage C: the Graphics Synthesizer draws the map.
//
// While ps2map_active() is true the EE no longer draws the floor, objects,
// roofs or the map-edge shadow into the map window; those pixels stay
// palette index 0 ("holes"). Only what Fallout draws after the roofs
// (selection outlines, floating text, projectiles) is still drawn by the EE,
// on top. At present time the GS draws floor -> objects -> roofs -> edge
// shadow under the visible part of the map window, then the EE frame on top.
//
// The draw list is built by running the game's own render passes with
// gPs2MapMode = PS2MAP_MODE_EMIT: the blit calls in tile.cc and object.cc
// hand their arguments to the ps2map_emit_* functions instead of drawing.

enum {
    PS2MAP_MODE_DRAW = 0, // original EE drawing
    PS2MAP_MODE_COLLECT = 1, // run the pass for its side effects, draw nothing
    PS2MAP_MODE_EMIT = 2, // build the GS draw list
};

extern int gPs2MapMode;

enum {
    PS2MAP_CLUT_BASE = 0, // palette as is (no lighting)
    PS2MAP_CLUT_LIGHT = 1, // darkened like dark_trans_buf_to_buf
};

enum {
    PS2MAP_TRANS_RED = 0,
    PS2MAP_TRANS_WALL,
    PS2MAP_TRANS_GLASS,
    PS2MAP_TRANS_STEAM,
    PS2MAP_TRANS_ENERGY,
    PS2MAP_TRANS_COUNT,
};

// False when GSFLOOR.OFF / GSMAP.OFF exists (everything drawn by the EE).
bool ps2map_enabled();

// True while the GS draws the map (the EE skips those layers).
bool ps2map_active();

// The map view changed (redraw, scroll, lighting): rebuild the draw list.
void ps2map_note_change();

// The palette changed. cyclingOnly: only the colour-cycling entries
// (229-255) changed, which only matter to the base and lit palettes.
void ps2map_palette_changed(bool cyclingOnly);

// Temporarily have the EE draw everything (snapshots of the map view).
void ps2map_set_suspended(bool suspended);

// The map window currently holds a full EE rendering (after a snapshot);
// the next redraw must replace all of it.
void ps2map_mark_baked();
bool ps2map_take_baked();

// --- Draw list (called from tile.cc / object.cc in PS2MAP_MODE_EMIT) -------

// A (part of an) art frame. src points inside frameData at the part's top
// left; destX/destY are map-window coordinates of that part.
void ps2map_emit_part(int fid, int frame, int rotation, const unsigned char* frameData, int frameWidth, int frameHeight,
    const unsigned char* src, int width, int height, int destX, int destY, int clut, int light);

// A floor tile with a light gradient, already lit by the EE (8-bit, base
// palette). intensity: the 10 vertex intensities (cache key).
void ps2map_emit_lit_tile(int fid, const int* intensity, const unsigned char* lit, const unsigned char* base, int width, int height, int destX, int destY);

// A translucent object part (glass, steam, energy, red, wall).
void ps2map_emit_trans(int fid, int frame, int rotation, const unsigned char* frameData, int frameWidth, int frameHeight,
    const unsigned char* src, int width, int height, int destX, int destY, int kind, int light);

// The part of a wall/roof inside the see-through cutout around the player
// (intensity_mask_buf_to_buf).
void ps2map_emit_egg(const unsigned char* src, int srcPitch, int width, int height, int destX, int destY,
    const unsigned char* mask, int maskPitch, int light);

// One of the four 16-pixel darkening strips along the map bounds. side:
// 0 left, 1 top, 2 right, 3 bottom (darkest at the outer edge).
void ps2map_emit_shadow(int ulx, int uly, int lrx, int lry, int side);

// --- Presenting --------------------------------------------------------------

typedef struct Ps2MapView {
    int screenWidth;
    int screenHeight;
    float scaleX;
    float scaleY;
    bool zoomed;
    SDL_Rect zoomSrc;
    int mapHeight;
} Ps2MapView;

// Reserves the map's GS memory (call before binding textures this frame).
void ps2map_prepare(struct gsGlobal* gs);

// Draws the map layers (between SDL's clear and the frame sprite).
void ps2map_draw(struct gsGlobal* gs, const Ps2MapView* view, SDL_Palette* palette);

void ps2map_take_stats(int* records, int* uploads, int* uploadKB, int* rebuilds);

// Lends the map's art atlas (GS memory) to the movie player, which needs a
// ~570 KB frame texture. The map re-uploads its art afterwards. Returns false
// if the map hasn't reserved GS memory.
bool ps2map_borrow_vram(struct gsGlobal* gs, unsigned int* base, int* bytes);

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2MAP_H_ */
