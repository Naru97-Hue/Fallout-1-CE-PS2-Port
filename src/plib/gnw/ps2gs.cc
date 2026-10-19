// Direct GS presentation (GS renderer, stages A and D).
//
// Stage A: the 640x480 8-bit screen is a GS_PSM_T8 texture and the palette a
// 32-bit CLUT, so the GS does the colour lookup and scaling.
//
// Stage D (UI and effects):
//  * The screen texture stays resident in gsKit's texture manager; only the
//    rectangles that changed are uploaded (it used to re-send all 300 KB
//    whenever anything changed).
//  * The mouse cursor is a GS sprite drawn on top: moving it no longer makes
//    the window system redraw and re-composite what's under it, and the
//    screen texture doesn't change at all.
//  * Palette fades to/from black are a GS brightness pass instead of a new
//    palette every step (which also rebuilt every map palette).
//
// We draw into the GS state SDL's renderer owns (captured in ps2video.cc),
// after flushing SDL's queued clear and before SDL draws its overlays. SDL's
// clear can leave a bogus scissor behind, so we always reset it, and the
// frame is sampled with REGION_CLAMP so scaling never reads past 640x480.

#ifdef __PS2__

#include "plib/gnw/ps2gs.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <dmaKit.h>
#include <gsKit.h>
#include <kernel.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <gsInline.h>
#pragma GCC diagnostic pop

#include "game/map.h"
#include "plib/color/color.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/ps2controls.h"
#include "plib/gnw/ps2map.h"
#include "plib/gnw/ps2zoom.h"

namespace fallout {

extern GSGLOBAL* gPs2GsGlobal;
extern int gPs2GsGeneration;

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define FRAME_BYTES (FRAME_WIDTH * 512) // T8 pages are 128x64: round rows up
#define CLUT_BYTES 1024
#define CURSOR_SIZE 128
#define CURSOR_BYTES (CURSOR_SIZE * CURSOR_SIZE)

#define MAX_DIRTY 16
// Disjoint rectangles, each widened to 16-pixel columns.
#define STAGING_BYTES (192 * 1024) // more than this in a frame: one full upload

static GSTEXTURE gFrame;
static GSTEXTURE gCursor;
static unsigned int* gClut = NULL; // 256 entries, CSM1 order
static unsigned char* gStaging = NULL; // dirty-rectangle uploads, one frame
static unsigned char* gCursorStaging = NULL;
static unsigned char gCursorLast[CURSOR_BYTES];
static int gCursorW = 0;
static int gCursorH = 0;
static bool gCursorValid = false;

static unsigned int* gCursorClut = NULL; // copy of gClut for the cursor
static unsigned char* gPixels = NULL; // EE copy of the screen texture
static bool gLoggedVram = false;

static GSGLOBAL* gResGs = NULL;
static int gResGeneration = -1;

static SDL_Rect gDirty[MAX_DIRTY];
static int gDirtyCount = 0;
static bool gPaletteDirty = true;
static bool gPaletteFullChange = true; // false: only cycling entries changed
static bool gEverything = true; // re-send the whole screen

static int gFadeLevel = 0x80; // 0x80 = no fade

// Stage D switches (create the file next to FALLOUT.ELF to turn it off).
static bool gConfigChecked = false;
static bool gGsFades = true; // GSFADE.OFF
static bool gGsCursor = true; // GSCURSOR.OFF

// Diagnostics.
static int gStatClutUploads = 0;
static int gStatCursorDraws = 0;
static int gStatFadeDraws = 0;

static bool ps2gs_file_exists(const char* name)
{
    FILE* stream = fopen(name, "rb");
    if (stream == NULL) {
        return false;
    }
    fclose(stream);
    return true;
}

static void ps2gs_check_config()
{
    if (gConfigChecked) {
        return;
    }
    gConfigChecked = true;
    gGsFades = ps2controls_video_setting(PS2_VIDEO_SETTING_GS_FADES);
    gGsCursor = ps2controls_video_setting(PS2_VIDEO_SETTING_GS_CURSOR);
    if (ps2gs_file_exists("GSFADE.OFF")) {
        gGsFades = false;
        debug_printf("ps2gs: GS fades disabled by GSFADE.OFF\n");
    }
    if (ps2gs_file_exists("GSCURSOR.OFF")) {
        gGsCursor = false;
        debug_printf("ps2gs: GS cursor disabled by GSCURSOR.OFF\n");
    }
}

// Simple performance counters, logged every 5 seconds.
static Uint32 gPerfStart = 0;
static Uint32 gPerfPresentMs = 0;
static int gPerfFrames = 0;
static unsigned int gPerfBytes = 0;

// --- Dirty rectangles ------------------------------------------------------------

static bool ps2gs_rects_touch(const SDL_Rect* a, const SDL_Rect* b)
{
    return a->x <= b->x + b->w && b->x <= a->x + a->w && a->y <= b->y + b->h && b->y <= a->y + a->h;
}

static void ps2gs_union(SDL_Rect* a, const SDL_Rect* b)
{
    int x1 = SDL_min(a->x, b->x);
    int y1 = SDL_min(a->y, b->y);
    int x2 = SDL_max(a->x + a->w, b->x + b->w);
    int y2 = SDL_max(a->y + a->h, b->y + b->h);
    a->x = x1;
    a->y = y1;
    a->w = x2 - x1;
    a->h = y2 - y1;
}

void ps2gs_mark_dirty(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    SDL_Rect r = { x, y, width, height };

    // Merge with any rectangle it touches (repeat: merging can chain).
    bool merged = true;
    while (merged) {
        merged = false;
        for (int i = 0; i < gDirtyCount; i++) {
            if (ps2gs_rects_touch(&(gDirty[i]), &r)) {
                ps2gs_union(&r, &(gDirty[i]));
                gDirty[i] = gDirty[--gDirtyCount];
                merged = true;
                break;
            }
        }
    }

    if (gDirtyCount == MAX_DIRTY) {
        // Too many: fold everything into one.
        for (int i = 0; i < gDirtyCount; i++) {
            ps2gs_union(&r, &(gDirty[i]));
        }
        gDirtyCount = 0;
    }
    gDirty[gDirtyCount++] = r;
}

void ps2gs_mark_palette()
{
    gPaletteDirty = true;
    gPaletteFullChange = true;
}

void ps2gs_mark_palette_range(int start, int count)
{
    gPaletteDirty = true;
    if (start < 229 || start + count > 256) {
        gPaletteFullChange = true;
    }
}

void ps2gs_set_fade(int level)
{
    if (level < 0) {
        level = 0;
    }
    if (level > 0x80) {
        level = 0x80;
    }
    gFadeLevel = level;
}

bool ps2gs_fades_supported()
{
    ps2gs_check_config();
    return gGsFades && gPs2GsGlobal != NULL && gClut != NULL;
}

bool ps2gs_cursor_enabled()
{
    ps2gs_check_config();
    return gGsCursor && ps2map_enabled();
}

// --- GS memory ---------------------------------------------------------------

static bool ps2gs_reserve(GSGLOBAL* gs)
{
    if (gResGs == gs && gResGeneration == gPs2GsGeneration) {
        return true;
    }

    if (gClut == NULL) {
        gClut = (unsigned int*)memalign(128, CLUT_BYTES);
        gCursorClut = (unsigned int*)memalign(128, CLUT_BYTES);
        gPixels = (unsigned char*)memalign(128, FRAME_BYTES);
        gStaging = (unsigned char*)memalign(128, STAGING_BYTES);
        gCursorStaging = (unsigned char*)memalign(128, CURSOR_BYTES);
        if (gClut == NULL || gCursorClut == NULL || gPixels == NULL || gStaging == NULL || gCursorStaging == NULL) {
            return false;
        }
        memset(gClut, 0, CLUT_BYTES);
        memset(gCursorClut, 0, CLUT_BYTES);
        memset(gPixels, 0, FRAME_BYTES);
        memset(gCursorStaging, 0, CURSOR_BYTES);
    }

    gResGs = gs;
    gResGeneration = gPs2GsGeneration;

    // Both textures live in gsKit's texture manager (the path proven since
    // stage A): it places them, uploads them whole when they aren't resident,
    // and we patch only the changed rectangles while they are.
    memset(&gFrame, 0, sizeof(gFrame));
    gFrame.Width = FRAME_WIDTH;
    gFrame.Height = FRAME_HEIGHT;
    gFrame.PSM = GS_PSM_T8;
    gFrame.Mem = (u32*)gPixels;
    gFrame.Clut = (u32*)gClut;
    gFrame.ClutPSM = GS_PSM_CT32;
    gFrame.ClutStorageMode = GS_CLUT_STORAGE_CSM1;
    gFrame.Filter = GS_FILTER_NEAREST;
    gsKit_setup_tbw(&gFrame);

    memset(&gCursor, 0, sizeof(gCursor));
    gCursor.Width = CURSOR_SIZE;
    gCursor.Height = CURSOR_SIZE;
    gCursor.PSM = GS_PSM_T8;
    gCursor.Mem = (u32*)gCursorStaging;
    gCursor.Clut = (u32*)gCursorClut;
    gCursor.ClutPSM = GS_PSM_CT32;
    gCursor.ClutStorageMode = GS_CLUT_STORAGE_CSM1;
    gCursor.Filter = GS_FILTER_NEAREST;
    gsKit_setup_tbw(&gCursor);

    gEverything = true;
    gPaletteDirty = true;
    gCursorValid = false;
    gLoggedVram = false;
    debug_printf("ps2gs: direct GS present (640x480 T8 + CLUT via texture manager, dirty-rectangle uploads, GS cursor %s)\n",
        ps2gs_cursor_enabled() ? "on" : "off");
    return true;
}

// Uploads an 8-bit image to (dx, dy) of the buffer at tbp. mem must stay
// untouched until the frame's render queue has run.
static void ps2gs_upload_t8(GSGLOBAL* gs, void* mem, int width, int height, u32 tbp, int tbw, int dx, int dy)
{
    int qwc = (width * height + 15) / 16;
    SyncDCache(mem, (unsigned char*)mem + qwc * 16);

    u64* p = (u64*)gsKit_heap_alloc(gs, 4, 64, GIF_AD);
    *p++ = GIF_TAG_AD(4);
    *p++ = GIF_AD;
    *p++ = GS_SETREG_BITBLTBUF(0, 0, 0, tbp / 256, tbw, GS_PSM_T8);
    *p++ = GS_BITBLTBUF;
    *p++ = GS_SETREG_TRXPOS(0, 0, dx, dy, 0);
    *p++ = GS_TRXPOS;
    *p++ = GS_SETREG_TRXREG(width, height);
    *p++ = GS_TRXREG;
    *p++ = GS_SETREG_TRXDIR(0);
    *p++ = GS_TRXDIR;

    p = (u64*)gsKit_heap_alloc_dma(gs, 3, 3 * 16);
    *p++ = DMA_TAG(1, 0, DMA_CNT, 0, 0, 0);
    *p++ = 0;
    *p++ = GIF_TAG(qwc, 0, 0, 0, GSKIT_GIF_FLG_IMAGE, 0);
    *p++ = 0;
    *p++ = DMA_TAG(qwc, 0, DMA_REF, 0, (u32)mem, 0);
    *p++ = 0;

    p = (u64*)gsKit_heap_alloc(gs, 1, 16, GIF_AD);
    *p++ = GIF_TAG_AD(1);
    *p++ = GIF_AD;
    *p++ = 0;
    *p++ = GS_TEXFLUSH;
}

void ps2gs_upload(void* mem, int width, int height, int psm, unsigned int tbp, int tbw, int dx, int dy)
{
    GSGLOBAL* gs = gPs2GsGlobal;
    if (gs == NULL) {
        return;
    }
    if (psm == GS_PSM_T8) {
        ps2gs_upload_t8(gs, mem, width, height, tbp, tbw, dx, dy);
        return;
    }

    int qwc = (width * height * 4 + 15) / 16;
    SyncDCache(mem, (unsigned char*)mem + qwc * 16);

    u64* p = (u64*)gsKit_heap_alloc(gs, 4, 64, GIF_AD);
    *p++ = GIF_TAG_AD(4);
    *p++ = GIF_AD;
    *p++ = GS_SETREG_BITBLTBUF(0, 0, 0, tbp / 256, tbw, psm);
    *p++ = GS_BITBLTBUF;
    *p++ = GS_SETREG_TRXPOS(0, 0, dx, dy, 0);
    *p++ = GS_TRXPOS;
    *p++ = GS_SETREG_TRXREG(width, height);
    *p++ = GS_TRXREG;
    *p++ = GS_SETREG_TRXDIR(0);
    *p++ = GS_TRXDIR;

    // Image data in chunks the GIF tag can describe.
    unsigned char* src = (unsigned char*)mem;
    while (qwc > 0) {
        int chunk = qwc > GS_GIF_BLOCKSIZE ? GS_GIF_BLOCKSIZE : qwc;
        p = (u64*)gsKit_heap_alloc_dma(gs, 3, 3 * 16);
        *p++ = DMA_TAG(1, 0, DMA_CNT, 0, 0, 0);
        *p++ = 0;
        *p++ = GIF_TAG(chunk, 0, 0, 0, GSKIT_GIF_FLG_IMAGE, 0);
        *p++ = 0;
        *p++ = DMA_TAG(chunk, 0, DMA_REF, 0, (u32)src, 0);
        *p++ = 0;
        src += chunk * 16;
        qwc -= chunk;
    }

    p = (u64*)gsKit_heap_alloc(gs, 1, 16, GIF_AD);
    *p++ = GIF_TAG_AD(1);
    *p++ = GIF_AD;
    *p++ = 0;
    *p++ = GS_TEXFLUSH;
}

// Copies the changed parts of the 8-bit screen into the EE copy of the
// texture. Returns false when nothing changed.
static bool ps2gs_copy_dirty(SDL_Surface* screen8)
{
    if (gEverything) {
        gDirtyCount = 0;
        SDL_Rect all = { 0, 0, FRAME_WIDTH, FRAME_HEIGHT };
        gDirty[gDirtyCount++] = all;
    }

    const unsigned char* pixels = (const unsigned char*)screen8->pixels;
    for (int i = 0; i < gDirtyCount; i++) {
        SDL_Rect* r = &(gDirty[i]);
        int x0 = SDL_max(r->x, 0) & ~15;
        int y0 = SDL_max(r->y, 0);
        int x1 = (SDL_min(r->x + r->w, FRAME_WIDTH) + 15) & ~15;
        int y1 = SDL_min(r->y + r->h, FRAME_HEIGHT);
        if (x1 > FRAME_WIDTH) {
            x1 = FRAME_WIDTH;
        }
        if (x1 <= x0 || y1 <= y0) {
            r->w = 0;
            r->h = 0;
            continue;
        }
        r->x = x0;
        r->y = y0;
        r->w = x1 - x0;
        r->h = y1 - y0;
        for (int y = y0; y < y1; y++) {
            memcpy(gPixels + y * FRAME_WIDTH + x0, pixels + y * screen8->pitch + x0, r->w);
        }
    }
    return gDirtyCount != 0;
}

// Sends the changed rectangles to the resident screen texture.
static void ps2gs_upload_dirty(GSGLOBAL* gs)
{
    unsigned char* staging = gStaging;
    unsigned int used = 0;

    for (int i = 0; i < gDirtyCount; i++) {
        SDL_Rect r = gDirty[i];
        if (r.w <= 0 || r.h <= 0) {
            continue;
        }

        unsigned int bytes = ((unsigned int)(r.w * r.h) + 127) & ~127u;
        if (used + bytes > STAGING_BYTES) {
            // Too much for one frame: re-send the whole texture instead.
            gsKit_TexManager_invalidate(gs, &gFrame);
            gsKit_TexManager_bind(gs, &gFrame);
            gPerfBytes += FRAME_WIDTH * FRAME_HEIGHT;
            break;
        }
        unsigned char* dest = staging + used;
        for (int y = 0; y < r.h; y++) {
            memcpy(dest + y * r.w, gPixels + (r.y + y) * FRAME_WIDTH + r.x, r.w);
        }
        ps2gs_upload_t8(gs, dest, r.w, r.h, gFrame.Vram, gFrame.TBW, r.x, r.y);
        used += bytes;
        gPerfBytes += (unsigned int)(r.w * r.h);
    }
}

// CSM1 stores CLUT entries with indices 8-15 and 16-23 of every 32 swapped.
static inline int ps2gs_csm1_index(int i)
{
    return (i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
}

static void ps2gs_update_clut(GSGLOBAL* gs, SDL_Surface* screen8)
{
    SDL_Palette* palette = screen8->format->palette;
    if (palette == NULL) {
        return;
    }
    int count = palette->ncolors < 256 ? palette->ncolors : 256;
    for (int i = 0; i < 256; i++) {
        unsigned int color = 0x80000000; // opaque (GS alpha 0x80 = 1.0)
        if (i == 0 && ps2map_enabled()) {
            // Index 0 is a hole: black on the PC (palette entry 0 is forced
            // to black), and where the GS map shows through.
            gClut[0] = 0;
            continue;
        }
        if (i < count) {
            SDL_Color c = palette->colors[i];
            color |= (unsigned int)c.r | ((unsigned int)c.g << 8) | ((unsigned int)c.b << 16);
        }
        gClut[ps2gs_csm1_index(i)] = color;
    }
    memcpy(gCursorClut, gClut, CLUT_BYTES);
    // Re-sent by the texture manager at the next bind.
    gFrame.VramClut = 0;
    gCursor.VramClut = 0;
    gStatClutUploads++;
}

static void ps2gs_draw(GSGLOBAL* gs, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh)
{
    // The texture is 1024x512 in GS terms: with the default REPEAT mode,
    // bilinear samples at the top/left edges wrap to rows/columns beyond the
    // 640x480 picture (stale memory) and flicker. Clamp to the picture.
    u8 wms = gs->Clamp->WMS;
    u8 wmt = gs->Clamp->WMT;
    int minu = gs->Clamp->MINU;
    int maxu = gs->Clamp->MAXU;
    int minv = gs->Clamp->MINV;
    int maxv = gs->Clamp->MAXV;
    gs->Clamp->MINU = 0;
    gs->Clamp->MAXU = FRAME_WIDTH - 1;
    gs->Clamp->MINV = 0;
    gs->Clamp->MAXV = FRAME_HEIGHT - 1;
    gsKit_set_clamp(gs, GS_CMODE_REGION_CLAMP);

    gsKit_prim_sprite_texture(gs, &gFrame,
        dx, dy, sx, sy,
        dx + dw, dy + dh, sx + sw, sy + sh,
        0, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));

    gs->Clamp->MINU = minu;
    gs->Clamp->MAXU = maxu;
    gs->Clamp->MINV = minv;
    gs->Clamp->MAXV = maxv;
    gs->Clamp->WMT = wmt;
    gsKit_set_clamp(gs, wms);
}

// --- Cursor ------------------------------------------------------------------

static void ps2gs_draw_cursor(GSGLOBAL* gs, float scaleX, float scaleY, bool zoomed, const SDL_Rect* zoomSrc, int mapHeight, int screenWidth)
{
    unsigned char* shape;
    int width;
    int height;
    int pitch;
    int x;
    int y;
    int trans;
    if (!mouse_get_gs_cursor(&shape, &width, &height, &pitch, &x, &y, &trans)) {
        return;
    }
    if (width > CURSOR_SIZE) {
        width = CURSOR_SIZE;
    }
    if (height > CURSOR_SIZE) {
        height = CURSOR_SIZE;
    }

    // Build the image (transparent colour -> index 0) and upload it if it
    // changed (animated cursors, new shapes).
    int paddedWidth = (width + 15) & ~15;
    unsigned char image[CURSOR_BYTES];
    int black = colorTable[0];
    for (int row = 0; row < height; row++) {
        const unsigned char* src = shape + row * pitch;
        unsigned char* dest = image + row * paddedWidth;
        for (int col = 0; col < width; col++) {
            unsigned char c = src[col];
            if (c == (unsigned char)trans) {
                c = 0;
            } else if (c == 0) {
                c = (unsigned char)black;
            }
            dest[col] = c;
        }
        for (int col = width; col < paddedWidth; col++) {
            dest[col] = 0;
        }
    }

    int bytes = paddedWidth * height;
    if (!gCursorValid || width != gCursorW || height != gCursorH || memcmp(image, gCursorLast, bytes) != 0) {
        memcpy(gCursorLast, image, bytes);
        memset(gCursorStaging, 0, CURSOR_BYTES);
        for (int row = 0; row < height; row++) {
            memcpy(gCursorStaging + row * CURSOR_SIZE, image + row * paddedWidth, paddedWidth);
        }
        // Texture only: the CLUT copy stays valid.
        gCursor.Vram = 0;
        gCursorW = width;
        gCursorH = height;
        gCursorValid = true;
    }

    // Where the hotspot lands on the TV picture (follows the zoomed map).
    int hotX;
    int hotY;
    mouse_get_hotspot(&hotX, &hotY);
    float px = (float)(x + hotX);
    float py = (float)(y + hotY);
    float gx;
    float gy;
    if (zoomed && py < mapHeight) {
        float destW = screenWidth * scaleX;
        float destH = mapHeight * scaleY;
        gx = (px - zoomSrc->x) * destW / zoomSrc->w;
        gy = (py - zoomSrc->y) * destH / zoomSrc->h;
    } else {
        gx = px * scaleX;
        gy = py * scaleY;
    }
    float x1 = gx - hotX * scaleX;
    float y1 = gy - hotY * scaleY;

    gStatCursorDraws++;
    gsKit_TexManager_bind(gs, &gCursor);
    gsKit_prim_sprite_texture(gs, &gCursor,
        x1, y1, 0.0f, 0.0f,
        x1 + width * scaleX, y1 + height * scaleY, (float)width, (float)height,
        0, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
}

// --- Diagnostics ---------------------------------------------------------------

// Logs what's on screen whenever the topmost window changes (diagnosing
// screens that come out black).
static void ps2gs_log_screen_change(SDL_Surface* screen8, int screenWidth, int screenHeight, bool zoomed)
{
    static int lastTop = -2;
    int top = win_top_visible(screenWidth / 2, screenHeight / 2);
    if (top == lastTop) {
        return;
    }
    lastTop = top;

    int zero = 0;
    int samples = 0;
    int histogram[256];
    memset(histogram, 0, sizeof(histogram));
    const unsigned char* pixels = (const unsigned char*)screen8->pixels;
    for (int y = 0; y < screenHeight; y += 4) {
        for (int x = 0; x < screenWidth; x += 4) {
            unsigned char c = pixels[y * screen8->pitch + x];
            histogram[c]++;
            if (c == 0) {
                zero++;
            }
            samples++;
        }
    }
    int distinct = 0;
    int common = 0;
    for (int i = 0; i < 256; i++) {
        if (histogram[i] != 0) {
            distinct++;
        }
        if (histogram[i] > histogram[common]) {
            common = i;
        }
    }

    int lit = 0;
    SDL_Palette* palette = screen8->format->palette;
    if (palette != NULL) {
        for (int i = 0; i < palette->ncolors; i++) {
            if (palette->colors[i].r > 8 || palette->colors[i].g > 8 || palette->colors[i].b > 8) {
                lit++;
            }
        }
    }

    Rect rects[16];
    int mapRects = display_win > 0 ? win_visible_rects(display_win, rects, 16) : 0;

    Window* w = GNW_find(top);
    if (w != NULL) {
        debug_printf("ps2gs: top window %d (%d,%d)-(%d,%d) flags 0x%x; screen %d%% index 0, %d distinct colours (most common %d, %d%%); %d palette colours lit; map visible in %d rects; zoom %s; holes %s; fade %d\n",
            top, w->rect.ulx, w->rect.uly, w->rect.lrx, w->rect.lry, w->flags,
            samples > 0 ? zero * 100 / samples : 0, distinct, common, samples > 0 ? histogram[common] * 100 / samples : 0,
            lit, mapRects, zoomed ? "on" : "off", ps2map_enabled() ? "on" : "off", gFadeLevel);
    }
}

// --- Present -----------------------------------------------------------------

bool ps2gs_present(SDL_Renderer* renderer, SDL_Surface* screen8, int screenWidth, int screenHeight)
{
    GSGLOBAL* gs = gPs2GsGlobal;
    if (gs == NULL || screen8 == NULL || screen8->format->BytesPerPixel != 1
        || screenWidth != FRAME_WIDTH || screenHeight != FRAME_HEIGHT) {
        return false;
    }

    Uint32 start = SDL_GetTicks();

    if (!ps2gs_reserve(gs)) {
        return false;
    }
    // The map takes its fixed GS memory first; the texture manager (screen,
    // cursor) gets what follows.
    ps2map_prepare(gs);

    // Let SDL emit its queued clear first so it doesn't wipe our drawing.
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderFlush(renderer);
    // SDL's clear restores a scissor from a viewport pointer that can be
    // stale (GS dump: Y 2047..2047, clipping everything): set our own.
    gsKit_set_scissor(gs, GS_SCISSOR_RESET);

    if (gPaletteDirty) {
        ps2gs_update_clut(gs, screen8);
        ps2map_palette_changed(!gPaletteFullChange);
        gPaletteDirty = false;
        gPaletteFullChange = false;
    }

    // Screen texture: a full upload when it isn't resident (first frame,
    // evicted, everything changed), otherwise only the changed rectangles.
    bool changed = ps2gs_copy_dirty(screen8);
    bool clutStale = gFrame.VramClut == 0;
    if (gEverything) {
        gsKit_TexManager_invalidate(gs, &gFrame);
        gEverything = false;
    }
    bool wasResident = gFrame.Vram != 0;
    unsigned int transferred = gsKit_TexManager_bind(gs, &gFrame);
    bool fullUpload = !wasResident || (transferred != 0 && !clutStale);
    if (fullUpload) {
        gPerfBytes += FRAME_WIDTH * FRAME_HEIGHT;
    } else if (changed) {
        ps2gs_upload_dirty(gs);
    }
    gDirtyCount = 0;

    if (!gLoggedVram) {
        gLoggedVram = true;
        debug_printf("ps2gs: screen texture at 0x%06X (TBW %d), CLUT at 0x%06X\n",
            (unsigned)gFrame.Vram, (int)gFrame.TBW, (unsigned)gFrame.VramClut);
    }

    // Draw in raw framebuffer coordinates (SDL may have moved the offsets
    // for its own viewport).
    int savedOffsetX = gs->OffsetX;
    int savedOffsetY = gs->OffsetY;
    int savedAlpha = gs->PrimAlphaEnable;
    gs->OffsetX = (int)(2048.0f * 16.0f);
    gs->OffsetY = (int)(2048.0f * 16.0f);

    bool holes = ps2map_enabled();

    // Texture alpha on (index 0 = transparent), normal blending, and reject
    // fully transparent pixels.
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gs->PrimAlphaEnable = holes ? GS_SETTING_ON : GS_SETTING_OFF;
    u8 savedAtst = gs->Test->ATST;
    u8 savedAref = gs->Test->AREF;
    u8 savedAfail = gs->Test->AFAIL;
    // Only fully opaque samples: when the frame is scaled (bilinear), pixels
    // that blend with a hole would otherwise leave a dark fringe.
    gs->Test->ATST = 6; // GREATER
    gs->Test->AREF = 0x7F;
    gs->Test->AFAIL = 0; // KEEP
    if (holes) {
        gsKit_set_test(gs, GS_ATEST_ON);
    }

    float scaleX = (float)gs->Width / (float)screenWidth;
    float scaleY = (float)gs->Height / (float)screenHeight;

    SDL_Rect mapSrc;
    int mapHeight;
    bool zoomed = ps2zoom_map_source(screenWidth, screenHeight, &mapSrc, &mapHeight);
    ps2gs_log_screen_change(screen8, screenWidth, screenHeight, zoomed);

    // Stage C: the map (floor, objects, roofs) first, the frame on top.
    Ps2MapView view;
    view.screenWidth = screenWidth;
    view.screenHeight = screenHeight;
    view.scaleX = scaleX;
    view.scaleY = scaleY;
    view.zoomed = zoomed;
    view.zoomSrc = mapSrc;
    view.mapHeight = zoomed ? mapHeight : screenHeight;
    if (holes) {
        ps2map_draw(gs, &view, screen8->format->palette);
    }

    // Picture filter (Settings > SYSTEM): Sharp = nearest everywhere;
    // Smooth = bilinear only where the picture is scaled; Soft = bilinear
    // sampled half a pixel off, a light 2x2 blend even at 1:1 (CRT-like).
    int filter = ps2controls_picture_filter();
    bool scaled = !(scaleX == 1.0f && scaleY == 1.0f);
    float soft = filter == PS2_FILTER_SOFT ? 0.5f : 0.0f;
    if (zoomed) {
        gFrame.Filter = filter == PS2_FILTER_SHARP ? GS_FILTER_NEAREST : GS_FILTER_LINEAR;
        ps2gs_draw(gs, (float)mapSrc.x + soft, (float)mapSrc.y + soft, (float)mapSrc.w, (float)mapSrc.h,
            0.0f, 0.0f, screenWidth * scaleX, mapHeight * scaleY);
        gFrame.Filter = filter == PS2_FILTER_SOFT ? GS_FILTER_LINEAR : GS_FILTER_NEAREST;
        ps2gs_draw(gs, soft, (float)mapHeight + soft, (float)screenWidth, (float)(screenHeight - mapHeight),
            0.0f, mapHeight * scaleY, screenWidth * scaleX, (screenHeight - mapHeight) * scaleY);
    } else {
        gFrame.Filter = filter == PS2_FILTER_SOFT || (filter == PS2_FILTER_SMOOTH && scaled) ? GS_FILTER_LINEAR : GS_FILTER_NEAREST;
        ps2gs_draw(gs, soft, soft, (float)screenWidth, (float)screenHeight,
            0.0f, 0.0f, screenWidth * scaleX, screenHeight * scaleY);
    }

    // Stage D: the cursor on top, then the fade over everything.
    if (ps2gs_cursor_enabled()) {
        gCursor.Filter = gFrame.Filter;
        ps2gs_draw_cursor(gs, scaleX, scaleY, zoomed, &mapSrc, mapHeight, screenWidth);
    }

    // Scanlines: darken every other line of the 640x480 picture (in TV
    // pixels: a band one picture-line tall every two lines), by 25/50/75%.
    int scanlines = ps2controls_scanlines();
    if (scanlines > 0) {
        static const int kAlpha[4] = { 0, 0x20, 0x40, 0x60 };
        gsKit_set_test(gs, GS_ATEST_OFF);
        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
        gs->PrimAlphaEnable = GS_SETTING_ON;
        u64 color = GS_SETREG_RGBAQ(0, 0, 0, kAlpha[scanlines], 0x00);
        float band = scaleY < 1.0f ? 1.0f : scaleY;
        for (int row = 1; row < screenHeight; row += 2) {
            float y = row * scaleY;
            gsKit_prim_sprite(gs, 0.0f, y, (float)gs->Width, y + band, 0, color);
        }
        if (holes) {
            gsKit_set_test(gs, GS_ATEST_ON);
        }
    }

    if (gFadeLevel < 0x80) {
        // Multiply the whole picture: Cd * FIX.
        gStatFadeDraws++;
        gsKit_set_test(gs, GS_ATEST_OFF);
        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(1, 2, 2, 2, gFadeLevel), 0);
        gs->PrimAlphaEnable = GS_SETTING_ON;
        gsKit_prim_sprite(gs, 0.0f, 0.0f, (float)gs->Width, (float)gs->Height, 0, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0x00));
        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    }

    gs->Test->ATST = savedAtst;
    gs->Test->AREF = savedAref;
    gs->Test->AFAIL = savedAfail;
    gsKit_set_test(gs, GS_ATEST_OFF);

    gs->OffsetX = savedOffsetX;
    gs->OffsetY = savedOffsetY;
    gs->PrimAlphaEnable = savedAlpha;

    // Performance log.
    Uint32 now = SDL_GetTicks();
    gPerfPresentMs += now - start;
    gPerfFrames++;
    if (gPerfStart == 0) {
        gPerfStart = now;
    } else if (now - gPerfStart >= 5000) {
        float seconds = (now - gPerfStart) / 1000.0f;
        int mapRecords;
        int mapUploads;
        int mapUploadKB;
        int mapRebuilds;
        ps2map_take_stats(&mapRecords, &mapUploads, &mapUploadKB, &mapRebuilds);
        // Screen content now (is the game drawing anything?).
        int distinct = 0;
        {
            unsigned char seen[256];
            memset(seen, 0, sizeof(seen));
            const unsigned char* pixels = (const unsigned char*)screen8->pixels;
            for (int y = 0; y < screenHeight; y += 8) {
                for (int x = 0; x < screenWidth; x += 8) {
                    seen[pixels[y * screen8->pitch + x]] = 1;
                }
            }
            for (int i = 0; i < 256; i++) {
                distinct += seen[i];
            }
        }
        debug_printf("perf: %.1f presents/s, present EE time %.2f ms avg, %u KB screen uploaded/s; GS map %d draws/frame, %d rebuilds, %d art uploads (%d KB); screen %d colours, fade %d, %d palette uploads, %d cursor draws, %d fade draws\n",
            gPerfFrames / seconds, (float)gPerfPresentMs / gPerfFrames, (unsigned)(gPerfBytes / 1024 / seconds),
            mapRecords / gPerfFrames, mapRebuilds, mapUploads, mapUploadKB,
            distinct, gFadeLevel, gStatClutUploads, gStatCursorDraws, gStatFadeDraws);
        gStatClutUploads = 0;
        gStatCursorDraws = 0;
        gStatFadeDraws = 0;
        gPerfStart = now;
        gPerfPresentMs = 0;
        gPerfFrames = 0;
        gPerfBytes = 0;
    }

    return true;
}

} // namespace fallout

#endif /* __PS2__ */
