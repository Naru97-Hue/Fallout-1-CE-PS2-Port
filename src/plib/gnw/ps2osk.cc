// On-screen keyboard for the PS2 build.
//
// R3 opens/closes it. While open:
//   D-pad      move selection        Cross    press key
//   Circle     backspace             Triangle space
//   Square     Enter                 L1       Shift (next key uppercase)
//   Start      Esc                   R3       close
// The left stick and mouse emulation keep working, so you can still click.

#ifdef __PS2__

#include "plib/gnw/ps2osk.h"
#include "plib/gnw/ps2crash.h"

#include <stdio.h>
#include <string.h>

namespace fallout {

enum {
    PAD_START = 3,
    PAD_UP = 4,
    PAD_RIGHT = 5,
    PAD_DOWN = 6,
    PAD_LEFT = 7,
    PAD_L1 = 10,
    PAD_TRIANGLE = 12,
    PAD_CIRCLE = 13,
    PAD_CROSS = 14,
    PAD_SQUARE = 15,
};

enum {
    SPECIAL_NONE = 0,
    SPECIAL_SHIFT,
    SPECIAL_DONE,
};

typedef struct OskKey {
    const char* label;
    int scancode;
    int width; // in key units
    int special;
} OskKey;

#define K(ch, sc)                              \
    {                                          \
        ch, SDL_SCANCODE_##sc, 1, SPECIAL_NONE \
    }

static const OskKey kRow0[] = { K("1", 1), K("2", 2), K("3", 3), K("4", 4), K("5", 5), K("6", 6), K("7", 7), K("8", 8), K("9", 9), K("0", 0), K("-", MINUS) };
static const OskKey kRow1[] = { K("Q", Q), K("W", W), K("E", E), K("R", R), K("T", T), K("Y", Y), K("U", U), K("I", I), K("O", O), K("P", P), K("'", APOSTROPHE) };
static const OskKey kRow2[] = { K("A", A), K("S", S), K("D", D), K("F", F), K("G", G), K("H", H), K("J", J), K("K", K), K("L", L), K(",", COMMA), K(".", PERIOD) };
static const OskKey kRow3[] = { K("Z", Z), K("X", X), K("C", C), K("V", V), K("B", B), K("N", N), K("M", M), K("/", SLASH), { "DEL", SDL_SCANCODE_BACKSPACE, 3, SPECIAL_NONE } };
static const OskKey kRow4[] = {
    { "SHIFT", 0, 3, SPECIAL_SHIFT },
    { "SPACE", SDL_SCANCODE_SPACE, 3, SPECIAL_NONE },
    { "ENTER", SDL_SCANCODE_RETURN, 3, SPECIAL_NONE },
    { "DONE", 0, 2, SPECIAL_DONE },
};

#undef K

typedef struct OskRow {
    const OskKey* keys;
    int count;
} OskRow;

static const OskRow kRows[] = {
    { kRow0, (int)(sizeof(kRow0) / sizeof(kRow0[0])) },
    { kRow1, (int)(sizeof(kRow1) / sizeof(kRow1[0])) },
    { kRow2, (int)(sizeof(kRow2) / sizeof(kRow2[0])) },
    { kRow3, (int)(sizeof(kRow3) / sizeof(kRow3[0])) },
    { kRow4, (int)(sizeof(kRow4) / sizeof(kRow4[0])) },
};
static const int kRowCount = (int)(sizeof(kRows) / sizeof(kRows[0]));

// 5x7 bitmap font (bit 4 = leftmost column).
typedef struct Glyph {
    char ch;
    unsigned char rows[7];
} Glyph;

static const Glyph kFont[] = {
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '\'', { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 } },
    { ',', { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { '/', { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 } },
    { ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
};

static const int kKeyUnit = 28; // key width in pixels (1 unit)
static const int kKeyHeight = 24;
static const int kGap = 3;
static const int kColumns = 11;

static char gMessage[48] = "";
static Uint32 gMessageUntil = 0;

static bool gVisible = false;
static bool gDirty = false;
static bool gShift = false;
static int gRow = 1;
static int gCol = 0;

bool ps2osk_is_visible()
{
    return gVisible;
}

void ps2osk_toggle()
{
    gVisible = !gVisible;
    ps2crash_step(gVisible ? "keyboard opened" : "keyboard closed", 0);
    gShift = false;
    gDirty = true;
}

void ps2osk_show_message(const char* text, unsigned int ms)
{
    snprintf(gMessage, sizeof(gMessage), "%s", text);
    gMessageUntil = SDL_GetTicks() + ms;
    gDirty = true;
}

bool ps2osk_consume_dirty()
{
    if (gMessage[0] != '\0' && (Sint32)(SDL_GetTicks() - gMessageUntil) >= 0) {
        gMessage[0] = '\0';
        gDirty = true;
    }

    bool dirty = gDirty;
    gDirty = false;
    return dirty;
}

static void ps2osk_tap(int scancode, bool shifted, Ps2PadKeyCallback* emitKey)
{
    if (shifted) {
        emitKey(SDL_SCANCODE_LSHIFT, true);
    }
    emitKey(scancode, true);
    emitKey(scancode, false);
    if (shifted) {
        emitKey(SDL_SCANCODE_LSHIFT, false);
    }
}

static void ps2osk_clamp_col()
{
    if (gCol >= kRows[gRow].count) {
        gCol = kRows[gRow].count - 1;
    }
}

// Keeps roughly the same horizontal position when moving between rows with
// different key widths.
static int ps2osk_key_start_unit(int row, int col)
{
    int unit = 0;
    for (int i = 0; i < col; i++) {
        unit += kRows[row].keys[i].width;
    }
    return unit;
}

static int ps2osk_col_at_unit(int row, int unit)
{
    int start = 0;
    for (int i = 0; i < kRows[row].count; i++) {
        int width = kRows[row].keys[i].width;
        if (unit < start + width) {
            return i;
        }
        start += width;
    }
    return kRows[row].count - 1;
}

static void ps2osk_move_row(int delta)
{
    int unit = ps2osk_key_start_unit(gRow, gCol);
    gRow = (gRow + delta + kRowCount) % kRowCount;
    gCol = ps2osk_col_at_unit(gRow, unit);
}

bool ps2osk_handle_button(int button, bool down, Ps2PadKeyCallback* emitKey)
{
    if (!gVisible) {
        return false;
    }

    switch (button) {
    case PAD_UP:
    case PAD_DOWN:
    case PAD_LEFT:
    case PAD_RIGHT:
    case PAD_CIRCLE:
    case PAD_TRIANGLE:
    case PAD_SQUARE:
    case PAD_L1:
    case PAD_START:
    case PAD_CROSS:
        break;
    default:
        // Everything else (stick, shoulder hotkeys...) passes through.
        return false;
    }

    if (!down) {
        return true;
    }

    gDirty = true;

    switch (button) {
    case PAD_UP:
        ps2osk_move_row(-1);
        break;
    case PAD_DOWN:
        ps2osk_move_row(1);
        break;
    case PAD_LEFT:
        gCol = (gCol - 1 + kRows[gRow].count) % kRows[gRow].count;
        break;
    case PAD_RIGHT:
        gCol = (gCol + 1) % kRows[gRow].count;
        break;
    case PAD_CIRCLE:
        ps2osk_tap(SDL_SCANCODE_BACKSPACE, false, emitKey);
        break;
    case PAD_TRIANGLE:
        ps2osk_tap(SDL_SCANCODE_SPACE, false, emitKey);
        break;
    case PAD_SQUARE:
        ps2osk_tap(SDL_SCANCODE_RETURN, false, emitKey);
        break;
    case PAD_START:
        ps2osk_tap(SDL_SCANCODE_ESCAPE, false, emitKey);
        break;
    case PAD_L1:
        gShift = !gShift;
        break;
    case PAD_CROSS: {
        const OskKey* key = &(kRows[gRow].keys[gCol]);
        if (key->special == SPECIAL_SHIFT) {
            gShift = !gShift;
        } else if (key->special == SPECIAL_DONE) {
            ps2osk_toggle();
        } else {
            ps2osk_tap(key->scancode, gShift, emitKey);
            gShift = false;
        }
        break;
    }
    }

    ps2osk_clamp_col();
    return true;
}

static const Glyph* ps2osk_find_glyph(char ch)
{
    for (size_t i = 0; i < sizeof(kFont) / sizeof(kFont[0]); i++) {
        if (kFont[i].ch == ch) {
            return &(kFont[i]);
        }
    }
    return NULL;
}

static void ps2osk_draw_text(SDL_Renderer* renderer, const char* text, int centerX, int centerY, int scale)
{
    int length = (int)strlen(text);
    int textWidth = length * 6 * scale - scale;
    int x = centerX - textWidth / 2;
    int y = centerY - (7 * scale) / 2;

    for (int i = 0; i < length; i++) {
        char ch = text[i];
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
        }
        const Glyph* glyph = ps2osk_find_glyph(ch);
        if (glyph != NULL) {
            // One rectangle per horizontal run of lit pixels (far fewer
            // draw calls than one per pixel).
            for (int row = 0; row < 7; row++) {
                int col = 0;
                while (col < 5) {
                    if ((glyph->rows[row] & (0x10 >> col)) == 0) {
                        col++;
                        continue;
                    }
                    int start = col;
                    while (col < 5 && (glyph->rows[row] & (0x10 >> col)) != 0) {
                        col++;
                    }
                    SDL_Rect run = { x + start * scale, y + row * scale, (col - start) * scale, scale };
                    SDL_RenderFillRect(renderer, &run);
                }
            }
        }
        x += 6 * scale;
    }
}

static void ps2osk_draw_text(SDL_Renderer* renderer, const char* text, int centerX, int centerY, int scale);

static void ps2osk_render_message(SDL_Renderer* renderer, int screenWidth)
{
    if (gMessage[0] == '\0') {
        return;
    }

    int width = (int)strlen(gMessage) * 12 + 16;
    SDL_Rect box = { (screenWidth - width) / 2, 12, width, 26 };
    SDL_SetRenderDrawColor(renderer, 8, 24, 8, 255);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 60, 200, 60, 255);
    SDL_RenderDrawRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 120, 255, 120, 255);
    ps2osk_draw_text(renderer, gMessage, screenWidth / 2, 12 + 13, 2);
}

void ps2osk_render(SDL_Renderer* renderer, int screenWidth, int screenHeight)
{
    ps2osk_render_message(renderer, screenWidth);

    if (!gVisible) {
        return;
    }

    int panelWidth = kColumns * kKeyUnit + (kColumns + 1) * kGap;
    int panelHeight = kRowCount * kKeyHeight + (kRowCount + 1) * kGap;
    int panelX = (screenWidth - panelWidth) / 2;
    // Sit just above Fallout's 99px bottom interface bar when possible.
    int panelY = screenHeight - 99 - panelHeight - 4;
    if (panelY < 0) {
        panelY = screenHeight - panelHeight;
    }

    SDL_BlendMode oldMode;
    SDL_GetRenderDrawBlendMode(renderer, &oldMode);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Panel (dark green, Pip-Boy flavoured).
    SDL_Rect panel = { panelX, panelY, panelWidth, panelHeight };
    SDL_SetRenderDrawColor(renderer, 8, 24, 8, 220);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 60, 200, 60, 255);
    SDL_RenderDrawRect(renderer, &panel);

    for (int row = 0; row < kRowCount; row++) {
        int x = panelX + kGap;
        int y = panelY + kGap + row * (kKeyHeight + kGap);

        for (int col = 0; col < kRows[row].count; col++) {
            const OskKey* key = &(kRows[row].keys[col]);
            int width = key->width * kKeyUnit + (key->width - 1) * kGap;
            SDL_Rect rect = { x, y, width, kKeyHeight };

            bool selected = row == gRow && col == gCol;
            bool active = key->special == SPECIAL_SHIFT && gShift;

            if (selected) {
                SDL_SetRenderDrawColor(renderer, 60, 200, 60, 255);
            } else if (active) {
                SDL_SetRenderDrawColor(renderer, 30, 110, 30, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 20, 50, 20, 255);
            }
            SDL_RenderFillRect(renderer, &rect);

            SDL_SetRenderDrawColor(renderer, 40, 140, 40, 255);
            SDL_RenderDrawRect(renderer, &rect);

            if (selected) {
                SDL_SetRenderDrawColor(renderer, 8, 24, 8, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 120, 255, 120, 255);
            }

            int scale = key->width == 1 ? 2 : 1;
            ps2osk_draw_text(renderer, key->label, x + width / 2, y + kKeyHeight / 2, scale);

            x += width + kGap;
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, oldMode);
}

} // namespace fallout

#endif /* __PS2__ */
