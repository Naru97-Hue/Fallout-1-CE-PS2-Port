// Host-side check of the memory card icon writer (tools/run_smoke.sh).
#include <sys/stat.h>
namespace fallout {
bool ps2card_write_icons(const char* dir, int slot, const char* description);
}
int main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "icon-out";
    mkdir(dir, 0755);
    return fallout::ps2card_write_icons(dir, 0, "Smoke test") ? 0 : 1;
}
