#ifndef FALLOUT_PLIB_GNW_DEBUG_H_
#define FALLOUT_PLIB_GNW_DEBUG_H_

namespace fallout {

typedef int(DebugFunc)(char* string);

void GNW_debug_init();
void debug_register_mono();
void debug_register_log(const char* fileName, const char* mode);
void debug_register_screen();
void debug_register_env();
void debug_register_func(DebugFunc* func);
int debug_printf(const char* format, ...);
#ifdef __PS2__
void ps2_log_memory(const char* tag);
long ps2_free_memory();
void debug_flush();
#endif
int debug_puts(char* string);
void debug_clear();

} // namespace fallout

#endif /* FALLOUT_PLIB_GNW_DEBUG_H_ */
