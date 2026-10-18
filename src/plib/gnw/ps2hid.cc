// USB keyboard and mouse on the PlayStation 2.
//
// SDL2's PS2 port only reports the DualShock, so we load the USB HID drivers
// (ps2kbd.irx / ps2mouse.irx, embedded in ps2_drivers) ourselves and poll
// them. The keyboard runs in raw mode, which reports USB HID usage codes -
// the same numbers as SDL scancodes - so key events go straight into the
// game's keyboard handling. Mouse movement is added to the DualShock's.
//
// PCSX2 emulates both (Settings > Controllers > USB Port: HID Keyboard /
// HID Mouse).

#ifdef __PS2__

#include "plib/gnw/ps2hid.h"

#include <libkbd.h>
#include <libmouse.h>
#include <ps2_keyboard_driver.h>
#include <ps2_mouse_driver.h>

#include <SDL.h>

#include "plib/gnw/debug.h"
#include "plib/gnw/ps2controls.h"
#include "plib/gnw/ps2osk.h"

namespace fallout {

static bool gInitialized = false;
static bool gKeyboard = false;
static bool gMouse = false;
static int gLastButtons = 0;
static int gMiceConnected = -1;
// Middle button: changes waiting to be applied (from the input loop), and
// the action it started.
static int gMiddleEvents = 0; // bit 0: pressed since, bit 1: released since
static bool gMiddleDown = false;
static int gMiddleAction = PS2_ACTION_NONE;
static bool gMiddleLeft = false;
static bool gMiddleRight = false;

void ps2hid_init()
{
    if (gInitialized) {
        return;
    }
    gInitialized = true;

    // USB support (usbd) is normally loaded already for USB storage; if
    // loading it again fails, try with what's there.
    int kbd = init_keyboard_driver(true);
    if (kbd < 0) {
        kbd = init_keyboard_driver(false);
    }
    if (kbd >= 0) {
        PS2KbdSetReadmode(PS2KBD_READMODE_RAW);
        PS2KbdSetBlockingMode(PS2KBD_NONBLOCKING);
        PS2KbdFlushBuffer();
        gKeyboard = true;
    }

    int mouse = init_mouse_driver(true);
    if (mouse < 0) {
        mouse = init_mouse_driver(false);
    }
    if (mouse >= 0) {
        PS2MouseSetReadMode(PS2MOUSE_READMODE_DIFF);
        gMouse = true;
    }

    debug_printf("ps2hid: USB keyboard driver %s (%d), USB mouse driver %s (%d)\n",
        gKeyboard ? "loaded" : "unavailable", kbd, gMouse ? "loaded" : "unavailable", mouse);
}

static void ps2hid_middle(bool down, Ps2HidKeyCallback* emitKey)
{
    if (down) {
        gMiddleAction = ps2controls_mouse_middle_action();
    }
    switch (gMiddleAction) {
    case PS2_ACTION_NONE:
        break;
    case PS2_ACTION_LEFT_CLICK:
        gMiddleLeft = down;
        break;
    case PS2_ACTION_RIGHT_CLICK:
        gMiddleRight = down;
        break;
    case PS2_ACTION_KEYBOARD:
        if (down) {
            ps2osk_toggle();
        }
        break;
    default:
        emitKey(gMiddleAction, down);
        break;
    }
    if (!down) {
        gMiddleAction = PS2_ACTION_NONE;
    }
}

void ps2hid_poll_keyboard(Ps2HidKeyCallback* emitKey)
{
    if ((gMiddleEvents & 1) != 0) {
        ps2hid_middle(true, emitKey);
    }
    if ((gMiddleEvents & 2) != 0) {
        ps2hid_middle(false, emitKey);
    }
    gMiddleEvents = 0;

    if (!gKeyboard) {
        return;
    }

    PS2KbdRawKey key;
    // Bounded, in case the driver keeps reporting.
    for (int i = 0; i < 64; i++) {
        if (PS2KbdReadRaw(&key) <= 0) {
            break;
        }
        if (key.key == 0) {
            continue;
        }
        bool down = key.state == PS2KBD_RAWKEY_DOWN;
        if (!down && key.state != PS2KBD_RAWKEY_UP) {
            continue;
        }
        // The Controls screen waiting for a key gets it instead of the game.
        if (down && ps2controls_capture_key(key.key)) {
            continue;
        }
        emitKey(ps2controls_translate_key(key.key), down);
    }
}

// Left/right (optionally swapped) plus a middle button mapped to a click;
// also notes middle-button changes for ps2hid_poll_keyboard.
static void ps2hid_buttons(bool* left, bool* right)
{
    bool b1 = (gLastButtons & PS2MOUSE_BTN1) != 0;
    bool b2 = (gLastButtons & PS2MOUSE_BTN2) != 0;
    bool b3 = (gLastButtons & PS2MOUSE_BTN3) != 0;
    if (ps2controls_mouse_swapped()) {
        bool t = b1;
        b1 = b2;
        b2 = t;
    }
    if (b3 != gMiddleDown) {
        gMiddleDown = b3;
        gMiddleEvents |= b3 ? 1 : 2;
    }
    *left = b1 || gMiddleLeft;
    *right = b2 || gMiddleRight;
}

bool ps2hid_get_mouse(int* dx, int* dy, bool* left, bool* right, int* wheel)
{
    *dx = 0;
    *dy = 0;
    *wheel = 0;
    ps2hid_buttons(left, right);
    if (!gMouse) {
        return false;
    }

    // Hot swap: notice mice being plugged in or out (once a second), and
    // let go of the buttons if the mouse goes away mid-click.
    static Uint32 nextEnum = 0;
    Uint32 now = SDL_GetTicks();
    if ((Sint32)(now - nextEnum) >= 0) {
        nextEnum = now + 1000;
        int mice = (int)PS2MouseEnum();
        if (mice != gMiceConnected) {
            if (gMiceConnected != -1 || mice > 0) {
                debug_printf("ps2hid: %d USB mouse/mice connected\n", mice);
            }
            gMiceConnected = mice;
            if (mice == 0) {
                gLastButtons = 0;
                ps2hid_buttons(left, right);
            }
        }
    }
    if (gMiceConnected == 0) {
        return false;
    }

    PS2MouseData data;
    if (PS2MouseRead(&data) <= 0) {
        return false;
    }

    *dx = data.x;
    *dy = data.y;
    *wheel = data.wheel;
    gLastButtons = (int)data.buttons;
    ps2hid_buttons(left, right);
    return data.x != 0 || data.y != 0 || data.wheel != 0;
}

} // namespace fallout

#endif /* __PS2__ */
