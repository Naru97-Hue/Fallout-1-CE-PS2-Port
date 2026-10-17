// Output resolution selection for the PS2 build: 480i, 480p, 720p, 1080i.
//
// The game always renders 640x480. SDL's PS2 renderer hard-codes NTSC 480i,
// so we intercept its call to gsKit_init_screen (linker --wrap) and set the
// chosen video mode first. The framebuffer stays small and the GS hardware
// magnifies it horizontally (MagH) to the TV resolution, which keeps the whole
// thing inside the GS's 4 MB of VRAM:
//
//   480i   640x448  24-bit  interlaced    (SDL default)
//   480p   640x480  24-bit  progressive
//   720p   640x720  16-bit  progressive   (GS doubles width -> 1280)
//   1080i  640x540  16-bit  interlaced    (GS triples width -> 1920, line-doubled)
//
// Changing mode resizes the SDL window to the new framebuffer size and
// rebuilds the renderer, which re-runs gsKit_init_screen with the new mode.

#ifdef __PS2__

#include "plib/gnw/ps2video.h"

#include <stdio.h>

#include <SDL.h>
#include <gsKit.h>

#include "plib/gnw/debug.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/ps2osk.h"
#include "plib/gnw/svga.h"

namespace fallout {

static const char* kConfigFile = "PS2VIDEO.CFG";
static int gMode = PS2_VIDEO_480I;

static const char* kNames[PS2_VIDEO_MODE_COUNT] = { "480i", "480p", "720p", "1080i" };

const char* ps2video_name(int mode)
{
    if (mode < 0 || mode >= PS2_VIDEO_MODE_COUNT) {
        return "?";
    }
    return kNames[mode];
}

int ps2video_mode()
{
    return gMode;
}

void ps2video_load()
{
    FILE* stream = fopen(kConfigFile, "rb");
    if (stream == NULL) {
        return;
    }

    char text[16] = { 0 };
    fread(text, 1, sizeof(text) - 1, stream);
    fclose(stream);

    for (int mode = 0; mode < PS2_VIDEO_MODE_COUNT; mode++) {
        if (strncmp(text, kNames[mode], strlen(kNames[mode])) == 0) {
            gMode = mode;
        }
    }
    debug_printf("ps2video: starting in %s\n", kNames[gMode]);
}

static void ps2video_save()
{
    FILE* stream = fopen(kConfigFile, "wb");
    if (stream != NULL) {
        fputs(kNames[gMode], stream);
        fclose(stream);
    }
}

void ps2video_framebuffer_size(int* width, int* height)
{
    *width = 640;
    switch (gMode) {
    case PS2_VIDEO_480P:
        *height = 480;
        break;
    case PS2_VIDEO_720P:
        *height = 720;
        break;
    case PS2_VIDEO_1080I:
        *height = 540;
        break;
    default:
        *height = 448;
        break;
    }
}

void ps2video_cycle()
{
    gMode = (gMode + 1) % PS2_VIDEO_MODE_COUNT;
    ps2video_save();

    int width;
    int height;
    ps2video_framebuffer_size(&width, &height);
    debug_printf("ps2video: switching to %s (%dx%d framebuffer)\n", kNames[gMode], width, height);

    // New window size -> renderer rebuilt -> gsKit_init_screen with new mode.
    SDL_SetWindowSize(gSdlWindow, width, height);
    handleWindowSizeChanged();
    SDL_FlushEvent(SDL_WINDOWEVENT);
    win_refresh_all(&scr_size);

    char message[48];
    snprintf(message, sizeof(message), "VIDEO: %s", kNames[gMode]);
    ps2osk_show_message(message, 2000);
    renderPresent();
}

} // namespace fallout

namespace fallout {
// The GS state SDL's renderer is using; captured each time the screen is
// (re)initialised so the direct GS present path can draw with it.
GSGLOBAL* gPs2GsGlobal = NULL;
// Bumped whenever GS memory is re-initialised (TV mode change); our own GS
// memory reservations are redone when it changes.
int gPs2GsGeneration = 0;
} // namespace fallout

extern "C" {

void __real_gsKit_init_screen(GSGLOBAL* gsGlobal);

void __wrap_gsKit_init_screen(GSGLOBAL* gsGlobal)
{
    using namespace fallout;

    int width;
    int height;
    ps2video_framebuffer_size(&width, &height);

    gsGlobal->Width = width;
    gsGlobal->Height = height;

    switch (gMode) {
    case PS2_VIDEO_480P:
        gsGlobal->Mode = GS_MODE_DTV_480P;
        gsGlobal->Interlace = GS_NONINTERLACED;
        gsGlobal->Field = GS_FRAME;
        gsGlobal->PSM = GS_PSM_CT24;
        break;
    case PS2_VIDEO_720P:
        gsGlobal->Mode = GS_MODE_DTV_720P;
        gsGlobal->Interlace = GS_NONINTERLACED;
        gsGlobal->Field = GS_FRAME;
        gsGlobal->PSM = GS_PSM_CT16;
        break;
    case PS2_VIDEO_1080I:
        gsGlobal->Mode = GS_MODE_DTV_1080I;
        gsGlobal->Interlace = GS_INTERLACED;
        gsGlobal->Field = GS_FIELD;
        gsGlobal->PSM = GS_PSM_CT16;
        break;
    default:
        gsGlobal->Mode = GS_MODE_NTSC;
        gsGlobal->Interlace = GS_INTERLACED;
        gsGlobal->Field = GS_FIELD;
        gsGlobal->PSM = GS_PSM_CT24;
        break;
    }

    __real_gsKit_init_screen(gsGlobal);
    gPs2GsGlobal = gsGlobal;
    gPs2GsGeneration++;
}

} // extern "C"

#endif /* __PS2__ */
