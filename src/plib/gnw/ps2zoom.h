#ifndef FALLOUT_PLIB_GNW_PS2ZOOM_H_
#define FALLOUT_PLIB_GNW_PS2ZOOM_H_

#ifdef __PS2__

#include <SDL.h>

namespace fallout {

// True while the main game map is on screen (zoom only applies there).
bool ps2zoom_available();

// Zoom one step in (+1) or out (-1). Returns true if the level changed.
bool ps2zoom_step(int direction);

// Current magnification (1.0 = normal).
float ps2zoom_level();

// Draws the frame texture with the map area magnified around the cursor and
// the interface bar at normal size. Returns false if zoom is off, in which
// case the caller draws normally.
bool ps2zoom_render(SDL_Renderer* renderer, SDL_Texture* texture, int width, int height);

// Same calculation for the direct GS path: when zoom is active, returns true
// and fills the magnified map source rect (the map occupies 0..mapHeight on
// screen; the interface bar below is drawn 1:1).
bool ps2zoom_map_source(int width, int height, SDL_Rect* src, int* mapHeight);

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2ZOOM_H_ */
