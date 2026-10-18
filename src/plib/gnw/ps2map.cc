// GS renderer, stage C: the Graphics Synthesizer draws the map.
//
// Layers drawn here, in Fallout's own order (the draw list comes from the
// game's render passes run in PS2MAP_MODE_EMIT):
//
//   floor tiles -> objects (flat ones first, then by tile) -> roofs ->
//   16-pixel darkening along the map bounds
//
// How each EE effect maps onto the GS:
//
//  * Evenly lit tiles/objects/roofs (dark_trans_buf_to_buf): one sprite
//    through a palette pre-darkened with the game's intensity table for that
//    light level -> identical colours.
//  * Floor tiles with a light gradient: lit once by the EE (exact), cached
//    in the atlas, drawn with the base palette.
//  * Translucent objects (dark_translucent_trans_buf_to_buf with the
//    red/wall/glass/steam/energy blend tables): pass 1 scales what's behind by
//    the light level, pass 2 blends the tint in with a per-colour alpha
//    palette that encodes the blend table rows.
//  * See-through cutout around the player (intensity_mask_buf_to_buf): a
//    small 32-bit texture with the mask as per-pixel alpha.
//  * Map-edge shadow (bounds_render): Gouraud alpha quads that multiply.
//
// GS memory: a 1024-wide 8-bit atlas (art frames, shelf allocator with LRU
// eviction), a 256x256 32-bit area for cutout pieces, and 64 palettes.

#ifdef __PS2__

#include "plib/gnw/ps2map.h"
#include "plib/gnw/ps2controls.h"

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
#include "game/object.h"
#include "game/tile.h"
#include "plib/color/color.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/ps2crash.h"

namespace fallout {

// Blend tables from object.cc.
extern unsigned char* wallBlendTable;
extern unsigned char* glassBlendTable;
extern unsigned char* steamBlendTable;
extern unsigned char* energyBlendTable;
extern unsigned char* redBlendTable;
extern unsigned char glassGrayTable[256];
extern unsigned char commonGrayTable[256];

int gPs2MapMode = PS2MAP_MODE_DRAW;

// --- Configuration -----------------------------------------------------------

#define ATLAS_WIDTH 1024
#define ATLAS_TBW (ATLAS_WIDTH / 64)
#define ATLAS_MAX_ROWS 1024
#define DYN_WIDTH 256
#define DYN_HEIGHT 256
#define DYN_TBW (DYN_WIDTH / 64)
#define DYN_BYTES (DYN_WIDTH * DYN_HEIGHT * 4)
#define CLUT_SLOTS 64
#define CLUT_BYTES 1024
// GS memory left for SDL's texture manager (the screen texture is now in
// ps2gs's own reservation; this is just a margin).
#define TEXMANAGER_RESERVE (480 * 1024) // screen texture + CLUT, cursor, SDL

#define MAX_ENTRIES 2048
#define HASH_SIZE 4096
#define MAX_SHELVES 160
#define MAX_RECORDS 4096
#define MAX_REGION_RECTS 16
#define ARENA_BYTES (768 * 1024)

// --- Types -------------------------------------------------------------------

enum {
    KEY_FRAME = 0,
    KEY_LIT_TILE = 1,
};

typedef struct Entry {
    int kind; // KEY_*; -1 = free, -2 = deleted
    int fid;
    int frameRot;
    int intensity[10];
    short x;
    short y;
    short width;
    short height;
    short shelf;
} Entry;

typedef struct Shelf {
    short y;
    short height;
    short usedX;
    bool inUse;
    unsigned int lastUsed;
} Shelf;

enum {
    REC_SPRITE = 0,
    REC_TRANS,
    REC_EGG,
    REC_SHADOW,
};

typedef struct Record {
    unsigned char type;
    unsigned char clut; // PS2MAP_CLUT_* or trans kind
    unsigned char level; // light >> 9 (clamped)
    unsigned char side; // shadow side
    short x; // map-window coordinates
    short y;
    short width;
    short height;
    short u; // texture coordinates (atlas or dynamic area)
    short v;
} Record;

// --- State -------------------------------------------------------------------

static bool gConfigChecked = false;
static bool gEnabled = true;
static bool gActive = false;
static bool gSuspended = false;
static bool gBaked = false;

static GSGLOBAL* gResGs = NULL;
static u32 gResEnd = 0;
static int gResGeneration = -1;
extern int gPs2GsGeneration;
static u32 gAtlasBase = 0;
static u32 gDynBase = 0;
static u32 gClutBase = 0;
static int gAtlasRows = 0;

static GSTEXTURE gAtlas;
static GSTEXTURE gDyn;

static unsigned char* gArena = NULL;
static unsigned int gArenaUsed = 0;
static bool gArenaFull = false;

static Entry gEntries[MAX_ENTRIES];
static short gHash[HASH_SIZE]; // entry index + 1, 0 = empty
static int gEntryCount = 0;
static int gDeletedCount = 0;

static Shelf gShelves[MAX_SHELVES];
static int gShelfCount = 0;
static int gAtlasTop = 0; // first row not covered by any shelf

static unsigned int* gClutMem = NULL;
static int gClutKey[CLUT_SLOTS];
static unsigned int gClutGenBuilt[CLUT_SLOTS];
static unsigned int gClutUsedFrame[CLUT_SLOTS];
static unsigned int gClutGen = 1;
static unsigned int gCycleGen = 1;
static unsigned int gClutCycleBuilt[CLUT_SLOTS];
static SDL_Color gPalette[256];

static Record gRecords[MAX_RECORDS];
static int gRecordCount = 0;
static bool gRecordOverflowLogged = false;
static bool gAtlasFullLogged = false;
static bool gTooBigLogged = false;

static int gDynX = 0;
static int gDynY = 0;
static int gDynRowHeight = 0;

static unsigned int gEpoch = 1;
static unsigned int gBuiltEpoch = 0;
static unsigned int gBuildNo = 1;
static unsigned int gFrameNo = 1;
static bool gForceRebuild = false;
static int gLitBytesThisBuild = 0;
static bool gAtlasOverflow = false; // art didn't fit this build
static bool gRepacked = false; // already repacked for the current view
// A view whose art doesn't fit even after repacking is drawn by the EE until
// another map or elevation is shown (no flicker from retrying).
static int gOverflowTile = -1;
static int gEvictCount = 0;
static unsigned int gEvictWindowStart = 0;
static int gOverflowElevation = -1;
static Rect gBuiltBounds;
static int gBuiltElevation = -1;
static GSGLOBAL* gEmitGs = NULL;
static bool gAtlasLent = false;

static int gStatRecords = 0;
static int gStatUploads = 0;
static unsigned int gStatUploadBytes = 0;
static int gStatRebuilds = 0;

// --- Public state ------------------------------------------------------------

static void ps2map_check_config()
{
    if (gConfigChecked) {
        return;
    }
    gConfigChecked = true;

    if (!ps2controls_video_setting(PS2_VIDEO_SETTING_GS_MAP)) {
        gEnabled = false;
        debug_printf("ps2map: GS map drawing turned off in Options > Settings > Video\n");
        return;
    }

    const char* names[] = { "GSFLOOR.OFF", "GSMAP.OFF" };
    for (int i = 0; i < 2; i++) {
        FILE* stream = fopen(names[i], "rb");
        if (stream != NULL) {
            fclose(stream);
            gEnabled = false;
            debug_printf("ps2map: GS map drawing disabled by %s\n", names[i]);
            return;
        }
    }
}

bool ps2map_enabled()
{
    ps2map_check_config();
    return gEnabled;
}

bool ps2map_active()
{
    return gEnabled && gActive && !gSuspended;
}

void ps2map_note_change()
{
    gEpoch++;
}

void ps2map_palette_changed(bool cyclingOnly)
{
    if (cyclingOnly) {
        // The game's colour table never maps to the cycling entries, so the
        // translucency palettes (built from it) are unaffected.
        gCycleGen++;
    } else {
        gClutGen++;
    }
}

void ps2map_set_suspended(bool suspended)
{
    gSuspended = suspended;
}

void ps2map_mark_baked()
{
    gBaked = true;
}

bool ps2map_take_baked()
{
    bool baked = gBaked;
    gBaked = false;
    return baked;
}

void ps2map_take_stats(int* records, int* uploads, int* uploadKB, int* rebuilds)
{
    *records = gStatRecords;
    *uploads = gStatUploads;
    *uploadKB = (int)(gStatUploadBytes / 1024);
    *rebuilds = gStatRebuilds;
    gStatRecords = 0;
    gStatUploads = 0;
    gStatUploadBytes = 0;
    gStatRebuilds = 0;
}

static void ps2map_set_active(bool active)
{
    if (gActive == active) {
        return;
    }
    gActive = active;
    debug_printf("ps2map: GS map drawing %s\n", active ? "on" : "off");
    // Have the EE redraw the map window with (or without) holes.
    tile_refresh_display();
}

// --- GS memory ---------------------------------------------------------------

static void ps2map_reset_cache()
{
    for (int i = 0; i < MAX_ENTRIES; i++) {
        gEntries[i].kind = -1;
    }
    memset(gHash, 0, sizeof(gHash));
    gEntryCount = 0;
    gDeletedCount = 0;
    gShelfCount = 0;
    gAtlasTop = 0;
    for (int i = 0; i < CLUT_SLOTS; i++) {
        gClutKey[i] = -1;
        gClutGenBuilt[i] = 0;
        gClutUsedFrame[i] = 0;
    }
    gRecordCount = 0;
    gBuiltEpoch = 0;
}

static bool ps2map_reserve(GSGLOBAL* gs)
{
    if (gResGs == gs && gs->CurrentPointer == gResEnd && gResEnd != 0 && gResGeneration == gPs2GsGeneration) {
        return true;
    }

    if (gArena == NULL) {
        gArena = (unsigned char*)memalign(128, ARENA_BYTES);
        gClutMem = (unsigned int*)memalign(128, CLUT_SLOTS * CLUT_BYTES);
        if (gArena == NULL || gClutMem == NULL) {
            debug_printf("ps2map: out of EE memory, the EE keeps drawing the map\n");
            free(gArena);
            free(gClutMem);
            gArena = NULL;
            gClutMem = NULL;
            gEnabled = false;
            return false;
        }
    }

    int freeBytes = 4 * 1024 * 1024 - (int)gs->CurrentPointer - TEXMANAGER_RESERVE;
    int atlasRows = (freeBytes - DYN_BYTES - CLUT_SLOTS * CLUT_BYTES) / ATLAS_WIDTH;
    atlasRows &= ~63;
    if (atlasRows > ATLAS_MAX_ROWS) {
        atlasRows = ATLAS_MAX_ROWS;
    }
    if (atlasRows < 256) {
        debug_printf("ps2map: not enough GS memory (%u used), the EE keeps drawing the map\n", (unsigned)gs->CurrentPointer);
        gEnabled = false;
        return false;
    }

    int total = atlasRows * ATLAS_WIDTH + DYN_BYTES + CLUT_SLOTS * CLUT_BYTES;
    u32 base = gsKit_vram_alloc(gs, total, GSKIT_ALLOC_USERBUFFER);
    if (base == GSKIT_ALLOC_ERROR) {
        debug_printf("ps2map: GS memory allocation failed, the EE keeps drawing the map\n");
        gEnabled = false;
        return false;
    }

    gResGs = gs;
    gResEnd = gs->CurrentPointer;
    gResGeneration = gPs2GsGeneration;
    gAtlasRows = atlasRows;
    gAtlasBase = base;
    gDynBase = base + atlasRows * ATLAS_WIDTH;
    gClutBase = gDynBase + DYN_BYTES;

    memset(&gAtlas, 0, sizeof(gAtlas));
    gAtlas.Width = ATLAS_WIDTH;
    gAtlas.Height = atlasRows;
    gAtlas.PSM = GS_PSM_T8;
    gAtlas.TBW = ATLAS_TBW;
    gAtlas.Vram = gAtlasBase;
    gAtlas.ClutPSM = GS_PSM_CT32;
    gAtlas.ClutStorageMode = GS_CLUT_STORAGE_CSM1;
    gAtlas.VramClut = gClutBase;
    gAtlas.Filter = GS_FILTER_NEAREST;

    memset(&gDyn, 0, sizeof(gDyn));
    gDyn.Width = DYN_WIDTH;
    gDyn.Height = DYN_HEIGHT;
    gDyn.PSM = GS_PSM_CT32;
    gDyn.TBW = DYN_TBW;
    gDyn.Vram = gDynBase;
    gDyn.VramClut = 0;
    gDyn.Filter = GS_FILTER_NEAREST;

    ps2map_reset_cache();

    debug_printf("ps2map: reserved %d KB of GS memory (atlas 1024x%d), %u KB left\n",
        total / 1024, atlasRows, (unsigned)((4 * 1024 * 1024 - gResEnd) / 1024));
    return true;
}

static void* ps2map_arena_alloc(unsigned int size)
{
    size = (size + 127) & ~127u;
    if (gArenaUsed + size > ARENA_BYTES) {
        gArenaFull = true;
        return NULL;
    }
    void* p = gArena + gArenaUsed;
    gArenaUsed += size;
    return p;
}

// Uploads an image to GS memory at (dx, dy) of the buffer at tbp. mem must
// stay untouched until the frame's render queue has run.
static void ps2map_upload(GSGLOBAL* gs, void* mem, int width, int height, int psm, u32 tbp, int tbw, int dx, int dy)
{
    int bytes = psm == GS_PSM_T8 ? width * height : width * height * 4;
    int qwc = (bytes + 15) / 16;
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

    gStatUploads++;
    gStatUploadBytes += bytes;
}

// --- Atlas (shelf allocator) ---------------------------------------------------

static unsigned int ps2map_hash(int kind, int fid, int frameRot, const int* intensity)
{
    unsigned int h = 2166136261u;
    h = (h ^ (unsigned int)kind) * 16777619u;
    h = (h ^ (unsigned int)fid) * 16777619u;
    h = (h ^ (unsigned int)frameRot) * 16777619u;
    if (intensity != NULL) {
        for (int i = 0; i < 10; i++) {
            h = (h ^ (unsigned int)intensity[i]) * 16777619u;
        }
    }
    return h;
}

static bool ps2map_entry_matches(const Entry* e, int kind, int fid, int frameRot, const int* intensity)
{
    if (e->kind != kind || e->fid != fid || e->frameRot != frameRot) {
        return false;
    }
    if (intensity != NULL) {
        for (int i = 0; i < 10; i++) {
            if (e->intensity[i] != intensity[i]) {
                return false;
            }
        }
    }
    return true;
}

static int ps2map_find_entry(int kind, int fid, int frameRot, const int* intensity)
{
    unsigned int h = ps2map_hash(kind, fid, frameRot, intensity) & (HASH_SIZE - 1);
    for (int probe = 0; probe < HASH_SIZE; probe++) {
        int slot = gHash[h];
        if (slot == 0) {
            return -1;
        }
        Entry* e = &(gEntries[slot - 1]);
        if (e->kind >= 0 && ps2map_entry_matches(e, kind, fid, frameRot, intensity)) {
            return slot - 1;
        }
        h = (h + 1) & (HASH_SIZE - 1);
    }
    return -1;
}

static void ps2map_rehash()
{
    memset(gHash, 0, sizeof(gHash));
    for (int i = 0; i < MAX_ENTRIES; i++) {
        Entry* e = &(gEntries[i]);
        if (e->kind < 0) {
            e->kind = -1;
            continue;
        }
        unsigned int h = ps2map_hash(e->kind, e->fid, e->frameRot, e->kind == KEY_LIT_TILE ? e->intensity : NULL) & (HASH_SIZE - 1);
        while (gHash[h] != 0) {
            h = (h + 1) & (HASH_SIZE - 1);
        }
        gHash[h] = (short)(i + 1);
    }
    gDeletedCount = 0;
}

static void ps2map_evict_shelf(int s)
{
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (gEntries[i].kind >= 0 && gEntries[i].shelf == s) {
            gEntries[i].kind = -2; // deleted (keeps hash probe chains intact)
            gEntryCount--;
            gDeletedCount++;
        }
    }
    gShelves[s].inUse = false;
    gShelves[s].usedX = 0;
}

// Finds room for a width x height image (width already a multiple of 16).
static bool ps2map_atlas_alloc(int width, int height, int* outX, int* outY, int* outShelf)
{
    if (width > ATLAS_WIDTH || height > gAtlasRows) {
        return false;
    }
    int shelfHeight = (height + 7) & ~7;

    for (int pass = 0; pass < 2; pass++) {
        // 1. An existing shelf of a similar height with room.
        int best = -1;
        for (int s = 0; s < gShelfCount; s++) {
            Shelf* sh = &(gShelves[s]);
            if (sh->inUse && sh->height >= shelfHeight && sh->height <= shelfHeight + shelfHeight / 2 + 8
                && sh->usedX + width <= ATLAS_WIDTH) {
                if (best == -1 || sh->height < gShelves[best].height) {
                    best = s;
                }
            }
        }
        if (best != -1) {
            Shelf* sh = &(gShelves[best]);
            *outX = sh->usedX;
            *outY = sh->y;
            *outShelf = best;
            sh->usedX += width;
            sh->lastUsed = gBuildNo;
            return true;
        }

        // 2. A free (evicted) shelf band that's tall enough.
        best = -1;
        for (int s = 0; s < gShelfCount; s++) {
            Shelf* sh = &(gShelves[s]);
            if (!sh->inUse && sh->height >= shelfHeight) {
                if (best == -1 || sh->height < gShelves[best].height) {
                    best = s;
                }
            }
        }
        if (best != -1) {
            Shelf* sh = &(gShelves[best]);
            sh->inUse = true;
            sh->usedX = width;
            sh->lastUsed = gBuildNo;
            *outX = 0;
            *outY = sh->y;
            *outShelf = best;
            return true;
        }

        // 3. A new shelf below the existing ones.
        if (gAtlasTop + shelfHeight <= gAtlasRows && gShelfCount < MAX_SHELVES) {
            Shelf* sh = &(gShelves[gShelfCount]);
            sh->y = (short)gAtlasTop;
            sh->height = (short)shelfHeight;
            sh->usedX = (short)width;
            sh->inUse = true;
            sh->lastUsed = gBuildNo;
            *outX = 0;
            *outY = gAtlasTop;
            *outShelf = gShelfCount;
            gShelfCount++;
            gAtlasTop += shelfHeight;
            return true;
        }

        if (pass == 1) {
            break;
        }

        // 4. Evict every shelf not used by the current draw list.
        bool evicted = false;
        for (int s = 0; s < gShelfCount; s++) {
            if (gShelves[s].inUse && gShelves[s].lastUsed != gBuildNo) {
                ps2map_evict_shelf(s);
                evicted = true;
            }
        }
        if (gDeletedCount > MAX_ENTRIES / 4) {
            ps2map_rehash();
        }

        // If nothing could be evicted and the atlas is fragmented into
        // shelves of the wrong heights, start over completely (only safe
        // when nothing is in use yet this build, which pass 1 rechecks).
        if (!evicted) {
            break;
        }
        // Evicting over and over means this view doesn't really fit:
        // treat it as an overflow (the EE takes over) instead of thrashing.
        if (gFrameNo - gEvictWindowStart > 120) {
            gEvictWindowStart = gFrameNo;
            gEvictCount = 0;
        }
        if (++gEvictCount >= 4) {
            gAtlasOverflow = true;
            gRepacked = true;
        }

        // Merge trailing free shelves back into open space.
        while (gShelfCount > 0 && !gShelves[gShelfCount - 1].inUse) {
            gAtlasTop = gShelves[gShelfCount - 1].y;
            gShelfCount--;
        }
    }

    return false;
}

static int ps2map_insert_entry(int kind, int fid, int frameRot, const int* intensity, int x, int y, int width, int height, int shelf)
{
    if (gEntryCount + gDeletedCount >= MAX_ENTRIES - 1) {
        ps2map_rehash();
        if (gEntryCount >= MAX_ENTRIES - 1) {
            return -1;
        }
    }

    int index = -1;
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (gEntries[i].kind == -1) {
            index = i;
            break;
        }
    }
    if (index == -1) {
        ps2map_rehash();
        for (int i = 0; i < MAX_ENTRIES; i++) {
            if (gEntries[i].kind == -1) {
                index = i;
                break;
            }
        }
        if (index == -1) {
            return -1;
        }
    }

    Entry* e = &(gEntries[index]);
    e->kind = kind;
    e->fid = fid;
    e->frameRot = frameRot;
    if (intensity != NULL) {
        memcpy(e->intensity, intensity, sizeof(e->intensity));
    }
    e->x = (short)x;
    e->y = (short)y;
    e->width = (short)width;
    e->height = (short)height;
    e->shelf = (short)shelf;

    unsigned int h = ps2map_hash(kind, fid, frameRot, intensity) & (HASH_SIZE - 1);
    while (gHash[h] != 0) {
        h = (h + 1) & (HASH_SIZE - 1);
    }
    gHash[h] = (short)(index + 1);
    gEntryCount++;
    return index;
}

// Returns the atlas entry for an 8-bit image, uploading it if needed.
static int ps2map_get_image(int kind, int fid, int frameRot, const int* intensity, const unsigned char* data, int width, int height)
{
    int index = ps2map_find_entry(kind, fid, frameRot, intensity);
    if (index != -1) {
        gShelves[gEntries[index].shelf].lastUsed = gBuildNo;
        return index;
    }

    int paddedWidth = (width + 15) & ~15;
    if (paddedWidth > ATLAS_WIDTH || height > gAtlasRows || height <= 0 || width <= 0) {
        if (!gTooBigLogged) {
            gTooBigLogged = true;
            debug_printf("ps2map: %dx%d image too big for the atlas, skipped\n", width, height);
        }
        return -1;
    }

    unsigned char* staging = (unsigned char*)ps2map_arena_alloc(paddedWidth * height);
    if (staging == NULL) {
        return -1; // finished next frame
    }

    int x;
    int y;
    int shelf;
    if (!ps2map_atlas_alloc(paddedWidth, height, &x, &y, &shelf)) {
        if (!gAtlasFullLogged) {
            gAtlasFullLogged = true;
            debug_printf("ps2map: atlas full (more distinct art on screen than fits), some objects skipped\n");
        }
        return -1;
    }

    for (int row = 0; row < height; row++) {
        unsigned char* dest = staging + row * paddedWidth;
        memcpy(dest, data + row * width, width);
        if (paddedWidth > width) {
            memset(dest + width, 0, paddedWidth - width);
        }
    }
    ps2map_upload(gEmitGs, staging, paddedWidth, height, GS_PSM_T8, gAtlasBase, ATLAS_TBW, x, y);

    return ps2map_insert_entry(kind, fid, frameRot, intensity, x, y, width, height, shelf);
}

// --- Palettes ------------------------------------------------------------------

static inline int ps2map_csm1_index(int i)
{
    return (i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
}

static inline unsigned int ps2map_rgba(int index, int alpha)
{
    SDL_Color c = gPalette[index & 0xFF];
    return ((unsigned int)alpha << 24) | (unsigned int)c.r | ((unsigned int)c.g << 8) | ((unsigned int)c.b << 16);
}

enum {
    CLUTKEY_BASE = 0,
    CLUTKEY_LIGHT = 1,
    CLUTKEY_TRANS = 2,
};

static void ps2map_trans_tables(int kind, unsigned char** blend, unsigned char** gray, int* tint)
{
    switch (kind) {
    case PS2MAP_TRANS_RED:
        *blend = redBlendTable;
        *gray = commonGrayTable;
        *tint = colorTable[31744];
        break;
    case PS2MAP_TRANS_WALL:
        *blend = wallBlendTable;
        *gray = commonGrayTable;
        *tint = colorTable[25439];
        break;
    case PS2MAP_TRANS_GLASS:
        *blend = glassBlendTable;
        *gray = glassGrayTable;
        *tint = colorTable[10239];
        break;
    case PS2MAP_TRANS_STEAM:
        *blend = steamBlendTable;
        *gray = commonGrayTable;
        *tint = colorTable[32767];
        break;
    default:
        *blend = energyBlendTable;
        *gray = commonGrayTable;
        *tint = colorTable[30689];
        break;
    }
}

static void ps2map_build_clut(GSGLOBAL* gs, int slot, int key)
{
    int type = key >> 16;
    int kind = (key >> 8) & 0xFF;
    int level = key & 0xFF;
    unsigned int* clut = gClutMem + slot * 256;

    clut[ps2map_csm1_index(0)] = 0; // transparent
    if (type == CLUTKEY_TRANS) {
        unsigned char* blend;
        unsigned char* gray;
        int tint;
        ps2map_trans_tables(kind, &blend, &gray, &tint);
        for (int c = 1; c < 256; c++) {
            int k = gray[c];
            unsigned int value;
            if (k == 0 || blend == NULL) {
                value = 0; // leaves what's behind (already dimmed by pass 1)
            } else if (k <= 7) {
                // (tint * k + behind * (7 - k)) / 7, then lit: tint * light
                // with alpha k/7.
                int index = intensityColorTable[tint & 0xFF][level];
                value = ps2map_rgba(index, (k * 0x80) / 7);
            } else {
                // Rows 8+ are a solid, brightened tint.
                int row = k > 13 ? 13 : k;
                int index = intensityColorTable[blend[row * 256]][level];
                value = ps2map_rgba(index, 0x80);
            }
            clut[ps2map_csm1_index(c)] = value;
        }
    } else {
        for (int c = 1; c < 256; c++) {
            int index = c;
            if (type == CLUTKEY_LIGHT && c < 0xE5) {
                index = intensityColorTable[c][level];
            }
            clut[ps2map_csm1_index(c)] = ps2map_rgba(index, 0x80);
        }
    }

    SyncDCache(clut, clut + 256);
    gsKit_texture_send_inline(gs, (u32*)clut, 16, 16, gClutBase + slot * CLUT_BYTES, GS_PSM_CT32, 1, GS_CLUT_PALLETE);
    gClutKey[slot] = key;
    gClutGenBuilt[slot] = gClutGen;
    gClutCycleBuilt[slot] = gCycleGen;
}

// Returns the GS address of a palette, or 0 if none is free this frame.
static u32 ps2map_clut(GSGLOBAL* gs, int key)
{
    int victim = -1;
    unsigned int oldest = 0xFFFFFFFF;
    for (int slot = 0; slot < CLUT_SLOTS; slot++) {
        if (gClutKey[slot] == key) {
            bool stale = gClutGenBuilt[slot] != gClutGen
                || ((key >> 16) != CLUTKEY_TRANS && gClutCycleBuilt[slot] != gCycleGen);
            if (stale) {
                ps2map_build_clut(gs, slot, key);
            }
            gClutUsedFrame[slot] = gFrameNo;
            return gClutBase + slot * CLUT_BYTES;
        }
        if (gClutUsedFrame[slot] != gFrameNo && gClutUsedFrame[slot] < oldest) {
            oldest = gClutUsedFrame[slot];
            victim = slot;
        }
    }
    if (victim == -1) {
        return 0;
    }
    ps2map_build_clut(gs, victim, key);
    gClutUsedFrame[victim] = gFrameNo;
    return gClutBase + victim * CLUT_BYTES;
}

// --- Draw list -----------------------------------------------------------------

static inline int ps2map_level(int light)
{
    int level = light >> 9;
    if (level < 0) {
        level = 0;
    }
    if (level > 255) {
        level = 255;
    }
    return level;
}

static Record* ps2map_new_record()
{
    if (gRecordCount >= MAX_RECORDS) {
        if (!gRecordOverflowLogged) {
            gRecordOverflowLogged = true;
            debug_printf("ps2map: more than %d draw records\n", MAX_RECORDS);
        }
        return NULL;
    }
    return &(gRecords[gRecordCount++]);
}

static void ps2map_emit_atlas(int recType, int fid, int frame, int rotation, const unsigned char* frameData, int frameWidth, int frameHeight,
    const unsigned char* src, int width, int height, int destX, int destY, int clut, int level)
{
    if (gEmitGs == NULL || frameData == NULL || width <= 0 || height <= 0) {
        return;
    }

    int index = ps2map_get_image(KEY_FRAME, fid, (frame << 8) | (rotation & 0xFF), NULL, frameData, frameWidth, frameHeight);
    if (index == -1) {
        if (gArenaFull) {
            gForceRebuild = true; // staging memory: finish next frame
        } else {
            gAtlasOverflow = true;
        }
        return;
    }

    int offset = (int)(src - frameData);
    int partX = offset % frameWidth;
    int partY = offset / frameWidth;

    Record* r = ps2map_new_record();
    if (r == NULL) {
        return;
    }
    r->type = (unsigned char)recType;
    r->clut = (unsigned char)clut;
    r->level = (unsigned char)level;
    r->x = (short)destX;
    r->y = (short)destY;
    r->width = (short)width;
    r->height = (short)height;
    r->u = (short)(gEntries[index].x + partX);
    r->v = (short)(gEntries[index].y + partY);
}

void ps2map_emit_part(int fid, int frame, int rotation, const unsigned char* frameData, int frameWidth, int frameHeight,
    const unsigned char* src, int width, int height, int destX, int destY, int clut, int light)
{
    ps2map_emit_atlas(REC_SPRITE, fid, frame, rotation, frameData, frameWidth, frameHeight, src, width, height, destX, destY,
        clut, clut == PS2MAP_CLUT_LIGHT ? ps2map_level(light) : 0);
}

void ps2map_emit_trans(int fid, int frame, int rotation, const unsigned char* frameData, int frameWidth, int frameHeight,
    const unsigned char* src, int width, int height, int destX, int destY, int kind, int light)
{
    ps2map_emit_atlas(REC_TRANS, fid, frame, rotation, frameData, frameWidth, frameHeight, src, width, height, destX, destY,
        kind, ps2map_level(light));
}

bool ps2map_emit_cached_lit_tile(int fid, const int* intensity, int width, int height, int destX, int destY)
{
    if (gEmitGs == NULL) {
        return false;
    }
    int index = ps2map_find_entry(KEY_LIT_TILE, fid, 0, intensity);
    if (index == -1) {
        return false;
    }
    gShelves[gEntries[index].shelf].lastUsed = gBuildNo;
    gLitBytesThisBuild += ((width + 15) & ~15) * height;

    Record* r = ps2map_new_record();
    if (r == NULL) {
        return true;
    }
    r->type = REC_SPRITE;
    r->clut = PS2MAP_CLUT_BASE;
    r->level = 0;
    r->x = (short)destX;
    r->y = (short)destY;
    r->width = (short)width;
    r->height = (short)height;
    r->u = gEntries[index].x;
    r->v = gEntries[index].y;
    return true;
}

void ps2map_emit_lit_tile(int fid, const int* intensity, const unsigned char* lit, const unsigned char* base, int width, int height, int destX, int destY)
{
    if (gEmitGs == NULL || lit == NULL) {
        return;
    }

    bool arenaWasFull = gArenaFull;
    // Gradient-lit tiles are unique images; cap them at a third of the atlas
    // so characters, items and walls (emitted after the floor) always fit.
    int index = -1;
    if (gLitBytesThisBuild + width * height <= gAtlasRows * ATLAS_WIDTH / 3) {
        index = ps2map_get_image(KEY_LIT_TILE, fid, 0, intensity, lit, width, height);
        if (index != -1) {
            gLitBytesThisBuild += ((width + 15) & ~15) * height;
        }
    }
    if (index == -1) {
        if (gArenaFull && !arenaWasFull) {
            gForceRebuild = true; // out of staging memory: finish next frame
            return;
        }
        if (gArenaFull && arenaWasFull) {
            gForceRebuild = true;
            return;
        }
        // Atlas full / over budget (every gradient-lit tile is a unique image, e.g. caves
        // with many lights): draw the plain tile with its average light
        // rather than leaving a hole.
        if (base != NULL) {
            int sum = 0;
            for (int i = 0; i < 10; i++) {
                sum += intensity[i];
            }
            ps2map_emit_atlas(REC_SPRITE, fid, 0, 0, base, width, height, base, width, height, destX, destY,
                PS2MAP_CLUT_LIGHT, ps2map_level(sum / 10));
        }
        return;
    }

    Record* r = ps2map_new_record();
    if (r == NULL) {
        return;
    }
    r->type = REC_SPRITE;
    r->clut = PS2MAP_CLUT_BASE;
    r->level = 0;
    r->x = (short)destX;
    r->y = (short)destY;
    r->width = (short)width;
    r->height = (short)height;
    r->u = gEntries[index].x;
    r->v = gEntries[index].y;
}

void ps2map_emit_egg(const unsigned char* src, int srcPitch, int width, int height, int destX, int destY,
    const unsigned char* mask, int maskPitch, int light)
{
    if (gEmitGs == NULL || width <= 0 || height <= 0) {
        return;
    }

    int paddedWidth = (width + 3) & ~3;
    if (paddedWidth > DYN_WIDTH || height > DYN_HEIGHT) {
        return;
    }

    // Place it in the dynamic area (refilled on every rebuild).
    if (gDynX + paddedWidth > DYN_WIDTH) {
        gDynX = 0;
        gDynY += gDynRowHeight;
        gDynRowHeight = 0;
    }
    if (gDynY + height > DYN_HEIGHT) {
        return; // too many cutout pieces; rare
    }

    unsigned int* pixels = (unsigned int*)ps2map_arena_alloc(paddedWidth * height * 4);
    if (pixels == NULL) {
        gForceRebuild = true;
        return;
    }

    int level = ps2map_level(light);
    for (int y = 0; y < height; y++) {
        const unsigned char* s = src + y * srcPitch;
        const unsigned char* m = mask + y * maskPitch;
        unsigned int* d = pixels + y * paddedWidth;
        for (int x = 0; x < width; x++) {
            unsigned char b = s[x];
            if (b == 0) {
                d[x] = 0;
                continue;
            }
            int lit = intensityColorTable[b][level];
            int alpha = m[x] == 0 ? 0x80 : m[x];
            if (alpha > 0x80) {
                alpha = 0x80;
            }
            d[x] = ps2map_rgba(lit, alpha);
        }
        for (int x = width; x < paddedWidth; x++) {
            d[x] = 0;
        }
    }

    ps2map_upload(gEmitGs, pixels, paddedWidth, height, GS_PSM_CT32, gDynBase, DYN_TBW, gDynX, gDynY);

    Record* r = ps2map_new_record();
    if (r != NULL) {
        r->type = REC_EGG;
        r->x = (short)destX;
        r->y = (short)destY;
        r->width = (short)width;
        r->height = (short)height;
        r->u = (short)gDynX;
        r->v = (short)gDynY;
    }

    gDynX += paddedWidth;
    if (height > gDynRowHeight) {
        gDynRowHeight = height;
    }
}

void ps2map_emit_shadow(int ulx, int uly, int lrx, int lry, int side)
{
    if (gEmitGs == NULL) {
        return;
    }
    Record* r = ps2map_new_record();
    if (r == NULL) {
        return;
    }
    r->type = REC_SHADOW;
    r->side = (unsigned char)side;
    r->x = (short)ulx;
    r->y = (short)uly;
    r->width = (short)(lrx - ulx + 1);
    r->height = (short)(lry - uly + 1);
}

static void ps2map_rebuild(GSGLOBAL* gs, const Rect* bounds)
{
    ps2crash_step("GS map rebuild", map_elevation);

    gBuildNo++;
    gRecordCount = 0;
    gDynX = 0;
    gDynY = 0;
    gDynRowHeight = 0;
    gForceRebuild = false;
    gLitBytesThisBuild = 0;
    gAtlasOverflow = false;
    gEmitGs = gs;

    Rect rect = *bounds;
    int savedMode = gPs2MapMode;
    gPs2MapMode = PS2MAP_MODE_EMIT;
    square_render_floor(&rect, map_elevation);
    obj_render_pre_roof(&rect, map_elevation);
    square_render_roof(&rect, map_elevation);
    bounds_render(&rect, map_elevation);
    gPs2MapMode = savedMode;

    gEmitGs = NULL;
    if (gAtlasOverflow && !gRepacked) {
        // The atlas is fragmented with art from earlier views: empty it and
        // pack only what this view needs (next frame).
        gRepacked = true;
        ps2map_reset_cache();
        ps2map_rebuild(gs, bounds);
        return;
    } else if (gAtlasOverflow) {
        gOverflowTile = map_get_index_number();
        gOverflowElevation = map_elevation;
        gRepacked = false;
        debug_printf("ps2map: this view needs more art than the atlas holds, the EE draws it\n");
    } else if (!gAtlasOverflow) {
        gRepacked = false;
    }
    gBuiltEpoch = gEpoch;
    gBuiltBounds = *bounds;
    gBuiltElevation = map_elevation;
    gStatRebuilds++;
}

// --- Drawing -------------------------------------------------------------------

typedef struct Xform {
    float ax;
    float bx;
    float ay;
    float by;
    float clipW;
    float clipH;
} Xform;

static void ps2map_make_xform(const Ps2MapView* view, GSGLOBAL* gs, Xform* xf)
{
    if (view->zoomed) {
        float destW = view->screenWidth * view->scaleX;
        float destH = view->mapHeight * view->scaleY;
        xf->ax = destW / (float)view->zoomSrc.w;
        xf->ay = destH / (float)view->zoomSrc.h;
        xf->bx = -(float)view->zoomSrc.x * xf->ax;
        xf->by = -(float)view->zoomSrc.y * xf->ay;
        xf->clipW = destW;
        xf->clipH = destH;
    } else {
        xf->ax = view->scaleX;
        xf->ay = view->scaleY;
        xf->bx = 0.0f;
        xf->by = 0.0f;
        xf->clipW = (float)gs->Width;
        xf->clipH = (float)gs->Height;
    }
}

static bool ps2map_set_scissor(GSGLOBAL* gs, const Xform* xf, const Rect* screenRect)
{
    float x0 = screenRect->ulx * xf->ax + xf->bx;
    float y0 = screenRect->uly * xf->ay + xf->by;
    float x1 = (screenRect->lrx + 1) * xf->ax + xf->bx;
    float y1 = (screenRect->lry + 1) * xf->ay + xf->by;
    if (x0 < 0.0f) x0 = 0.0f;
    if (y0 < 0.0f) y0 = 0.0f;
    if (x1 > xf->clipW) x1 = xf->clipW;
    if (y1 > xf->clipH) y1 = xf->clipH;

    int ix0 = (int)(x0 + 0.5f);
    int iy0 = (int)(y0 + 0.5f);
    int ix1 = (int)(x1 + 0.5f) - 1;
    int iy1 = (int)(y1 + 0.5f) - 1;
    if (ix1 < ix0 || iy1 < iy0) {
        return false;
    }
    gsKit_set_scissor(gs, GS_SETREG_SCISSOR(ix0, ix1, iy0, iy1));
    return true;
}

// GS state, emitted only when it changes.
enum {
    BLEND_NORMAL = 0, // (Cs - Cd) * As + Cd
    BLEND_SCALE_DEST, // Cd * FIX
    BLEND_MULTIPLY_ALPHA, // Cd * As
};

static int gCurBlend = -1;
static int gCurFix = -1;
static int gCurTest = -1;

static void ps2map_set_blend(GSGLOBAL* gs, int mode, int fix)
{
    if (mode == gCurBlend && (mode != BLEND_SCALE_DEST || fix == gCurFix)) {
        return;
    }
    gCurBlend = mode;
    gCurFix = fix;
    switch (mode) {
    case BLEND_SCALE_DEST:
        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(1, 2, 2, 2, fix), 0);
        break;
    case BLEND_MULTIPLY_ALPHA:
        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(1, 2, 0, 2, 0), 0);
        break;
    default:
        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
        break;
    }
}

// 0 = no alpha test, 1 = opaque texels only, 2 = any non-zero alpha.
static void ps2map_set_test(GSGLOBAL* gs, int test)
{
    if (test == gCurTest) {
        return;
    }
    gCurTest = test;
    if (test == 0) {
        gsKit_set_test(gs, GS_ATEST_OFF);
        return;
    }
    gs->Test->ATST = 6; // GREATER
    gs->Test->AREF = test == 1 ? 0x7F : 0x00;
    gs->Test->AFAIL = 0;
    gsKit_set_test(gs, GS_ATEST_ON);
}

static void ps2map_sprite(GSGLOBAL* gs, GSTEXTURE* tex, const Xform* xf, float sx, float sy, const Record* r, u64 color)
{
    float x1 = sx * xf->ax + xf->bx;
    float y1 = sy * xf->ay + xf->by;
    float x2 = (sx + r->width) * xf->ax + xf->bx;
    float y2 = (sy + r->height) * xf->ay + xf->by;
    gsKit_prim_sprite_texture(gs, tex,
        x1, y1, (float)r->u, (float)r->v,
        x2, y2, (float)(r->u + r->width), (float)(r->v + r->height),
        0, color);
}

static const u64 kWhite = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00);

static void ps2map_draw_record(GSGLOBAL* gs, const Xform* xf, const Record* r, int originX, int originY)
{
    float sx = (float)(r->x + originX);
    float sy = (float)(r->y + originY);

    switch (r->type) {
    case REC_SPRITE: {
        int key = r->clut == PS2MAP_CLUT_LIGHT ? ((CLUTKEY_LIGHT << 16) | r->level) : (CLUTKEY_BASE << 16);
        u32 clut = ps2map_clut(gs, key);
        u64 color = kWhite;
        if (clut == 0) {
            // No free palette: approximate the light with vertex colour.
            clut = ps2map_clut(gs, CLUTKEY_BASE << 16);
            int l = r->clut == PS2MAP_CLUT_LIGHT ? (r->level > 128 ? 128 : r->level) : 0x80;
            color = GS_SETREG_RGBAQ(l, l, l, 0x80, 0x00);
            if (clut == 0) {
                return;
            }
        }
        gAtlas.VramClut = clut;
        ps2map_set_blend(gs, BLEND_NORMAL, 0);
        ps2map_set_test(gs, 1);
        ps2map_sprite(gs, &gAtlas, xf, sx, sy, r, color);
        break;
    }
    case REC_TRANS: {
        // Pass 1: scale what's behind by the light level.
        int level = r->level > 128 ? 128 : r->level;
        if (level < 128) {
            u32 base = ps2map_clut(gs, CLUTKEY_BASE << 16);
            if (base != 0) {
                gAtlas.VramClut = base;
                ps2map_set_blend(gs, BLEND_SCALE_DEST, level);
                ps2map_set_test(gs, 1);
                ps2map_sprite(gs, &gAtlas, xf, sx, sy, r, kWhite);
            }
        }
        // Pass 2: blend the (lit) tint in.
        u32 clut = ps2map_clut(gs, (CLUTKEY_TRANS << 16) | (r->clut << 8) | r->level);
        if (clut != 0) {
            gAtlas.VramClut = clut;
            ps2map_set_blend(gs, BLEND_NORMAL, 0);
            ps2map_set_test(gs, 2);
            ps2map_sprite(gs, &gAtlas, xf, sx, sy, r, kWhite);
        }
        break;
    }
    case REC_EGG:
        ps2map_set_blend(gs, BLEND_NORMAL, 0);
        ps2map_set_test(gs, 2);
        ps2map_sprite(gs, &gDyn, xf, sx, sy, r, kWhite);
        break;
    case REC_SHADOW: {
        float x1 = sx * xf->ax + xf->bx;
        float y1 = sy * xf->ay + xf->by;
        float x2 = (sx + r->width) * xf->ax + xf->bx;
        float y2 = (sy + r->height) * xf->ay + xf->by;
        // Alpha 0 (black) at the map edge, 0x80 (unchanged) 16 px inside.
        u64 dark = GS_SETREG_RGBAQ(0, 0, 0, 0x00, 0x00);
        u64 clear = GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0x00);
        u64 tl = clear, tr = clear, bl = clear, br = clear;
        switch (r->side) {
        case 0: // left: dark on the left
            tl = dark;
            bl = dark;
            break;
        case 1: // top
            tl = dark;
            tr = dark;
            break;
        case 2: // right
            tr = dark;
            br = dark;
            break;
        default: // bottom
            bl = dark;
            br = dark;
            break;
        }
        ps2map_set_blend(gs, BLEND_MULTIPLY_ALPHA, 0);
        ps2map_set_test(gs, 0);
        gsKit_prim_quad_gouraud_3d(gs, x1, y1, 0, x2, y1, 0, x1, y2, 0, x2, y2, 0, tl, tr, bl, br);
        break;
    }
    }
}

bool ps2map_borrow_vram(GSGLOBAL* gs, unsigned int* base, int* bytes)
{
    if (!gEnabled || gResGs != gs || gResEnd == 0 || gs->CurrentPointer != gResEnd || gResGeneration != gPs2GsGeneration) {
        return false;
    }
    gAtlasLent = true;
    *base = gAtlasBase;
    *bytes = gAtlasRows * ATLAS_WIDTH;
    return true;
}

void ps2map_prepare(GSGLOBAL* gs)
{
    ps2map_check_config();
    if (gEnabled) {
        ps2map_reserve(gs);
    }
}

void ps2map_draw(GSGLOBAL* gs, const Ps2MapView* view, SDL_Palette* palette)
{
    ps2map_check_config();
    if (!gEnabled) {
        return;
    }

    if (!ps2map_reserve(gs)) {
        if (gActive) {
            ps2map_set_active(false);
        }
        return;
    }

    if (gAtlasLent) {
        // A movie used the atlas memory: everything in it is gone.
        gAtlasLent = false;
        ps2map_reset_cache();
    }

    gFrameNo++;
    gArenaUsed = 0;
    gArenaFull = false;

    // display_win 0 would be the screen background: the map window exists
    // only once the map system is up.
    if (display_win <= 0) {
        return;
    }
    Window* w = GNW_find(display_win);
    if (w == NULL) {
        return;
    }

    Rect rects[MAX_REGION_RECTS];
    int count = win_visible_rects(display_win, rects, MAX_REGION_RECTS);
    if (count <= 0) {
        return;
    }

    if (gOverflowTile != -1) {
        if (gOverflowTile == map_get_index_number() && gOverflowElevation == map_elevation) {
            if (gActive) {
                ps2map_set_active(false);
            }
            return;
        }
        gOverflowTile = -1; // new map: try the GS again
    }

    if (!gActive) {
        if (!tile_refresh_is_enabled()) {
            return;
        }
        ps2map_set_active(true);
    }

    // After a snapshot the map window holds a full EE picture; once nothing
    // covers it any more, go back to GS drawing.
    if (gBaked && count == 1 && rects[0].ulx == w->rect.ulx && rects[0].uly == w->rect.uly
        && rects[0].lrx == w->rect.lrx && rects[0].lry == w->rect.lry) {
        tile_refresh_display();
    }

    if (palette != NULL) {
        int n = palette->ncolors < 256 ? palette->ncolors : 256;
        memcpy(gPalette, palette->colors, n * sizeof(SDL_Color));
    }

    Rect bounds = rects[0];
    for (int i = 1; i < count; i++) {
        if (rects[i].ulx < bounds.ulx) bounds.ulx = rects[i].ulx;
        if (rects[i].uly < bounds.uly) bounds.uly = rects[i].uly;
        if (rects[i].lrx > bounds.lrx) bounds.lrx = rects[i].lrx;
        if (rects[i].lry > bounds.lry) bounds.lry = rects[i].lry;
    }
    bounds.ulx -= w->rect.ulx;
    bounds.lrx -= w->rect.ulx;
    bounds.uly -= w->rect.uly;
    bounds.lry -= w->rect.uly;

    bool boundsChanged = bounds.ulx != gBuiltBounds.ulx || bounds.uly != gBuiltBounds.uly
        || bounds.lrx != gBuiltBounds.lrx || bounds.lry != gBuiltBounds.lry;

    if (tile_refresh_is_enabled()
        && (gBuiltEpoch != gEpoch || boundsChanged || gBuiltElevation != map_elevation || gForceRebuild)) {
        ps2map_rebuild(gs, &bounds);
        if (gOverflowTile != -1) {
            ps2map_set_active(false);
            return;
        }
    }

    if (gRecordCount == 0) {
        return;
    }

    Xform xf;
    ps2map_make_xform(view, gs, &xf);

    gCurBlend = -1;
    gCurTest = -1;

    for (int i = 0; i < count; i++) {
        if (!ps2map_set_scissor(gs, &xf, &(rects[i]))) {
            continue;
        }

        int rx0 = rects[i].ulx - w->rect.ulx;
        int ry0 = rects[i].uly - w->rect.uly;
        int rx1 = rects[i].lrx - w->rect.ulx;
        int ry1 = rects[i].lry - w->rect.uly;

        for (int j = 0; j < gRecordCount; j++) {
            const Record* r = &(gRecords[j]);
            if (r->x > rx1 || r->x + r->width <= rx0 || r->y > ry1 || r->y + r->height <= ry0) {
                continue;
            }
            ps2map_draw_record(gs, &xf, r, w->rect.ulx, w->rect.uly);
            gStatRecords++;
        }
    }

    // Leave the state the frame sprite expects.
    gsKit_set_scissor(gs, GS_SCISSOR_RESET);
    ps2map_set_blend(gs, BLEND_NORMAL, 0);
    gCurTest = -1;
    ps2map_set_test(gs, 1);
}

} // namespace fallout

#endif /* __PS2__ */
