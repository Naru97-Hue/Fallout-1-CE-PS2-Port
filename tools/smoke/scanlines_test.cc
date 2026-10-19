// Scanline layout check (tools/run_smoke.sh): progressive modes darken every
// other picture line; interlaced modes give both TV fields the same amount
// of dark lines (no 30 Hz flicker).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "plib/gnw/ps2scanlines.h"

static int check(const char* name, int fbHeight, int pictureHeight, bool interlaced)
{
    float scaleY = (float)fbHeight / pictureHeight;
    static float dark[2048];
    memset(dark, 0, sizeof(dark));
    int bands = ps2scanlines_bands(fbHeight, scaleY, interlaced, [&](float y0, float y1) {
        // Bands are whole TV lines: [y0, y1).
        for (int y = (int)y0; y < fbHeight && y < y1; y++) {
            if (y >= 0) dark[y] = 1;
        }
    });
    int total = 0, even = 0, odd = 0, run = 0, maxRun = 0;
    for (int y = 0; y < fbHeight; y++) {
        total += dark[y] > 0;
        (y % 2 ? odd : even) += dark[y] > 0;
        run = dark[y] > 0 ? run + 1 : 0;
        if (run > maxRun) maxRun = run;
    }
    double share = (double)total / fbHeight;
    bool ok = bands > 0 && share > 0.30 && share < 0.70 && maxRun <= (interlaced ? 2 : (int)ceilf(scaleY) + 1);
    if (interlaced) ok = ok && abs(even - odd) <= 1;
    printf("%s %s: %d bands, %.0f%% of lines dark, fields %d/%d\n", ok ? "OK:" : "FAIL:", name, bands, share * 100, even, odd);
    return ok ? 0 : 1;
}

int main()
{
    int bad = 0;
    bad += check("480p", 480, 480, false);
    bad += check("720p", 720, 480, false);
    bad += check("480i", 448, 480, true);
    bad += check("1080i", 540, 480, true);
    return bad != 0;
}
