#ifndef FALLOUT_PLIB_GNW_PS2NAV_H_
#define FALLOUT_PLIB_GNW_PS2NAV_H_

#ifdef __PS2__

namespace fallout {

// False when PADNAV.OFF exists (D-pad sends arrow keys instead).
bool ps2nav_enabled();

// Moves the cursor to the nearest clickable thing in direction (dx, dy)
// (each -1, 0 or 1). Returns true when the D-pad press was handled.
bool ps2nav_step(int dx, int dy);

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2NAV_H_ */
