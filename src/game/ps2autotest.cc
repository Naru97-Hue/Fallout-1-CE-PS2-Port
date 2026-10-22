// Unattended test run (see ps2autotest.h).

#ifdef __PS2__

#include "game/ps2autotest.h"

#include "ps2_thread_stack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "game/anim.h"
#include "game/art.h"
#include "game/game.h"
#include "game/gsound.h"
#include "game/loadsave.h"
#include "game/map.h"
#include "game/object.h"
#include "game/tile.h"
#include "plib/gnw/debug.h"

namespace fallout {

static int gChecked = 0; // 0 unknown, 1 active, -1 off
static int gSlot = 1;
static int gSeconds = 60;
static int gSaveSlot = 0;
static int gArtCacheMb = 0;
static char gBigMap[16] = "";
static char gBootPad[64] = "";
static int gFadeTest = 1;
static Uint32 gStart = 0;
static Uint32 gNextWalk = 0;
static int gWalks = 0;
static int gFadeStep = 0;

bool ps2autotest_active()
{
    if (gChecked == 0) {
        gChecked = -1;
        FILE* stream = fopen("AUTOTEST.TXT", "rb");
        if (stream == NULL) {
            stream = fopen("autotest.txt", "rb");
        }
        if (stream != NULL) {
            char line[64];
            while (fgets(line, sizeof(line), stream) != NULL) {
                sscanf(line, "slot=%d", &gSlot);
                sscanf(line, "seconds=%d", &gSeconds);
                sscanf(line, "save=%d", &gSaveSlot);
                sscanf(line, "artcache=%d", &gArtCacheMb);
                sscanf(line, "bigmap=%15s", gBigMap);
                sscanf(line, "bootpad=%63s", gBootPad);
                sscanf(line, "fadetest=%d", &gFadeTest);
            }
            fclose(stream);
            gChecked = 1;
            debug_printf("autotest: slot %d, %d s, save slot %d\n", gSlot, gSeconds, gSaveSlot);
        }
    }
    return gChecked == 1;
}

int ps2autotest_art_cache_mb()
{
    return ps2autotest_active() ? gArtCacheMb : 0;
}

const char* ps2autotest_boot_pad()
{
    return ps2autotest_active() && gBootPad[0] != '\0' ? gBootPad : NULL;
}

const char* ps2autotest_big_map()
{
    return ps2autotest_active() && gBigMap[0] != '\0' ? gBigMap : NULL;
}

void ps2autotest_run_from_main_menu()
{
    Uint32 t0 = SDL_GetTicks();
    int rc = LoadGameAutotest(gSlot - 1);
    debug_printf("autotest: load slot %d -> %d in %u ms (map %s)\n", gSlot, rc, SDL_GetTicks() - t0, map_data.name);
    ps2_log_memory("autotest after load");
    art_cache_log_stats("after load");
    gStart = SDL_GetTicks();
    gNextWalk = gStart + 1000;
}

// Called every frame from renderPresent: if the run is stuck somewhere the
// game loop isn't (world map, a dialog), still end it.
void ps2autotest_watchdog()
{
    if (gChecked != 1 || gStart == 0) {
        return;
    }
    if (SDL_GetTicks() - gStart >= (Uint32)(gSeconds + 60) * 1000) {
        debug_printf("autotest: stuck outside the map view (world map or a dialog?) - ending\n");
        ps2_log_memory("autotest end");
        ps2_thread_stack_report("autotest end");
        exit(0);
    }
}

void ps2autotest_tick()
{
    if (gChecked != 1 || gStart == 0) {
        return;
    }
    Uint32 now = SDL_GetTicks();

    // Sound fade test: background music "nar_1" (the test setup copies a
    // long narrator sound into the music folder), started, faded out and
    // freed - three times. Fades
    // finish on the main thread (sound.cc).
    Uint32 elapsed = now - gStart;
    if (gFadeTest && gFadeStep < 6 && elapsed >= 3000 + (Uint32)gFadeStep * 4000) {
        if (gFadeStep % 2 == 0) {
            int rc = gsound_background_play("nar_1", 12, 14, 16);
            debug_printf("autotest: fade test %d - streaming sound started (%d)\n", gFadeStep / 2 + 1, rc);
        } else {
            gsound_background_fade_set(1);
            gsound_background_stop();
            debug_printf("autotest: fade test %d - fading out\n", gFadeStep / 2 + 1);
        }
        gFadeStep++;
    }

    // Walk somewhere new every 3 seconds: scrolls the map and loads art.
    // Destinations near an exit grid are skipped (walking onto one leaves
    // the map for the world map, which would end the test early).
    if ((Sint32)(now - gNextWalk) >= 0) {
        gNextWalk = now + 3000;
        int dest = -1;
        for (int attempt = 0; attempt < 20 && dest == -1; attempt++) {
            int x = obj_dude->tile % 200 + (rand() % 17) - 8;
            int y = obj_dude->tile / 200 + (rand() % 17) - 8;
            if (x < 0 || x > 199 || y < 0 || y > 199) {
                continue;
            }
            int candidate = y * 200 + x;
            bool nearExit = false;
            for (Object* obj = obj_find_first_at(obj_dude->elevation); obj != NULL; obj = obj_find_next_at()) {
                if (obj->pid >= 0x5000010 && obj->pid <= 0x5000017
                    && (tile_dist(obj->tile, candidate) < 12 || tile_dist(obj->tile, obj_dude->tile) < 12)) {
                    nearExit = true;
                    break;
                }
            }
            if (!nearExit) {
                dest = candidate;
            }
        }
        if (dest == -1) {
            return;
        }
        register_begin(ANIMATION_REQUEST_RESERVED);
        register_object_move_to_tile(obj_dude, dest, obj_dude->elevation, -1, 0);
        register_end();
        gWalks++;
        if (gWalks % 5 == 0) {
            debug_printf("autotest: walk %d, player at tile %d\n", gWalks, obj_dude->tile);
            ps2_log_memory("autotest walking");
            art_cache_log_stats("walking");
        }
    }

    if (now - gStart >= (Uint32)gSeconds * 1000) {
        if (gSaveSlot > 0) {
            Uint32 t0 = SDL_GetTicks();
            int rc = SaveGameAutotest(gSaveSlot - 1);
            debug_printf("autotest: quick save to slot %d -> %d in %u ms\n", gSaveSlot, rc, SDL_GetTicks() - t0);
        }
        ps2_log_memory("autotest end");
        ps2_thread_stack_report("autotest end");
        art_cache_log_stats("end");
        art_cache_map_changed("NOTBIG.MAP"); // as if the next map were a normal one
        art_cache_log_stats("after shrinking");
        ps2_log_memory("autotest after shrinking");
        debug_printf("autotest: done after %d walks\n", gWalks);
        gStart = 0;
        game_user_wants_to_quit = 2;
    }
}

} // namespace fallout

#endif /* __PS2__ */
