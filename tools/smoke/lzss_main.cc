// Decodes LZSS blocks with the game's decoder (tools/run_smoke.sh compares the
// PS2 fast path against the original one).
#include <stdio.h>
#include <stdlib.h>
#include "plib/db/lzss.h"
int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* out = (unsigned char*)calloc(1, 1 << 20);
    int total = 0;
    // decode as several blocks (like chunked entries), sizes from argv
    for (int i = 2; i < argc; i++) total += fallout::lzss_decode_to_buf(f, out + total, atoi(argv[i]));
    fwrite(out, 1, total, stdout); return 0; }
