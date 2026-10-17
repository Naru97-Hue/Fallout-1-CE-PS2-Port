// Hardware MPEG movie playback for the PS2 build.
//
// The PS2 can't decode Interplay MVE in hardware, so each movie is converted
// once on the PC (see tools/convert_movies_ps2.sh) into:
//
//   MOVIES/<name>.M2V   MPEG-2 video elementary stream (decoded by the IPU)
//   MOVIES/<name>.PCM   raw signed 16-bit little-endian stereo, 22050 Hz
//
// Video is decoded with ps2sdk's open-source libmpeg (IPU + DMA + scratchpad)
// and shown through SDL; audio is streamed through the game's audio engine.
// If the converted files are missing the caller falls back to the original
// software MVE player.

#ifdef __PS2__

#include "int/ps2movie.h"
#include "plib/gnw/ps2crash.h"

#include <ctype.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include <dma.h>
#include <kernel.h>
#include <libmpeg.h>

#include "audio_engine.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/ps2gs.h"
#include "plib/gnw/ps2map.h"
#include "plib/gnw/svga.h"

#include <gsKit.h>

namespace fallout {

#define PS2MOVIE_BLOCK_SIZE 2048
#define PS2MOVIE_AUDIO_RATE 22050
#define PS2MOVIE_AUDIO_FRAME 4 // 16-bit stereo
#define PS2MOVIE_AUDIO_BUFFER (PS2MOVIE_AUDIO_RATE * PS2MOVIE_AUDIO_FRAME * 2) // 2 seconds

typedef struct Ps2MovieState {
    FILE* video;
    FILE* audio;
    bool videoEof;
    bool audioEof;
    unsigned char* dmaBlocks[2];
    int dmaIndex;

    MPEGSequenceInfo* info;
    unsigned char* picture; // RGBA32 in 16x16 macroblock order
    unsigned char* linear; // RGBA32 row-major, for SDL
    int width;
    int height;

    int soundBuffer;
    unsigned int audioWritePos;
} Ps2MovieState;

static Ps2MovieState gState;

// Builds "MOVIES/IPLOGO" from "art\cuts\iplogo.mve".
static void ps2movie_base_name(const char* mvePath, char* out, size_t outSize)
{
    const char* name = mvePath;
    for (const char* p = mvePath; *p != '\0'; p++) {
        if (*p == '\\' || *p == '/' || *p == ':') {
            name = p + 1;
        }
    }

    char stem[64];
    size_t length = 0;
    while (name[length] != '\0' && name[length] != '.' && length < sizeof(stem) - 1) {
        stem[length] = (char)toupper((unsigned char)name[length]);
        length++;
    }
    stem[length] = '\0';

    snprintf(out, outSize, "MOVIES/%s", stem);
}

// libmpeg data callback: DMA the next 2 KB of bitstream to the IPU.
// Two blocks alternate, and we wait for the previous transfer before reusing
// a block, so the DMA engine never reads memory we're overwriting.
static int ps2movie_feed(void* userData)
{
    (void)userData;

    if (gState.videoEof) {
        return 0;
    }

    unsigned char* block = gState.dmaBlocks[gState.dmaIndex];
    gState.dmaIndex ^= 1;

    dma_channel_wait(DMA_CHANNEL_toIPU, 0);

    size_t got = fread(block, 1, PS2MOVIE_BLOCK_SIZE, gState.video);
    if (got == 0) {
        gState.videoEof = true;
        return 0;
    }

    if (got < PS2MOVIE_BLOCK_SIZE) {
        // Pad the tail with zeros; the decoder stops at the sequence end code.
        memset(block + got, 0, PS2MOVIE_BLOCK_SIZE - got);
        gState.videoEof = true;
    }

    SyncDCache(block, block + PS2MOVIE_BLOCK_SIZE);
    dma_channel_send_normal(DMA_CHANNEL_toIPU, block, PS2MOVIE_BLOCK_SIZE >> 4, 0, 0);
    return 1;
}

// libmpeg sequence callback: allocate the decode target.
static void* ps2movie_init_picture(void* userData, MPEGSequenceInfo* info)
{
    (void)userData;

    gState.info = info;

    if (gState.picture == NULL || gState.width != info->m_Width || gState.height != info->m_Height) {
        free(gState.picture);
        free(gState.linear);
        gState.width = info->m_Width;
        gState.height = info->m_Height;
        gState.picture = (unsigned char*)memalign(64, gState.width * gState.height * 4);
        gState.linear = (unsigned char*)memalign(64, gState.width * gState.height * 4);
    }

    debug_printf("ps2movie: sequence %dx%d, %d ms/frame\n", info->m_Width, info->m_Height, info->m_MSPerFrame);
    return gState.picture;
}

extern GSGLOBAL* gPs2GsGlobal;

// Draws the decoded frame straight from GS memory borrowed from the map's
// art atlas (an SDL texture this size no longer fits in the GS memory left
// to SDL). Returns false if that memory isn't available.
static bool ps2movie_present_direct()
{
    GSGLOBAL* gs = gPs2GsGlobal;
    unsigned int base;
    int bytes;
    if (gs == NULL || !ps2map_borrow_vram(gs, &base, &bytes)) {
        return false;
    }

    int tbw = (gState.width + 63) / 64;
    int rows = (gState.height + 31) & ~31;
    if (tbw * 64 * rows * 4 > bytes) {
        return false;
    }

    GSTEXTURE tex;
    memset(&tex, 0, sizeof(tex));
    tex.Width = gState.width;
    tex.Height = gState.height;
    tex.PSM = GS_PSM_CT32;
    tex.TBW = tbw;
    tex.Vram = base;
    tex.VramClut = 0;

    SDL_SetRenderDrawColor(gSdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(gSdlRenderer);
    SDL_RenderFlush(gSdlRenderer);
    gsKit_set_scissor(gs, GS_SCISSOR_RESET); // SDL's clear can leave a bogus one

    ps2gs_upload(gState.linear, gState.width, gState.height, GS_PSM_CT32, base, tbw, 0, 0);

    // Centre the movie at its original size inside the 640x480 screen,
    // scaled to the TV mode like everything else.
    float scaleX = (float)gs->Width / 640.0f;
    float scaleY = (float)gs->Height / 480.0f;
    float x1 = (640 - gState.width) / 2 * scaleX;
    float y1 = (480 - gState.height) / 2 * scaleY;
    float x2 = x1 + gState.width * scaleX;
    float y2 = y1 + gState.height * scaleY;
    tex.Filter = (scaleX == 1.0f && scaleY == 1.0f) ? GS_FILTER_NEAREST : GS_FILTER_LINEAR;

    int savedOffsetX = gs->OffsetX;
    int savedOffsetY = gs->OffsetY;
    int savedAlpha = gs->PrimAlphaEnable;
    gs->OffsetX = (int)(2048.0f * 16.0f);
    gs->OffsetY = (int)(2048.0f * 16.0f);
    gs->PrimAlphaEnable = GS_SETTING_OFF; // opaque, colour only
    gsKit_prim_sprite_texture(gs, &tex,
        x1, y1, 0.0f, 0.0f,
        x2, y2, (float)gState.width, (float)gState.height,
        0, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
    gs->OffsetX = savedOffsetX;
    gs->OffsetY = savedOffsetY;
    gs->PrimAlphaEnable = savedAlpha;

    SDL_RenderPresent(gSdlRenderer);
    return true;
}

// Converts libmpeg's macroblock layout to row-major RGBA.
static void ps2movie_linearize()
{
    // The decoder writes with the IPU/DMA; drop stale cache lines first.
    SyncDCache(gState.picture, gState.picture + gState.width * gState.height * 4);

    int mbw = gState.width / 16;
    int mbh = gState.height / 16;
    const unsigned char* src = gState.picture;
    int pitch = gState.width * 4;

    for (int my = 0; my < mbh; my++) {
        for (int mx = 0; mx < mbw; mx++) {
            unsigned char* dst = gState.linear + (my * 16) * pitch + mx * 16 * 4;
            for (int row = 0; row < 16; row++) {
                memcpy(dst, src, 16 * 4);
                src += 16 * 4;
                dst += pitch;
            }
        }
    }
}

static void ps2movie_fill_audio(unsigned int bytes)
{
    if (gState.soundBuffer == -1 || bytes == 0) {
        return;
    }

    void* ptr1;
    void* ptr2;
    unsigned int len1;
    unsigned int len2;
    if (!audioEngineSoundBufferLock(gState.soundBuffer, gState.audioWritePos, bytes, &ptr1, &len1, &ptr2, &len2, 0)) {
        return;
    }

    void* ptrs[2] = { ptr1, ptr2 };
    unsigned int lens[2] = { len1, len2 };
    for (int i = 0; i < 2; i++) {
        if (ptrs[i] == NULL || lens[i] == 0) {
            continue;
        }

        size_t got = 0;
        if (!gState.audioEof) {
            got = fread(ptrs[i], 1, lens[i], gState.audio);
            if (got < lens[i]) {
                gState.audioEof = true;
            }
        }

        if (got < lens[i]) {
            memset((unsigned char*)ptrs[i] + got, 0, lens[i] - got);
        }
    }

    audioEngineSoundBufferUnlock(gState.soundBuffer, ptr1, len1, ptr2, len2);
    gState.audioWritePos = (gState.audioWritePos + bytes) % PS2MOVIE_AUDIO_BUFFER;
}

// Tops up the ring buffer up to (but not past) the play cursor.
static void ps2movie_pump_audio()
{
    if (gState.soundBuffer == -1) {
        return;
    }

    unsigned int readPos;
    if (!audioEngineSoundBufferGetCurrentPosition(gState.soundBuffer, &readPos, NULL)) {
        return;
    }

    unsigned int free = (readPos + PS2MOVIE_AUDIO_BUFFER - gState.audioWritePos) % PS2MOVIE_AUDIO_BUFFER;
    // Keep a small gap so the writer never catches the reader.
    if (free > 4096) {
        free -= 4096;
        free &= ~(PS2MOVIE_AUDIO_FRAME - 1);
        ps2movie_fill_audio(free);
    }
}

// Returns true when the player pressed something to skip the movie.
static bool ps2movie_skip_requested()
{
    SDL_Event event;
    bool skip = false;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_JOYBUTTONDOWN:
        case SDL_KEYDOWN:
        case SDL_MOUSEBUTTONDOWN:
            skip = true;
            break;
        case SDL_QUIT:
            skip = true;
            break;
        }
    }
    return skip;
}

// Hardware registers (EE user manual): DMA channel 3 = IPU -> memory,
// channel 4 = memory -> IPU.
#define PS2_REG(addr) (*(volatile unsigned int*)(addr))
#define PS2_D3_CHCR PS2_REG(0x1000B000)
#define PS2_D3_QWC PS2_REG(0x1000B020)
#define PS2_D4_CHCR PS2_REG(0x1000B400)
#define PS2_D4_QWC PS2_REG(0x1000B420)
#define PS2_D_ENABLER PS2_REG(0x1000F520)
#define PS2_D_ENABLEW PS2_REG(0x1000F590)
#define PS2_IPU_CMD PS2_REG(0x10002000)
#define PS2_IPU_CTRL PS2_REG(0x10002010)
#define PS2_CHCR_STR 0x100
#define PS2_IPU_CTRL_RST 0x40000000
#define PS2_IPU_CTRL_BUSY 0x80000000

// libmpeg's MPEG_Destroy() leaves the IPU and its DMA channels running. If a
// movie is skipped mid-picture, a transfer can still be in flight and the IPU
// keeps writing decoded data into buffers we are about to free, corrupting
// whatever the game allocates next (seen as "Memory header stomped" and
// zeroed script procedure tables). Halt both channels and reset the IPU.
static void ps2movie_halt_ipu()
{
    int oldIntr = DIntr();

    // Suspend the DMAC while we stop the channels.
    unsigned int enabler = PS2_D_ENABLER;
    PS2_D_ENABLEW = enabler | 0x10000;
    asm volatile("sync.l");

    PS2_D3_CHCR &= ~PS2_CHCR_STR;
    PS2_D4_CHCR &= ~PS2_CHCR_STR;
    PS2_D3_QWC = 0;
    PS2_D4_QWC = 0;

    PS2_D_ENABLEW = enabler;
    asm volatile("sync.l");

    if (oldIntr) {
        EIntr();
    }

    // Reset the IPU so it drops anything half-decoded.
    PS2_IPU_CTRL = PS2_IPU_CTRL_RST;
    for (int spin = 0; spin < 100000 && (PS2_IPU_CTRL & PS2_IPU_CTRL_BUSY); spin++) {
    }

    // Make sure no cache line from the old buffers gets written back later.
    FlushCache(0);
}

static void ps2movie_close()
{
    if (gState.soundBuffer != -1) {
        audioEngineSoundBufferStop(gState.soundBuffer);
        audioEngineSoundBufferRelease(gState.soundBuffer);
        gState.soundBuffer = -1;
    }

    if (gState.video != NULL) {
        fclose(gState.video);
        gState.video = NULL;
    }

    if (gState.audio != NULL) {
        fclose(gState.audio);
        gState.audio = NULL;
    }

    for (int i = 0; i < 2; i++) {
        free(gState.dmaBlocks[i]);
        gState.dmaBlocks[i] = NULL;
    }

    free(gState.picture);
    free(gState.linear);
    gState.picture = NULL;
    gState.linear = NULL;
    gState.info = NULL;
}

bool ps2movie_play(const char* mvePath)
{
    char base[128];
    char path[160];
    ps2movie_base_name(mvePath, base, sizeof(base));

    memset(&gState, 0, sizeof(gState));
    gState.soundBuffer = -1;

    snprintf(path, sizeof(path), "%s.M2V", base);
    gState.video = fopen(path, "rb");
    if (gState.video == NULL) {
        debug_printf("ps2movie: %s not found, using software MVE player\n", path);
        return false;
    }

    snprintf(path, sizeof(path), "%s.PCM", base);
    gState.audio = fopen(path, "rb");
    if (gState.audio == NULL) {
        debug_printf("ps2movie: %s not found, playing without sound\n", path);
        gState.audioEof = true;
    }

    gState.dmaBlocks[0] = (unsigned char*)memalign(64, PS2MOVIE_BLOCK_SIZE);
    gState.dmaBlocks[1] = (unsigned char*)memalign(64, PS2MOVIE_BLOCK_SIZE);
    if (gState.dmaBlocks[0] == NULL || gState.dmaBlocks[1] == NULL) {
        ps2movie_close();
        return false;
    }

    debug_printf("ps2movie: playing %s via IPU\n", base);

    // Audio: 2-second ring buffer, pre-filled, played looping.
    if (!gState.audioEof) {
        gState.soundBuffer = audioEngineCreateSoundBuffer(PS2MOVIE_AUDIO_BUFFER, 16, 2, PS2MOVIE_AUDIO_RATE);
        if (gState.soundBuffer != -1) {
            ps2movie_fill_audio(PS2MOVIE_AUDIO_BUFFER - PS2MOVIE_AUDIO_FRAME);
            gState.audioWritePos = PS2MOVIE_AUDIO_BUFFER - PS2MOVIE_AUDIO_FRAME;
        }
    }

    dma_channel_initialize(DMA_CHANNEL_toIPU, NULL, 0);
    dma_channel_initialize(DMA_CHANNEL_fromIPU, NULL, 0);

    s64 currentPts = 0;
    MPEG_Initialize(ps2movie_feed, NULL, ps2movie_init_picture, NULL, &currentPts);

    SDL_Texture* texture = NULL;
    // Direct GS drawing when the map's GS memory can be borrowed; otherwise
    // (GSMAP.OFF) there's room for an SDL texture.
    unsigned int probeBase;
    int probeBytes;
    bool useDirect = gPs2GsGlobal != NULL && ps2map_borrow_vram(gPs2GsGlobal, &probeBase, &probeBytes);
    debug_printf("ps2movie: drawing frames %s\n", useDirect ? "directly from GS memory" : "through an SDL texture");
    Uint32 startTicks = 0;
    int frame = 0;
    bool started = false;

    while (true) {
        s64 pts;
        if (!MPEG_Picture(gState.picture, &pts)) {
            break;
        }

        if (gState.picture == NULL || gState.info == NULL) {
            break;
        }

        if (texture == NULL && !useDirect) {
            texture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, gState.width, gState.height);
            if (texture == NULL) {
                debug_printf("ps2movie: SDL_CreateTexture failed: %s\n", SDL_GetError());
                break;
            }
        }

        if (!started) {
            started = true;
            startTicks = SDL_GetTicks();
            if (gState.soundBuffer != -1) {
                audioEngineSoundBufferPlay(gState.soundBuffer, AUDIO_ENGINE_SOUND_BUFFER_PLAY_LOOPING);
            }
        }

        ps2movie_linearize();
        if (!useDirect || !ps2movie_present_direct()) {
            if (texture == NULL) {
                texture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, gState.width, gState.height);
                if (texture == NULL) {
                    debug_printf("ps2movie: SDL_CreateTexture failed: %s\n", SDL_GetError());
                    break;
                }
            }
            SDL_UpdateTexture(texture, NULL, gState.linear, gState.width * 4);

            // Centre the movie at its original size inside the 640x480 screen.
            int screenWidth = screenGetWidth();
            int screenHeight = screenGetHeight();
            SDL_Rect dest;
            dest.w = gState.width;
            dest.h = gState.height;
            dest.x = (screenWidth - dest.w) / 2;
            dest.y = (screenHeight - dest.h) / 2;

            SDL_SetRenderDrawColor(gSdlRenderer, 0, 0, 0, 255);
            SDL_RenderClear(gSdlRenderer);
            SDL_RenderCopy(gSdlRenderer, texture, NULL, &dest);
            SDL_RenderPresent(gSdlRenderer);
        }
        ps2crash_heartbeat();

        frame++;

        // Pace to the stream's frame rate, keeping audio topped up.
        int msPerFrame = gState.info->m_MSPerFrame > 0 ? gState.info->m_MSPerFrame : 33;
        Uint32 due = startTicks + (Uint32)(frame * msPerFrame);
        while (true) {
            ps2movie_pump_audio();
            if (ps2movie_skip_requested()) {
                debug_printf("ps2movie: skipped at frame %d\n", frame);
                goto done;
            }
            if ((Sint32)(SDL_GetTicks() - due) >= 0) {
                break;
            }
            SDL_Delay(2);
        }
    }

done:
    debug_printf("ps2movie: finished after %d frames\n", frame);

    ps2movie_halt_ipu();
    MPEG_Destroy();
    debug_printf("ps2movie: IPU halted and decoder destroyed\n");

    if (texture != NULL) {
        SDL_DestroyTexture(texture);
    }

    SDL_SetRenderDrawColor(gSdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(gSdlRenderer);
    SDL_RenderPresent(gSdlRenderer);

    ps2movie_close();

    // Drop the button press that skipped the movie so it doesn't leak into
    // the game.
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

    return true;
}

} // namespace fallout

#endif /* __PS2__ */
