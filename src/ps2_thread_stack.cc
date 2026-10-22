// PS2: SDL gives a thread a 6 KB stack unless told otherwise, which the
// audio mixer + resampler overflow, corrupting the heap ("Memory header
// stomped", crashes in SDL_UnlockMutex).
//
// - SDL's own threads (audio mixer, timer): the linker option
//   --wrap=SDL_CreateThreadInternal routes them here for a 128 KB stack.
// - The game's threads (audio open, crash watchdog) are made with
//   ps2_thread_create(), which takes an explicit size.
//
// Every thread made either way gets its unused stack filled with a pattern
// at start, so ps2_thread_stack_report() can tell how deep each one has
// actually gone (written to debug.log).

#ifdef __PS2__

#include "ps2_thread_stack.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "plib/gnw/debug.h"

namespace fallout {

#define STACK_PATTERN 0x5AC3A55Cu
#define STACK_MARGIN 4096 // room for what sits above the thread's start (kernel context, SDL frames)
#define MAX_TRACKED 12

typedef struct TrackedStack {
    char name[16];
    uint32_t* low; // deepest painted word
    uint8_t* start; // stack pointer when the thread started
    size_t size;
    volatile int alive;
    int finalUsed; // bytes used, kept once the thread ends
} TrackedStack;

static TrackedStack gStacks[MAX_TRACKED];
static SDL_atomic_t gStackCount;

typedef struct Trampoline {
    int (*fn)(void*);
    void* data;
    size_t size;
    char name[16];
} Trampoline;

static int stack_used(const TrackedStack* s)
{
    uint32_t* p = s->low;
    uint32_t* end = (uint32_t*)(s->start - 512);
    while (p < end && *p == STACK_PATTERN) {
        p++;
    }
    return (int)(s->start - (uint8_t*)p);
}

static int ps2_thread_trampoline(void* arg)
{
    Trampoline t = *(Trampoline*)arg;
    free(arg);

    TrackedStack* slot = NULL;
    int index = SDL_AtomicAdd(&gStackCount, 1);
    if (index < MAX_TRACKED && t.size > 2 * STACK_MARGIN) {
        slot = &gStacks[index];
        uint8_t* sp = (uint8_t*)__builtin_frame_address(0);
        uint32_t* low = (uint32_t*)(((uintptr_t)(sp - t.size + STACK_MARGIN) + 15) & ~(uintptr_t)15);
        uint32_t* high = (uint32_t*)(sp - 512); // well below this frame
        for (uint32_t* p = low; p < high; p++) {
            *p = STACK_PATTERN;
        }
        strncpy(slot->name, t.name, sizeof(slot->name) - 1);
        slot->low = low;
        slot->start = sp;
        slot->size = t.size;
        slot->finalUsed = -1;
        slot->alive = 1;
    }

    int rc = t.fn(t.data);

    if (slot != NULL) {
        slot->finalUsed = stack_used(slot);
        slot->alive = 0; // the stack is freed after this returns
    }
    return rc;
}

static Trampoline* make_trampoline(int (*fn)(void*), const char* name, size_t size, void* data)
{
    Trampoline* t = (Trampoline*)malloc(sizeof(*t));
    if (t != NULL) {
        t->fn = fn;
        t->data = data;
        t->size = size;
        strncpy(t->name, name != NULL ? name : "?", sizeof(t->name) - 1);
        t->name[sizeof(t->name) - 1] = '\0';
    }
    return t;
}

SDL_Thread* ps2_thread_create(int (*fn)(void*), const char* name, size_t stackSize, void* data)
{
    Trampoline* t = make_trampoline(fn, name, stackSize, data);
    if (t == NULL) {
        return SDL_CreateThreadWithStackSize(fn, name, stackSize, data);
    }
    SDL_Thread* thread = SDL_CreateThreadWithStackSize(ps2_thread_trampoline, name, stackSize, t);
    if (thread == NULL) {
        free(t);
    }
    return thread;
}

void ps2_thread_stack_report(const char* tag)
{
    int count = SDL_AtomicGet(&gStackCount);
    if (count > MAX_TRACKED) {
        count = MAX_TRACKED;
    }
    for (int i = 0; i < count; i++) {
        TrackedStack* s = &gStacks[i];
        if (s->start == NULL) {
            continue; // still starting
        }
        int used = s->alive ? stack_used(s) : s->finalUsed;
        debug_printf("stack[%s]: thread %s used %d of %d KB (%s)\n",
            tag, s->name, (used + 1023) / 1024, (int)(s->size / 1024), s->alive ? "running" : "ended");
    }
}

} // namespace fallout

extern "C" {

SDL_Thread* __real_SDL_CreateThreadInternal(int (*fn)(void*), const char* name, size_t stacksize, void* data);

SDL_Thread* __wrap_SDL_CreateThreadInternal(int (*fn)(void*), const char* name, size_t stacksize, void* data)
{
    const size_t kMinStack = 128 * 1024;
    if (stacksize < kMinStack) {
        stacksize = kMinStack;
    }
    fallout::Trampoline* t = fallout::make_trampoline(fn, name, stacksize, data);
    if (t == NULL) {
        return __real_SDL_CreateThreadInternal(fn, name, stacksize, data);
    }
    SDL_Thread* thread = __real_SDL_CreateThreadInternal(fallout::ps2_thread_trampoline, name, stacksize, t);
    if (thread == NULL) {
        free(t);
    }
    return thread;
}

} // extern "C"

#endif /* __PS2__ */
