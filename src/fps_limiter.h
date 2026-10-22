#ifndef FALLOUT_FPS_LIMITER_H_
#define FALLOUT_FPS_LIMITER_H_

namespace fallout {

class FpsLimiter {
public:
    FpsLimiter(unsigned int fps = 60);
    void mark();
    void throttle() const;

private:
    const unsigned int _fps;
    unsigned int _ticks;
#ifdef __PS2__
    // Next frame deadline in ms (fractional): exactly 60 frames a second
    // instead of 16 ms steps (62.5 a second, unevenly spaced).
    mutable double _deadline = 0.0;
#endif
};

} // namespace fallout

#endif /* FALLOUT_FPS_LIMITER_H_ */
