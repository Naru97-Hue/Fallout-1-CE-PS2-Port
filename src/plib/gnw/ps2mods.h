#ifndef FALLOUT_PLIB_GNW_PS2MODS_H_
#define FALLOUT_PLIB_GNW_PS2MODS_H_

#ifdef __PS2__

namespace fallout {

// Mod loader. Every folder in mods/ (next to MASTER.DAT) is a mod: its loose
// files replace the game's data/ files of the same name (mods/MYMOD/ART/...
// over data/ART/...). When there are any, a boot menu picks one (or none);
// the last choice is remembered in mods/LAST.TXT and highlighted.
void ps2mods_boot_menu();

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2MODS_H_ */
