#ifndef FALLOUT_GAME_PS2AUTOTEST_H_
#define FALLOUT_GAME_PS2AUTOTEST_H_

#ifdef __PS2__

namespace fallout {

// Unattended test run, only when AUTOTEST.TXT is next to MASTER.DAT:
//   slot=3       load this save slot straight from the main menu
//   seconds=60   then play for this long (walking the player around)
//   save=10      quick-save into this slot at the end (exercises saving)
// debug.log gets map load times, memory and frame rate; then the game quits.
// Used with the emulator test (tools/emu); players never have this file.
bool ps2autotest_active();
void ps2autotest_run_from_main_menu();
void ps2autotest_tick(); // once per frame of the game loop
void ps2autotest_watchdog(); // every presented frame

} // namespace fallout

#endif /* __PS2__ */

#endif /* FALLOUT_GAME_PS2AUTOTEST_H_ */
