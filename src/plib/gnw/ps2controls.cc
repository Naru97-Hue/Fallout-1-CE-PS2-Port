// Remappable controls for the PS2 port, and the in-game Controls screen
// (Options menu > CONTROLS).
//
// Settings live in CONTROLS.CFG next to FALLOUT.ELF:
//   [pad]      one entry per DualShock button: an action (see kActions)
//   [keyboard] one entry per game hotkey: the USB keyboard key that does it
//   [mouse]    swap_buttons, middle_button (an action)
//   [stick]    cursor_speed (1-10), dead_zone (1-3), invert_zoom, dpad_navigation
// Actions and keys are stored as numbers (SDL scancodes, or the negative
// PS2_ACTION_* values).

#ifdef __PS2__

#include "plib/gnw/ps2controls.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "fps_limiter.h"
#include "game/art.h"
#include "game/config.h"
#include "game/gsound.h"
#include "game/options.h"
#include "game/stat.h"
#include "platform_compat.h"
#include "plib/color/color.h"
#include "plib/gnw/button.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/ps2video.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"

namespace fallout {

#define CONTROLS_FILE "CONTROLS.CFG"

// --- Actions -------------------------------------------------------------------

typedef struct Action {
    int code; // PS2_ACTION_* or SDL scancode
    const char* name;
    const char* shortName; // for the GAMEPAD picture
} Action;

static const Action kActions[] = {
    { PS2_ACTION_NONE, "Nothing", "-" },
    { PS2_ACTION_LEFT_CLICK, "Left click", "Left click" },
    { PS2_ACTION_RIGHT_CLICK, "Right click", "Right click" },
    { PS2_ACTION_KEYBOARD, "On-screen keyboard", "Keyboard" },
    { SDL_SCANCODE_RETURN, "Enter / confirm", "Enter" },
    { SDL_SCANCODE_ESCAPE, "Esc / cancel", "Esc" },
    { SDL_SCANCODE_I, "Inventory", "Inventory" },
    { SDL_SCANCODE_A, "Enter combat", "Combat" },
    { SDL_SCANCODE_SPACE, "End turn", "End turn" },
    { SDL_SCANCODE_C, "Character", "Character" },
    { SDL_SCANCODE_P, "Pip-Boy", "Pip-Boy" },
    { SDL_SCANCODE_TAB, "Automap", "Automap" },
    { SDL_SCANCODE_B, "Switch hand", "Swap hand" },
    { SDL_SCANCODE_S, "Skilldex", "Skilldex" },
    { SDL_SCANCODE_O, "Options", "Options" },
    { SDL_SCANCODE_F6, "Quick save", "Quicksave" },
    { SDL_SCANCODE_F7, "Quick load", "Quickload" },
    { SDL_SCANCODE_F1, "Help", "Help" },
};
#define ACTION_COUNT (int)(sizeof(kActions) / sizeof(kActions[0]))

// --- DualShock buttons -------------------------------------------------------

typedef struct PadButton {
    int button; // SDL PS2 button index (see ps2pad.cc)
    const char* name;
    const char* key; // config key
    int defaultAction;
} PadButton;

static const PadButton kPadButtons[] = {
    { 14, "Cross", "cross", PS2_ACTION_LEFT_CLICK },
    { 13, "Circle", "circle", PS2_ACTION_RIGHT_CLICK },
    { 15, "Square", "square", SDL_SCANCODE_RETURN },
    { 12, "Triangle", "triangle", SDL_SCANCODE_I },
    { 10, "L1", "l1", SDL_SCANCODE_A },
    { 11, "R1", "r1", SDL_SCANCODE_SPACE },
    { 8, "L2", "l2", SDL_SCANCODE_C },
    { 9, "R2", "r2", SDL_SCANCODE_P },
    { 1, "L3", "l3", SDL_SCANCODE_B },
    { 2, "R3", "r3", PS2_ACTION_KEYBOARD },
    { 0, "Select", "select", SDL_SCANCODE_TAB },
    { 3, "Start", "start", SDL_SCANCODE_ESCAPE },
};
#define PAD_BUTTON_ROWS (int)(sizeof(kPadButtons) / sizeof(kPadButtons[0]))

// --- Keyboard hotkeys ----------------------------------------------------------

typedef struct Hotkey {
    int scancode; // what the game expects
    const char* name;
    const char* key; // config key
} Hotkey;

static const Hotkey kHotkeys[] = {
    { SDL_SCANCODE_I, "Inventory", "inventory" },
    { SDL_SCANCODE_A, "Enter combat", "combat" },
    { SDL_SCANCODE_SPACE, "End turn", "end_turn" },
    { SDL_SCANCODE_C, "Character", "character" },
    { SDL_SCANCODE_P, "Pip-Boy", "pipboy" },
    { SDL_SCANCODE_TAB, "Automap", "automap" },
    { SDL_SCANCODE_B, "Switch hand", "switch_hand" },
    { SDL_SCANCODE_S, "Skilldex", "skilldex" },
    { SDL_SCANCODE_O, "Options", "options" },
    { SDL_SCANCODE_F6, "Quick save", "quick_save" },
    { SDL_SCANCODE_F7, "Quick load", "quick_load" },
    { SDL_SCANCODE_F1, "Help", "help" },
};
#define HOTKEY_ROWS (int)(sizeof(kHotkeys) / sizeof(kHotkeys[0]))

// --- Settings ------------------------------------------------------------------

typedef struct Settings {
    int pad[PAD_BUTTON_ROWS];
    int hotkey[HOTKEY_ROWS]; // physical key per hotkey
    bool mouseSwap;
    int mouseMiddle;
    int cursorSpeed; // 1-10, 5 = default
    int deadZone; // 1-3
    bool invertZoom;
    bool dpadNavigation;
    bool video[PS2_VIDEO_SETTING_COUNT];
    int saveLocation; // SAVE_LOCATION_*
} Settings;

enum {
    SAVE_LOCATION_GAME_FOLDER,
    SAVE_LOCATION_MC0,
    SAVE_LOCATION_MC1,
    SAVE_LOCATION_COUNT,
};

static const char* kSaveRoots[SAVE_LOCATION_COUNT] = { NULL, "mc0:", "mc1:" };
static const char* kSaveNames[SAVE_LOCATION_COUNT] = { "Game folder (USB / HDD)", "Memory card 1", "Memory card 2" };

static void ps2controls_apply_save_location(int location)
{
    if (location < 0 || location >= SAVE_LOCATION_COUNT) {
        location = SAVE_LOCATION_GAME_FOLDER;
    }
    compat_set_save_root(kSaveRoots[location]);
}

// Is there a memory card in that slot (location 1/2)?
static bool ps2controls_card_present(int location)
{
    if (location == SAVE_LOCATION_GAME_FOLDER) {
        return true;
    }
    DIR* dir = opendir(location == SAVE_LOCATION_MC0 ? "mc0:/" : "mc1:/");
    if (dir == NULL) {
        return false;
    }
    closedir(dir);
    return true;
}

static const char* kVideoKeys[PS2_VIDEO_SETTING_COUNT] = { "gs_map", "gs_cursor", "gs_fades" };

static Settings gSettings;

void ps2controls_save_location_fell_back()
{
    // For this session only: CONTROLS.CFG keeps the player's choice.
    gSettings.saveLocation = SAVE_LOCATION_GAME_FOLDER;
}
static bool gLoaded = false;
static int gKeyMap[SDL_NUM_SCANCODES]; // physical -> game scancode

static void ps2controls_defaults(Settings* settings)
{
    for (int i = 0; i < PAD_BUTTON_ROWS; i++) {
        settings->pad[i] = kPadButtons[i].defaultAction;
    }
    for (int i = 0; i < HOTKEY_ROWS; i++) {
        settings->hotkey[i] = kHotkeys[i].scancode;
    }
    settings->mouseSwap = false;
    settings->mouseMiddle = SDL_SCANCODE_RETURN;
    settings->cursorSpeed = 5;
    settings->deadZone = 2;
    settings->invertZoom = false;
    for (int i = 0; i < PS2_VIDEO_SETTING_COUNT; i++) {
        settings->video[i] = true;
    }
    settings->saveLocation = SAVE_LOCATION_GAME_FOLDER;
    // PADNAV.OFF (older builds) still turns D-pad navigation off by default.
    FILE* stream = fopen("PADNAV.OFF", "rb");
    settings->dpadNavigation = stream == NULL;
    if (stream != NULL) {
        fclose(stream);
    }
}

static bool ps2controls_valid_action(int action)
{
    for (int i = 0; i < ACTION_COUNT; i++) {
        if (kActions[i].code == action) {
            return true;
        }
    }
    return false;
}

static void ps2controls_rebuild_keymap()
{
    for (int i = 0; i < SDL_NUM_SCANCODES; i++) {
        gKeyMap[i] = i;
    }
    for (int i = 0; i < HOTKEY_ROWS; i++) {
        int physical = gSettings.hotkey[i];
        if (physical > 0 && physical < SDL_NUM_SCANCODES && physical != kHotkeys[i].scancode) {
            gKeyMap[physical] = kHotkeys[i].scancode;
        }
    }
}

void ps2controls_load()
{
    if (gLoaded) {
        return;
    }
    gLoaded = true;
    ps2controls_defaults(&gSettings);

    Config config;
    if (config_init(&config)) {
        if (config_load(&config, CONTROLS_FILE, false)) {
            int value;
            for (int i = 0; i < PAD_BUTTON_ROWS; i++) {
                if (config_get_value(&config, "pad", kPadButtons[i].key, &value) && ps2controls_valid_action(value)) {
                    gSettings.pad[i] = value;
                }
            }
            for (int i = 0; i < HOTKEY_ROWS; i++) {
                if (config_get_value(&config, "keyboard", kHotkeys[i].key, &value) && value > 0 && value < SDL_NUM_SCANCODES) {
                    gSettings.hotkey[i] = value;
                }
            }
            if (config_get_value(&config, "mouse", "swap_buttons", &value)) {
                gSettings.mouseSwap = value != 0;
            }
            if (config_get_value(&config, "mouse", "middle_button", &value) && ps2controls_valid_action(value)) {
                gSettings.mouseMiddle = value;
            }
            if (config_get_value(&config, "stick", "cursor_speed", &value) && value >= 1 && value <= 10) {
                gSettings.cursorSpeed = value;
            }
            if (config_get_value(&config, "stick", "dead_zone", &value) && value >= 1 && value <= 3) {
                gSettings.deadZone = value;
            }
            if (config_get_value(&config, "stick", "invert_zoom", &value)) {
                gSettings.invertZoom = value != 0;
            }
            if (config_get_value(&config, "stick", "dpad_navigation", &value)) {
                gSettings.dpadNavigation = value != 0;
            }
            for (int i = 0; i < PS2_VIDEO_SETTING_COUNT; i++) {
                if (config_get_value(&config, "video", kVideoKeys[i], &value)) {
                    gSettings.video[i] = value != 0;
                }
            }
            if (config_get_value(&config, "saves", "location", &value) && value >= 0 && value < SAVE_LOCATION_COUNT) {
                gSettings.saveLocation = value;
            }
            debug_printf("ps2controls: loaded %s\n", CONTROLS_FILE);
        }
        config_exit(&config);
    }

    ps2controls_rebuild_keymap();
    ps2controls_apply_save_location(gSettings.saveLocation);
}

// Only settings that differ from the defaults are written, so the file
// stays a few lines long (and is empty / removed when nothing changed).
static void ps2controls_save()
{
    Settings def;
    ps2controls_defaults(&def);

    Config config;
    if (!config_init(&config)) {
        return;
    }
    int changed = 0;
#define SAVE_IF(cond, section, key, value)                \
    if (cond) {                                           \
        config_set_value(&config, section, key, (value)); \
        changed++;                                        \
    }
    for (int i = 0; i < PAD_BUTTON_ROWS; i++) {
        SAVE_IF(gSettings.pad[i] != def.pad[i], "pad", kPadButtons[i].key, gSettings.pad[i]);
    }
    for (int i = 0; i < HOTKEY_ROWS; i++) {
        SAVE_IF(gSettings.hotkey[i] != def.hotkey[i], "keyboard", kHotkeys[i].key, gSettings.hotkey[i]);
    }
    SAVE_IF(gSettings.mouseSwap != def.mouseSwap, "mouse", "swap_buttons", gSettings.mouseSwap ? 1 : 0);
    SAVE_IF(gSettings.mouseMiddle != def.mouseMiddle, "mouse", "middle_button", gSettings.mouseMiddle);
    SAVE_IF(gSettings.cursorSpeed != def.cursorSpeed, "stick", "cursor_speed", gSettings.cursorSpeed);
    SAVE_IF(gSettings.deadZone != def.deadZone, "stick", "dead_zone", gSettings.deadZone);
    SAVE_IF(gSettings.invertZoom != def.invertZoom, "stick", "invert_zoom", gSettings.invertZoom ? 1 : 0);
    SAVE_IF(gSettings.dpadNavigation != def.dpadNavigation, "stick", "dpad_navigation", gSettings.dpadNavigation ? 1 : 0);
    for (int i = 0; i < PS2_VIDEO_SETTING_COUNT; i++) {
        SAVE_IF(gSettings.video[i] != def.video[i], "video", kVideoKeys[i], gSettings.video[i] ? 1 : 0);
    }
    SAVE_IF(gSettings.saveLocation != def.saveLocation, "saves", "location", gSettings.saveLocation);
#undef SAVE_IF

    if (changed == 0) {
        // All defaults: no file needed (an empty one if it can't be removed).
        if (compat_remove(CONTROLS_FILE) != 0) {
            FILE* stream = fopen(CONTROLS_FILE, "wb");
            if (stream != NULL) {
                fclose(stream);
            }
        }
    } else if (!config_save(&config, CONTROLS_FILE, false)) {
        debug_printf("ps2controls: couldn't write %s\n", CONTROLS_FILE);
    }
    config_exit(&config);
}

// --- Queries -------------------------------------------------------------------

int ps2controls_pad_action(int button)
{
    ps2controls_load();
    for (int i = 0; i < PAD_BUTTON_ROWS; i++) {
        if (kPadButtons[i].button == button) {
            return gSettings.pad[i];
        }
    }
    return PS2_ACTION_NONE;
}

int ps2controls_translate_key(int scancode)
{
    ps2controls_load();
    if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES) {
        return scancode;
    }
    return gKeyMap[scancode];
}

bool ps2controls_mouse_swapped()
{
    ps2controls_load();
    return gSettings.mouseSwap;
}

int ps2controls_mouse_middle_action()
{
    ps2controls_load();
    return gSettings.mouseMiddle;
}

float ps2controls_cursor_speed()
{
    ps2controls_load();
    // 1 -> 0.4x, 5 -> 1.0x, 10 -> 2.0x
    static const float kSpeeds[11] = { 1.0f, 0.4f, 0.55f, 0.7f, 0.85f, 1.0f, 1.2f, 1.4f, 1.6f, 1.8f, 2.0f };
    return kSpeeds[gSettings.cursorSpeed];
}

int ps2controls_dead_zone()
{
    ps2controls_load();
    static const int kZones[4] = { 6000, 3500, 6000, 9500 };
    return kZones[gSettings.deadZone];
}

bool ps2controls_zoom_inverted()
{
    ps2controls_load();
    return gSettings.invertZoom;
}

bool ps2controls_video_setting(int setting)
{
    ps2controls_load();
    if (setting < 0 || setting >= PS2_VIDEO_SETTING_COUNT) {
        return true;
    }
    return gSettings.video[setting];
}

bool ps2controls_dpad_navigation()
{
    ps2controls_load();
    return gSettings.dpadNavigation;
}

// --- Key capture -----------------------------------------------------------------

static bool gCapturing = false;
static int gCaptured = 0;

bool ps2controls_capture_key(int scancode)
{
    if (!gCapturing) {
        return false;
    }
    if (scancode > 0) {
        gCaptured = scancode;
        gCapturing = false;
    }
    return true;
}

// --- Settings screen -------------------------------------------------------------
//
// Tabs: CONTROLS (the remapping list), GAMEPAD and KEYBOARD (pictures of
// what every button/key does with the current mapping), VIDEO (TV mode and
// GS renderer switches). From the main menu a GAME PREFS button opens the
// game's own preferences screen.

enum {
    TAB_CONTROLS,
    TAB_GAMEPAD,
    TAB_KEYBOARD,
    TAB_VIDEO,
    TAB_COUNT,
};

static const char* kTabNames[TAB_COUNT] = { "CONTROLS", "GAMEPAD", "KEYBOARD", "SYSTEM" };

enum {
    ROW_PAD,
    ROW_HOTKEY,
    ROW_MOUSE_SWAP,
    ROW_MOUSE_MIDDLE,
    ROW_SPEED,
    ROW_DEAD_ZONE,
    ROW_INVERT_ZOOM,
    ROW_DPAD_NAV,
    ROW_TV_MODE,
    ROW_VIDEO_SETTING,
    ROW_SAVE_LOCATION,
};

typedef struct Row {
    int kind;
    int index;
    int x;
    int y;
    int width;
} Row;

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define ROW_HEIGHT 17
#define ROW_WIDTH 290
#define MAX_ROWS 40
#define CONTENT_TOP 80

#define EVENT_ROW_NEXT 1000
#define EVENT_ROW_PREV 1100
#define EVENT_ROW_HOVER 1200
#define EVENT_DONE 1300
#define EVENT_DEFAULTS 1301
#define EVENT_CANCEL 1302
#define EVENT_PREFS 1303
#define EVENT_TAB 1310

static Row gRows[MAX_ROWS];
static int gRowCount = 0;
static int gWindow = -1;
static unsigned char* gBuffer = NULL;
static int gHover = -1;
static int gCaptureRow = -1;
static int gTab = TAB_CONTROLS;
static bool gFromMainMenu = false;
static int gTvMode = 0;

static int gGreen;
static int gBright;
static int gYellow;
static int gDim;

static const char* ps2controls_action_name(int action)
{
    for (int i = 0; i < ACTION_COUNT; i++) {
        if (kActions[i].code == action) {
            return kActions[i].name;
        }
    }
    return "?";
}

static const char* ps2controls_action_short_name(int action)
{
    for (int i = 0; i < ACTION_COUNT; i++) {
        if (kActions[i].code == action) {
            return kActions[i].shortName;
        }
    }
    return "?";
}

static int ps2controls_cycle_action(int action, int step)
{
    int index = 0;
    for (int i = 0; i < ACTION_COUNT; i++) {
        if (kActions[i].code == action) {
            index = i;
            break;
        }
    }
    index = (index + step + ACTION_COUNT) % ACTION_COUNT;
    return kActions[index].code;
}

static const char* ps2controls_key_name(int scancode)
{
    const char* name = SDL_GetScancodeName((SDL_Scancode)scancode);
    return name != NULL && name[0] != '\0' ? name : "?";
}

static void ps2controls_row_text(int rowIndex, char* label, char* value, size_t size)
{
    const Row* row = &(gRows[rowIndex]);
    static const char* kVideoNames[PS2_VIDEO_SETTING_COUNT] = { "GS map drawing", "GS mouse cursor", "GS fades" };
    switch (row->kind) {
    case ROW_PAD:
        snprintf(label, size, "%s", kPadButtons[row->index].name);
        snprintf(value, size, "%s", ps2controls_action_name(gSettings.pad[row->index]));
        break;
    case ROW_HOTKEY:
        snprintf(label, size, "%s", kHotkeys[row->index].name);
        if (rowIndex == gCaptureRow) {
            snprintf(value, size, "press a key...");
        } else {
            snprintf(value, size, "%s", ps2controls_key_name(gSettings.hotkey[row->index]));
        }
        break;
    case ROW_MOUSE_SWAP:
        snprintf(label, size, "Swap buttons");
        snprintf(value, size, "%s", gSettings.mouseSwap ? "Yes" : "No");
        break;
    case ROW_MOUSE_MIDDLE:
        snprintf(label, size, "Middle button");
        snprintf(value, size, "%s", ps2controls_action_name(gSettings.mouseMiddle));
        break;
    case ROW_SPEED:
        snprintf(label, size, "Cursor speed");
        snprintf(value, size, "%d", gSettings.cursorSpeed);
        break;
    case ROW_DEAD_ZONE:
        snprintf(label, size, "Stick dead zone");
        snprintf(value, size, "%s", gSettings.deadZone == 1 ? "Small" : (gSettings.deadZone == 2 ? "Medium" : "Large"));
        break;
    case ROW_INVERT_ZOOM:
        snprintf(label, size, "Invert zoom");
        snprintf(value, size, "%s", gSettings.invertZoom ? "Yes" : "No");
        break;
    case ROW_DPAD_NAV:
        snprintf(label, size, "D-pad");
        snprintf(value, size, "%s", gSettings.dpadNavigation ? "Jump to targets" : "Arrow keys");
        break;
    case ROW_TV_MODE:
        snprintf(label, size, "TV output");
        snprintf(value, size, "%s", ps2video_name(ps2video_mode()));
        break;
    case ROW_VIDEO_SETTING:
        snprintf(label, size, "%s", kVideoNames[row->index]);
        snprintf(value, size, "%s", gSettings.video[row->index] ? "On" : "Off (EE draws it)");
        break;
    case ROW_SAVE_LOCATION:
        snprintf(label, size, "Save / load games on");
        snprintf(value, size, "%s%s", kSaveNames[gSettings.saveLocation],
            ps2controls_card_present(gSettings.saveLocation) ? "" : " (no card!)");
        break;
    }
}

// "Keep this video mode?" with a 6 second countdown. The cursor starts on
// NO, so if the TV shows nothing, waiting or pressing X both go back.
static void ps2controls_confirm_video()
{
    const int width = 340;
    const int height = 120;
    int x = (screenGetWidth() - width) / 2;
    int y = (screenGetHeight() - height) / 2;
    int win = win_add(x, y, width, height, colorTable[0], WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (win == -1) {
        ps2video_trial_answer(false);
        return;
    }
    unsigned char* buf = win_get_buf(win);
    const int yesX = 70;
    const int noX = 230;
    const int buttonY = 80;
    win_register_button(win, yesX - 40, buttonY - 4, 80, 26, -1, -1, -1, 'y', NULL, NULL, NULL, 0);
    win_register_button(win, noX - 40, buttonY - 4, 80, 26, -1, -1, -1, 'n', NULL, NULL, NULL, 0);
    mouse_set_position(x + noX, y + buttonY + 8);

    int shown = -1;
    while (ps2video_trial_active()) {
        sharedFpsLimiter.mark();
        int seconds = ps2video_trial_seconds_left();
        if (seconds != shown) {
            shown = seconds;
            buf_fill(buf, width, height, width, colorTable[0]);
            draw_box(buf, width, 0, 0, width - 1, height - 1, gGreen);
            char text[64];
            snprintf(text, sizeof(text), "Keep %s?", ps2video_name(ps2video_mode()));
            text_font(103);
            text_to_buf(buf + width * 14 + (width - text_width(text)) / 2, text, width, width, gYellow);
            text_font(101);
            snprintf(text, sizeof(text), "Going back in %d second%s.", seconds, seconds == 1 ? "" : "s");
            text_to_buf(buf + width * 48 + (width - text_width(text)) / 2, text, width, width, gGreen);
            text_font(103);
            text_to_buf(buf + width * buttonY + yesX - text_width("YES") / 2, "YES", width, width, gYellow);
            text_to_buf(buf + width * buttonY + noX - text_width("NO") / 2, "NO", width, width, gYellow);
            text_font(101);
            win_draw(win);
        }

        int input = get_input();
        if (input == 'y' || input == 'Y') {
            ps2video_trial_answer(true);
        } else if (input == 'n' || input == 'N' || input == KEY_ESCAPE) {
            ps2video_trial_answer(false);
        }

        renderPresent(); // also undoes the mode when time runs out
        sharedFpsLimiter.throttle();
    }

    win_delete(win);
    win_draw(gWindow);
}

static void ps2controls_change(int rowIndex, int step)
{
    Row* row = &(gRows[rowIndex]);
    switch (row->kind) {
    case ROW_PAD:
        gSettings.pad[row->index] = ps2controls_cycle_action(gSettings.pad[row->index], step);
        break;
    case ROW_HOTKEY:
        if (step > 0) {
            gCaptureRow = rowIndex;
            gCaptured = 0;
            gCapturing = true;
        } else {
            gSettings.hotkey[row->index] = kHotkeys[row->index].scancode; // back to default
        }
        break;
    case ROW_MOUSE_SWAP:
        gSettings.mouseSwap = !gSettings.mouseSwap;
        break;
    case ROW_MOUSE_MIDDLE:
        gSettings.mouseMiddle = ps2controls_cycle_action(gSettings.mouseMiddle, step);
        break;
    case ROW_SPEED:
        gSettings.cursorSpeed = (gSettings.cursorSpeed - 1 + step + 10) % 10 + 1;
        break;
    case ROW_DEAD_ZONE:
        gSettings.deadZone = (gSettings.deadZone - 1 + step + 3) % 3 + 1;
        break;
    case ROW_INVERT_ZOOM:
        gSettings.invertZoom = !gSettings.invertZoom;
        break;
    case ROW_DPAD_NAV:
        gSettings.dpadNavigation = !gSettings.dpadNavigation;
        break;
    case ROW_TV_MODE:
        // Tried right away; kept only if confirmed within 6 seconds.
        ps2video_start_trial((ps2video_mode() + step + PS2_VIDEO_MODE_COUNT) % PS2_VIDEO_MODE_COUNT, true);
        ps2controls_confirm_video();
        break;
    case ROW_VIDEO_SETTING:
        gSettings.video[row->index] = !gSettings.video[row->index];
        break;
    case ROW_SAVE_LOCATION:
        gSettings.saveLocation = (gSettings.saveLocation + step + SAVE_LOCATION_COUNT) % SAVE_LOCATION_COUNT;
        ps2controls_apply_save_location(gSettings.saveLocation);
        break;
    }
}

// --- Drawing helpers ---

static void ps2controls_text(const char* text, int x, int y, int color)
{
    if (x < 0) {
        x = 0;
    }
    if (x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT - text_height()) {
        return;
    }
    text_to_buf(gBuffer + SCREEN_WIDTH * y + x, text, SCREEN_WIDTH - x, SCREEN_WIDTH, color);
}

static void ps2controls_text_centered(const char* text, int cx, int y, int color)
{
    ps2controls_text(text, cx - text_width(text) / 2, y, color);
}

static void ps2controls_box(int left, int top, int right, int bottom, int color)
{
    draw_box(gBuffer, SCREEN_WIDTH, left, top, right, bottom, color);
}

static void ps2controls_line(int x1, int y1, int x2, int y2, int color)
{
    draw_line(gBuffer, SCREEN_WIDTH, x1, y1, x2, y2, color);
}

// A 24-sided circle.
static void ps2controls_circle(int cx, int cy, int r, int color)
{
    // cos(i * 15 degrees) * 1000
    static const int kCos[24] = { 1000, 966, 866, 707, 500, 259, 0, -259, -500, -707, -866, -966,
        -1000, -966, -866, -707, -500, -259, 0, 259, 500, 707, 866, 966 };
    for (int i = 0; i < 24; i++) {
        int j = (i + 1) % 24;
        int x1 = cx + r * kCos[i] / 1000;
        int y1 = cy - r * kCos[(i + 18) % 24] / 1000;
        int x2 = cx + r * kCos[j] / 1000;
        int y2 = cy - r * kCos[(j + 18) % 24] / 1000;
        ps2controls_line(x1, y1, x2, y2, color);
    }
}

// --- GAMEPAD tab: a DualShock 2 with every button labelled ---

static int ps2controls_pad_row(int button)
{
    for (int i = 0; i < PAD_BUTTON_ROWS; i++) {
        if (kPadButtons[i].button == button) {
            return i;
        }
    }
    return -1;
}

// A label at (lx, ly) joined to the button at (bx, by). right = text grows
// leftwards from lx (labels on the right side of the pad grow rightwards).
#define TEXT_LEFT 18
#define TEXT_RIGHT (SCREEN_WIDTH - 18)

// Shortens text (adding '.') until it is at most maxWidth pixels wide.
static void ps2controls_fit(char* text, int maxWidth)
{
    size_t len = strlen(text);
    if (text_width(text) <= maxWidth) {
        return;
    }
    while (len > 1) {
        text[--len] = '\0';
        text[len - 1] = '.';
        if (text_width(text) <= maxWidth) {
            return;
        }
    }
}

// A label at (lx, ly) joined to the button at (bx, by). Labels left of the
// pad end at lx; labels right of it start at lx. Text is kept inside the
// frame.
static void ps2controls_callout(int button, const char* fixedText, int bx, int by, int lx, int ly, bool leftSide)
{
    char text[96];
    if (fixedText != NULL) {
        snprintf(text, sizeof(text), "%s", fixedText);
    } else {
        int row = ps2controls_pad_row(button);
        snprintf(text, sizeof(text), "%s: %s", kPadButtons[row].name, ps2controls_action_short_name(gSettings.pad[row]));
    }
    ps2controls_fit(text, leftSide ? lx - TEXT_LEFT : TEXT_RIGHT - lx);
    int width = text_width(text);
    int textX = leftSide ? lx - width : lx;
    int anchorX = leftSide ? lx + 3 : lx - 3;
    ps2controls_line(bx, by, anchorX, ly + text_height() / 2, gDim);
    ps2controls_text(text, textX, ly, gGreen);
}

// Pad coordinates: x relative to the centre line, scaled to 4/5.
#define PAD_CX (SCREEN_WIDTH / 2)
#define PX(x) (PAD_CX + (x) * 4 / 5)

static void ps2controls_draw_gamepad()
{
    int c = gYellow;
    int d = gDim;

    // Body outline (right half, mirrored): the DualShock 2's wide top,
    // shoulders and long grips.
    static const int kBody[][2] = {
        { 0, 186 },
        { 60, 186 },
        { 92, 178 },
        { 122, 180 },
        { 142, 192 },
        { 152, 210 },
        { 158, 236 },
        { 168, 280 },
        { 176, 318 },
        { 176, 338 },
        { 166, 352 },
        { 148, 358 },
        { 130, 350 },
        { 116, 330 },
        { 98, 302 },
        { 80, 292 },
        { 40, 288 },
        { 0, 288 },
    };
    int points = (int)(sizeof(kBody) / sizeof(kBody[0]));
    for (int i = 0; i + 1 < points; i++) {
        ps2controls_line(PX(kBody[i][0]), kBody[i][1], PX(kBody[i + 1][0]), kBody[i + 1][1], c);
        ps2controls_line(PX(-kBody[i][0]), kBody[i][1], PX(-kBody[i + 1][0]), kBody[i + 1][1], c);
    }

    // Shoulder buttons: L1/R1 in front, L2/R2 behind them.
    ps2controls_box(PX(-128), 160, PX(-74), 167, d); // L2
    ps2controls_box(PX(-130), 170, PX(-72), 178, c); // L1
    ps2controls_box(PX(74), 160, PX(128), 167, d); // R2
    ps2controls_box(PX(72), 170, PX(130), 178, c); // R1

    // D-pad in its round well.
    int dx = PX(-100), dy = 226;
    ps2controls_circle(dx, dy, 26, d);
    ps2controls_box(dx - 5, dy - 20, dx + 5, dy - 6, c);
    ps2controls_box(dx - 5, dy + 6, dx + 5, dy + 20, c);
    ps2controls_box(dx - 20, dy - 5, dx - 6, dy + 5, c);
    ps2controls_box(dx + 6, dy - 5, dx + 20, dy + 5, c);

    // Face buttons with their symbols.
    int fx = PX(100), fy = 226;
    ps2controls_circle(fx, fy, 30, d);
    int tx = fx, ty = fy - 17; // triangle
    ps2controls_circle(tx, ty, 8, c);
    ps2controls_line(tx, ty - 4, tx - 4, ty + 3, c);
    ps2controls_line(tx - 4, ty + 3, tx + 4, ty + 3, c);
    ps2controls_line(tx + 4, ty + 3, tx, ty - 4, c);
    int ox = fx + 17, oy = fy; // circle
    ps2controls_circle(ox, oy, 8, c);
    ps2controls_circle(ox, oy, 4, c);
    int xx = fx, xy = fy + 17; // cross
    ps2controls_circle(xx, xy, 8, c);
    ps2controls_line(xx - 4, xy - 4, xx + 4, xy + 4, c);
    ps2controls_line(xx - 4, xy + 4, xx + 4, xy - 4, c);
    int sx = fx - 17, sy = fy; // square
    ps2controls_circle(sx, sy, 8, c);
    ps2controls_box(sx - 3, sy - 3, sx + 3, sy + 3, c);

    // Select (bar), Start (triangle), Analog button.
    int selX = PX(-26), startX = PX(26), midY = 222;
    ps2controls_box(selX - 7, midY - 2, selX + 7, midY + 2, c);
    ps2controls_line(startX - 5, midY - 4, startX - 5, midY + 4, c);
    ps2controls_line(startX - 5, midY - 4, startX + 6, midY, c);
    ps2controls_line(startX - 5, midY + 4, startX + 6, midY, c);
    ps2controls_circle(PAD_CX, 252, 5, d);
    ps2controls_text_centered("ANALOG", PAD_CX, 236, d);

    // Analog sticks.
    int lsx = PX(-54), rsx = PX(54), sy2 = 272;
    ps2controls_circle(lsx, sy2, 19, c);
    ps2controls_circle(lsx, sy2, 11, d);
    ps2controls_circle(rsx, sy2, 19, c);
    ps2controls_circle(rsx, sy2, 11, d);

    // Callouts: left side.
    int lx = 172;
    ps2controls_callout(8, NULL, PX(-128), 163, lx, 140, true);
    ps2controls_callout(10, NULL, PX(-130), 174, lx, 160, true);
    ps2controls_callout(-1, gSettings.dpadNavigation ? "D-pad: targets" : "D-pad: arrows", dx - 26, dy, lx, dy - 6, true);
    ps2controls_callout(0, NULL, selX, midY + 2, lx, 254, true);
    ps2controls_callout(-1, "L stick: cursor", lsx - 19, sy2, lx, 282, true);
    ps2controls_callout(1, NULL, lsx, sy2 + 19, lx, 316, true);

    // Right side.
    int rx = 468;
    ps2controls_callout(9, NULL, PX(128), 163, rx, 140, false);
    ps2controls_callout(11, NULL, PX(130), 174, rx, 160, false);
    ps2controls_callout(12, NULL, tx + 8, ty, rx, 192, false);
    ps2controls_callout(13, NULL, ox + 8, oy, rx, 212, false);
    ps2controls_callout(14, NULL, xx + 8, xy, rx, 232, false);
    ps2controls_callout(15, NULL, sx, sy + 8, rx, 252, false);
    ps2controls_callout(3, NULL, startX + 6, midY, rx, 272, false);
    ps2controls_callout(-1, "R stick: zoom", rsx + 19, sy2, rx, 292, false);
    ps2controls_callout(2, NULL, rsx, sy2 + 19, rx, 316, false);

    ps2controls_text_centered("L1 + R1 + Select: change TV output", SCREEN_WIDTH / 2, 380, gDim);
}

// --- KEYBOARD tab: a keyboard with the game's hotkeys marked ---

typedef struct KeyCap {
    int scancode;
    const char* label;
    int units; // width in quarter keys
} KeyCap;

static const KeyCap kRow0[] = { { SDL_SCANCODE_ESCAPE, "Esc", 4 }, { SDL_SCANCODE_F1, "F1", 4 }, { SDL_SCANCODE_F2, "F2", 4 },
    { SDL_SCANCODE_F3, "F3", 4 }, { SDL_SCANCODE_F4, "F4", 4 }, { SDL_SCANCODE_F5, "F5", 4 }, { SDL_SCANCODE_F6, "F6", 4 },
    { SDL_SCANCODE_F7, "F7", 4 }, { SDL_SCANCODE_F8, "F8", 4 }, { SDL_SCANCODE_F9, "F9", 4 }, { SDL_SCANCODE_F10, "F10", 4 },
    { SDL_SCANCODE_F11, "F11", 4 }, { SDL_SCANCODE_F12, "F12", 4 }, { 0, NULL, 0 } };
static const KeyCap kRow1[] = { { SDL_SCANCODE_GRAVE, "`", 4 }, { SDL_SCANCODE_1, "1", 4 }, { SDL_SCANCODE_2, "2", 4 },
    { SDL_SCANCODE_3, "3", 4 }, { SDL_SCANCODE_4, "4", 4 }, { SDL_SCANCODE_5, "5", 4 }, { SDL_SCANCODE_6, "6", 4 },
    { SDL_SCANCODE_7, "7", 4 }, { SDL_SCANCODE_8, "8", 4 }, { SDL_SCANCODE_9, "9", 4 }, { SDL_SCANCODE_0, "0", 4 },
    { SDL_SCANCODE_MINUS, "-", 4 }, { SDL_SCANCODE_EQUALS, "=", 4 }, { SDL_SCANCODE_BACKSPACE, "Bksp", 8 }, { 0, NULL, 0 } };
static const KeyCap kRow2[] = { { SDL_SCANCODE_TAB, "Tab", 6 }, { SDL_SCANCODE_Q, "Q", 4 }, { SDL_SCANCODE_W, "W", 4 },
    { SDL_SCANCODE_E, "E", 4 }, { SDL_SCANCODE_R, "R", 4 }, { SDL_SCANCODE_T, "T", 4 }, { SDL_SCANCODE_Y, "Y", 4 },
    { SDL_SCANCODE_U, "U", 4 }, { SDL_SCANCODE_I, "I", 4 }, { SDL_SCANCODE_O, "O", 4 }, { SDL_SCANCODE_P, "P", 4 },
    { SDL_SCANCODE_LEFTBRACKET, "[", 4 }, { SDL_SCANCODE_RIGHTBRACKET, "]", 4 }, { SDL_SCANCODE_BACKSLASH, "\\", 6 }, { 0, NULL, 0 } };
static const KeyCap kRow3[] = { { SDL_SCANCODE_CAPSLOCK, "Caps", 7 }, { SDL_SCANCODE_A, "A", 4 }, { SDL_SCANCODE_S, "S", 4 },
    { SDL_SCANCODE_D, "D", 4 }, { SDL_SCANCODE_F, "F", 4 }, { SDL_SCANCODE_G, "G", 4 }, { SDL_SCANCODE_H, "H", 4 },
    { SDL_SCANCODE_J, "J", 4 }, { SDL_SCANCODE_K, "K", 4 }, { SDL_SCANCODE_L, "L", 4 }, { SDL_SCANCODE_SEMICOLON, ";", 4 },
    { SDL_SCANCODE_APOSTROPHE, "'", 4 }, { SDL_SCANCODE_RETURN, "Enter", 9 }, { 0, NULL, 0 } };
static const KeyCap kRow4[] = { { SDL_SCANCODE_LSHIFT, "Shift", 9 }, { SDL_SCANCODE_Z, "Z", 4 }, { SDL_SCANCODE_X, "X", 4 },
    { SDL_SCANCODE_C, "C", 4 }, { SDL_SCANCODE_V, "V", 4 }, { SDL_SCANCODE_B, "B", 4 }, { SDL_SCANCODE_N, "N", 4 },
    { SDL_SCANCODE_M, "M", 4 }, { SDL_SCANCODE_COMMA, ",", 4 }, { SDL_SCANCODE_PERIOD, ".", 4 }, { SDL_SCANCODE_SLASH, "/", 4 },
    { SDL_SCANCODE_RSHIFT, "Shift", 11 }, { 0, NULL, 0 } };
static const KeyCap kRow5[] = { { SDL_SCANCODE_LCTRL, "Ctrl", 6 }, { SDL_SCANCODE_LALT, "Alt", 6 }, { SDL_SCANCODE_SPACE, "Space", 28 },
    { SDL_SCANCODE_RALT, "Alt", 6 }, { SDL_SCANCODE_RCTRL, "Ctrl", 6 }, { 0, NULL, 0 } };
static const KeyCap* kKeyRows[] = { kRow0, kRow1, kRow2, kRow3, kRow4, kRow5 };

// The hotkey a physical key performs (index into kHotkeys), or -1.
static int ps2controls_hotkey_for_key(int scancode)
{
    for (int i = 0; i < HOTKEY_ROWS; i++) {
        if (gSettings.hotkey[i] == scancode) {
            return i;
        }
    }
    return -1;
}

static void ps2controls_draw_keyboard()
{
    const int unit = 7; // pixels per quarter key
    const int keyHeight = 22;
    int y = CONTENT_TOP + 6;
    for (int r = 0; r < 6; r++) {
        int x = 40;
        for (const KeyCap* key = kKeyRows[r]; key->label != NULL; key++) {
            int width = key->units * unit;
            int hotkey = ps2controls_hotkey_for_key(key->scancode);
            int color = hotkey >= 0 ? gBright : gDim;
            ps2controls_box(x, y, x + width - 3, y + keyHeight - 3, color);
            ps2controls_text_centered(key->label, x + (width - 3) / 2, y + 2, hotkey >= 0 ? gYellow : gDim);
            if (hotkey >= 0) {
                char number[8];
                snprintf(number, sizeof(number), "%d", hotkey + 1);
                ps2controls_text(number, x + 2, y + keyHeight - 3 - text_height(), gGreen);
            }
            x += width;
        }
        y += keyHeight + (r == 0 ? 6 : 0);
    }

    // Legend in two columns.
    y += 10;
    for (int i = 0; i < HOTKEY_ROWS; i++) {
        char text[64];
        snprintf(text, sizeof(text), "%d  %s: %s", i + 1, kHotkeys[i].name, ps2controls_key_name(gSettings.hotkey[i]));
        int column = i / 6;
        ps2controls_fit(text, 270);
        ps2controls_text(text, 60 + column * 280, y + (i % 6) * 14, gGreen);
    }
}

// --- Window, rows, tabs ---

// The Vault Boy card from the character screen (game art), drawn at (x, y).
static void ps2controls_draw_vault_boy(int x, int y)
{
    CacheEntry* handle;
    int width;
    int height;
    int fid = art_id(OBJ_TYPE_SKILLDEX, stat_picture(STAT_LUCK), 0, 0, 0);
    unsigned char* data = art_lock(fid, &handle, &width, &height);
    if (data == NULL) {
        return;
    }
    int copyWidth = width;
    if (x + copyWidth > TEXT_RIGHT) {
        copyWidth = TEXT_RIGHT - x;
    }
    for (int row = 0; row < height && y + row < SCREEN_HEIGHT - 70; row++) {
        unsigned char* dest = gBuffer + SCREEN_WIDTH * (y + row) + x;
        const unsigned char* src = data + row * width;
        for (int col = 0; col < copyWidth; col++) {
            if (src[col] != 0) {
                dest[col] = src[col];
            }
        }
    }
    art_ptr_unlock(handle);
}

static void ps2controls_draw()
{
    buf_fill(gBuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH, colorTable[0]);
    ps2controls_box(8, 8, SCREEN_WIDTH - 9, SCREEN_HEIGHT - 9, gGreen);

    text_font(103);
    ps2controls_text_centered("SETTINGS", SCREEN_WIDTH / 2, 16, gYellow);

    // Tab bar.
    text_font(101);
    int tabWidth = (SCREEN_WIDTH - 40) / TAB_COUNT;
    for (int t = 0; t < TAB_COUNT; t++) {
        int x = 20 + t * tabWidth;
        if (t == gTab) {
            ps2controls_box(x + 4, 46, x + tabWidth - 4, 64, gGreen);
        }
        ps2controls_text_centered(kTabNames[t], x + tabWidth / 2, 51, t == gTab ? gBright : gGreen);
    }
    ps2controls_line(16, 68, SCREEN_WIDTH - 17, 68, gDim);

    if (gTab == TAB_CONTROLS) {
        ps2controls_text("CONTROLLER", 30, CONTENT_TOP, gYellow);
        ps2controls_text("KEYBOARD", 340, CONTENT_TOP, gYellow);
        ps2controls_text("MOUSE", 30, CONTENT_TOP + (PAD_BUTTON_ROWS + 1) * ROW_HEIGHT + 4, gYellow);
        ps2controls_text("STICK AND CURSOR", 340, CONTENT_TOP + (HOTKEY_ROWS + 1) * ROW_HEIGHT + 4, gYellow);
    } else if (gTab == TAB_GAMEPAD) {
        ps2controls_draw_gamepad();
    } else if (gTab == TAB_KEYBOARD) {
        ps2controls_draw_keyboard();
    } else if (gTab == TAB_VIDEO) {
        ps2controls_text("DISPLAY", 30, CONTENT_TOP, gYellow);
        ps2controls_text("A new TV output must be confirmed within", 30, CONTENT_TOP + 96, gDim);
        ps2controls_text("6 seconds or it switches back by itself.", 30, CONTENT_TOP + 110, gDim);
        ps2controls_text("GS options apply after restarting the game.", 30, CONTENT_TOP + 128, gDim);
        ps2controls_text("SAVE GAMES", 30, CONTENT_TOP + 183, gYellow);
        ps2controls_text("Used by both the save and the load screen. Memory card saves get", 30, CONTENT_TOP + 226, gDim);
        ps2controls_text("one folder per slot, with its own icon in the PS2 browser.", 30, CONTENT_TOP + 240, gDim);
        ps2controls_draw_vault_boy(480, CONTENT_TOP + 4);
    }

    for (int i = 0; i < gRowCount; i++) {
        char label[64];
        char value[64];
        ps2controls_row_text(i, label, value, sizeof(label));
        bool highlight = i == gHover || i == gCaptureRow;
        if (highlight) {
            ps2controls_box(gRows[i].x - 4, gRows[i].y - 2, gRows[i].x + gRows[i].width - 20, gRows[i].y + ROW_HEIGHT - 4, gGreen);
        }
        int valueX = gRows[i].x + (gRows[i].width > ROW_WIDTH ? 200 : 130);
        ps2controls_fit(label, valueX - gRows[i].x - 6);
        ps2controls_fit(value, (gRows[i].x < SCREEN_WIDTH / 2 && gRows[i].width <= ROW_WIDTH ? 330 : TEXT_RIGHT) - valueX);
        ps2controls_text(label, gRows[i].x, gRows[i].y, highlight ? gBright : gGreen);
        ps2controls_text(value, valueX, gRows[i].y, highlight ? gBright : gGreen);
    }

    const char* help;
    if (gCaptureRow >= 0) {
        help = "Press the new key on the USB keyboard (Esc cancels).";
    } else if (gTab == TAB_CONTROLS || gTab == TAB_VIDEO) {
        help = "Click / Cross: change   Right click / Circle: back (keys: reset)";
    } else {
        help = "Change these on the CONTROLS tab.";
    }
    ps2controls_text_centered(help, SCREEN_WIDTH / 2, SCREEN_HEIGHT - 66, gDim);

    text_font(103);
    if (gFromMainMenu) {
        ps2controls_text_centered("DONE", 100, SCREEN_HEIGHT - 42, gYellow);
        ps2controls_text_centered("GAME PREFS", 250, SCREEN_HEIGHT - 42, gYellow);
        ps2controls_text_centered("DEFAULTS", 410, SCREEN_HEIGHT - 42, gYellow);
        ps2controls_text_centered("CANCEL", 545, SCREEN_HEIGHT - 42, gYellow);
    } else {
        ps2controls_text_centered("DONE", 150, SCREEN_HEIGHT - 42, gYellow);
        ps2controls_text_centered("DEFAULTS", 320, SCREEN_HEIGHT - 42, gYellow);
        ps2controls_text_centered("CANCEL", 500, SCREEN_HEIGHT - 42, gYellow);
    }
    text_font(101);

    win_draw(gWindow);
}

static void ps2controls_add_row(int kind, int index, int x, int y, int width)
{
    if (gRowCount >= MAX_ROWS) {
        return;
    }
    Row* row = &(gRows[gRowCount]);
    row->kind = kind;
    row->index = index;
    row->x = x;
    row->y = y;
    row->width = width;
    int btn = win_register_button(gWindow, x - 4, y - 2, width - 16, ROW_HEIGHT - 1,
        EVENT_ROW_HOVER + gRowCount, -1, -1, EVENT_ROW_NEXT + gRowCount, NULL, NULL, NULL, 0);
    if (btn != -1) {
        // Right click (Circle): step back / reset a key.
        win_register_right_button(btn, -1, EVENT_ROW_PREV + gRowCount, NULL, NULL);
    }
    gRowCount++;
}

static void ps2controls_add_button(int cx, int width, int event)
{
    int btn = win_register_button(gWindow, cx - width / 2, SCREEN_HEIGHT - 46, width, 26, -1, -1, -1, event, NULL, NULL, NULL, 0);
    if (btn != -1) {
        win_register_button_sound_func(btn, gsound_red_butt_press, gsound_red_butt_release);
    }
}

static bool ps2controls_open_window()
{
    gWindow = win_add((screenGetWidth() - SCREEN_WIDTH) / 2, (screenGetHeight() - SCREEN_HEIGHT) / 2,
        SCREEN_WIDTH, SCREEN_HEIGHT, colorTable[0], WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (gWindow == -1) {
        return false;
    }
    gBuffer = win_get_buf(gWindow);
    gRowCount = 0;
    gHover = -1;
    gCaptureRow = -1;
    gCapturing = false;

    int tabWidth = (SCREEN_WIDTH - 40) / TAB_COUNT;
    for (int t = 0; t < TAB_COUNT; t++) {
        int btn = win_register_button(gWindow, 20 + t * tabWidth + 4, 46, tabWidth - 8, 19, -1, -1, -1, EVENT_TAB + t, NULL, NULL, NULL, 0);
        if (btn != -1) {
            win_register_button_sound_func(btn, gsound_med_butt_press, gsound_med_butt_release);
        }
    }

    if (gTab == TAB_CONTROLS) {
        int y = CONTENT_TOP + ROW_HEIGHT;
        for (int i = 0; i < PAD_BUTTON_ROWS; i++, y += ROW_HEIGHT) {
            ps2controls_add_row(ROW_PAD, i, 30, y, ROW_WIDTH);
        }
        y += ROW_HEIGHT + 4;
        ps2controls_add_row(ROW_MOUSE_SWAP, 0, 30, y, ROW_WIDTH);
        y += ROW_HEIGHT;
        ps2controls_add_row(ROW_MOUSE_MIDDLE, 0, 30, y, ROW_WIDTH);

        y = CONTENT_TOP + ROW_HEIGHT;
        for (int i = 0; i < HOTKEY_ROWS; i++, y += ROW_HEIGHT) {
            ps2controls_add_row(ROW_HOTKEY, i, 340, y, ROW_WIDTH);
        }
        y += ROW_HEIGHT + 4;
        ps2controls_add_row(ROW_SPEED, 0, 340, y, ROW_WIDTH);
        y += ROW_HEIGHT;
        ps2controls_add_row(ROW_DEAD_ZONE, 0, 340, y, ROW_WIDTH);
        y += ROW_HEIGHT;
        ps2controls_add_row(ROW_INVERT_ZOOM, 0, 340, y, ROW_WIDTH);
        y += ROW_HEIGHT;
        ps2controls_add_row(ROW_DPAD_NAV, 0, 340, y, ROW_WIDTH);
    } else if (gTab == TAB_VIDEO) {
        int y = CONTENT_TOP + ROW_HEIGHT;
        ps2controls_add_row(ROW_TV_MODE, 0, 30, y, 420);
        for (int i = 0; i < PS2_VIDEO_SETTING_COUNT; i++) {
            y += ROW_HEIGHT;
            ps2controls_add_row(ROW_VIDEO_SETTING, i, 30, y, 420);
        }
        ps2controls_add_row(ROW_SAVE_LOCATION, 0, 30, CONTENT_TOP + 200, 560);
    }

    if (gFromMainMenu) {
        ps2controls_add_button(100, 90, EVENT_DONE);
        ps2controls_add_button(250, 150, EVENT_PREFS);
        ps2controls_add_button(410, 130, EVENT_DEFAULTS);
        ps2controls_add_button(545, 110, EVENT_CANCEL);
    } else {
        ps2controls_add_button(150, 100, EVENT_DONE);
        ps2controls_add_button(320, 140, EVENT_DEFAULTS);
        ps2controls_add_button(500, 120, EVENT_CANCEL);
    }

    ps2controls_draw();
    return true;
}

static void ps2controls_close_window()
{
    gCapturing = false;
    if (gWindow != -1) {
        win_delete(gWindow);
        gWindow = -1;
    }
}

void ps2controls_screen(bool fromMainMenu)
{
    ps2controls_load();
    Settings saved = gSettings;
    gTvMode = ps2video_mode();
    gFromMainMenu = fromMainMenu;
    gTab = TAB_CONTROLS;

    gGreen = colorTable[992];
    gBright = colorTable[32767];
    gYellow = colorTable[18979];
    gDim = colorTable[10570];

    int fontSaved = text_curr();
    text_font(101);
    if (!ps2controls_open_window()) {
        text_font(fontSaved);
        return;
    }

    bool done = false;
    bool keep = true;
    while (!done) {
        sharedFpsLimiter.mark();
        int input = get_input();

        if (gCaptureRow >= 0) {
            if (!gCapturing) {
                // A key arrived through ps2controls_capture_key.
                if (gCaptured != 0 && gCaptured != SDL_SCANCODE_ESCAPE) {
                    gSettings.hotkey[gRows[gCaptureRow].index] = gCaptured;
                }
                gCaptureRow = -1;
                ps2controls_draw();
            } else if (input == EVENT_CANCEL || input == EVENT_DONE) {
                gCapturing = false;
                gCaptureRow = -1;
                ps2controls_draw();
            }
        } else if (input == KEY_ESCAPE || input == EVENT_CANCEL) {
            keep = false;
            done = true;
        } else if (input == EVENT_DONE || input == KEY_RETURN) {
            done = true;
        } else if (input == EVENT_DEFAULTS) {
            bool video[PS2_VIDEO_SETTING_COUNT];
            memcpy(video, gSettings.video, sizeof(video));
            int location = gSettings.saveLocation;
            ps2controls_defaults(&gSettings);
            if (gTab != TAB_VIDEO) {
                memcpy(gSettings.video, video, sizeof(video)); // only reset what's on screen
                gSettings.saveLocation = location;
            }
            ps2controls_apply_save_location(gSettings.saveLocation);
            ps2controls_draw();
        } else if (input == EVENT_PREFS && gFromMainMenu) {
            ps2controls_close_window();
            options_preferences();
            text_font(101);
            if (!ps2controls_open_window()) {
                break;
            }
        } else if (input >= EVENT_TAB && input < EVENT_TAB + TAB_COUNT) {
            int tab = input - EVENT_TAB;
            if (tab != gTab) {
                ps2controls_close_window();
                gTab = tab;
                if (!ps2controls_open_window()) {
                    break;
                }
            }
        } else if (input >= EVENT_ROW_NEXT && input < EVENT_ROW_NEXT + gRowCount) {
            gsound_play_sfx_file("ib1p1xx1");
            ps2controls_change(input - EVENT_ROW_NEXT, 1);
            ps2controls_draw();
        } else if (input >= EVENT_ROW_PREV && input < EVENT_ROW_PREV + gRowCount) {
            gsound_play_sfx_file("ib1p1xx1");
            ps2controls_change(input - EVENT_ROW_PREV, -1);
            ps2controls_draw();
        } else if (input >= EVENT_ROW_HOVER && input < EVENT_ROW_HOVER + gRowCount) {
            gHover = input - EVENT_ROW_HOVER;
            ps2controls_draw();
        } else if (input == KEY_ARROW_LEFT || input == KEY_ARROW_RIGHT) {
            // Keyboard/shoulder-free tab switching.
            int tab = (gTab + (input == KEY_ARROW_RIGHT ? 1 : TAB_COUNT - 1)) % TAB_COUNT;
            ps2controls_close_window();
            gTab = tab;
            if (!ps2controls_open_window()) {
                break;
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    ps2controls_close_window();
    text_font(fontSaved);

    if (keep) {
        ps2controls_rebuild_keymap();
        ps2controls_save();
    } else {
        gSettings = saved;
        ps2controls_apply_save_location(gSettings.saveLocation);
        ps2video_set_mode(gTvMode); // undo a TV mode kept on this visit
    }
}

} // namespace fallout

#endif /* __PS2__ */
