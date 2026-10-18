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

// Switches to the next mode as a trial (L1 + R1 + Select).
void ps2video_cycle();

// Switches to a given mode (saved, applied immediately).
void ps2video_set_mode(int mode);

// Trial switch: the new mode is undone after 6 seconds unless confirmed
// (a TV that can't show it would stay black). quiet: the caller shows the
// question itself; otherwise an on-screen message asks for X / O.
void ps2video_start_trial(int mode, bool quiet);
bool ps2video_trial_active();
bool ps2video_trial_quiet();
int ps2video_trial_seconds_left();
void ps2video_trial_answer(bool keep);
// Call every frame: reverts when time runs out, updates the message.
void ps2video_trial_poll();

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2VIDEO_H_ */
