#include "plib/gnw/svga.h"

#ifdef __PS2__
#include "plib/gnw/ps2osk.h"
#include "plib/gnw/ps2zoom.h"
#include "plib/gnw/ps2video.h"
#include "plib/gnw/ps2gs.h"
#include "plib/gnw/ps2crash.h"
#endif

#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/winmain.h"

namespace fallout {

static bool createRenderer(int width, int height);
static void destroyRenderer();

// screen rect
Rect scr_size;

// 0x6ACA18
ScreenBlitFunc* scr_blit = GNW95_ShowRect;

SDL_Window* gSdlWindow = NULL;
SDL_Surface* gSdlSurface = NULL;
SDL_Renderer* gSdlRenderer = NULL;
SDL_Texture* gSdlTexture = NULL;
SDL_Surface* gSdlTextureSurface = NULL;

// TODO: Remove once migration to update-render cycle is completed.
FpsLimiter sharedFpsLimiter;

#ifdef __PS2__
// Region of the texture surface changed since the last upload. Only this
// part is sent to the GS each present (full-screen uploads are expensive).
static SDL_Rect gPs2Dirty = { 0, 0, 0, 0 };

static void ps2MarkDirty(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (gPs2Dirty.w <= 0 || gPs2Dirty.h <= 0) {
        gPs2Dirty.x = x;
        gPs2Dirty.y = y;
        gPs2Dirty.w = w;
        gPs2Dirty.h = h;
        return;
    }
    int x1 = SDL_min(gPs2Dirty.x, x);
    int y1 = SDL_min(gPs2Dirty.y, y);
    int x2 = SDL_max(gPs2Dirty.x + gPs2Dirty.w, x + w);
    int y2 = SDL_max(gPs2Dirty.y + gPs2Dirty.h, y + h);
    gPs2Dirty.x = x1;
    gPs2Dirty.y = y1;
    gPs2Dirty.w = x2 - x1;
    gPs2Dirty.h = y2 - y1;
}

static void ps2MarkAllDirty()
{
    if (gSdlTextureSurface != NULL) {
        ps2MarkDirty(0, 0, gSdlTextureSurface->w, gSdlTextureSurface->h);
    }
}

// True while the direct GS path (ps2gs.cc) is presenting frames; the EE then
// skips converting 8-bit pixels into the SDL texture surface entirely.
static bool gPs2Direct = false;
#endif

// 0x4CB310
void GNW95_SetPaletteEntries(unsigned char* palette, int start, int count)
{
    if (gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        SDL_Color colors[256];

        if (count != 0) {
            for (int index = 0; index < count; index++) {
                colors[index].r = palette[index * 3] << 2;
                colors[index].g = palette[index * 3 + 1] << 2;
                colors[index].b = palette[index * 3 + 2] << 2;
                colors[index].a = 255;
            }
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, start, count);
#ifdef __PS2__
        ps2gs_mark_palette_range(start, count);
        if (!gPs2Direct) {
            SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
            ps2MarkAllDirty();
        }
#else
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
#endif
    }
}

// 0x4CB568
void GNW95_SetPalette(unsigned char* palette)
{
    if (gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        SDL_Color colors[256];

        for (int index = 0; index < 256; index++) {
            colors[index].r = palette[index * 3] << 2;
            colors[index].g = palette[index * 3 + 1] << 2;
            colors[index].b = palette[index * 3 + 2] << 2;
            colors[index].a = 255;
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);
#ifdef __PS2__
        ps2gs_mark_palette();
        if (!gPs2Direct) {
            SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
            ps2MarkAllDirty();
        }
#else
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
#endif
    }
}

// 0x4CB850
void GNW95_ShowRect(unsigned char* src, unsigned int srcPitch, unsigned int a3, unsigned int srcX, unsigned int srcY, unsigned int srcWidth, unsigned int srcHeight, unsigned int destX, unsigned int destY)
{
    buf_to_buf(src + srcPitch * srcY + srcX, srcWidth, srcHeight, srcPitch, (unsigned char*)gSdlSurface->pixels + gSdlSurface->pitch * destY + destX, gSdlSurface->pitch);

    SDL_Rect srcRect;
    srcRect.x = destX;
    srcRect.y = destY;
    srcRect.w = srcWidth;
    srcRect.h = srcHeight;

    SDL_Rect destRect;
    destRect.x = destX;
    destRect.y = destY;
#ifdef __PS2__
    ps2gs_mark_dirty(destX, destY, srcWidth, srcHeight);
    if (!gPs2Direct) {
        SDL_BlitSurface(gSdlSurface, &srcRect, gSdlTextureSurface, &destRect);
        ps2MarkDirty(destX, destY, srcWidth, srcHeight);
    }
#else
    SDL_BlitSurface(gSdlSurface, &srcRect, gSdlTextureSurface, &destRect);
#endif
}

bool svga_init(VideoOptions* video_options)
{
#ifndef __PS2__
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        return false;
    }

#ifdef __PS2__
    Uint32 windowFlags = SDL_WINDOW_FULLSCREEN;
#else
    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
#endif

    if (video_options->fullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN;
    }

#ifdef __PS2__
    // The window size is the GS framebuffer size for the chosen TV mode; the
    // renderer scales the game's 640x480 into it.
    ps2video_load();
    int ps2WindowWidth;
    int ps2WindowHeight;
    ps2video_framebuffer_size(&ps2WindowWidth, &ps2WindowHeight);
    gSdlWindow = SDL_CreateWindow(GNW95_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        ps2WindowWidth,
        ps2WindowHeight,
        windowFlags);
#else
    gSdlWindow = SDL_CreateWindow(GNW95_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        video_options->width * video_options->scale,
        video_options->height * video_options->scale,
        windowFlags);
#endif
    if (gSdlWindow == NULL) {
        return false;
    }

    if (!createRenderer(video_options->width, video_options->height)) {
        destroyRenderer();

        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;

        return false;
    }

    gSdlSurface = SDL_CreateRGBSurface(0,
        video_options->width,
        video_options->height,
        8,
        0,
        0,
        0,
        0);
    if (gSdlSurface == NULL) {
        destroyRenderer();

        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;
    }

    SDL_Color colors[256];
    for (int index = 0; index < 256; index++) {
        colors[index].r = index;
        colors[index].g = index;
        colors[index].b = index;
        colors[index].a = 255;
    }

    SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);

    scr_size.ulx = 0;
    scr_size.uly = 0;
    scr_size.lrx = video_options->width - 1;
    scr_size.lry = video_options->height - 1;

    mouse_blit_trans = NULL;
    scr_blit = GNW95_ShowRect;
    mouse_blit = GNW95_ShowRect;

    return true;
}

void svga_exit()
{
    destroyRenderer();

    if (gSdlWindow != NULL) {
        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

int screenGetWidth()
{
    // TODO: Make it on par with _xres;
    return rectGetWidth(&scr_size);
}

int screenGetHeight()
{
    // TODO: Make it on par with _yres.
    return rectGetHeight(&scr_size);
}

static bool createRenderer(int width, int height)
{
    gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, 0);
    if (gSdlRenderer == NULL) {
        return false;
    }

    if (SDL_RenderSetLogicalSize(gSdlRenderer, width, height) != 0) {
        return false;
    }

#ifdef __PS2__
    // ABGR1555 is native to SDL's PS2 renderer: no per-frame CPU conversion,
    // half the upload size. Fallout is 8-bit paletted (6 bits per channel), so
    // 5 bits per channel is barely distinguishable.
    gSdlTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (gSdlTexture != NULL) {
        SDL_SetTextureBlendMode(gSdlTexture, SDL_BLENDMODE_NONE);
    }
#else
    gSdlTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, width, height);
#endif
    if (gSdlTexture == NULL) {
        return false;
    }

    Uint32 format;
    if (SDL_QueryTexture(gSdlTexture, &format, NULL, NULL, NULL) != 0) {
        return false;
    }

    gSdlTextureSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, SDL_BITSPERPIXEL(format), format);
    if (gSdlTextureSurface == NULL) {
        return false;
    }

#ifdef __PS2__
    // Fresh texture: refill it from the 8-bit screen on the next present.
    if (gSdlSurface != NULL) {
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
    }
    ps2MarkAllDirty();
#endif

    return true;
}

static void destroyRenderer()
{
    if (gSdlTextureSurface != NULL) {
        SDL_FreeSurface(gSdlTextureSurface);
        gSdlTextureSurface = NULL;
    }

    if (gSdlTexture != NULL) {
        SDL_DestroyTexture(gSdlTexture);
        gSdlTexture = NULL;
    }

    if (gSdlRenderer != NULL) {
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlRenderer = NULL;
    }
}

void handleWindowSizeChanged()
{
    destroyRenderer();
    createRenderer(screenGetWidth(), screenGetHeight());
}

void renderPresent()
{
#ifdef __PS2__
    ps2crash_heartbeat();
    ps2video_trial_poll();
    if (ps2gs_present(gSdlRenderer, gSdlSurface, screenGetWidth(), screenGetHeight())) {
        gPs2Direct = true;
        if (ps2osk_is_visible()) {
            ps2crash_step("osk render", 0);
        }
        ps2osk_render(gSdlRenderer, screenGetWidth(), screenGetHeight());
        SDL_RenderPresent(gSdlRenderer);
        return;
    }

    if (gPs2Direct) {
        // Falling back to the SDL path: bring its texture surface up to date.
        gPs2Direct = false;
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
        ps2MarkAllDirty();
    }

    if (gPs2Dirty.w > 0 && gPs2Dirty.h > 0) {
        int bpp = gSdlTextureSurface->format->BytesPerPixel;
        const unsigned char* pixels = (const unsigned char*)gSdlTextureSurface->pixels
            + gPs2Dirty.y * gSdlTextureSurface->pitch + gPs2Dirty.x * bpp;
        SDL_UpdateTexture(gSdlTexture, &gPs2Dirty, pixels, gSdlTextureSurface->pitch);
        gPs2Dirty.w = 0;
        gPs2Dirty.h = 0;
    }
#else
    SDL_UpdateTexture(gSdlTexture, NULL, gSdlTextureSurface->pixels, gSdlTextureSurface->pitch);
#endif
    SDL_RenderClear(gSdlRenderer);
#ifdef __PS2__
    if (!ps2zoom_render(gSdlRenderer, gSdlTexture, screenGetWidth(), screenGetHeight())) {
        SDL_RenderCopy(gSdlRenderer, gSdlTexture, NULL, NULL);
    }
#else
    SDL_RenderCopy(gSdlRenderer, gSdlTexture, NULL, NULL);
#endif
#ifdef __PS2__
    ps2osk_render(gSdlRenderer, screenGetWidth(), screenGetHeight());
#endif
    SDL_RenderPresent(gSdlRenderer);
}

} // namespace fallout
