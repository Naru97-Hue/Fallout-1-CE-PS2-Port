#ifndef FALLOUT_PLIB_GNW_PS2CONTROLS_H_
#define FALLOUT_PLIB_GNW_PS2CONTROLS_H_

#ifdef __PS2__

namespace fallout {

// Special (non-keyboard) actions a pad or mouse button can perform.
enum {
    PS2_ACTION_NONE = -1,
    PS2_ACTION_LEFT_CLICK = -2,
    PS2_ACTION_RIGHT_CLICK = -3,
    PS2_ACTION_KEYBOARD = -4, // on-screen keyboard
};

// Loads CONTROLS.CFG (defaults when missing). Safe to call more than once.
void ps2controls_load();

// What a DualShock button (SDL PS2 button index) does: a PS2_ACTION_* value
// or an SDL scancode.
int ps2controls_pad_action(int button);

// USB keyboard: the scancode the game should see for a physical key.
int ps2controls_translate_key(int scancode);

// USB mouse.
bool ps2controls_mouse_swapped();
int ps2controls_mouse_middle_action();

// Cursor speed multiplier (1.0 = default), stick dead zone, zoom direction.
float ps2controls_cursor_speed();
int ps2controls_dead_zone();
bool ps2controls_zoom_inverted();
bool ps2controls_dpad_navigation();

// Key capture for the Controls screen: while active, the next USB keyboard
// key is recorded instead of reaching the game.
bool ps2controls_capture_key(int scancode);

// Video settings kept in CONTROLS.CFG [video] (read once at startup; a
// change takes effect after restarting the game).
enum {
    PS2_VIDEO_SETTING_GS_MAP,
    PS2_VIDEO_SETTING_GS_CURSOR,
    PS2_VIDEO_SETTING_GS_FADES,
    PS2_VIDEO_SETTING_COUNT,
};
bool ps2controls_video_setting(int setting);

// The settings screen (Controls / Gamepad / Keyboard / Video tabs). From the
// main menu it also offers the game's preferences. Returns when closed.
void ps2controls_screen(bool fromMainMenu);

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2CONTROLS_H_ */
