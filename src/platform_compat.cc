#include <ctype.h>
#include "platform_compat.h"
#ifdef __PS2__
#include "plib/gnw/ps2modarc.h"
#endif

#include <stdio.h>
#include <string.h>


#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <timeapi.h>
#else
#include <chrono>
#endif

#include <SDL.h>

namespace fallout {

int compat_stricmp(const char* string1, const char* string2)
{
    return SDL_strcasecmp(string1, string2);
}

int compat_strnicmp(const char* string1, const char* string2, size_t size)
{
    return SDL_strncasecmp(string1, string2, size);
}

char* compat_strupr(char* string)
{
    return SDL_strupr(string);
}

char* compat_strlwr(char* string)
{
    return SDL_strlwr(string);
}

char* compat_itoa(int value, char* buffer, int radix)
{
    return SDL_itoa(value, buffer, radix);
}

void compat_splitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
{
#ifdef _WIN32
    _splitpath(path, drive, dir, fname, ext);
#else
    const char* driveStart = path;
    if (path[0] == '/' && path[1] == '/') {
        path += 2;
        while (*path != '\0' && *path != '/' && *path != '.') {
            path++;
        }
    }

    if (drive != NULL) {
        size_t driveSize = path - driveStart;
        if (driveSize > COMPAT_MAX_DRIVE - 1) {
            driveSize = COMPAT_MAX_DRIVE - 1;
        }
        strncpy(drive, path, driveSize);
        drive[driveSize] = '\0';
    }

    const char* dirStart = path;
    const char* fnameStart = path;
    const char* extStart = NULL;

    const char* end = path;
    while (*end != '\0') {
        if (*end == '/') {
            fnameStart = end + 1;
        } else if (*end == '.') {
            extStart = end;
        }
        end++;
    }

    if (extStart == NULL) {
        extStart = end;
    }

    if (dir != NULL) {
        size_t dirSize = fnameStart - dirStart;
        if (dirSize > COMPAT_MAX_DIR - 1) {
            dirSize = COMPAT_MAX_DIR - 1;
        }
        strncpy(dir, path, dirSize);
        dir[dirSize] = '\0';
    }

    if (fname != NULL) {
        size_t fileNameSize = extStart - fnameStart;
        if (fileNameSize > COMPAT_MAX_FNAME - 1) {
            fileNameSize = COMPAT_MAX_FNAME - 1;
        }
        strncpy(fname, fnameStart, fileNameSize);
        fname[fileNameSize] = '\0';
    }

    if (ext != NULL) {
        size_t extSize = end - extStart;
        if (extSize > COMPAT_MAX_EXT - 1) {
            extSize = COMPAT_MAX_EXT - 1;
        }
        strncpy(ext, extStart, extSize);
        ext[extSize] = '\0';
    }
#endif
}

void compat_makepath(char* path, const char* drive, const char* dir, const char* fname, const char* ext)
{
#ifdef _WIN32
    _makepath(path, drive, dir, fname, ext);
#else
    path[0] = '\0';

    if (drive != NULL) {
        if (*drive != '\0') {
            strcpy(path, drive);
            path = strchr(path, '\0');

            if (path[-1] == '/') {
                path--;
            } else {
                *path = '/';
            }
        }
    }

    if (dir != NULL) {
        if (*dir != '\0') {
            if (*dir != '/' && *path == '/') {
                path++;
            }

            strcpy(path, dir);
            path = strchr(path, '\0');

            if (path[-1] == '/') {
                path--;
            } else {
                *path = '/';
            }
        }
    }

    if (fname != NULL && *fname != '\0') {
        if (*fname != '/' && *path == '/') {
            path++;
        }

        strcpy(path, fname);
        path = strchr(path, '\0');
    } else {
        if (*path == '/') {
            path++;
        }
    }

    if (ext != NULL) {
        if (*ext != '\0') {
            if (*ext != '.') {
                *path++ = '.';
            }

            strcpy(path, ext);
            path = strchr(path, '\0');
        }
    }

    *path = '\0';
#endif
}

int compat_read(int fileHandle, void* buf, unsigned int size)
{
    return read(fileHandle, buf, size);
}

int compat_write(int fileHandle, const void* buf, unsigned int size)
{
    return write(fileHandle, buf, size);
}

long compat_lseek(int fileHandle, long offset, int origin)
{
    return lseek(fileHandle, offset, origin);
}

long compat_tell(int fd)
{
    return lseek(fd, 0, SEEK_CUR);
}

long compat_filelength(int fd)
{
    long originalOffset = lseek(fd, 0, SEEK_CUR);
    lseek(fd, 0, SEEK_SET);
    long filesize = lseek(fd, 0, SEEK_END);
    lseek(fd, originalOffset, SEEK_SET);
    return filesize;
}

int compat_mkdir(const char* path)
{
    char nativePath[COMPAT_MAX_PATH];
    strcpy(nativePath, path);
    compat_windows_path_to_native(nativePath);
    compat_resolve_path(nativePath);

#ifdef _WIN32
    return mkdir(nativePath);
#else
    return mkdir(nativePath, 0755);
#endif
}

unsigned int compat_timeGetTime()
{
#if defined(__PS2__)
    // newlib's steady_clock is not reliable on the PS2; SDL's timer is.
    return SDL_GetTicks();
#elif defined(_WIN32)
    return timeGetTime();
#else
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
#endif
}

#ifdef __PS2__
static bool compat_is_sav(const char* path)
{
    size_t len = strlen(path);
    return len > 4 && compat_stricmp(path + len - 4, ".SAV") == 0;
}
#endif

#ifdef __PS2__
// Is this file on PCSX2's host: device (relative paths: is the game there)?
static bool compat_is_host_path(const char* nativePath)
{
    static int onHost = -1;
    if (onHost == -1) {
        char cwd[64];
        onHost = getcwd(cwd, sizeof(cwd)) != NULL && compat_strnicmp(cwd, "host", 4) == 0;
    }
    if (strchr(nativePath, ':') == NULL) {
        return onHost == 1;
    }
    return compat_strnicmp(nativePath, "host", 4) == 0;
}
#endif

#ifdef __PS2__
static FILE* compat_mod_fopen(const char* nativePath, const char* mode);
#endif

FILE* compat_fopen(const char* path, const char* mode)
{
    char nativePath[COMPAT_MAX_PATH];
    strcpy(nativePath, path);
    compat_windows_path_to_native(nativePath);
    compat_resolve_path(nativePath);
#ifdef __PS2__
    FILE* modStream = compat_mod_fopen(nativePath, mode);
    if (modStream != NULL) {
        return modStream;
    }
    // An empty .SAV is a map state "deleted" where removing files isn't
    // possible (see compat_remove): treat it as missing.
    if (mode[0] == 'r' && compat_is_sav(nativePath)) {
        FILE* stream = fopen(nativePath, mode);
        if (stream != NULL) {
            fseek(stream, 0, SEEK_END);
            long size = ftell(stream);
            if (size <= 0) {
                fclose(stream);
                return NULL;
            }
            fseek(stream, 0, SEEK_SET);
        }
        return stream;
    }
#endif
    return fopen(nativePath, mode);
}

int compat_remove(const char* path)
{
    char nativePath[COMPAT_MAX_PATH];
    strcpy(nativePath, path);
    compat_windows_path_to_native(nativePath);
    compat_resolve_path(nativePath);
#ifdef __PS2__
    // PCSX2's host: device can't delete at all (every attempt just logs
    // "IOPHLE remove_HLE failed"), so don't try there.
    bool hostPath = compat_is_host_path(nativePath);
    if (!hostPath && remove(nativePath) == 0) {
        return 0;
    }
    // PCSX2's host: device can't delete files. Map states (.SAV) must really
    // go away (or an old one comes back on loading), so empty the file
    // instead; compat_fopen treats empty .SAV files as missing.
    if (compat_is_sav(nativePath)) {
        FILE* stream = fopen(nativePath, "wb");
        if (stream != NULL) {
            fclose(stream);
            return 0;
        }
    }
    return -1;
#else
    return remove(nativePath);
#endif
}

int compat_rename(const char* oldFileName, const char* newFileName)
{
    char nativeOldFileName[COMPAT_MAX_PATH];
    strcpy(nativeOldFileName, oldFileName);
    compat_windows_path_to_native(nativeOldFileName);
    compat_resolve_path(nativeOldFileName);

    char nativeNewFileName[COMPAT_MAX_PATH];
    strcpy(nativeNewFileName, newFileName);
    compat_windows_path_to_native(nativeNewFileName);
    compat_resolve_path(nativeNewFileName);

#ifdef __PS2__
    // PCSX2's host: device (and some PS2 file systems) can't rename: copy the
    // file and remove the original instead.
    if (rename(nativeOldFileName, nativeNewFileName) == 0) {
        return 0;
    }
    FILE* in = fopen(nativeOldFileName, "rb");
    if (in == NULL) {
        return -1;
    }
    if (!compat_is_host_path(nativeNewFileName)) {
        remove(nativeNewFileName);
    }
    FILE* out = fopen(nativeNewFileName, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }
    static char buffer[16384];
    bool ok = true;
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    if (ferror(in)) {
        ok = false;
    }
    fclose(in);
    if (fclose(out) != 0) {
        ok = false;
    }
    if (!ok) {
        return -1;
    }
    if (!compat_is_host_path(nativeOldFileName)) {
        remove(nativeOldFileName);
    }
    return 0;
#else
    return rename(nativeOldFileName, nativeNewFileName);
#endif
}

void compat_windows_path_to_native(char* path)
{
#ifndef _WIN32
    char* pch = path;
    while (*pch != '\0') {
        if (*pch == '\\') {
            *pch = '/';
        }
        pch++;
    }
#endif
}

#ifdef __PS2__
// Where SAVEGAME lives (NULL: inside the game's data folder). For a memory
// card this is the card ("mc0:"): every save slot gets its own card folder
// (FALLOUT1-SLOT01 ...), each with its own icon, so the PS2 browser shows,
// copies and deletes saves one at a time. Elsewhere (USB / HDD) it's a
// folder that takes ".../SAVEGAME/..." as usual.
static char gSaveRoot[32] = { 0 };

void compat_set_save_root(const char* root)
{
    if (root == NULL || root[0] == '\0') {
        gSaveRoot[0] = '\0';
        return;
    }
    snprintf(gSaveRoot, sizeof(gSaveRoot), "%s", root);
}

const char* compat_get_save_root()
{
    return gSaveRoot[0] != '\0' ? gSaveRoot : NULL;
}

bool compat_save_root_is_card()
{
    return compat_strnicmp(gSaveRoot, "mc", 2) == 0 && gSaveRoot[3] == ':';
}

void compat_card_slot_dir(int slot, char* out, size_t size)
{
    snprintf(out, size, "%s/FALLOUT1-SLOT%02d", gSaveRoot, slot + 1);
}

static void compat_redirect_save_path(char* path)
{
    if (gSaveRoot[0] == '\0') {
        return;
    }
    // Find a "savegame" path component (any case).
    char* p = path;
    while (*p != '\0') {
        if ((p == path || p[-1] == '/') && compat_strnicmp(p, "savegame", 8) == 0 && (p[8] == '/' || p[8] == '\0')) {
            char rest[COMPAT_MAX_PATH];
            snprintf(rest, sizeof(rest), "%s", p + 8);
            if (!compat_save_root_is_card()) {
                snprintf(path, COMPAT_MAX_PATH, "%s/SAVEGAME%s", gSaveRoot, rest);
                return;
            }
            // Card: "/SLOTnn/..." -> "mc0:/FALLOUT1-SLOTnn/...". SAVEGAME
            // itself is the card root (so nothing icon-less is created).
            if (compat_strnicmp(rest, "/slot", 5) == 0 && isdigit((unsigned char)rest[5]) && isdigit((unsigned char)rest[6])) {
                int slot = (rest[5] - '0') * 10 + (rest[6] - '0');
                char dir[COMPAT_MAX_PATH];
                compat_card_slot_dir(slot - 1, dir, sizeof(dir));
                snprintf(path, COMPAT_MAX_PATH, "%s%s", dir, rest + 7);
            } else {
                snprintf(path, COMPAT_MAX_PATH, "%s%s", gSaveRoot, rest[0] != '\0' ? rest : "/");
            }
            return;
        }
        p++;
    }
}

// Mod overlay: files in mods/<name>/ (loose, or inside .ZIP / .DAT files)
// replace the game's data/ files; a mod can also be one .ZIP / .DAT file.
static char gModRoot[64] = { 0 };
static ModPack* gModPack = NULL;
static bool gModIsArchive = false;

void compat_set_mod_root(const char* root)
{
    modpack_close(gModPack);
    gModPack = NULL;
    snprintf(gModRoot, sizeof(gModRoot), "%s", root != NULL ? root : "");
    gModIsArchive = gModRoot[0] != '\0' && modpack_is_archive_file(gModRoot);
    if (gModRoot[0] != '\0') {
        gModPack = modpack_open(gModRoot);
    }
}

const char* compat_get_mod_root()
{
    return gModRoot[0] != '\0' ? gModRoot : NULL;
}

// Read-only opens of "data/..." try the mod first.
static FILE* compat_mod_fopen(const char* nativePath, const char* mode)
{
    if (gModRoot[0] == '\0' || mode[0] != 'r' || strchr(mode, '+') != NULL) {
        return NULL;
    }
    const char* p = nativePath;
    if (p[0] == '.' && p[1] == '/') {
        p += 2;
    }
    if (compat_strnicmp(p, "data/", 5) != 0) {
        return NULL;
    }
    if (!gModIsArchive) {
        char modPath[COMPAT_MAX_PATH];
        snprintf(modPath, sizeof(modPath), "%s/%s", gModRoot, p + 5);
        FILE* stream = fopen(modPath, mode);
        if (stream != NULL) {
            return stream;
        }
    }
    return modpack_fopen(gModPack, p + 5);
}
#endif

void compat_resolve_path(char* path)
{
#ifdef __PS2__
    compat_redirect_save_path(path);
#endif
#if !defined(_WIN32) && !defined(__PS2__)
    // (PS2: host:, FAT and ISO9660 are all case-insensitive, and scanning the
    // directory over host:/USB on every fopen is slow, so this is skipped.)
    char* pch = path;

    DIR* dir;
    if (pch[0] == '/') {
        dir = opendir("/");
        pch++;
    } else {
        dir = opendir(".");
    }

    while (dir != NULL) {
        char* sep = strchr(pch, '/');
        size_t length;
        if (sep != NULL) {
            length = sep - pch;
        } else {
            length = strlen(pch);
        }

        bool found = false;

        struct dirent* entry = readdir(dir);
        while (entry != NULL) {
            if (strlen(entry->d_name) == length && compat_strnicmp(pch, entry->d_name, length) == 0) {
                strncpy(pch, entry->d_name, length);
                found = true;
                break;
            }
            entry = readdir(dir);
        }

        closedir(dir);
        dir = NULL;

        if (!found) {
            break;
        }

        if (sep == NULL) {
            break;
        }

        *sep = '\0';
        dir = opendir(path);
        *sep = '/';

        pch = sep + 1;
    }
#endif
}

char* compat_strdup(const char* string)
{
    return SDL_strdup(string);
}

// It's a replacement for compat_filelength(fileno(stream)) on platforms without
// fileno defined.
long getFileSize(FILE* stream)
{
    long originalOffset = ftell(stream);
    fseek(stream, 0, SEEK_END);
    long filesize = ftell(stream);
    fseek(stream, originalOffset, SEEK_SET);
    return filesize;
}

} // namespace fallout

namespace fallout {

void compat_spin_yield()
{
#ifdef __PS2__
    // The PS2 kernel does not time-slice threads: give the audio/IO threads a
    // chance to run while the movie player waits for its next frame.
    SDL_Delay(1);
#endif
}

} // namespace fallout
