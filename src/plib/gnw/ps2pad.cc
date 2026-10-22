// PlayStation 2 DualShock -> mouse/keyboard emulation (the D-pad does
// console-style jumps between clickable things, see ps2nav.cc).
//
// Fallout is a mouse-driven game, and SDL2's PS2 port exposes only the pad,
// so the left stick drives the cursor and face/shoulder buttons are mapped to
// the game's mouse buttons and hotkeys.
//
// Button indices follow SDL2's PS2 joystick driver, which reports libpad bits
// in order (bit 0 = Select ... bit 15 = Square).

#ifdef __PS2__

#include "plib/gnw/ps2pad.h"
#include "plib/gnw/ps2controls.h"
#include "plib/gnw/ps2nav.h"
#include "plib/gnw/ps2osk.h"
#include "plib/gnw/ps2video.h"

#include <stdlib.h>

namespace fallout {

enum {
    PAD_SELECT = 0,
    PAD_L3 = 1,
    PAD_R3 = 2,
    PAD_START = 3,
    PAD_UP = 4,
    PAD_RIGHT = 5,
    PAD_DOWN = 6,
    PAD_LEFT = 7,
    PAD_L2 = 8,
    PAD_R2 = 9,
    PAD_L1 = 10,
    PAD_R1 = 11,
    PAD_TRIANGLE = 12,
    PAD_CIRCLE = 13,
    PAD_CROSS = 14,
    PAD_SQUARE = 15,
    PAD_BUTTON_COUNT = 16,
};

enum {
    AXIS_LX = 0,
    AXIS_LY = 1,
    AXIS_RX = 2,
    AXIS_RY = 3,
    AXIS_COUNT = 4,
};

// Scancode for each pad button; 0 means "handled specially / unmapped".
static const int kButtonKeys[PAD_BUTTON_COUNT] = {
    /* SELECT   */ SDL_SCANCODE_TAB, // automap
    /* L3       */ SDL_SCANCODE_B, // switch active hand
    /* R3       */ 0, // toggle on-screen keyboard
    /* START    */ SDL_SCANCODE_ESCAPE, // options / cancel
    /* UP       */ SDL_SCANCODE_UP, // scroll map
    /* RIGHT    */ SDL_SCANCODE_RIGHT,
    /* DOWN     */ SDL_SCANCODE_DOWN,
    /* LEFT     */ SDL_SCANCODE_LEFT,
    /* L2       */ SDL_SCANCODE_C, // character screen
    /* R2       */ SDL_SCANCODE_P, // Pip-Boy
    /* L1       */ SDL_SCANCODE_A, // enter combat
    /* R1       */ SDL_SCANCODE_SPACE, // end turn
    /* TRIANGLE */ SDL_SCANCODE_I, // inventory
    /* CIRCLE   */ 0, // right mouse button
    /* CROSS    */ 0, // left mouse button
    /* SQUARE   */ SDL_SCANCODE_RETURN, // confirm / end combat
};

static const int kScrollThreshold = 16000;
// Cursor speed at full stick deflection, in game pixels per second.
static const float kMaxCursorSpeed = 700.0f;

static bool gInitialized = false;
static SDL_Joystick* gJoystick = NULL;
static int gAxis[AXIS_COUNT];
static bool gHeld[PAD_BUTTON_COUNT];
static bool gLeftDown = false;
static bool gRightDown = false;
static Uint32 gLastTick = 0;
static float gRemainderX = 0.0f;
static float gRemainderY = 0.0f;
// Right-stick scroll state: arrow scancode currently held per axis (0 = none).
static int gScrollKeyX = 0;
static int gScrollKeyY = 0;
// D-pad presses that fell back to arrow keys (to release them on button up).
static bool gDpadSentKey[PAD_BUTTON_COUNT];

// Holding a D-pad direction keeps jumping to the next target: first repeat
// after 300 ms, then every 130 ms (it used to need a new press each time).
#define DPAD_REPEAT_DELAY 300
#define DPAD_REPEAT_RATE 130
static int gDpadHeldDir = -1;
static Uint32 gDpadNextRepeat = 0;
// The action each button started when pressed (released with the button,
// even if the mapping changed in between). PS2_ACTION_NONE = nothing held.
static int gPressedAction[PAD_BUTTON_COUNT] = { PS2_ACTION_NONE, PS2_ACTION_NONE, PS2_ACTION_NONE, PS2_ACTION_NONE,
    PS2_ACTION_NONE, PS2_ACTION_NONE, PS2_ACTION_NONE, PS2_ACTION_NONE, PS2_ACTION_NONE, PS2_ACTION_NONE,
    PS2_ACTION_NONE, PS2_ACTION_NONE, PS2_ACTION_NONE, PS2_ACTION_NONE, PS2_ACTION_NONE, PS2_ACTION_NONE };

static void ps2pad_apply(int action, bool down, Ps2PadKeyCallback* emitKey)
{
    switch (action) {
    case PS2_ACTION_NONE:
        break;
    case PS2_ACTION_LEFT_CLICK:
        gLeftDown = down;
        break;
    case PS2_ACTION_RIGHT_CLICK:
        gRightDown = down;
        break;
    case PS2_ACTION_KEYBOARD:
        if (down) {
            ps2osk_toggle();
        }
        break;
    default:
        emitKey(action, down);
        break;
    }
}

static void ps2pad_release(int button, Ps2PadKeyCallback* emitKey)
{
    if (gPressedAction[button] != PS2_ACTION_NONE) {
        ps2pad_apply(gPressedAction[button], false, emitKey);
        gPressedAction[button] = PS2_ACTION_NONE;
    }
}

bool ps2pad_connected();

static void ps2pad_open_first()
{
    if (gJoystick != NULL) {
        return;
    }

    if (SDL_NumJoysticks() > 0) {
        gJoystick = SDL_JoystickOpen(0);
    }
}

void ps2pad_init()
{
    if (gInitialized) {
        return;
    }

    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
        return;
    }

    SDL_JoystickEventState(SDL_ENABLE);
    ps2pad_open_first();
    gLastTick = SDL_GetTicks();
    gInitialized = true;
}

static void ps2pad_update_scroll(int value, int negKey, int posKey, int* held, Ps2PadKeyCallback* emitKey)
{
    int want = 0;
    if (value <= -kScrollThreshold) {
        want = negKey;
    } else if (value >= kScrollThreshold) {
        want = posKey;
    }

    if (want == *held) {
        return;
    }

    if (*held != 0) {
        emitKey(*held, false);
    }

    if (want != 0) {
        emitKey(want, true);
    }

    *held = want;
}

bool ps2pad_handle_event(SDL_Event* event, Ps2PadKeyCallback* emitKey)
{
    switch (event->type) {
    case SDL_JOYDEVICEADDED:
        ps2pad_open_first();
        return true;
    case SDL_JOYDEVICEREMOVED:
        if (gJoystick != NULL && SDL_JoystickInstanceID(gJoystick) == event->jdevice.which) {
            SDL_JoystickClose(gJoystick);
            gJoystick = NULL;
            gLeftDown = false;
            gRightDown = false;
            for (int i = 0; i < AXIS_COUNT; i++) {
                gAxis[i] = 0;
            }
            ps2pad_open_first();
        }
        return true;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        bool down = event->type == SDL_JOYBUTTONDOWN;
        if (down && !ps2pad_connected()) {
            return true;
        }
        int button = event->jbutton.button;
        if (button >= 0 && button < PAD_BUTTON_COUNT) {
            gHeld[button] = down;
        }

        // L1 + R1 + Select: cycle TV output mode (480i/480p/720p/1080i).
        if (button == PAD_SELECT && gHeld[PAD_L1] && gHeld[PAD_R1]) {
            if (down) {
                // Undo what L1/R1 started when they were pressed.
                ps2pad_release(PAD_L1, emitKey);
                ps2pad_release(PAD_R1, emitKey);
                ps2video_cycle();
            }
            return true;
        }

        if (button < 0 || button >= PAD_BUTTON_COUNT) {
            return true;
        }

        // Confirming a TV mode switched with the hotkey: X keeps it, O undoes
        // it (the Settings screen asks with its own dialog instead).
        if (ps2video_trial_active() && !ps2video_trial_quiet() && (button == PAD_CROSS || button == PAD_CIRCLE)) {
            if (down) {
                ps2video_trial_answer(button == PAD_CROSS);
            }
            return true;
        }

        bool keyboardButton = ps2controls_pad_action(button) == PS2_ACTION_KEYBOARD;
        if (!keyboardButton && ps2osk_handle_button(button, down, emitKey)) {
            // consumed by the on-screen keyboard
        } else if (button >= PAD_UP && button <= PAD_LEFT) {
            // Console-style: jump to the next clickable thing that way;
            // arrow keys where there's nothing to jump to or when turned off.
            if (down) {
                static const int kDirX[4] = { 0, 1, 0, -1 };
                static const int kDirY[4] = { -1, 0, 1, 0 };
                int dir = button - PAD_UP;
                if (!ps2nav_step(kDirX[dir], kDirY[dir])) {
                    emitKey(kButtonKeys[button], true);
                    gDpadSentKey[button] = true;
                    gDpadHeldDir = -1;
                } else {
                    gDpadHeldDir = dir;
                    gDpadNextRepeat = SDL_GetTicks() + DPAD_REPEAT_DELAY;
                }
            } else {
                if (gDpadHeldDir == button - PAD_UP) {
                    gDpadHeldDir = -1;
                }
                if (gDpadSentKey[button]) {
                    emitKey(kButtonKeys[button], false);
                    gDpadSentKey[button] = false;
                }
            }
        } else if (down) {
            ps2pad_release(button, emitKey);
            gPressedAction[button] = ps2controls_pad_action(button);
            ps2pad_apply(gPressedAction[button], true, emitKey);
        } else {
            ps2pad_release(button, emitKey);
        }
        return true;
    }
    case SDL_JOYAXISMOTION:
        if (event->jaxis.axis < AXIS_COUNT && ps2pad_connected()) {
            gAxis[event->jaxis.axis] = event->jaxis.value;
            // Right stick Y acts as the mouse wheel (see ps2pad_get_wheel).
        }
        return true;
    }

    return false;
}

// Maps a raw axis value to [-1, 1] with a dead zone and a quadratic curve,
// so small deflections allow precise aiming.
static float ps2pad_curve(int value)
{
    int magnitude = abs(value);
    int deadZone = ps2controls_dead_zone();
    if (magnitude <= deadZone) {
        return 0.0f;
    }

    float t = (float)(magnitude - deadZone) / (float)(32767 - deadZone);
    if (t > 1.0f) {
        t = 1.0f;
    }

    t = t * t;
    return value < 0 ? -t : t;
}

// Right stick up/down -> mouse wheel notches, repeating faster the further
// the stick is pushed (like spinning a wheel).
int ps2pad_get_wheel()
{
    static Uint32 nextTick = 0;
    int value = gAxis[AXIS_RY];
    if (abs(value) < kScrollThreshold) {
        nextTick = 0;
        return 0;
    }

    Uint32 now = SDL_GetTicks();
    if (nextTick != 0 && (Sint32)(now - nextTick) < 0) {
        return 0;
    }

    // 250 ms between notches at the threshold, 80 ms at full tilt.
    int span = 32767 - kScrollThreshold;
    int over = abs(value) - kScrollThreshold;
    Uint32 interval = 250 - (Uint32)(170 * over / span);
    nextTick = now + interval;

    // Stick up (negative Y) = wheel up (positive), like scrolling forward.
    return value < 0 ? 1 : -1;
}

void ps2pad_ensure_analog();
bool ps2pad_connected();

void ps2pad_check_connection(Ps2PadKeyCallback* emitKey)
{
    static bool wasConnected = true;
    bool connected = ps2pad_connected();

    // D-pad held on a target: keep jumping (once per frame check).
    if (gDpadHeldDir >= 0 && connected) {
        Uint32 now = SDL_GetTicks();
        if ((Sint32)(now - gDpadNextRepeat) >= 0) {
            static const int kDirX[4] = { 0, 1, 0, -1 };
            static const int kDirY[4] = { -1, 0, 1, 0 };
            gDpadNextRepeat = now + DPAD_REPEAT_RATE;
            ps2nav_step(kDirX[gDpadHeldDir], kDirY[gDpadHeldDir]);
        }
    } else if (!connected) {
        gDpadHeldDir = -1;
    }
    if (wasConnected && !connected) {
        // Unplugged mid-press: let go of everything so nothing sticks.
        for (int button = 0; button < PAD_BUTTON_COUNT; button++) {
            ps2pad_release(button, emitKey);
            if (gDpadSentKey[button]) {
                emitKey(kButtonKeys[button], false);
            }
            gHeld[button] = false;
            gDpadSentKey[button] = false;
        }
        if (gScrollKeyX != 0) {
            emitKey(gScrollKeyX, false);
            gScrollKeyX = 0;
        }
        if (gScrollKeyY != 0) {
            emitKey(gScrollKeyY, false);
            gScrollKeyY = 0;
        }
        for (int i = 0; i < AXIS_COUNT; i++) {
            gAxis[i] = 0;
        }
        gLeftDown = false;
        gRightDown = false;
        gRemainderX = 0.0f;
        gRemainderY = 0.0f;
    }
    wasConnected = connected;
}

void ps2pad_get_mouse(int* dx, int* dy, bool* left, bool* right)
{
    ps2pad_ensure_analog();

    Uint32 now = SDL_GetTicks();
    Uint32 elapsed = now - gLastTick;
    gLastTick = now;

    // Avoid a cursor jump after long stalls (loading screens).
    if (elapsed > 100) {
        elapsed = 100;
    }

    float seconds = (float)elapsed / 1000.0f;
    float speed = kMaxCursorSpeed * ps2controls_cursor_speed();
    gRemainderX += ps2pad_curve(gAxis[AXIS_LX]) * speed * seconds;
    gRemainderY += ps2pad_curve(gAxis[AXIS_LY]) * speed * seconds;

    int moveX = (int)gRemainderX;
    int moveY = (int)gRemainderY;
    gRemainderX -= (float)moveX;
    gRemainderY -= (float)moveY;

    *dx = moveX;
    *dy = moveY;
    *left = gLeftDown;
    *right = gRightDown;
}

} // namespace fallout

#endif /* __PS2__ */
