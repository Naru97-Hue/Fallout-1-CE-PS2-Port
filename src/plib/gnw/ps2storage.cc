// Where the game files can live on a PS2.
//
// The PS2 filesystem drivers loaded at startup (ps2_drivers, via SDL's
// main) already include:
//   - USB mass storage through BDM (usbmass_bd + FAT/exFAT), the same stack
//     OPL uses: sticks and USB hard drives, mounted as mass0:, mass1:, ...
//   - MX4SIO (SD card adapter in a memory card slot), also a BDM device and
//     so also massN:
//   - the internal HDD (pfs0: when launched from it), and PCSX2's host:
// On top of that this loads MMCE (mmceman: SD2PSX / MemCard PRO 2 / PicoMemcard+
// style SD memory cards, mmce0: / mmce1:) when the game isn't found without it.
//
// USB and SD devices can take a few seconds to show up after the drivers
// load, so the search retries for a while instead of giving up at once.

#ifdef __PS2__

#include "plib/gnw/ps2storage.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>

#include <SDL.h>

namespace fallout {

extern "C" {
extern unsigned char ps2_mmceman_irx[];
extern unsigned int size_ps2_mmceman_irx;
}

// Device roots, in order of preference. massN: covers USB (BDM) and MX4SIO.
static const char* kDevices[] = {
    "mass0:/",
    "mass1:/",
    "mass2:/",
    "mass3:/",
    "mass:/", // older USB driver (usbhdfsd) name
    "mmce0:/",
    "mmce1:/",
    "pfs0:/",
    "hdd0:/",
};

// Folders tried on each device.
static const char* kFolders[] = {
    "FALLOUT/",
    "FALLOUT1/",
    "GAMES/FALLOUT/",
    "APPS/FALLOUT/",
    "",
};

#define SEARCH_SECONDS 8

static bool ps2storage_has_master_dat(const char* dir)
{
    static const char* names[] = { "MASTER.DAT", "master.dat", "Master.dat" };
    char path[512];
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%s%s", dir, names[i]);
        FILE* stream = fopen(path, "rb");
        if (stream != NULL) {
            fclose(stream);
            return true;
        }
    }
    return false;
}

// Is the device there at all? (Saves trying every folder on a missing one.)
static bool ps2storage_device_ready(const char* root)
{
    DIR* dir = opendir(root);
    if (dir == NULL) {
        return false;
    }
    closedir(dir);
    return true;
}

static bool ps2storage_search_devices(char* out, size_t size, bool log)
{
    for (size_t d = 0; d < sizeof(kDevices) / sizeof(kDevices[0]); d++) {
        if (!ps2storage_device_ready(kDevices[d])) {
            continue;
        }
        if (log) {
            printf("fallout-ce PS2: storage %s is ready\n", kDevices[d]);
        }
        for (size_t f = 0; f < sizeof(kFolders) / sizeof(kFolders[0]); f++) {
            char dir[256];
            snprintf(dir, sizeof(dir), "%s%s", kDevices[d], kFolders[f]);
            if (ps2storage_has_master_dat(dir)) {
                snprintf(out, size, "%s", dir);
                return true;
            }
        }
    }
    return false;
}

static void ps2storage_load_mmce()
{
    int ret = 0;
    int id = SifExecModuleBuffer(ps2_mmceman_irx, size_ps2_mmceman_irx, 0, NULL, &ret);
    printf("fallout-ce PS2: MMCE driver: id %d, ret %d\n", id, ret);
}

bool ps2storage_find_game(const char* const* first, int firstCount, char* out, size_t size)
{
    // The ELF's folder etc. first: right whenever the game sits next to it.
    for (int i = 0; i < firstCount; i++) {
        if (first[i] != NULL && first[i][0] != '\0' && ps2storage_has_master_dat(first[i])) {
            snprintf(out, size, "%s", first[i]);
            return true;
        }
    }
    if (ps2storage_has_master_dat("host:")) {
        snprintf(out, size, "host:");
        return true;
    }

    // Then every device; USB / SD devices may still be starting up.
    Uint32 start = SDL_GetTicks();
    bool mmceLoaded = false;
    bool log = true;
    while (true) {
        if (ps2storage_search_devices(out, size, log)) {
            return true;
        }
        log = false;
        // MMCE only when needed: plain memory cards never need it.
        if (!mmceLoaded) {
            mmceLoaded = true;
            ps2storage_load_mmce();
            continue;
        }
        if (SDL_GetTicks() - start >= SEARCH_SECONDS * 1000) {
            break;
        }
        printf("fallout-ce PS2: MASTER.DAT not found yet, waiting for storage...\n");
        SDL_Delay(500);
    }
    return false;
}

} // namespace fallout

#endif /* __PS2__ */
