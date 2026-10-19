// Stand-in for SDL so PS2-only files build on a PC for the smoke tests.
#pragma once
typedef struct SDL_mutex SDL_mutex;
static inline SDL_mutex* SDL_CreateMutex(void) { return 0; }
static inline int SDL_LockMutex(SDL_mutex*) { return 0; }
static inline int SDL_UnlockMutex(SDL_mutex*) { return 0; }
