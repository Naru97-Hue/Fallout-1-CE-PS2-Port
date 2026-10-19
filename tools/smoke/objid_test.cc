// new_obj_id: the original loop vs the PS2 version (same logic as scripts.cc), on random worlds.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <chrono>
static std::vector<int> world; static size_t it;
static int* first() { it = 0; return world.empty() ? NULL : &world[0]; }
static int* next() { return ++it < world.size() ? &world[it] : NULL; }
static int orig(int& cur) { int* p; do { cur++; for (p = first(); p; p = next()) if (cur == *p) break; } while (p); cur++; return cur; }
static int fast(int& cur) {
    { int want = cur + 1; int* p; for (p = first(); p; p = next()) if (*p == want) break; if (!p) { cur = want + 1; return cur; } }
    enum { WINDOW = 32768 }; static unsigned taken[WINDOW / 32]; memset(taken, 0, sizeof(taken));
    int base = cur + 1;
    for (int* p = first(); p; p = next()) { unsigned o = (unsigned)(*p - base); if (o < WINDOW) taken[o >> 5] |= 1u << (o & 31); }
    for (unsigned o = 0; o < WINDOW; o++) if (!(taken[o >> 5] & (1u << (o & 31)))) { cur = base + o; cur++; return cur; }
    return -1;
}
int main() {
    srand(3); int bad = 0; double to = 0, tf = 0, lo = 0, lf = 0;
    for (int t = 0; t < 200; t++) {
        world.clear(); int n = 500 + rand() % 3000;
        int start = rand() % 2000;
        bool load = t % 2;
        for (int i = 0; i < n; i++) world.push_back(load ? 5 + i : (rand() % 3 ? start + i : rand() % 20000 - 50));
        int c1 = load ? 4 : rand() % 3000, c2 = c1;
        for (int k = 0; k < 50; k++) {
            auto a = std::chrono::steady_clock::now(); int r1 = orig(c1);
            auto b = std::chrono::steady_clock::now(); int r2 = fast(c2);
            auto c = std::chrono::steady_clock::now();
            double da = std::chrono::duration<double>(b - a).count(), db = std::chrono::duration<double>(c - b).count();
            if (load) { lo += da; lf += db; } else { to += da; tf += db; }
            if (r1 != r2 || c1 != c2) bad++;
            world.push_back(r1); // the new object joins the world
        }
    }
    printf("new_obj_id: %d mismatches in 10000 calls\n", bad);
    printf("  scattered ids (normal play): original %.1f ms, new %.1f ms\n", to * 1000, tf * 1000);
    printf("  map load (ids taken from the start): original %.1f ms, new %.1f ms\n", lo * 1000, lf * 1000);
    return bad != 0;
}
