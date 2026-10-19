#ifndef FALLOUT_PLIB_GNW_PS2CARD_H_
#define FALLOUT_PLIB_GNW_PS2CARD_H_

#ifdef __PS2__

namespace fallout {

#define PS2CARD_ICON_NAME "fallout.ico"

// Free space on the memory card in port 0/1 in KB, or -1 when there's no
// formatted PS2 card there.
int ps2card_free_kb(int port);

// Writes icon.sys and the 3D icon into a save slot's card folder so the PS2
// browser shows it as "Fallout Slot N" with the save's description.
bool ps2card_write_icons(const char* dir, int slot, const char* description);

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2CARD_H_ */
