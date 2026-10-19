#ifndef FALLOUT_PLIB_GNW_PS2STORAGE_H_
#define FALLOUT_PLIB_GNW_PS2STORAGE_H_

#ifdef __PS2__

#include <stddef.h>

namespace fallout {

// Finds the folder holding MASTER.DAT on any storage the PS2 can reach and
// writes it to `out` (e.g. "mass1:/FALLOUT/"). `first` lists folders to try
// before everything else (the ELF's folder, the current directory). Waits a
// few seconds for USB / SD adapters that are still starting up, and loads
// the MMCE driver (SD2PSX, MemCard PRO 2) if nothing is found without it.
bool ps2storage_find_game(const char* const* first, int firstCount, char* out, size_t size);

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2STORAGE_H_ */
