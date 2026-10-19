#ifndef FALLOUT_PLIB_GNW_PS2MODARC_H_
#define FALLOUT_PLIB_GNW_PS2MODARC_H_

#ifdef __PS2__

#include <stddef.h>
#include <stdio.h>

namespace fallout {

// Compressed mods: a mod can be a .ZIP or Fallout .DAT file in mods/, or a
// folder holding loose files and/or .ZIP / .DAT files. Files inside are
// read straight from the archive (ZIP: stored or deflate; DAT: Fallout 1
// format, stored or LZSS), no unpacking needed.

typedef struct ModPack ModPack;

// root: "mods/NAME" (folder) or "mods/NAME.ZIP" / "mods/NAME.DAT".
ModPack* modpack_open(const char* root);
void modpack_close(ModPack* pack);

// Whether the pack is one archive file (not a folder).
bool modpack_is_archive_file(const char* root);

// A file from the archives, as a read-only stream (freed on fclose).
// `path` is relative to DATA, e.g. "art/critters/hmjmpsaa.frm".
FILE* modpack_fopen(ModPack* pack, const char* path);

// Reads a file anywhere in the archives by its name only (e.g. "MOD.TXT",
// "PREVIEW.BMP"; the shallowest match wins). Free with free().
bool modpack_read_named(ModPack* pack, const char* name, unsigned char** dataOut, size_t* sizeOut);

// Number of files in the pack's archives (for the log).
int modpack_file_count(ModPack* pack);

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2MODARC_H_ */
