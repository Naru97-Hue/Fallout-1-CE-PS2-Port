// Host-side check of the mod archive reader (tools/run_smoke.sh).
#include <stdlib.h>
#include <string.h>
#include "plib/gnw/ps2modarc.h"

struct Shim { void* c; int (*r)(void*, char*, int); long (*s)(void*, long, int); int (*cl)(void*); };
static ssize_t sh_read(void* p, char* b, size_t n) { Shim* s = (Shim*)p; return s->r(s->c, b, (int)n); }
static int sh_seek(void* p, off64_t* o, int w) { Shim* s = (Shim*)p; long r = s->s(s->c, (long)*o, w); if (r < 0) return -1; *o = r; return 0; }
static int sh_close(void* p) { Shim* s = (Shim*)p; int r = s->cl(s->c); free(s); return r; }
FILE* funopen_shim(void* c, int (*r)(void*, char*, int), void*, long (*sk)(void*, long, int), int (*cl)(void*))
{
    Shim* s = (Shim*)malloc(sizeof(Shim)); s->c = c; s->r = r; s->s = sk; s->cl = cl;
    cookie_io_functions_t f = { sh_read, NULL, sh_seek, sh_close };
    return fopencookie(s, "r", f);
}

static int check(fallout::ModPack* pack, const char* path, const char* expect)
{
    FILE* f = fallout::modpack_fopen(pack, path);
    if (f == NULL) { printf("FAIL: %s missing\n", path); return 1; }
    char buf[256] = { 0 };
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    fread(buf, 1, sizeof(buf) - 1, f); fclose(f);
    if ((size_t)n != strlen(expect) || strcmp(buf, expect) != 0) { printf("FAIL: %s = '%s'\n", path, buf); return 1; }
    return 0;
}

int main(int argc, char** argv)
{
    int bad = 0;
    fallout::ModPack* zip = fallout::modpack_open(argv[1]);
    if (zip == NULL) { printf("FAIL: zip\n"); return 1; }
    bad += check(zip, "art/critters/test.frm", "deflated critter art, deflated critter art, deflated critter art");
    bad += check(zip, "TEXT\\ENGLISH\\GAME\\STORED.MSG", "stored");
    unsigned char* d; size_t n;
    if (!fallout::modpack_read_named(zip, "MOD.TXT", &d, &n)) { printf("FAIL: MOD.TXT\n"); bad++; } else free(d);
    fallout::modpack_close(zip);

    fallout::ModPack* dat = fallout::modpack_open(argv[2]);
    if (dat == NULL) { printf("FAIL: dat\n"); return 1; }
    bad += check(dat, "maps/test.map", "plain stored map");
    bad += check(dat, "scripts/lzss.int", "ABCDEFGHABCDEFGHrawtail");
    fallout::modpack_close(dat);
    if (bad == 0) printf("OK: zip (stored, deflate, DATA/ prefix) and DAT (stored, LZSS) mods read\n");
    return bad != 0;
}
