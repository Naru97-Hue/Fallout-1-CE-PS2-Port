// Switches controller 1 into analog (DualShock) mode. Kept in its own file
// because libpad.h's PAD_* macros clash with ps2pad.cc's button enum.

#ifdef __PS2__

#include <SDL.h>
#include <libpad.h>

#include "plib/gnw/debug.h"

namespace fallout {

// SDL's PS2 joystick driver opens the pad but never switches it to analog
// (DualShock) mode, so the sticks report nothing. Request DualShock mode,
// locked so the Analog button can't turn it off, and retry until the pad
// reports it. Port 0 / slot 0 is controller 1.
void ps2pad_ensure_analog()
{
    static bool done = false;
    static Uint32 nextTry = 0;
    static int attempts = 0;

    if (done) {
        return;
    }

    Uint32 now = SDL_GetTicks();
    if ((Sint32)(now - nextTry) < 0) {
        return;
    }
    nextTry = now + 500;

    int state = padGetState(0, 0);
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) {
        return;
    }

    if (padInfoMode(0, 0, PAD_MODECURID, 0) == PAD_TYPE_DUALSHOCK) {
        done = true;
        debug_printf("ps2pad: controller in analog (DualShock) mode\n");
        return;
    }

    if (padGetReqState(0, 0) == PAD_RSTAT_BUSY) {
        return;
    }

    if (attempts < 20) {
        attempts++;
        padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
        debug_printf("ps2pad: requesting analog mode (attempt %d)\n", attempts);
    }
}

} // namespace fallout

#endif /* __PS2__ */
