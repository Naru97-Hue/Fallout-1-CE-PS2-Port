#include "fps_limiter.h"

#include <SDL.h>

namespace fallout {

FpsLimiter::FpsLimiter(unsigned int fps)
    : _fps(fps)
    , _ticks(0)
{
}

void FpsLimiter::mark()
{
    _ticks = SDL_GetTicks();
}

void FpsLimiter::throttle() const
{
#ifdef __PS2__
    // Even frame spacing: the controller is read once at the start of each
    // frame, so evenly spaced frames mean evenly spaced (and predictable)
    // input reads.
    double frame = 1000.0 / _fps;
    double now = (double)SDL_GetTicks();
    _deadline += frame;
    if (_deadline < now - frame || _deadline > now + 2 * frame) {
        _deadline = now; // fell behind (loading) or first call: start over
        return;
    }
    if (_deadline > now) {
        SDL_Delay((Uint32)(_deadline - now));
    }
    return;
#endif
    if (1000 / _fps > SDL_GetTicks() - _ticks) {
        SDL_Delay(1000 / _fps - (SDL_GetTicks() - _ticks));
    }
}

} // namespace fallout
