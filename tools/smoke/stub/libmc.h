// Stand-in for the PS2 libmc header so ps2card.cc builds on a PC.
#define sceMcTypePS2 2
static inline int mcGetInfo(int, int, int*, int*, int*) { return -1; }
static inline int mcSync(int, int*, int*) { return 0; }
