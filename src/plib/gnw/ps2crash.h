#ifndef FALLOUT_PLIB_GNW_PS2CRASH_H_
#define FALLOUT_PLIB_GNW_PS2CRASH_H_

#ifdef __PS2__

namespace fallout {

// Catches EE CPU exceptions (bad memory access, illegal instruction...) and
// writes a report to the console and debug.log instead of hanging silently.
// Also starts a watchdog that reports when no frame has been shown for a
// while (a freeze).
void ps2crash_install();

// Records a step in a small ring buffer that the reports print, so we can
// see what the game was doing. `tag` must be a string literal.
void ps2crash_step(const char* tag, int value);

// Called once per presented frame (watchdog heartbeat).
void ps2crash_heartbeat();

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_PLIB_GNW_PS2CRASH_H_ */
