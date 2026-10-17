#ifndef FALLOUT_INT_PS2MOVIE_H_
#define FALLOUT_INT_PS2MOVIE_H_

#ifdef __PS2__

namespace fallout {

// Plays MOVIES/<name>.M2V (+ .PCM) with the PS2's IPU. Blocks until the movie
// ends or the player presses a button. Returns false if no converted movie
// exists, so the caller can use the original MVE player instead.
bool ps2movie_play(const char* mvePath);

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_INT_PS2MOVIE_H_ */
