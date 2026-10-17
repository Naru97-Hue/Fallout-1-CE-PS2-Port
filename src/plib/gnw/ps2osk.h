#ifndef FALLOUT_PLIB_GNW_PS2OSK_H_
#define FALLOUT_PLIB_GNW_PS2OSK_H_

#ifdef __PS2__

#include <SDL.h>

#include "plib/gnw/ps2pad.h"

namespace fallout {

bool ps2osk_is_visible();
void ps2osk_toggle();

// Handles a pad button while the keyboard is open. Returns true if consumed.
bool ps2osk_handle_button(int button, bool down, Ps2PadKeyCallback* emitKey);

// Draws the keyboard on top of the frame (call between RenderCopy and Present).
void ps2osk_render(SDL_Renderer* renderer, int screenWidth, int screenHeight);

// Shows a short message at the top of the screen for `ms` milliseconds.
void ps2osk_show_message(const char* text, unsigned int ms);

// True when the overlay changed and needs a redraw.
bool ps2osk_consume_dirty();

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2OSK_H_ */
