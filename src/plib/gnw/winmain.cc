#include "plib/gnw/winmain.h"

#include <stdio.h>
#ifdef __PS2__
#include <kernel.h>
#endif
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "game/main.h"
#include "plib/gnw/debug.h"
#ifdef __PS2__
#include "plib/gnw/ps2crash.h"
#endif
#include "plib/gnw/gnw.h"
#include "plib/gnw/svga.h"

#if __APPLE__ && TARGET_OS_IOS
#include "platform/ios/paths.h"
#endif

namespace fallout {

// 0x53A290
bool GNW95_isActive = false;

#if _WIN32
// 0x53A294
HANDLE GNW95_mutex = NULL;
#endif

// 0x6B0760
char GNW95_title[256];

#ifdef __PS2__
static bool ps2_has_master_dat(const char* dir)
{
    static const char* names[] = { "MASTER.DAT", "master.dat", "Master.dat" };
    char path[512];
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%s%s", dir, names[i]);
        FILE* stream = fopen(path, "rb");
        if (stream != NULL) {
            fclose(stream);
            printf("fallout-ce PS2: found %s\n", path);
            return true;
        }
    }
    printf("fallout-ce PS2: no MASTER.DAT in '%s'\n", dir);
    return false;
}

// Locates the folder holding MASTER.DAT and makes it the working directory.
// Order: the ELF's own folder, the current directory, then common locations.
static void ps2_find_data_dir(int argc, char** argv)
{
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        cwd[0] = '\0';
    }
    printf("fallout-ce PS2: argv[0]='%s' cwd='%s'\n", argc > 0 ? argv[0] : "", cwd);

    char elfDir[512] = "";
    if (argc > 0 && argv[0] != NULL) {
        snprintf(elfDir, sizeof(elfDir), "%s", argv[0]);
        char* slash = strrchr(elfDir, '/');
        char* backslash = strrchr(elfDir, '\\');
        char* colon = strrchr(elfDir, ':');
        char* cut = slash;
        if (backslash != NULL && (cut == NULL || backslash > cut)) cut = backslash;
        if (colon != NULL && (cut == NULL || colon > cut)) cut = colon;
        if (cut != NULL) {
            cut[1] = '\0';
        } else {
            elfDir[0] = '\0';
        }
    }

    char cwdDir[512];
    size_t cwdLength = strlen(cwd);
    snprintf(cwdDir, sizeof(cwdDir), "%s%s", cwd,
        (cwdLength > 0 && cwd[cwdLength - 1] != '/' && cwd[cwdLength - 1] != ':') ? "/" : "");

    const char* candidates[] = {
        elfDir,
        cwdDir,
        "host:",
        "mass:/FALLOUT/",
        "mass:/",
        "mass0:/FALLOUT/",
        "hdd0:/FALLOUT/",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        const char* dir = candidates[i];
        if (dir[0] == '\0') {
            continue;
        }

        if (ps2_has_master_dat(dir)) {
            char target[512];
            snprintf(target, sizeof(target), "%s", dir);
            size_t length = strlen(target);
            // chdir("host:") / chdir("mass:/FALLOUT") - drop a trailing slash
            // unless it is the device root.
            if (length > 1 && target[length - 1] == '/' && target[length - 2] != ':') {
                target[length - 1] = '\0';
            }
            int rc = chdir(target);
            printf("fallout-ce PS2: chdir('%s') = %d\n", target, rc);
            return;
        }
    }

    printf("fallout-ce PS2: MASTER.DAT not found anywhere\n");
}
#endif

int main(int argc, char* argv[])
{
    int rc;

#if _WIN32
    GNW95_mutex = CreateMutexA(0, TRUE, "GNW95MUTEX");
    if (GetLastError() != ERROR_SUCCESS) {
        return 0;
    }
#endif

#if __APPLE__ && TARGET_OS_IOS
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    chdir(iOSGetDocumentsPath());
#endif

#if __APPLE__ && TARGET_OS_OSX
    char* basePath = SDL_GetBasePath();
    chdir(basePath);
    SDL_free(basePath);
#endif

#if __ANDROID__
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    chdir(SDL_AndroidGetExternalStoragePath());
#endif

    SDL_ShowCursor(SDL_DISABLE);

#ifdef __PS2__
    ps2_find_data_dir(argc, argv);

    // Let SDL's audio thread (priority 16) pre-empt the game loop. The EE
    // kernel only switches to a higher-priority (lower number) thread, and
    // the game rarely sleeps, so at equal/higher priority the audio thread
    // starved and the sound hardware replayed stale buffers ("audio loops").
    {
        ee_thread_status_t status;
        int oldPriority = -1;
        if (ReferThreadStatus(GetThreadId(), &status) == 0) {
            oldPriority = status.current_priority;
        }
        ChangeThreadPriority(GetThreadId(), 64);
        printf("fallout-ce PS2: main thread priority %d -> 64\n", oldPriority);
    }
    debug_register_env();
    debug_printf("fallout-ce PS2: starting\n");
    ps2_log_memory("startup");
    ps2crash_install();
#endif

    GNW95_isActive = true;
    rc = gnw_main(argc, argv);

#ifdef __PS2__
    debug_printf("fallout-ce PS2: gnw_main returned %d\n", rc);
#endif

#if _WIN32
    CloseHandle(GNW95_mutex);
#endif

    return rc;
}

} // namespace fallout

int main(int argc, char* argv[])
{
    return fallout::main(argc, argv);
}
