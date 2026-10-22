#ifndef FALLOUT_PS2_THREAD_STACK_H_
#define FALLOUT_PS2_THREAD_STACK_H_

#ifdef __PS2__

#include <stddef.h>

#include <SDL.h>

namespace fallout {

// Use instead of SDL_CreateThread, which gives a 6 KB stack on PS2.
SDL_Thread* ps2_thread_create(int (*fn)(void*), const char* name, size_t stackSize, void* data);

// Writes how much stack each thread has used so far to debug.log.
void ps2_thread_stack_report(const char* tag);

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PS2_THREAD_STACK_H_ */
