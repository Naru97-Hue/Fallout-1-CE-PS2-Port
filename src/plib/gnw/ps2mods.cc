// Mod loader with a boot menu (see ps2mods.h).

#ifdef __PS2__

#include "plib/gnw/ps2mods.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "platform_compat.h"
#include "plib/color/color.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/intrface.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"

namespace fallout {

#define MAX_MODS 24
#define MOD_NAME_SIZE 32

static char gModNames[MAX_MODS][MOD_NAME_SIZE];
static int gModCount = 0;

static bool ps2mods_is_dir(const char* name)
{
    char path[COMPAT_MAX_PATH];
    snprintf(path, sizeof(path), "mods/%s", name);
    // Not every PS2 file system fills d_type: check by opening it.
    DIR* dir = opendir(path);
    if (dir == NULL) {
        return false;
    }
    closedir(dir);
    return true;
}

static void ps2mods_scan()
{
    gModCount = 0;
    DIR* dir = opendir("mods");
    if (dir == NULL) {
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && gModCount < MAX_MODS) {
        const char* name = entry->d_name;
        if (name[0] == '.' || strlen(name) >= MOD_NAME_SIZE) {
            continue;
        }
        if (!ps2mods_is_dir(name)) {
            continue;
        }
        snprintf(gModNames[gModCount++], MOD_NAME_SIZE, "%s", name);
    }
    closedir(dir);
}

static void ps2mods_read_last(char* out, size_t size)
{
    out[0] = '\0';
    FILE* stream = fopen("mods/LAST.TXT", "rb");
    if (stream == NULL) {
        return;
    }
    if (fgets(out, (int)size, stream) == NULL) {
        out[0] = '\0';
    }
    fclose(stream);
    out[strcspn(out, "\r\n")] = '\0';
}

static void ps2mods_write_last(const char* name)
{
    char last[MOD_NAME_SIZE];
    ps2mods_read_last(last, sizeof(last));
    if (strcmp(last, name) == 0) {
        return; // unchanged: don't touch the USB stick
    }
    FILE* stream = fopen("mods/LAST.TXT", "wb");
    if (stream != NULL) {
        fprintf(stream, "%s\n", name);
        fclose(stream);
    }
}

void ps2mods_boot_menu()
{
    ps2mods_scan();
    if (gModCount == 0) {
        return;
    }

    char* items[MAX_MODS + 1];
    items[0] = (char*)"Original game (no mod)";
    for (int i = 0; i < gModCount; i++) {
        items[i + 1] = gModNames[i];
    }

    char last[MOD_NAME_SIZE];
    ps2mods_read_last(last, sizeof(last));
    int start = 0;
    for (int i = 0; i < gModCount; i++) {
        if (compat_stricmp(gModNames[i], last) == 0) {
            start = i + 1;
        }
    }

    int x = screenGetWidth() / 2 - 120;
    int y = screenGetHeight() / 2 - 100;
    int choice = win_list_select_at("CHOOSE A MOD", items, gModCount + 1, NULL, x, y, colorTable[32747] | 0x10000, start);

    if (choice <= 0) {
        compat_set_mod_root(NULL);
        ps2mods_write_last("");
        debug_printf("ps2mods: playing the original game\n");
        return;
    }

    char root[64];
    snprintf(root, sizeof(root), "mods/%s", gModNames[choice - 1]);
    compat_set_mod_root(root);
    ps2mods_write_last(gModNames[choice - 1]);
    debug_printf("ps2mods: using mod '%s'\n", gModNames[choice - 1]);
}

} // namespace fallout

#endif /* __PS2__ */
