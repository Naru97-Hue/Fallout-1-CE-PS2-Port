#ifndef FALLOUT_PLIB_GNW_PS2HID_H_
#define FALLOUT_PLIB_GNW_PS2HID_H_

#ifdef __PS2__

namespace fallout {

typedef void(Ps2HidKeyCallback)(int scancode, bool down);

// Loads the USB keyboard/mouse drivers. Safe to call more than once.
void ps2hid_init();

// Delivers pending USB keyboard presses (SDL scancodes).
void ps2hid_poll_keyboard(Ps2HidKeyCallback* emitKey);

// USB mouse movement since the last call, its buttons and wheel notches.
// Returns true when the mouse moved or scrolled.
bool ps2hid_get_mouse(int* dx, int* dy, bool* left, bool* right, int* wheel);

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2HID_H_ */
