// Memory card saves: free space and the PS2 browser icon.
//
// Each save slot lives in its own card folder (FALLOUT1-SLOT01 ...). The PS2
// browser only lists a folder properly when it holds an icon.sys (title,
// colours, lights) and the 3D model it names, so both are written next to
// the save. Both files are generated here, from scratch: a little "holotape"
// cartridge with a vault-door label, in Vault-Tec blue and yellow.

#ifdef __PS2__

#include "plib/gnw/ps2card.h"

#include <libmc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plib/gnw/debug.h"

namespace fallout {

// --- Free space ----------------------------------------------------------------

int ps2card_free_kb(int port)
{
    int type = 0;
    int free = 0;
    int format = 0;
    int result = -10;
    // The first call after a card is (re)inserted only reports the change.
    for (int attempt = 0; attempt < 2; attempt++) {
        if (mcGetInfo(port, 0, &type, &free, &format) != 0) {
            return -1;
        }
        mcSync(0, NULL, &result);
        if (result == 0) {
            break;
        }
    }
    if (result < -1 || type != sceMcTypePS2 || !format) {
        return -1; // no card, not a PS2 card, or unformatted
    }
    return free; // 1 KB clusters
}

// --- Little-endian writers -------------------------------------------------------

static void put16(unsigned char* p, int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put32(unsigned char* p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void putf(unsigned char* p, float f)
{
    unsigned int v;
    memcpy(&v, &f, 4);
    put32(p, v);
}

// --- icon.sys ---------------------------------------------------------------------

#define ICON_SYS_SIZE 964
#define ICON_TITLE_SIZE 68

// The browser shows titles in Shift-JIS; plain ASCII is converted to the
// full-width characters it expects (2 bytes each).
static int sjis_append(unsigned char* out, int pos, char c)
{
    unsigned int code;
    if (c >= 'A' && c <= 'Z') {
        code = 0x8260 + (c - 'A');
    } else if (c >= 'a' && c <= 'z') {
        code = 0x8281 + (c - 'a');
    } else if (c >= '0' && c <= '9') {
        code = 0x824F + (c - '0');
    } else {
        switch (c) {
        case ':':
            code = 0x8146;
            break;
        case '-':
            code = 0x817C;
            break;
        case '.':
            code = 0x8144;
            break;
        case ',':
            code = 0x8143;
            break;
        case '!':
            code = 0x8149;
            break;
        case '?':
            code = 0x8148;
            break;
        case '\'':
            code = 0x8166;
            break;
        case '(':
            code = 0x8169;
            break;
        case ')':
            code = 0x816A;
            break;
        case '/':
            code = 0x815E;
            break;
        case '&':
            code = 0x8195;
            break;
        default:
            code = 0x8140; // full-width space
            break;
        }
    }
    if (pos + 2 > ICON_TITLE_SIZE - 2) {
        return pos; // keep the terminator
    }
    out[pos] = (unsigned char)(code >> 8);
    out[pos + 1] = (unsigned char)(code & 0xFF);
    return pos + 2;
}

static void build_icon_sys(unsigned char* b, const char* line1, const char* line2)
{
    memset(b, 0, ICON_SYS_SIZE);
    memcpy(b, "PS2D", 4);

    // Title: two lines; offset 6 says where the second starts (in bytes).
    unsigned char* title = b + 0xC0;
    int pos = 0;
    for (const char* p = line1; *p != '\0'; p++) {
        pos = sjis_append(title, pos, *p);
    }
    put16(b + 0x06, pos);
    for (const char* p = line2; *p != '\0'; p++) {
        pos = sjis_append(title, pos, *p);
    }

    put32(b + 0x0C, 0x60); // background opacity

    // Background corners (top-left, top-right, bottom-left, bottom-right):
    // a dark navy fading to Vault-Tec blue.
    static const unsigned int corners[4][3] = {
        { 6, 12, 40 },
        { 6, 12, 40 },
        { 20, 50, 120 },
        { 20, 50, 120 },
    };
    for (int i = 0; i < 4; i++) {
        unsigned char* c = b + 0x10 + i * 16;
        put32(c + 0, corners[i][0]);
        put32(c + 4, corners[i][1]);
        put32(c + 8, corners[i][2]);
        put32(c + 12, 0x80);
    }

    // Three lights: a warm key light, a cool fill and a dim back light.
    static const float dirs[3][3] = {
        { 0.5f, 0.5f, 0.5f },
        { -0.6f, 0.3f, 0.4f },
        { 0.0f, -0.8f, -0.6f },
    };
    static const float colors[3][3] = {
        { 0.9f, 0.8f, 0.6f },
        { 0.35f, 0.4f, 0.55f },
        { 0.2f, 0.2f, 0.2f },
    };
    for (int i = 0; i < 3; i++) {
        float len = sqrtf(dirs[i][0] * dirs[i][0] + dirs[i][1] * dirs[i][1] + dirs[i][2] * dirs[i][2]);
        for (int k = 0; k < 3; k++) {
            putf(b + 0x50 + i * 16 + k * 4, dirs[i][k] / len);
            putf(b + 0x80 + i * 16 + k * 4, colors[i][k]);
        }
        putf(b + 0x50 + i * 16 + 12, 0.0f);
        putf(b + 0x80 + i * 16 + 12, 1.0f);
    }
    putf(b + 0xB0, 0.35f); // ambient
    putf(b + 0xB4, 0.35f);
    putf(b + 0xB8, 0.35f);
    putf(b + 0xBC, 1.0f);

    // Normal, copy and delete icons: the same model.
    strcpy((char*)b + 0x104, PS2CARD_ICON_NAME);
    strcpy((char*)b + 0x144, PS2CARD_ICON_NAME);
    strcpy((char*)b + 0x184, PS2CARD_ICON_NAME);
}

// --- The 3D icon --------------------------------------------------------------------

#define TEX_SIZE 128
#define FIXED 4096.0f

static unsigned short rgb5(int r, int g, int b)
{
    return (unsigned short)(((r >> 3) & 31) | (((g >> 3) & 31) << 5) | (((b >> 3) & 31) << 10) | 0x8000);
}

// The cartridge label: Vault-Tec blue, a yellow cog-edged vault door with a
// dark centre and a big "1", and a yellow stripe along the bottom.
static void build_texture(unsigned short* tex)
{
    const unsigned short blue = rgb5(30, 70, 150);
    const unsigned short darkBlue = rgb5(14, 30, 70);
    const unsigned short yellow = rgb5(240, 196, 40);
    const unsigned short darkYellow = rgb5(170, 120, 20);

    const float cx = 64.0f;
    const float cy = 54.0f;
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            unsigned short c = blue;
            // Bevelled edge.
            if (x < 4 || y < 4 || x >= TEX_SIZE - 4 || y >= TEX_SIZE - 4) {
                c = darkBlue;
            }
            // Bottom stripe.
            if (y >= 104 && y < 116 && x >= 10 && x < 118) {
                c = ((x / 8) % 2 == 0) ? yellow : darkYellow;
            }

            float dx = x - cx;
            float dy = y - cy;
            float r = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx);
            // Cog: 12 teeth between radius 38 and 44.
            float teeth = cosf(angle * 12.0f) > 0.2f ? 44.0f : 38.0f;
            if (r < teeth) {
                c = yellow;
            }
            if (r < 32.0f) {
                c = darkYellow;
            }
            if (r < 28.0f) {
                c = darkBlue;
            }
            // The "1".
            if ((x >= 60 && x < 70 && y >= 36 && y < 72) || (x >= 54 && x < 62 && y >= 36 && y < 44) || (x >= 54 && x < 76 && y >= 68 && y < 74)) {
                c = yellow;
            }
            tex[y * TEX_SIZE + x] = c;
        }
    }
}

typedef struct IconVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
} IconVertex;

// PS2 icon space: y points down (the model stands on y = 0), and -z faces
// the viewer.
static int build_model(IconVertex* out)
{
    const float hw = 1.0f; // half width
    const float top = -2.1f;
    const float bottom = 0.0f;
    const float hd = 0.3f; // half depth
    // Each face: 4 corners (counter-clockwise from the viewer), normal, and
    // whether it carries the label (others sample the plain blue border).
    struct Face {
        float c[4][3];
        float n[3];
        bool label;
    };
    const Face faces[6] = {
        // front
        { { { -hw, top, -hd }, { hw, top, -hd }, { hw, bottom, -hd }, { -hw, bottom, -hd } }, { 0, 0, -1 }, true },
        // back
        { { { hw, top, hd }, { -hw, top, hd }, { -hw, bottom, hd }, { hw, bottom, hd } }, { 0, 0, 1 }, true },
        // left
        { { { -hw, top, hd }, { -hw, top, -hd }, { -hw, bottom, -hd }, { -hw, bottom, hd } }, { -1, 0, 0 }, false },
        // right
        { { { hw, top, -hd }, { hw, top, hd }, { hw, bottom, hd }, { hw, bottom, -hd } }, { 1, 0, 0 }, false },
        // top
        { { { -hw, top, hd }, { hw, top, hd }, { hw, top, -hd }, { -hw, top, -hd } }, { 0, -1, 0 }, false },
        // bottom
        { { { -hw, bottom, -hd }, { hw, bottom, -hd }, { hw, bottom, hd }, { -hw, bottom, hd } }, { 0, 1, 0 }, false },
    };
    static const float labelUV[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    static const int tri[6] = { 0, 1, 2, 0, 2, 3 };

    int count = 0;
    for (int f = 0; f < 6; f++) {
        for (int k = 0; k < 6; k++) {
            const float* c = faces[f].c[tri[k]];
            IconVertex* v = &out[count++];
            v->x = c[0];
            v->y = c[1];
            v->z = c[2];
            v->nx = faces[f].n[0];
            v->ny = faces[f].n[1];
            v->nz = faces[f].n[2];
            if (faces[f].label) {
                v->u = labelUV[tri[k]][0];
                v->v = labelUV[tri[k]][1];
            } else {
                v->u = 0.01f; // dark border pixel
                v->v = 0.01f;
            }
        }
    }
    return count;
}

static short fx(float f)
{
    return (short)lrintf(f * FIXED);
}

// Layout: header, vertices (position, normal, UV, colour), a one-frame
// "animation", then the 128x128 RGB5A1 texture, uncompressed.
static unsigned char* build_icon(int* sizeOut)
{
    IconVertex model[36];
    int vertexCount = build_model(model);

    int size = 20 + vertexCount * (8 + 8 + 4 + 4) + 20 + 16 + TEX_SIZE * TEX_SIZE * 2;
    unsigned char* b = (unsigned char*)calloc(1, size);
    if (b == NULL) {
        return NULL;
    }

    unsigned char* p = b;
    put32(p + 0, 0x00010000); // magic
    put32(p + 4, 1); // one shape
    put32(p + 8, 0x07); // uncompressed texture
    putf(p + 12, 1.0f);
    put32(p + 16, vertexCount);
    p += 20;

    for (int i = 0; i < vertexCount; i++) {
        const IconVertex* v = &model[i];
        put16(p + 0, fx(v->x));
        put16(p + 2, fx(v->y));
        put16(p + 4, fx(v->z));
        put16(p + 6, 0);
        put16(p + 8, fx(v->nx));
        put16(p + 10, fx(v->ny));
        put16(p + 12, fx(v->nz));
        put16(p + 14, 0);
        put16(p + 16, fx(v->u));
        put16(p + 18, fx(v->v));
        p[20] = 0x80; // vertex colour: neutral (texture as is)
        p[21] = 0x80;
        p[22] = 0x80;
        p[23] = 0x80;
        p += 24;
    }

    // Animation header: tag, frame length, speed, offset, one frame.
    put32(p + 0, 1);
    put32(p + 4, 1);
    putf(p + 8, 1.0f);
    put32(p + 12, 0);
    put32(p + 16, 1);
    p += 20;
    // The frame: shape 0, one key (stored as key count + 1, so no key data).
    put32(p + 0, 0);
    put32(p + 4, 1);
    putf(p + 8, 1.0f);
    putf(p + 12, 1.0f);
    p += 16;

    unsigned short* tex = (unsigned short*)malloc(TEX_SIZE * TEX_SIZE * 2);
    if (tex == NULL) {
        free(b);
        return NULL;
    }
    build_texture(tex);
    for (int i = 0; i < TEX_SIZE * TEX_SIZE; i++) {
        put16(p + i * 2, tex[i]);
    }
    free(tex);
    p += TEX_SIZE * TEX_SIZE * 2;

    *sizeOut = (int)(p - b);
    return b;
}

// --- Writing ----------------------------------------------------------------------

static bool write_file(const char* path, const unsigned char* data, int size)
{
    FILE* stream = fopen(path, "wb");
    if (stream == NULL) {
        return false;
    }
    bool ok = fwrite(data, 1, size, stream) == (size_t)size;
    ok = (fclose(stream) == 0) && ok;
    return ok;
}

bool ps2card_write_icons(const char* dir, int slot, const char* description)
{
    char path[128];

    // The model only needs writing once per folder.
    snprintf(path, sizeof(path), "%s/%s", dir, PS2CARD_ICON_NAME);
    FILE* existing = fopen(path, "rb");
    if (existing != NULL) {
        fclose(existing);
    } else {
        int size = 0;
        unsigned char* icon = build_icon(&size);
        if (icon == NULL || !write_file(path, icon, size)) {
            free(icon);
            debug_printf("ps2card: couldn't write %s\n", path);
            return false;
        }
        free(icon);
    }

    // The title changes with every save: "Fallout 1 - Slot 3" / description.
    char line1[40];
    snprintf(line1, sizeof(line1), "Fallout Slot %d", slot + 1);
    char line2[40];
    snprintf(line2, sizeof(line2), "%.16s", description != NULL && description[0] != '\0' ? description : "Saved game");

    unsigned char sys[ICON_SYS_SIZE];
    build_icon_sys(sys, line1, line2);
    snprintf(path, sizeof(path), "%s/icon.sys", dir);
    if (!write_file(path, sys, ICON_SYS_SIZE)) {
        debug_printf("ps2card: couldn't write %s\n", path);
        return false;
    }
    return true;
}

} // namespace fallout

#endif /* __PS2__ */
