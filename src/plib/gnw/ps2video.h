#ifndef FALLOUT_PLIB_GNW_PS2VIDEO_H_
#define FALLOUT_PLIB_GNW_PS2VIDEO_H_

#ifdef __PS2__

namespace fallout {

enum Ps2VideoMode {
    PS2_VIDEO_480I = 0,
    PS2_VIDEO_480P,
    PS2_VIDEO_720P,
    PS2_VIDEO_1080I,
    PS2_VIDEO_MODE_COUNT,
};

// Reads PS2VIDEO.CFG (if present). Call before the window is created.
void ps2video_load();

int ps2video_mode();
const char* ps2video_name(int mode);

// Framebuffer size used for the current mode (the GS scales it to the TV).
void ps2video_framebuffer_size(int* width, int* height);

// Switches to the next mode, saves it and rebuilds the renderer.
void ps2video_cycle();

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2VIDEO_H_ */
