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
#include "plib/gnw/ps2modarc.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"
#include "fps_limiter.h"
#include "game/art.h"
#include "game/gsound.h"
#include "game/skill.h"
#include "game/skill_defs.h"
#include "game/object_types.h"
#include "plib/gnw/button.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/ps2pad.h"
#include "game/palette.h"
#include "game/ps2autotest.h"

#include <SDL.h>
#include <stdlib.h>

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
        if (!modpack_is_archive_file(name) && !ps2mods_is_dir(name)) {
            continue; // a mod is a folder, or a .ZIP / .DAT file
        }
        snprintf(gModNames[gModCount++], MOD_NAME_SIZE, "%s", name);
    }
    closedir(dir);
}

// mods/LAST.TXT: line 1 the mod in use (empty: none), line 2 "menu=0" when
// the startup menu is turned off.
static bool gAskAtStartup = true;

static void ps2mods_read_last(char* out, size_t size)
{
    out[0] = '\0';
    gAskAtStartup = true;
    FILE* stream = fopen("mods/LAST.TXT", "rb");
    if (stream == NULL) {
        return;
    }
    if (fgets(out, (int)size, stream) == NULL) {
        out[0] = '\0';
    }
    char line[32];
    if (fgets(line, sizeof(line), stream) != NULL && strncmp(line, "menu=0", 6) == 0) {
        gAskAtStartup = false;
    }
    fclose(stream);
    out[strcspn(out, "\r\n")] = '\0';
}

static void ps2mods_write_last(const char* name, bool ask)
{
    char last[MOD_NAME_SIZE];
    ps2mods_read_last(last, sizeof(last));
    if (strcmp(last, name) == 0 && gAskAtStartup == ask) {
        return; // unchanged: don't touch the USB stick
    }
    FILE* stream = fopen("mods/LAST.TXT", "wb");
    if (stream != NULL) {
        fprintf(stream, "%s\n%s\n", name, ask ? "menu=1" : "menu=0");
        fclose(stream);
    }
    gAskAtStartup = ask;
}

static void ps2mods_use(const char* name)
{
    if (name == NULL || name[0] == '\0') {
        compat_set_mod_root(NULL);
        debug_printf("ps2mods: playing the original game\n");
        return;
    }
    char root[64];
    snprintf(root, sizeof(root), "mods/%s", name);
    compat_set_mod_root(root);
    debug_printf("ps2mods: using mod '%s'\n", name);
}

bool ps2mods_boot_menu()
{
    ps2mods_scan();
    if (gModCount == 0) {
        return false;
    }

    char last[MOD_NAME_SIZE];
    ps2mods_read_last(last, sizeof(last));
    int start = 0;
    for (int i = 0; i < gModCount; i++) {
        if (compat_stricmp(gModNames[i], last) == 0) {
            start = i + 1;
        }
    }

    // Menu turned off (MODS screen): just start with the remembered choice.
    if (!gAskAtStartup) {
        ps2mods_use(start > 0 ? gModNames[start - 1] : NULL);
        return false;
    }

    char* items[MAX_MODS + 1];
    items[0] = (char*)"Original game (no mod)";
    for (int i = 0; i < gModCount; i++) {
        items[i + 1] = gModNames[i];
    }

    int x = screenGetWidth() / 2 - 120;
    int y = screenGetHeight() / 2 - 100;
    // This runs before the game is set up: the splash screen's palette is
    // still on, the mouse cursor is hidden (so clicks are ignored) and the
    // D-pad would jump that hidden cursor around. Use the game's palette and
    // plain menu keys: D-pad picks, X / Square starts, O / Start = no mod.
    palette_set_to(cmap);
    mouse_show();
    ps2pad_set_menu_keys(true);
    kb_clear();

    // Test mode: press the listed pad buttons, as the controller would.
    const char* testPad = ps2autotest_boot_pad();
    if (testPad != NULL) {
        debug_printf("ps2mods: test presses %s\n", testPad);
        for (const char* p = testPad; *p != '\0';) {
            int button = atoi(p);
            for (int down = 1; down >= 0; down--) {
                SDL_Event e;
                memset(&e, 0, sizeof(e));
                e.type = down ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
                e.jbutton.button = (Uint8)button;
                e.jbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
                SDL_PushEvent(&e);
            }
            const char* comma = strchr(p, ',');
            p = comma != NULL ? comma + 1 : p + strlen(p);
        }
    }

    int choice = win_list_select_at("CHOOSE A MOD", items, gModCount + 1, NULL, x, y, colorTable[32747] | 0x10000, start);

    ps2pad_set_menu_keys(false);
    mouse_hide();

    const char* name = choice > 0 ? gModNames[choice - 1] : "";
    ps2mods_use(name);
    ps2mods_write_last(name, true);
    return true; // the splash screen needs showing again
}

int ps2mods_count()
{
    return gModCount;
}

// --- MODS screen (main menu) -------------------------------------------------------

#define SCREEN_W 640
#define SCREEN_H 480
#define LIST_X 24
#define LIST_Y 70
#define LIST_W 250
#define LIST_ROW 17
#define LIST_ROWS 20
#define ART_X 300
#define ART_Y 70
#define ART_W 316
#define ART_H 237
#define INFO_Y 318

#define EVENT_ITEM 2000
#define EVENT_USE 2100
#define EVENT_ASK 2101
#define EVENT_BACK 2102

typedef struct ModInfo {
    char title[64];
    char author[64];
    char lines[6][80];
    int lineCount;
} ModInfo;

static int gWin = -1;
static unsigned char* gBuf = NULL;
static int gSelected = 0; // 0 = original game, n = gModNames[n - 1]
static int gActive = 0; // what LAST.TXT says
static unsigned char* gArt = NULL; // ART_W x ART_H, palette indices
static int gArtW = 0;
static int gArtH = 0;
static ModInfo gInfo;

// The mod being looked at on the MODS screen (its archives, if any).
static ModPack* gPreviewPack = NULL;

// A file of the mod (MOD.TXT, PREVIEW.BMP): loose in its folder, or
// anywhere inside its .ZIP / .DAT files.
static bool ps2mods_read_extra(int item, const char* file, unsigned char** data, size_t* size)
{
    const char* name = gModNames[item - 1];
    if (!modpack_is_archive_file(name)) {
        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "mods/%s/%s", name, file);
        FILE* stream = fopen(path, "rb");
        if (stream != NULL) {
            fseek(stream, 0, SEEK_END);
            long length = ftell(stream);
            fseek(stream, 0, SEEK_SET);
            unsigned char* buf = length > 0 && length < 8 * 1024 * 1024 ? (unsigned char*)malloc(length + 1) : NULL;
            bool ok = buf != NULL && fread(buf, 1, length, stream) == (size_t)length;
            fclose(stream);
            if (ok) {
                buf[length] = '\0';
                *data = buf;
                *size = (size_t)length;
                return true;
            }
            free(buf);
        }
    }
    if (gPreviewPack != NULL && modpack_read_named(gPreviewPack, file, data, size)) {
        unsigned char* grown = (unsigned char*)realloc(*data, *size + 1);
        if (grown != NULL) {
            grown[*size] = '\0';
            *data = grown;
        }
        return true;
    }
    return false;
}

// MOD.TXT: line 1 the name, line 2 author / version, the rest a description
// (word-wrapped to the box).
static void ps2mods_load_info(int item)
{
    memset(&gInfo, 0, sizeof(gInfo));
    if (item == 0) {
        snprintf(gInfo.title, sizeof(gInfo.title), "Original game");
        snprintf(gInfo.lines[0], sizeof(gInfo.lines[0]), "Fallout as it shipped, no mod files used.");
        gInfo.lineCount = 1;
        if (gModCount == 0) {
            static const char* help[] = {
                "Fallout as it shipped.",
                "No mods installed. To add one, make",
                "a folder mods/<NAME>/ next to",
                "MASTER.DAT with the mod's files,",
                "laid out like DATA. Optional extras:",
                "PREVIEW.BMP (art), MOD.TXT (text).",
            };
            for (int i = 0; i < 6; i++) {
                snprintf(gInfo.lines[i], sizeof(gInfo.lines[i]), "%s", help[i]);
            }
            gInfo.lineCount = 6;
        }
        return;
    }
    const char* name = gModNames[item - 1];
    snprintf(gInfo.title, sizeof(gInfo.title), "%s", name);

    unsigned char* data = NULL;
    size_t dataSize = 0;
    if (!ps2mods_read_extra(item, "MOD.TXT", &data, &dataSize)) {
        snprintf(gInfo.lines[0], sizeof(gInfo.lines[0]), "No MOD.TXT in this mod.");
        gInfo.lineCount = 1;
        return;
    }

    int lineNo = 0;
    char text[1024] = "";
    char* cursor = (char*)data;
    while (*cursor != '\0') {
        char* end = cursor + strcspn(cursor, "\r\n");
        char saved = *end;
        *end = '\0';
        char line[256];
        snprintf(line, sizeof(line), "%s", cursor);
        *end = saved;
        cursor = end;
        if (*cursor == '\r') {
            cursor++;
        }
        if (*cursor == '\n') {
            cursor++;
        }
        if (lineNo == 0 && line[0] != '\0') {
            snprintf(gInfo.title, sizeof(gInfo.title), "%s", line);
        } else if (lineNo == 1) {
            snprintf(gInfo.author, sizeof(gInfo.author), "%s", line);
        } else if (strlen(text) + strlen(line) + 2 < sizeof(text)) {
            if (text[0] != '\0') {
                strcat(text, " ");
            }
            strcat(text, line);
        }
        lineNo++;
    }
    free(data);

    // Word wrap into the description box.
    const int maxWidth = SCREEN_W - 2 * LIST_X - LIST_W - 10;
    char* p = text;
    while (*p != '\0' && gInfo.lineCount < 6) {
        while (*p == ' ') {
            p++;
        }
        char* out = gInfo.lines[gInfo.lineCount];
        int len = 0;
        int lastSpace = -1;
        while (p[len] != '\0' && len < 79) {
            out[len] = p[len];
            out[len + 1] = '\0';
            if (text_width(out) > maxWidth) {
                break;
            }
            if (p[len] == ' ') {
                lastSpace = len;
            }
            len++;
        }
        if (p[len] != '\0' && lastSpace > 0) {
            len = lastSpace;
        }
        out[len] = '\0';
        p += len;
        gInfo.lineCount++;
    }
}

// PREVIEW.BMP (any size; 8, 24 or 32-bit), scaled to fit the art box and
// mapped to the game's palette.
static void ps2mods_load_art(int item)
{
    free(gArt);
    gArt = NULL;
    gArtW = 0;
    gArtH = 0;
    if (item == 0) {
        return;
    }

    unsigned char* bmp = NULL;
    size_t bmpSize = 0;
    if (!ps2mods_read_extra(item, "PREVIEW.BMP", &bmp, &bmpSize)) {
        return;
    }
    SDL_Surface* loaded = SDL_LoadBMP_RW(SDL_RWFromConstMem(bmp, (int)bmpSize), 1);
    free(bmp);
    if (loaded == NULL) {
        return;
    }
    SDL_Surface* rgb = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGB888, 0);
    SDL_FreeSurface(loaded);
    if (rgb == NULL) {
        return;
    }

    // Fit inside the box, keeping the shape.
    int w = ART_W;
    int h = rgb->h * ART_W / (rgb->w > 0 ? rgb->w : 1);
    if (h > ART_H) {
        h = ART_H;
        w = rgb->w * ART_H / (rgb->h > 0 ? rgb->h : 1);
    }
    if (w > 0 && h > 0) {
        gArt = (unsigned char*)malloc(w * h);
    }
    if (gArt != NULL) {
        SDL_LockSurface(rgb);
        for (int y = 0; y < h; y++) {
            const Uint32* row = (const Uint32*)((const Uint8*)rgb->pixels + (y * rgb->h / h) * rgb->pitch);
            for (int x = 0; x < w; x++) {
                Uint32 c = row[x * rgb->w / w];
                int r = (c >> 16) & 0xFF;
                int g = (c >> 8) & 0xFF;
                int b = c & 0xFF;
                gArt[y * w + x] = (unsigned char)colorTable[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
            }
        }
        SDL_UnlockSurface(rgb);
        gArtW = w;
        gArtH = h;
    }
    SDL_FreeSurface(rgb);
}

static void ps2mods_select(int item)
{
    modpack_close(gPreviewPack);
    gPreviewPack = NULL;
    if (item > 0) {
        char root[64];
        snprintf(root, sizeof(root), "mods/%s", gModNames[item - 1]);
        gPreviewPack = modpack_open(root);
    }
    gSelected = item;
    ps2mods_load_info(item);
    ps2mods_load_art(item);
}

static void ps2mods_text(const char* text, int x, int y, int color)
{
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H - text_height()) {
        return;
    }
    text_to_buf(gBuf + SCREEN_W * y + x, text, SCREEN_W - x, SCREEN_W, color);
}

static void ps2mods_text_fit(const char* text, int x, int y, int maxWidth, int color)
{
    char fit[80];
    snprintf(fit, sizeof(fit), "%s", text);
    size_t len = strlen(fit);
    while (len > 1 && text_width(fit) > maxWidth) {
        fit[--len] = '\0';
        if (len > 3) {
            fit[len - 1] = '.';
            fit[len - 2] = '.';
        }
    }
    ps2mods_text(fit, x, y, color);
}

// Vault Boy with a wrench (the Repair skill picture), centred on cx.
static void ps2mods_draw_vault_boy(int cx, int y)
{
    CacheEntry* handle;
    int width;
    int height;
    int fid = art_id(OBJ_TYPE_SKILLDEX, skill_pic(SKILL_REPAIR), 0, 0, 0);
    unsigned char* data = art_lock(fid, &handle, &width, &height);
    if (data == NULL) {
        return;
    }
    int x = cx - width / 2;
    for (int row = 0; row < height && y + row < ART_Y + ART_H; row++) {
        unsigned char* dest = gBuf + SCREEN_W * (y + row);
        const unsigned char* src = data + row * width;
        for (int col = 0; col < width; col++) {
            int dx = x + col;
            if (src[col] != 0 && dx >= ART_X && dx < ART_X + ART_W) {
                dest[dx] = src[col];
            }
        }
    }
    art_ptr_unlock(handle);
}

static void ps2mods_draw()
{
    const int green = colorTable[992];
    const int bright = colorTable[32767];
    const int yellow = colorTable[18979];
    const int dim = colorTable[10570];
    const int itemCount = gModCount + 1 < LIST_ROWS ? gModCount + 1 : LIST_ROWS;

    buf_fill(gBuf, SCREEN_W, SCREEN_H, SCREEN_W, colorTable[0]);
    draw_box(gBuf, SCREEN_W, 8, 8, SCREEN_W - 9, SCREEN_H - 9, green);

    text_font(103);
    ps2mods_text("MODS", SCREEN_W / 2 - text_width("MODS") / 2, 16, yellow);
    text_font(101);
    draw_line(gBuf, SCREEN_W, 16, 50, SCREEN_W - 17, 50, dim);

    // List.
    ps2mods_text("INSTALLED", LIST_X, LIST_Y - 16, yellow);
    for (int i = 0; i < itemCount; i++) {
        int y = LIST_Y + i * LIST_ROW;
        const char* name = i == 0 ? "Original game" : gModNames[i - 1];
        if (i == gSelected) {
            draw_box(gBuf, SCREEN_W, LIST_X - 4, y - 2, LIST_X + LIST_W, y + LIST_ROW - 4, green);
        }
        ps2mods_text_fit(name, LIST_X + 12, y, LIST_W - 70, i == gSelected ? bright : green);
        if (i == gActive) {
            ps2mods_text("*", LIST_X, y, yellow);
            ps2mods_text("IN USE", LIST_X + LIST_W - text_width("IN USE") - 6, y, yellow);
        }
    }

    // Artwork.
    draw_box(gBuf, SCREEN_W, ART_X - 2, ART_Y - 2, ART_X + ART_W + 1, ART_Y + ART_H + 1, dim);
    if (gArt != NULL) {
        int ox = ART_X + (ART_W - gArtW) / 2;
        int oy = ART_Y + (ART_H - gArtH) / 2;
        buf_to_buf(gArt, gArtW, gArtH, gArtW, gBuf + SCREEN_W * oy + ox, SCREEN_W);
    } else if (gModCount == 0) {
        ps2mods_draw_vault_boy(ART_X + ART_W / 2, ART_Y + 6);
    } else {
        const char* none = gSelected == 0 ? "FALLOUT" : "No artwork (PREVIEW.BMP)";
        if (gSelected == 0) {
            text_font(103);
        }
        ps2mods_text(none, ART_X + ART_W / 2 - text_width(none) / 2, ART_Y + ART_H / 2 - text_height() / 2, gSelected == 0 ? yellow : dim);
        text_font(101);
    }

    // Info.
    int infoX = ART_X;
    int y = INFO_Y;
    text_font(103);
    ps2mods_text_fit(gInfo.title, infoX, y, ART_W, yellow);
    y += text_height() + 2;
    text_font(101);
    if (gInfo.author[0] != '\0') {
        ps2mods_text_fit(gInfo.author, infoX, y, ART_W, dim);
        y += LIST_ROW;
    }
    for (int i = 0; i < gInfo.lineCount; i++) {
        ps2mods_text(gInfo.lines[i], infoX, y, green);
        y += text_height() + 1;
    }

    // Notes and buttons.
    char ask[64];
    snprintf(ask, sizeof(ask), "Ask at startup: %s", gAskAtStartup ? "Yes" : "No");
    ps2mods_text("A new mod choice takes effect the next time you start the game.", 24, SCREEN_H - 70, dim);

    const char* use = gSelected == gActive ? "IN USE" : "USE THIS";
    ps2mods_text(use, 120 - text_width(use) / 2, SCREEN_H - 40, gSelected == gActive ? dim : bright);
    draw_box(gBuf, SCREEN_W, 120 - 60, SCREEN_H - 46, 120 + 60, SCREEN_H - 22, green);
    ps2mods_text(ask, 320 - text_width(ask) / 2, SCREEN_H - 40, bright);
    draw_box(gBuf, SCREEN_W, 320 - 90, SCREEN_H - 46, 320 + 90, SCREEN_H - 22, green);
    ps2mods_text("BACK", 520 - text_width("BACK") / 2, SCREEN_H - 40, bright);
    draw_box(gBuf, SCREEN_W, 520 - 60, SCREEN_H - 46, 520 + 60, SCREEN_H - 22, green);

    win_draw(gWin);
}

static void ps2mods_button(int x, int y, int w, int h, int event)
{
    int btn = win_register_button(gWin, x, y, w, h, -1, -1, -1, event, NULL, NULL, NULL, 0);
    if (btn != -1) {
        win_register_button_sound_func(btn, gsound_red_butt_press, gsound_red_butt_release);
    }
}

static void ps2mods_restart_notice()
{
    const int width = 360;
    const int height = 90;
    int x = (screenGetWidth() - width) / 2;
    int y = (screenGetHeight() - height) / 2;
    int win = win_add(x, y, width, height, colorTable[0], WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (win == -1) {
        return;
    }
    unsigned char* buf = win_get_buf(win);
    buf_fill(buf, width, height, width, colorTable[0]);
    draw_box(buf, width, 0, 0, width - 1, height - 1, colorTable[992]);
    const char* l1 = "Mod choice saved.";
    const char* l2 = "Restart the game to play with it.";
    const char* l3 = "Press any button";
    text_to_buf(buf + width * 16 + (width - text_width(l1)) / 2, l1, width, width, colorTable[18979]);
    text_to_buf(buf + width * 36 + (width - text_width(l2)) / 2, l2, width, width, colorTable[992]);
    text_to_buf(buf + width * 64 + (width - text_width(l3)) / 2, l3, width, width, colorTable[10570]);
    win_register_button(win, 0, 0, width, height, -1, -1, -1, KEY_RETURN, NULL, NULL, NULL, 0);
    win_draw(win);
    while (true) {
        sharedFpsLimiter.mark();
        int input = get_input();
        if (input != -1) {
            break;
        }
        renderPresent();
        sharedFpsLimiter.throttle();
    }
    win_delete(win);
}

void ps2mods_screen()
{
    ps2mods_scan();

    char last[MOD_NAME_SIZE];
    ps2mods_read_last(last, sizeof(last));
    gActive = 0;
    for (int i = 0; i < gModCount; i++) {
        if (compat_stricmp(gModNames[i], last) == 0) {
            gActive = i + 1;
        }
    }

    int fontSaved = text_curr();
    text_font(101);

    gWin = win_add((screenGetWidth() - SCREEN_W) / 2, (screenGetHeight() - SCREEN_H) / 2, SCREEN_W, SCREEN_H,
        colorTable[0], WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (gWin == -1) {
        text_font(fontSaved);
        return;
    }
    gBuf = win_get_buf(gWin);

    const int itemCount = gModCount + 1 < LIST_ROWS ? gModCount + 1 : LIST_ROWS;
    for (int i = 0; i < itemCount; i++) {
        ps2mods_button(LIST_X - 4, LIST_Y + i * LIST_ROW - 2, LIST_W + 4, LIST_ROW - 1, EVENT_ITEM + i);
    }
    ps2mods_button(120 - 60, SCREEN_H - 46, 120, 24, EVENT_USE);
    ps2mods_button(320 - 90, SCREEN_H - 46, 180, 24, EVENT_ASK);
    ps2mods_button(520 - 60, SCREEN_H - 46, 120, 24, EVENT_BACK);

    ps2mods_select(gActive);
    ps2mods_draw();

    while (true) {
        sharedFpsLimiter.mark();
        int input = get_input();
        if (input == KEY_ESCAPE || input == EVENT_BACK) {
            break;
        } else if (input >= EVENT_ITEM && input < EVENT_ITEM + itemCount) {
            int item = input - EVENT_ITEM;
            if (item != gSelected) {
                ps2mods_select(item);
                ps2mods_draw();
            }
        } else if (input == KEY_ARROW_UP || input == KEY_ARROW_DOWN) {
            int item = gSelected + (input == KEY_ARROW_UP ? -1 : 1);
            if (item >= 0 && item < itemCount) {
                ps2mods_select(item);
                ps2mods_draw();
            }
        } else if (input == EVENT_USE || input == KEY_RETURN) {
            if (gSelected != gActive) {
                gActive = gSelected;
                ps2mods_write_last(gActive > 0 ? gModNames[gActive - 1] : "", gAskAtStartup);
                ps2mods_draw();
                ps2mods_restart_notice();
                ps2mods_draw();
            }
        } else if (input == EVENT_ASK) {
            ps2mods_write_last(gActive > 0 ? gModNames[gActive - 1] : "", !gAskAtStartup);
            ps2mods_draw();
        }
        renderPresent();
        sharedFpsLimiter.throttle();
    }

    win_delete(gWin);
    gWin = -1;
    modpack_close(gPreviewPack);
    gPreviewPack = NULL;
    free(gArt);
    gArt = NULL;
    text_font(fontSaved);
}

} // namespace fallout

#endif /* __PS2__ */
