// Map zoom for the PS2 build.
//
// Fallout 1 has no camera zoom, so this magnifies the finished frame instead:
// the map area (everything above the 100 px interface bar) is scaled up
// around the mouse cursor, and the bar is drawn at normal size underneath.
// The game itself still runs at 640x480, so clicks, pathing and scripts are
// untouched; the view simply follows the cursor so every tile stays reachable.

#ifdef __PS2__

#include "plib/gnw/ps2zoom.h"

#include "game/intface.h"
#include "plib/gnw/mouse.h"

namespace fallout {

static const float kZoomLevels[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
static const int kZoomLevelCount = (int)(sizeof(kZoomLevels) / sizeof(kZoomLevels[0]));
static int gZoomIndex = 0;

bool ps2zoom_available()
{
    return intface_is_hidden() == 0;
}

bool ps2zoom_step(int direction)
{
    int index = gZoomIndex + (direction > 0 ? 1 : -1);
    if (index < 0) {
        index = 0;
    }
    if (index >= kZoomLevelCount) {
        index = kZoomLevelCount - 1;
    }
    if (index == gZoomIndex) {
        return false;
    }
    gZoomIndex = index;
    return true;
}

float ps2zoom_level()
{
    return kZoomLevels[gZoomIndex];
}

bool ps2zoom_map_source(int width, int height, SDL_Rect* src, int* mapHeightOut)
{
    if (gZoomIndex == 0 || !ps2zoom_available()) {
        return false;
    }

    float zoom = kZoomLevels[gZoomIndex];
    int mapHeight = height - INTERFACE_BAR_HEIGHT;
    if (mapHeight <= 0) {
        return false;
    }

    src->w = (int)(width / zoom);
    src->h = (int)(mapHeight / zoom);

    int mouseX;
    int mouseY;
    mouse_get_position(&mouseX, &mouseY);

    src->x = mouseX - src->w / 2;
    src->y = mouseY - src->h / 2;
    if (src->x < 0) src->x = 0;
    if (src->y < 0) src->y = 0;
    if (src->x + src->w > width) src->x = width - src->w;
    if (src->y + src->h > mapHeight) src->y = mapHeight - src->h;

    *mapHeightOut = mapHeight;
    return true;
}

bool ps2zoom_render(SDL_Renderer* renderer, SDL_Texture* texture, int width, int height)
{
    if (gZoomIndex == 0 || !ps2zoom_available()) {
        return false;
    }

    float zoom = kZoomLevels[gZoomIndex];
    int mapHeight = height - INTERFACE_BAR_HEIGHT;
    if (mapHeight <= 0) {
        return false;
    }

    // Visible part of the map, centred on the cursor and kept inside the map.
    SDL_Rect src;
    src.w = (int)(width / zoom);
    src.h = (int)(mapHeight / zoom);

    int mouseX;
    int mouseY;
    mouse_get_position(&mouseX, &mouseY);

    src.x = mouseX - src.w / 2;
    src.y = mouseY - src.h / 2;
    if (src.x < 0) {
        src.x = 0;
    }
    if (src.y < 0) {
        src.y = 0;
    }
    if (src.x + src.w > width) {
        src.x = width - src.w;
    }
    if (src.y + src.h > mapHeight) {
        src.y = mapHeight - src.h;
    }

    SDL_Rect dst = { 0, 0, width, mapHeight };
    SDL_RenderCopy(renderer, texture, &src, &dst);

    // Interface bar at 1:1.
    SDL_Rect bar = { 0, mapHeight, width, INTERFACE_BAR_HEIGHT };
    SDL_RenderCopy(renderer, texture, &bar, &bar);
    return true;
}

} // namespace fallout

#endif /* __PS2__ */
