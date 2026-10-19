// Mods packed in .ZIP or Fallout .DAT files (see ps2modarc.h).
//
// Each archive's index is read once when the mod is opened into a hash
// table of upper-case "ART/CRITTERS/X.FRM" style names. A file is unpacked
// into memory when the game opens it and handed back as a FILE* (funopen),
// so the rest of the game can't tell it from a loose file.

#ifdef __PS2__

// funopen() is a BSD extension in newlib.
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#include "plib/gnw/ps2modarc.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "plib/gnw/debug.h"

#include <SDL.h>

namespace fallout {

#define MAX_ARCHIVES 8

enum {
    METHOD_STORED,
    METHOD_DEFLATE, // ZIP
    METHOD_LZSS, // DAT, whole file one LZSS stream
    METHOD_LZSS_CHUNKED, // DAT, blocks with a 16-bit header each
};

typedef struct Entry {
    unsigned int hash;
    const char* name; // upper case, '/' separated, relative to DATA
    int archive;
    unsigned int offset; // ZIP: local header offset; DAT: data offset
    unsigned int size;
    unsigned int packed;
    unsigned char method;
    unsigned char depth; // path components (for name-only lookups)
} Entry;

typedef struct Archive {
    FILE* stream;
    bool zip;
} Archive;

struct ModPack {
    Archive archives[MAX_ARCHIVES];
    int archiveCount;
    Entry* entries;
    int entryCount;
    int entryCapacity;
    int* table; // open addressing, -1 = empty
    int tableSize;
    char* names; // string pool
    size_t namesUsed;
    size_t namesCapacity;
};

// --- Names ----------------------------------------------------------------------

static void normalize(char* out, size_t size, const char* in)
{
    size_t i = 0;
    for (; *in != '\0' && i + 1 < size; in++) {
        char c = *in;
        if (c == '\\') {
            c = '/';
        }
        if (c == '/' && (i == 0 || out[i - 1] == '/')) {
            continue; // no leading or doubled slashes
        }
        out[i++] = (char)toupper((unsigned char)c);
    }
    out[i] = '\0';
}

static unsigned int hash_name(const char* s)
{
    unsigned int h = 2166136261u;
    for (; *s != '\0'; s++) {
        h = (h ^ (unsigned char)*s) * 16777619u;
    }
    return h;
}

static const char* pool_add(ModPack* pack, const char* name)
{
    size_t len = strlen(name) + 1;
    if (pack->namesUsed + len > pack->namesCapacity) {
        // Names point into the pool, so it can't move: chain a new block by
        // just allocating the strings separately once the first block fills.
        char* copy = (char*)malloc(len);
        if (copy != NULL) {
            memcpy(copy, name, len);
        }
        return copy;
    }
    char* dest = pack->names + pack->namesUsed;
    memcpy(dest, name, len);
    pack->namesUsed += len;
    return dest;
}

static bool pool_owns(const ModPack* pack, const char* p)
{
    return p >= pack->names && p < pack->names + pack->namesCapacity;
}

static void add_entry(ModPack* pack, const char* name, int archive, unsigned int offset, unsigned int size,
    unsigned int packed, int method)
{
    if (name[0] == '\0' || name[strlen(name) - 1] == '/') {
        return; // folder
    }
    if (pack->entryCount == pack->entryCapacity) {
        int capacity = pack->entryCapacity == 0 ? 256 : pack->entryCapacity * 2;
        Entry* entries = (Entry*)realloc(pack->entries, capacity * sizeof(Entry));
        if (entries == NULL) {
            return;
        }
        pack->entries = entries;
        pack->entryCapacity = capacity;
    }
    const char* stored = pool_add(pack, name);
    if (stored == NULL) {
        return;
    }
    Entry* e = &pack->entries[pack->entryCount++];
    e->hash = hash_name(stored);
    e->name = stored;
    e->archive = archive;
    e->offset = offset;
    e->size = size;
    e->packed = packed;
    e->method = (unsigned char)method;
    int depth = 0;
    for (const char* p = stored; *p != '\0'; p++) {
        if (*p == '/') {
            depth++;
        }
    }
    e->depth = (unsigned char)(depth > 255 ? 255 : depth);
}

static void build_table(ModPack* pack)
{
    int size = 64;
    while (size < pack->entryCount * 2) {
        size *= 2;
    }
    pack->table = (int*)malloc(size * sizeof(int));
    if (pack->table == NULL) {
        return;
    }
    pack->tableSize = size;
    for (int i = 0; i < size; i++) {
        pack->table[i] = -1;
    }
    // Earlier archives win: insert in order and keep the first of a name.
    for (int i = 0; i < pack->entryCount; i++) {
        unsigned int slot = pack->entries[i].hash & (size - 1);
        bool duplicate = false;
        while (pack->table[slot] != -1) {
            const Entry* other = &pack->entries[pack->table[slot]];
            if (other->hash == pack->entries[i].hash && strcmp(other->name, pack->entries[i].name) == 0) {
                duplicate = true;
                break;
            }
            slot = (slot + 1) & (size - 1);
        }
        if (!duplicate) {
            pack->table[slot] = i;
        }
    }
}

static const Entry* find_entry(const ModPack* pack, const char* name)
{
    if (pack->table == NULL) {
        return NULL;
    }
    unsigned int hash = hash_name(name);
    unsigned int slot = hash & (pack->tableSize - 1);
    while (pack->table[slot] != -1) {
        const Entry* e = &pack->entries[pack->table[slot]];
        if (e->hash == hash && strcmp(e->name, name) == 0) {
            return e;
        }
        slot = (slot + 1) & (pack->tableSize - 1);
    }
    return NULL;
}

// --- Little/big endian -------------------------------------------------------------

static unsigned int le16(const unsigned char* p)
{
    return p[0] | (p[1] << 8);
}

static unsigned int le32(const unsigned char* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}

static bool read_be32(FILE* f, unsigned int* v)
{
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) {
        return false;
    }
    *v = ((unsigned int)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    return true;
}

// --- ZIP -------------------------------------------------------------------------------

// Where "DATA/" starts inside the zip, if the mod was zipped with that
// folder (or inside a folder of its own): "MYMOD/DATA/".
static void zip_data_prefix(const char* name, char* prefix, size_t size)
{
    const char* p = name;
    while (*p != '\0') {
        if ((p == name || p[-1] == '/') && strncmp(p, "DATA/", 5) == 0) {
            size_t len = (size_t)(p - name) + 5;
            if (len < size) {
                memcpy(prefix, name, len);
                prefix[len] = '\0';
            }
            return;
        }
        p++;
    }
}

static bool zip_load(ModPack* pack, int index)
{
    FILE* f = pack->archives[index].stream;
    if (fseek(f, 0, SEEK_END) != 0) {
        return false;
    }
    long fileSize = ftell(f);
    long tail = fileSize < 65557 ? fileSize : 65557;
    unsigned char* buf = (unsigned char*)malloc(tail);
    if (buf == NULL) {
        return false;
    }
    fseek(f, fileSize - tail, SEEK_SET);
    if (fread(buf, 1, tail, f) != (size_t)tail) {
        free(buf);
        return false;
    }
    long eocd = -1;
    for (long i = tail - 22; i >= 0; i--) {
        if (le32(buf + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        free(buf);
        debug_printf("ps2modarc: not a zip file\n");
        return false;
    }
    unsigned int count = le16(buf + eocd + 10);
    unsigned int dirSize = le32(buf + eocd + 12);
    unsigned int dirOffset = le32(buf + eocd + 16);
    free(buf);

    unsigned char* dir = (unsigned char*)malloc(dirSize);
    if (dir == NULL) {
        return false;
    }
    fseek(f, dirOffset, SEEK_SET);
    if (fread(dir, 1, dirSize, f) != dirSize) {
        free(dir);
        return false;
    }

    // First pass: find the DATA/ prefix, if any.
    char prefix[128] = "";
    unsigned int pos = 0;
    for (unsigned int i = 0; i < count && pos + 46 <= dirSize && prefix[0] == '\0'; i++) {
        if (le32(dir + pos) != 0x02014b50) {
            break;
        }
        unsigned int nameLen = le16(dir + pos + 28);
        char raw[256];
        char name[256];
        size_t n = nameLen < sizeof(raw) - 1 ? nameLen : sizeof(raw) - 1;
        memcpy(raw, dir + pos + 46, n);
        raw[n] = '\0';
        normalize(name, sizeof(name), raw);
        zip_data_prefix(name, prefix, sizeof(prefix));
        pos += 46 + nameLen + le16(dir + pos + 30) + le16(dir + pos + 32);
    }
    size_t prefixLen = strlen(prefix);

    pos = 0;
    for (unsigned int i = 0; i < count && pos + 46 <= dirSize; i++) {
        if (le32(dir + pos) != 0x02014b50) {
            break;
        }
        unsigned int flags = le16(dir + pos + 8);
        unsigned int method = le16(dir + pos + 10);
        unsigned int packed = le32(dir + pos + 20);
        unsigned int size = le32(dir + pos + 24);
        unsigned int nameLen = le16(dir + pos + 28);
        unsigned int extraLen = le16(dir + pos + 30);
        unsigned int commentLen = le16(dir + pos + 32);
        unsigned int local = le32(dir + pos + 42);

        char raw[256];
        char name[256];
        size_t n = nameLen < sizeof(raw) - 1 ? nameLen : sizeof(raw) - 1;
        memcpy(raw, dir + pos + 46, n);
        raw[n] = '\0';
        normalize(name, sizeof(name), raw);
        pos += 46 + nameLen + extraLen + commentLen;

        if ((flags & 1) != 0 || (method != 0 && method != 8)) {
            continue; // encrypted, or a compression we don't do
        }
        const char* rel = name;
        if (prefixLen > 0 && strncmp(name, prefix, prefixLen) == 0) {
            rel = name + prefixLen;
        }
        add_entry(pack, rel, index, local, size, packed, method == 8 ? METHOD_DEFLATE : METHOD_STORED);
    }
    free(dir);
    return true;
}

// --- Fallout 1 DAT -------------------------------------------------------------

static bool dat_load(ModPack* pack, int index)
{
    FILE* f = pack->archives[index].stream;
    fseek(f, 0, SEEK_SET);
    unsigned int dirCount;
    unsigned int skip[3];
    if (!read_be32(f, &dirCount) || dirCount > 4096 || !read_be32(f, &skip[0]) || !read_be32(f, &skip[1]) || !read_be32(f, &skip[2])) {
        return false;
    }

    char (*dirs)[256] = (char(*)[256])malloc(dirCount * 256);
    if (dirs == NULL) {
        return false;
    }
    for (unsigned int d = 0; d < dirCount; d++) {
        int len = fgetc(f);
        if (len == EOF || fread(dirs[d], 1, len, f) != (size_t)len) {
            free(dirs);
            return false;
        }
        dirs[d][len] = '\0';
    }

    bool ok = true;
    for (unsigned int d = 0; d < dirCount && ok; d++) {
        unsigned int fileCount;
        if (!read_be32(f, &fileCount) || !read_be32(f, &skip[0]) || !read_be32(f, &skip[1]) || !read_be32(f, &skip[2])) {
            ok = false;
            break;
        }
        for (unsigned int i = 0; i < fileCount; i++) {
            char file[256];
            int len = fgetc(f);
            if (len == EOF || fread(file, 1, len, f) != (size_t)len) {
                ok = false;
                break;
            }
            file[len] = '\0';
            unsigned int attr;
            unsigned int offset;
            unsigned int size;
            unsigned int packed;
            if (!read_be32(f, &attr) || !read_be32(f, &offset) || !read_be32(f, &size) || !read_be32(f, &packed)) {
                ok = false;
                break;
            }
            char raw[600];
            char name[512];
            // The root folder is "." in DAT files.
            if (strcmp(dirs[d], ".") == 0) {
                snprintf(raw, sizeof(raw), "%s", file);
            } else {
                snprintf(raw, sizeof(raw), "%s\\%s", dirs[d], file);
            }
            normalize(name, sizeof(name), raw);
            int method;
            switch (attr & 0xF0) {
            case 0x10:
                method = METHOD_LZSS;
                break;
            case 0x40:
                method = METHOD_LZSS_CHUNKED;
                break;
            case 0x00: // the game treats "no flags" as LZSS too
                method = METHOD_LZSS;
                break;
            default:
                method = METHOD_STORED;
                break;
            }
            add_entry(pack, name, index, offset, size, packed, method);
        }
    }
    free(dirs);
    return ok;
}

// The game's LZSS: 4 KB ring buffer of spaces, writes start at 4078, a flag
// byte per 8 items (1 = literal byte, 0 = 12-bit offset + 4-bit length + 3).
static size_t lzss_block(const unsigned char* in, size_t inLen, unsigned char* out, size_t outLen)
{
    unsigned char ring[4096];
    memset(ring, ' ', sizeof(ring));
    unsigned int r = 4078;
    size_t i = 0;
    size_t o = 0;
    while (i < inLen && o < outLen) {
        unsigned int flags = in[i++];
        for (int b = 0; b < 8 && i < inLen && o < outLen; b++) {
            if ((flags & (1 << b)) != 0) {
                unsigned char c = in[i++];
                out[o++] = c;
                ring[r] = c;
                r = (r + 1) & 4095;
            } else {
                if (i + 1 >= inLen) {
                    return o;
                }
                unsigned int offset = in[i] | ((in[i + 1] & 0xF0) << 4);
                unsigned int length = (in[i + 1] & 0x0F) + 3;
                i += 2;
                for (unsigned int k = 0; k < length && o < outLen; k++) {
                    unsigned char c = ring[(offset + k) & 4095];
                    out[o++] = c;
                    ring[r] = c;
                    r = (r + 1) & 4095;
                }
            }
        }
    }
    return o;
}

// --- Unpacking -----------------------------------------------------------------------

static unsigned char* unpack_locked(const ModPack* pack, const Entry* e);

// Sound is read on its own thread: one archive read at a time.
static unsigned char* unpack(const ModPack* pack, const Entry* e)
{
    static SDL_mutex* lock = NULL;
    if (lock == NULL) {
        lock = SDL_CreateMutex();
    }
    if (lock != NULL) {
        SDL_LockMutex(lock);
    }
    unsigned char* data = unpack_locked(pack, e);
    if (lock != NULL) {
        SDL_UnlockMutex(lock);
    }
    return data;
}

static unsigned char* unpack_locked(const ModPack* pack, const Entry* e)
{
    const Archive* a = &pack->archives[e->archive];
    FILE* f = a->stream;
    unsigned int dataOffset = e->offset;
    if (a->zip) {
        unsigned char local[30];
        if (fseek(f, e->offset, SEEK_SET) != 0 || fread(local, 1, 30, f) != 30 || le32(local) != 0x04034b50) {
            return NULL;
        }
        dataOffset = e->offset + 30 + le16(local + 26) + le16(local + 28);
    }

    unsigned int packedSize = e->method == METHOD_STORED ? e->size : e->packed;
    unsigned char* packed = (unsigned char*)malloc(packedSize > 0 ? packedSize : 1);
    unsigned char* out = (unsigned char*)malloc(e->size > 0 ? e->size : 1);
    if (packed == NULL || out == NULL || fseek(f, dataOffset, SEEK_SET) != 0 || fread(packed, 1, packedSize, f) != packedSize) {
        free(packed);
        free(out);
        return NULL;
    }

    bool ok = true;
    switch (e->method) {
    case METHOD_STORED:
        memcpy(out, packed, e->size);
        break;
    case METHOD_DEFLATE: {
        z_stream z;
        memset(&z, 0, sizeof(z));
        z.next_in = packed;
        z.avail_in = packedSize;
        z.next_out = out;
        z.avail_out = e->size;
        ok = inflateInit2(&z, -MAX_WBITS) == Z_OK;
        if (ok) {
            int rc = inflate(&z, Z_FINISH);
            ok = (rc == Z_STREAM_END || rc == Z_OK) && z.total_out == e->size;
            inflateEnd(&z);
        }
        break;
    }
    case METHOD_LZSS:
        ok = lzss_block(packed, packedSize, out, e->size) == e->size;
        break;
    case METHOD_LZSS_CHUNKED: {
        size_t i = 0;
        size_t o = 0;
        while (i + 2 <= packedSize && o < e->size) {
            unsigned int n = (packed[i] << 8) | packed[i + 1];
            i += 2;
            if (n == 0) {
                break;
            }
            if ((n & 0x8000) != 0) {
                n &= 0x7FFF; // stored block
                if (i + n > packedSize || o + n > e->size) {
                    break;
                }
                memcpy(out + o, packed + i, n);
                o += n;
            } else {
                if (i + n > packedSize) {
                    break;
                }
                o += lzss_block(packed + i, n, out + o, e->size - o);
            }
            i += n;
        }
        ok = o == e->size;
        break;
    }
    }
    free(packed);
    if (!ok) {
        debug_printf("ps2modarc: couldn't unpack %s\n", e->name);
        free(out);
        return NULL;
    }
    return out;
}

// --- Memory stream ---------------------------------------------------------------

typedef struct MemFile {
    unsigned char* data;
    size_t size;
    size_t pos;
} MemFile;

static int mem_read(void* cookie, char* buf, int n)
{
    MemFile* m = (MemFile*)cookie;
    size_t left = m->size - m->pos;
    size_t count = (size_t)n < left ? (size_t)n : left;
    memcpy(buf, m->data + m->pos, count);
    m->pos += count;
    return (int)count;
}

static fpos_t mem_seek(void* cookie, fpos_t offset, int whence)
{
    MemFile* m = (MemFile*)cookie;
    long base = whence == SEEK_SET ? 0 : (whence == SEEK_CUR ? (long)m->pos : (long)m->size);
    long target = base + (long)offset;
    if (target < 0 || (size_t)target > m->size) {
        return -1;
    }
    m->pos = (size_t)target;
    return target;
}

static int mem_close(void* cookie)
{
    MemFile* m = (MemFile*)cookie;
    free(m->data);
    free(m);
    return 0;
}

static FILE* mem_open(unsigned char* data, size_t size)
{
    MemFile* m = (MemFile*)malloc(sizeof(MemFile));
    if (m == NULL) {
        free(data);
        return NULL;
    }
    m->data = data;
    m->size = size;
    m->pos = 0;
    FILE* stream = funopen(m, mem_read, NULL, mem_seek, mem_close);
    if (stream == NULL) {
        mem_close(m);
    }
    return stream;
}

// --- Public ------------------------------------------------------------------------------

static bool has_extension(const char* name, const char* ext)
{
    size_t len = strlen(name);
    size_t extLen = strlen(ext);
    return len > extLen && strcasecmp(name + len - extLen, ext) == 0;
}

bool modpack_is_archive_file(const char* root)
{
    return has_extension(root, ".zip") || has_extension(root, ".dat");
}

static void open_archive(ModPack* pack, const char* path)
{
    if (pack->archiveCount >= MAX_ARCHIVES) {
        return;
    }
    FILE* stream = fopen(path, "rb");
    if (stream == NULL) {
        return;
    }
    int index = pack->archiveCount;
    pack->archives[index].stream = stream;
    pack->archives[index].zip = has_extension(path, ".zip");
    int before = pack->entryCount;
    bool ok = pack->archives[index].zip ? zip_load(pack, index) : dat_load(pack, index);
    if (!ok) {
        debug_printf("ps2modarc: can't read %s\n", path);
        pack->entryCount = before;
        fclose(stream);
        return;
    }
    pack->archiveCount++;
    debug_printf("ps2modarc: %s: %d files\n", path, pack->entryCount - before);
}

ModPack* modpack_open(const char* root)
{
    ModPack* pack = (ModPack*)calloc(1, sizeof(ModPack));
    if (pack == NULL) {
        return NULL;
    }
    pack->namesCapacity = 256 * 1024;
    pack->names = (char*)malloc(pack->namesCapacity);
    if (pack->names == NULL) {
        pack->namesCapacity = 0;
    }

    if (modpack_is_archive_file(root)) {
        open_archive(pack, root);
    } else {
        // A folder: every .ZIP / .DAT in it, in name order.
        char names[MAX_ARCHIVES][64];
        int count = 0;
        DIR* dir = opendir(root);
        if (dir != NULL) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL && count < MAX_ARCHIVES) {
                if (modpack_is_archive_file(entry->d_name) && strlen(entry->d_name) < 64) {
                    snprintf(names[count++], 64, "%s", entry->d_name);
                }
            }
            closedir(dir);
        }
        for (int i = 1; i < count; i++) {
            for (int j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--) {
                char t[64];
                memcpy(t, names[j], 64);
                memcpy(names[j], names[j - 1], 64);
                memcpy(names[j - 1], t, 64);
            }
        }
        for (int i = 0; i < count; i++) {
            char path[256];
            snprintf(path, sizeof(path), "%s/%s", root, names[i]);
            open_archive(pack, path);
        }
    }

    if (pack->archiveCount == 0) {
        modpack_close(pack);
        return NULL;
    }
    build_table(pack);
    return pack;
}

void modpack_close(ModPack* pack)
{
    if (pack == NULL) {
        return;
    }
    for (int i = 0; i < pack->archiveCount; i++) {
        fclose(pack->archives[i].stream);
    }
    for (int i = 0; i < pack->entryCount; i++) {
        if (!pool_owns(pack, pack->entries[i].name)) {
            free((void*)pack->entries[i].name);
        }
    }
    free(pack->entries);
    free(pack->table);
    free(pack->names);
    free(pack);
}

FILE* modpack_fopen(ModPack* pack, const char* path)
{
    if (pack == NULL) {
        return NULL;
    }
    char name[256];
    normalize(name, sizeof(name), path);
    const Entry* e = find_entry(pack, name);
    if (e == NULL) {
        return NULL;
    }
    unsigned char* data = unpack(pack, e);
    if (data == NULL) {
        return NULL;
    }
    return mem_open(data, e->size);
}

bool modpack_read_named(ModPack* pack, const char* name, unsigned char** dataOut, size_t* sizeOut)
{
    if (pack == NULL) {
        return false;
    }
    char wanted[64];
    normalize(wanted, sizeof(wanted), name);
    size_t wantedLen = strlen(wanted);
    const Entry* best = NULL;
    for (int i = 0; i < pack->entryCount; i++) {
        const Entry* e = &pack->entries[i];
        size_t len = strlen(e->name);
        if (len < wantedLen || strcmp(e->name + len - wantedLen, wanted) != 0) {
            continue;
        }
        if (len > wantedLen && e->name[len - wantedLen - 1] != '/') {
            continue;
        }
        if (best == NULL || e->depth < best->depth) {
            best = e;
        }
    }
    if (best == NULL) {
        return false;
    }
    unsigned char* data = unpack(pack, best);
    if (data == NULL) {
        return false;
    }
    *dataOut = data;
    *sizeOut = best->size;
    return true;
}

int modpack_file_count(ModPack* pack)
{
    return pack != NULL ? pack->entryCount : 0;
}

} // namespace fallout

#endif /* __PS2__ */
