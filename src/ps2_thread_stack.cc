// PS2: SDL creates its internal threads (audio mixer, etc.) with a 6 KB
// default stack, which the audio mixer + resampler overflow, corrupting the
// heap ("Memory header stomped", crashes in SDL_UnlockMutex). The linker
// option --wrap=SDL_CreateThreadInternal routes those calls here so we can
// enforce a sane minimum stack size.

#ifdef __PS2__

#include <stddef.h>

struct SDL_Thread;

extern "C" {

SDL_Thread* __real_SDL_CreateThreadInternal(int (*fn)(void*), const char* name, size_t stacksize, void* data);

SDL_Thread* __wrap_SDL_CreateThreadInternal(int (*fn)(void*), const char* name, size_t stacksize, void* data)
{
    const size_t kMinStack = 128 * 1024;
    if (stacksize < kMinStack) {
        stacksize = kMinStack;
    }
    return __real_SDL_CreateThreadInternal(fn, name, stacksize, data);
}

} // extern "C"

#endif /* __PS2__ */
