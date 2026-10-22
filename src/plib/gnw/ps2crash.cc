// Crash and freeze reporting for the PS2 build.
//
// An unhandled EE exception normally just stops the game with no message.
// Here ps2sdk's eedebug library routes them to a handler that saves the
// registers and resumes in ps2crash_report() (normal thread context, on its
// own stack), which prints the exception, the program counter and the last
// recorded steps to the console (PCSX2's log) and debug.log.

#ifdef __PS2__

#include "plib/gnw/ps2crash.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>
#include <ee_debug.h>
#include <kernel.h>

#include "plib/gnw/debug.h"
#include "ps2_thread_stack.h"

namespace fallout {

#define STEP_COUNT 16

typedef struct Step {
    const char* tag;
    int value;
    Uint32 time;
} Step;

static Step gSteps[STEP_COUNT];
static volatile unsigned int gStepIndex = 0;

static EE_RegFrame gCrashFrame;
static volatile int gCrashed = 0;
static unsigned char gCrashStack[32 * 1024] __attribute__((aligned(16)));

static volatile unsigned int gHeartbeat = 0;

void ps2crash_step(const char* tag, int value)
{
    unsigned int index = gStepIndex++ % STEP_COUNT;
    gSteps[index].tag = tag;
    gSteps[index].value = value;
    gSteps[index].time = SDL_GetTicks();
}

void ps2crash_heartbeat()
{
    gHeartbeat++;
}

static void ps2crash_log(const char* format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    // debug_printf also echoes to the console.
    debug_printf("%s", buffer);
}

static void ps2crash_print_steps()
{
    ps2crash_log("last steps (oldest first):\n");
    unsigned int end = gStepIndex;
    unsigned int begin = end > STEP_COUNT ? end - STEP_COUNT : 0;
    for (unsigned int i = begin; i < end; i++) {
        Step* step = &(gSteps[i % STEP_COUNT]);
        if (step->tag != NULL) {
            ps2crash_log("  %8u ms  %s %d\n", (unsigned)step->time, step->tag, step->value);
        }
    }
}

static const char* ps2crash_cause_name(int code)
{
    switch (code) {
    case 1:
        return "TLB modification";
    case 2:
        return "bad memory read (TLB load)";
    case 3:
        return "bad memory write (TLB store)";
    case 4:
        return "unaligned/invalid read address";
    case 5:
        return "unaligned/invalid write address";
    case 6:
        return "instruction bus error";
    case 7:
        return "data bus error";
    case 9:
        return "breakpoint";
    case 10:
        return "illegal instruction";
    case 11:
        return "coprocessor unusable";
    case 12:
        return "arithmetic overflow";
    case 13:
        return "trap";
    default:
        return "unknown";
    }
}

extern "C" void ps2crash_report()
{
    EE_RegFrame* f = &gCrashFrame;
    int code = (f->cause >> 2) & 0x1F;

    ps2crash_log("\n=== CRASH: %s (exception %d) ===\n", ps2crash_cause_name(code), code);
    ps2crash_log("pc=%08x ra=%08x badvaddr=%08x sp=%08x\n", (unsigned)f->epc, (unsigned)f->ra[0], (unsigned)f->badvaddr, (unsigned)f->sp[0]);
    ps2crash_log("a0=%08x a1=%08x a2=%08x a3=%08x v0=%08x v1=%08x\n",
        (unsigned)f->a0[0], (unsigned)f->a1[0], (unsigned)f->a2[0], (unsigned)f->a3[0], (unsigned)f->v0[0], (unsigned)f->v1[0]);
    ps2crash_log("s0=%08x s1=%08x s2=%08x s3=%08x s4=%08x s5=%08x s6=%08x s7=%08x\n",
        (unsigned)f->s0[0], (unsigned)f->s1[0], (unsigned)f->s2[0], (unsigned)f->s3[0],
        (unsigned)f->s4[0], (unsigned)f->s5[0], (unsigned)f->s6[0], (unsigned)f->s7[0]);
    ps2crash_log("thread %d\n", GetThreadId());
    ps2crash_print_steps();
    ps2crash_log("=== end of crash report ===\n");
    debug_flush();

    while (1) {
        SleepThread();
    }
}

// Runs in exception context: save the registers and resume in
// ps2crash_report on a private stack.
static int ps2crash_handler(EE_RegFrame* frame)
{
    if (gCrashed) {
        // A second fault (e.g. inside the report): just stop.
        while (1) {
        }
    }
    gCrashed = 1;

    memcpy(&gCrashFrame, frame, sizeof(gCrashFrame));
    frame->epc = (u32)ps2crash_report;
    frame->sp[0] = (u32)(gCrashStack + sizeof(gCrashStack) - 64);
    frame->sp[1] = 0;
    frame->sp[2] = 0;
    frame->sp[3] = 0;
    return 0;
}

static int ps2crash_watchdog(void* data)
{
    (void)data;
    unsigned int last = gHeartbeat;
    int stalledSeconds = 0;
    bool reported = false;

    while (1) {
        SDL_Delay(1000);
        if (gCrashed) {
            continue;
        }

        unsigned int now = gHeartbeat;
        if (now != last) {
            last = now;
            stalledSeconds = 0;
            if (reported) {
                ps2crash_log("watchdog: frames are being shown again\n");
                reported = false;
            }
            continue;
        }

        stalledSeconds++;
        if (stalledSeconds == 10 && !reported) {
            reported = true;
            ps2crash_log("\n=== WATCHDOG: no new frame for 10 s (frozen or very long load) ===\n");
            ps2crash_print_steps();
            debug_flush();
        }
    }

    return 0;
}

void ps2crash_install()
{
    if (ee_dbg_install(1) != 0) {
        debug_printf("ps2crash: could not install exception handlers\n");
        return;
    }

    static const int kCauses[] = { 1, 2, 3, 4, 5, 6, 7, 10, 12, 13 };
    for (size_t i = 0; i < sizeof(kCauses) / sizeof(kCauses[0]); i++) {
        ee_dbg_set_level1_handler(kCauses[i], ps2crash_handler);
    }

    SDL_Thread* thread = ps2_thread_create(ps2crash_watchdog, "watchdog", 32 * 1024, NULL);
    if (thread != NULL) {
        SDL_DetachThread(thread);
    }

    debug_printf("ps2crash: crash reporting on\n");
}

} // namespace fallout

#endif /* __PS2__ */
