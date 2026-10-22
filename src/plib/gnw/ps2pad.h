#ifndef FALLOUT_PLIB_GNW_PS2PAD_H_
#define FALLOUT_PLIB_GNW_PS2PAD_H_

#ifdef __PS2__

#include <SDL.h>

namespace fallout {

// Callback used to deliver emulated keyboard presses (SDL scancodes).
typedef void(Ps2PadKeyCallback)(int scancode, bool down);

// Opens the joystick subsystem. Safe to call more than once.
void ps2pad_init();

// Handles SDL joystick events. Returns true when the event was consumed.
bool ps2pad_handle_event(SDL_Event* event, Ps2PadKeyCallback* emitKey);

// Accumulated cursor movement since the last call, plus emulated mouse
// buttons (Cross = left, Circle = right).
void ps2pad_get_mouse(int* dx, int* dy, bool* left, bool* right);

// Releases everything the pad was holding if it got unplugged (hot swap).
void ps2pad_check_connection(Ps2PadKeyCallback* emitKey);

// Plain menu keys for screens shown before the game is set up (the boot
// mod menu): D-pad = arrow keys, X / Square = Enter, O / Start = Escape.
void ps2pad_set_menu_keys(bool on);

// Mouse-wheel notches from the right stick since the last call (+1 up, -1 down).
int ps2pad_get_wheel();

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2PAD_H_ */
