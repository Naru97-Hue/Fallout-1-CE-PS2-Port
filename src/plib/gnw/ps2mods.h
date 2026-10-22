#ifndef FALLOUT_PLIB_GNW_PS2MODS_H_
#define FALLOUT_PLIB_GNW_PS2MODS_H_

#ifdef __PS2__

namespace fallout {

// Mod loader. Every folder in mods/ (next to MASTER.DAT) is a mod: its loose
// files replace the game's data/ files of the same name (mods/MYMOD/ART/...
// over data/ART/...). When there are any, a boot menu picks one (or none);
// the last choice is remembered in mods/LAST.TXT and highlighted.
bool ps2mods_boot_menu(); // true if the menu was shown

// How many mods are installed (after ps2mods_boot_menu).
int ps2mods_count();

// The MODS screen (main menu): the installed mods with their artwork
// (mods/<NAME>/PREVIEW.BMP) and description (mods/<NAME>/MOD.TXT). The
// choice is remembered and used from the next start.
void ps2mods_screen();

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2MODS_H_ */
