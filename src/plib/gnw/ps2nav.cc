// Console-style navigation for the DualShock.
//
// The D-pad jumps the cursor to the next thing you can click in that
// direction instead of nudging it: buttons in menus and dialogs, and on the
// map, people, items, doors, stairs and ladders, plus the interface bar's
// buttons. Cross/Circle then click as usual, so every screen keeps working
// without being rewritten for a pad. The left stick still moves the cursor
// freely for anything the D-pad can't reach.
//
// Options > CONTROLS > D-pad switches back to arrow keys (scroll the map).

#ifdef __PS2__

#include "plib/gnw/ps2nav.h"

#include <stdio.h>
#include <stdlib.h>

#include "game/map.h"
#include "game/object.h"
#include "game/proto.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/gnw_types.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/ps2controls.h"

namespace fallout {

#define MAX_TARGETS 512

typedef struct Target {
    int x;
    int y;
} Target;

static Target gTargets[MAX_TARGETS];
static int gTargetCount = 0;

bool ps2nav_enabled()
{
    return ps2controls_dpad_navigation();
}

static void ps2nav_add(int win, int x, int y)
{
    if (gTargetCount >= MAX_TARGETS) {
        return;
    }
    // Only points you can actually see (not under another window).
    if (win_top_visible(x, y) != win) {
        return;
    }
    gTargets[gTargetCount].x = x;
    gTargets[gTargetCount].y = y;
    gTargetCount++;
}

static void ps2nav_add_buttons(int win)
{
    Window* w = GNW_find(win);
    if (w == NULL || (w->flags & WINDOW_HIDDEN) != 0) {
        return;
    }
    for (Button* button = w->buttonListHead; button != NULL; button = button->next) {
        if ((button->flags & BUTTON_FLAG_DISABLED) != 0) {
            continue;
        }
        int width = button->rect.lrx - button->rect.ulx + 1;
        int height = button->rect.lry - button->rect.uly + 1;
        // Tiny or window-sized regions are hit areas, not buttons.
        if (width < 4 || height < 4 || (width >= w->width - 4 && height >= w->height - 4)) {
            continue;
        }
        int x = w->rect.ulx + (button->rect.ulx + button->rect.lrx) / 2;
        int y = w->rect.uly + (button->rect.uly + button->rect.lry) / 2;
        ps2nav_add(win, x, y);
    }
}

static bool ps2nav_is_target_object(Object* obj)
{
    if (obj == obj_dude || (obj->flags & OBJECT_HIDDEN) != 0) {
        return false;
    }
    switch (PID_TYPE(obj->pid)) {
    case OBJ_TYPE_CRITTER:
    case OBJ_TYPE_ITEM:
        return true;
    case OBJ_TYPE_SCENERY: {
        Proto* proto;
        if (proto_ptr(obj->pid, &proto) == -1) {
            return false;
        }
        return proto->scenery.type != SCENERY_TYPE_GENERIC;
    }
    }
    return false;
}

static void ps2nav_add_objects()
{
    if (display_win <= 0) {
        return;
    }
    Window* w = GNW_find(display_win);
    if (w == NULL || (w->flags & WINDOW_HIDDEN) != 0) {
        return;
    }
    for (Object* obj = obj_find_first_at(map_elevation); obj != NULL; obj = obj_find_next_at()) {
        if (!ps2nav_is_target_object(obj)) {
            continue;
        }
        Rect rect;
        obj_bound(obj, &rect);
        if (rect.lrx < w->rect.ulx || rect.ulx > w->rect.lrx || rect.lry < w->rect.uly || rect.uly > w->rect.lry) {
            continue;
        }
        // A bit below the middle: on the body rather than above the head.
        int x = (rect.ulx + rect.lrx) / 2;
        int y = rect.uly + (rect.lry - rect.uly) * 3 / 5;
        ps2nav_add(display_win, x, y);
    }
}

static void ps2nav_collect()
{
    gTargetCount = 0;

    int ids[64];
    int count = win_list_visible(ids, 64);
    bool mapReachable = false;
    for (int i = 0; i < count; i++) {
        ps2nav_add_buttons(ids[i]);
        if (ids[i] == display_win) {
            mapReachable = true;
        }
    }

    // On the map: everything you can talk to, pick up or use.
    if (mapReachable && display_win > 0) {
        ps2nav_add_objects();
    }
}

bool ps2nav_step(int dx, int dy)
{
    if (!ps2nav_enabled() || (dx == 0 && dy == 0)) {
        return false;
    }

    ps2nav_collect();
    if (gTargetCount == 0) {
        return false;
    }

    int cx;
    int cy;
    mouse_get_position(&cx, &cy);

    // The closest target in a 90-degree cone around the direction, favouring
    // ones straight ahead.
    int best = -1;
    long bestScore = 0;
    for (int i = 0; i < gTargetCount; i++) {
        int vx = gTargets[i].x - cx;
        int vy = gTargets[i].y - cy;
        int along = vx * dx + vy * dy;
        int across = abs(vx * dy - vy * dx);
        if (along <= 2 || across > along) {
            continue;
        }
        long score = (long)along + (long)across * 2;
        if (best == -1 || score < bestScore) {
            best = i;
            bestScore = score;
        }
    }

    if (best == -1) {
        return true; // nothing that way: stay put rather than scroll
    }

    mouse_set_position(gTargets[best].x, gTargets[best].y);
    return true;
}

} // namespace fallout

#endif /* __PS2__ */
