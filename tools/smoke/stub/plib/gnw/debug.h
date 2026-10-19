// Stand-in for the game's debug log so ps2card.cc builds on a PC.
namespace fallout {
static inline void debug_printf(const char*, ...) { }
}
