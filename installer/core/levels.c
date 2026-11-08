/*
 * The edition's patch of the levels' data (levels.h), as tools/gen_boots.py and tools/patch_levels.py make it, byte
 * for byte.
 *
 * The table of contents (sector 1500) lists each level's header sector at TOC_LEVEL_TABLE; the header's +0x08 holds
 * the data archive's first sector and length in sectors, which the game reads whole. The archive starts with a
 * table of eleven sections {s32 offset, s32 size} (overlay, sound bank, core index, GS RAM, HUD header, five HUD
 * banks, core data); the patch changes three of them: the core index (raw), GS RAM (raw) and the core data
 * (WAD-compressed), whose compressed and decompressed sizes the index's header words +0x88 and +0x8C give.
 *
 * Every level gets, in this order: the seed code's glyphs in its three menu fonts (its effect textures 1-3); the
 * Charge Boots class and its texture; on Kalebo III, the rebuilt Gadgetron sign; the extra classes the Randomizer's
 * stand-ins need, copied from the levels that have them as this patch leaves those levels before their own extra
 * classes. A class goes in where the moby class blobs end, inside the part of the core the engine keeps resident
 * (below +0x7C), every core offset past it moved up; the class table grows in place (moved, even unchanged, it
 * hangs the load), the tables after it moving down; the texture table and the GS RAM upload list are copied to the
 * index's end with their new entries.
 *
 * Two ways in, one patch: patch_levels reads and writes each level on an image; levels_produce patches one level's
 * span (its header and archive) handed to it by a writer that streams the new image front to back. Both share a
 * plan, made first: the levels' places, the boots and the extra classes' sources.
 *
 * Memory: one level is in memory at a time: its archive as read (a few MB), its decoded core (16-24 MB) and, while it
 * is compressed again, the stream and a copy decoded back to check it. The six levels the extra classes come from
 * are prepared once for their classes alone (kept: the blobs, pixels and palettes, some 300 KB) and again at their
 * turn. The compression runs on the thread pool, a level's chunks side by side on every core.
 */
#include "levels.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytes.h"
#include "disc.h"
#include "files.h"
#include "fill.h"
#include "json.h"
#include "moby.h"
#include "pool.h"
#include "wad.h"

#define TOC_LEVEL_TABLE 0x28C8u     /* in the table of contents: {u32 header sector, u32} per level */
#define SECTIONS 11
#define SECTION_CORE_INDEX 2
#define SECTION_GS_RAM 3
#define SECTION_CORE_DATA 10
#define SECTION_ALIGN 0x100u
#define CORE_ROOM (1u << 20)        /* what a level's decoded core is given to grow by before it is moved */

/* The core index's header words. */
#define INDEX_UPLOADS 0x00u         /* the GS RAM upload list {count, offset} of 16-byte entries */
#define INDEX_MOBY_CLASSES 0x18u    /* the moby class table {count, offset} of 32-byte entries */
#define INDEX_MOBY_TEXTURES 0x38u   /* the moby texture table {count, offset} of 16-byte entries */
#define INDEX_FX_TEXTURES 0x58u     /* the effect texture table {count, offset}: the menu fonts are 1, 2 and 3 */
#define INDEX_TEXTURES_BASE 0x60u   /* where the textures' pixels are counted from */
#define INDEX_FX_BANK 0x68u         /* the effect bank, which the effect textures' offsets count from */
#define INDEX_SOUND_BLOCK 0x70u
#define INDEX_SEQUENCES 0x78u       /* Ratchet's 256 sequences */
#define INDEX_RESIDENT_END 0x7Cu    /* the end of the core's resident part (wrench's scene_view_size) */
#define INDEX_GADGETS 0x80u         /* the gadget blobs {count, offset} of 16-byte entries */
#define INDEX_STREAM_SIZE 0x88u
#define INDEX_CORE_SIZE 0x8Cu
#define INDEX_HEADER 0xACu

/* The level's loader registers two classes after the table's: 224 slots less those (223 hung Batalia's load). */
#define MAX_CLASSES 222
#define MAX_SLOT 0xFE               /* texture slots are bytes; 0xFF ends a class's list */
#define CLASS_ENTRY 32
#define TEXTURE_ENTRY 16
#define UPLOAD_ENTRY 16
#define PALETTE 0x400u              /* 256 RGBA entries in the GS's CSM1 order, alpha 0..0x80 */

#define BOOTS_CLASS 2000            /* the Charge Boots' id in R&C1 (its ids go up to 1973) */
#define RAC2_BOOTS_CLASS 3696       /* Going Commando's Charge Boots, in each of its levels */
#define RAC2_LEVEL "G/LEVEL1.WAD"
#define GRINDBOOTS_CLASS 195        /* the R&C1 boots they stand in for, on Novalis */
#define GRINDBOOTS_LEVEL 3
#define SIGN_LEVEL 16               /* Kalebo III, the only level with the Gadgetron sign */
#define SIGN_CLASS 1891
#define SIGN_STEP 0x100u            /* the sign's space grows in steps that keep every later block aligned */
#define FONTS 3

/* Header words that are offsets into the core index itself: the ten {count, offset} tables' offsets, then the
   particle definitions, the sound block, Ratchet's sequences and the gadget table. */
static const uint8_t INDEX_POINTERS[] = {0x04, 0x1C, 0x24, 0x2C, 0x34, 0x3C, 0x44, 0x4C, 0x54, 0x5C, 0x6C, 0x70,
                                         0x78, 0x84};

/* Header words that are core offsets: terrain, occlusion, sky, collision, textures, particle bank, effect bank,
   +0x74, the resident part's end, +0xA0..+0xA8. */
static const uint8_t BLOCK_POINTERS[] = {0x08, 0x0C, 0x10, 0x14, 0x60, 0x64, 0x68, 0x74, 0x7C, 0xA0, 0xA4, 0xA8};

/* The moby, tie and shrub class tables, each entry's first word its blob's core offset. */
static const struct {
    uint8_t at;
    uint8_t entry;
} CLASS_TABLES[] = {{0x18, 32}, {0x20, 32}, {0x28, 48}};

/* The texture tables (terrain, moby, tie, shrub, particle, effect), each entry's first word its pixels' offset
   from the textures' base. */
static const uint8_t TEXTURE_TABLES[] = {0x30, 0x38, 0x40, 0x48, 0x50, 0x58};

/*
 * The extra classes, in the order a level gets them while its class table has room: the infobot first (it hosts
 * checks), the Codebot last (the largest). Each is copied from its own level. The levels too near the stack for the
 * one-planet items (they would move the effect pool up by some 139 KB) get the infobot alone.
 */
#define EXTRA_CLASSES 6
#define INFOBOT_CLASS 750

static const struct {
    int32_t id;
    const char *name;
    int level;
    bool one_planet;
} EXTRA[EXTRA_CLASSES] = {
    {INFOBOT_CLASS, "infobot", 1, false},
    {645, "Nanotech can", 10, true},        /* the Orxon Nanotech vendor's */
    {84, "Hoverboard", 2, true},            /* Skid's, from Aridia's cutscene */
    {1002, "Zoomerator", 5, true},          /* the Rilgar race's prize */
    {37, "Raritanium", 12, true},           /* Hoven's pickup */
    {1428, "Codebot", 15, true},            /* the pickup's, as on Drek's Fleet */
};

static const int NEAR_THE_STACK[] = {5, 6, 15};     /* Rilgar, Blarg, Quartu */

/* The sound block's spans before the classes': the slot table, the remap. */
#define SPAN_FIXED 2

/* --------------------------------------------------------------------------------------------------- buffers */

/* A growing byte buffer: a level's core index, GS RAM image or core data. */
struct buffer {
    uint8_t *bytes;
    size_t size;
    size_t capacity;
};

/* Room for SIZE bytes: exactly that much at first (a decoded core is given its room at once), then twice as much
   each time it grows. */
static bool buffer_reserve(struct buffer *buffer, size_t size)
{
    size_t capacity = 2 * buffer->capacity;
    uint8_t *grown;

    if (size <= buffer->capacity)
        return true;
    if (capacity < size)
        capacity = size;
    grown = realloc(buffer->bytes, capacity);
    if (!grown)
        return false;
    buffer->bytes = grown;
    buffer->capacity = capacity;
    return true;
}

/* SIZE bytes at AT (no further than the buffer's end), BYTES or zeros when it is NULL. */
static bool buffer_insert(struct buffer *buffer, size_t at, const void *bytes, size_t size)
{
    if (size == 0)
        return true;
    if (!buffer_reserve(buffer, buffer->size + size))
        return false;
    memmove(buffer->bytes + at + size, buffer->bytes + at, buffer->size - at);
    if (bytes)
        memcpy(buffer->bytes + at, bytes, size);
    else
        memset(buffer->bytes + at, 0, size);
    buffer->size += size;
    return true;
}

static bool buffer_append(struct buffer *buffer, const void *bytes, size_t size)
{
    return buffer_insert(buffer, buffer->size, bytes, size);
}

/* Zeros up to the next multiple of ALIGNMENT. */
static bool buffer_pad(struct buffer *buffer, size_t alignment)
{
    return buffer_append(buffer, NULL, (alignment - buffer->size % alignment) % alignment);
}

static size_t align(size_t size, size_t alignment)
{
    return (size + alignment - 1) / alignment * alignment;
}

/* ---------------------------------------------------------------------------------------------------- errors */

static bool out_of_memory(InstallError *error)
{
    return install_fail(error, "internal", "out of memory patching the levels");
}

static bool image_failed(const IsoImage *image, InstallError *error)
{
    return install_fail(error, image->error == ENOSPC ? "no_space" : "not_writable", "the image could not be written");
}

/* TEXT's end extended like printf, within CAPACITY. */
static void text_extend(char *text, size_t capacity, const char *format, ...)
{
    size_t length = strlen(text);
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(text + length, capacity - length, format, arguments);
    va_end(arguments);
}

/* IDS as Python prints a list: "[750, 645]". */
static void id_list(char *text, size_t capacity, const int32_t *ids, size_t count)
{
    text[0] = 0;
    text_extend(text, capacity, "[");
    for (size_t k = 0; k < count; k++)
        text_extend(text, capacity, k ? ", %d" : "%d", (int)ids[k]);
    text_extend(text, capacity, "]");
}

/* ---------------------------------------------------------------------------------------------------- levels */

struct place {
    uint32_t header_sector;     /* the level's header, whose +0x08 holds the archive's place */
    uint32_t sector;            /* the archive's first sector */
    uint32_t sectors;
};

/* A level's data archive in memory: the core decoded, the core index and GS RAM editable. */
struct level {
    int number;
    uint8_t *archive;           /* as read: the head and the sections the patch keeps */
    size_t head_size;           /* the bytes before the first section */
    size_t offsets[SECTIONS];
    size_t sizes[SECTIONS];     /* 0: the section is absent */
    struct buffer index;
    struct buffer gs;
    struct buffer core;
    bool malformed;             /* a read or write of the index fell outside it */
};

static void level_free(struct level *level)
{
    free(level->archive);
    free(level->index.bytes);
    free(level->gs.bytes);
    free(level->core.bytes);
    memset(level, 0, sizeof *level);
}

/* The index's word at AT; 0, with the level marked malformed, outside the index. */
static uint32_t index_u32(struct level *level, size_t at)
{
    if (at > level->index.size || level->index.size - at < 4) {
        level->malformed = true;
        return 0;
    }
    return read_u32_le(level->index.bytes + at);
}

static int32_t index_s32(struct level *level, size_t at)
{
    return (int32_t)index_u32(level, at);
}

static void index_put(struct level *level, size_t at, uint32_t value)
{
    if (at > level->index.size || level->index.size - at < 4)
        level->malformed = true;
    else
        write_u32_le(level->index.bytes + at, value);
}

/* The {count, offset} table at AT of ENTRY-byte entries; false, with the level marked malformed, when its entries
   lie outside the index. */
static bool index_table(struct level *level, size_t at, size_t entry, uint32_t *count, uint32_t *offset)
{
    *count = index_u32(level, at);
    *offset = index_u32(level, at + 4);
    if (level->malformed || *offset > level->index.size || (level->index.size - *offset) / entry < *count) {
        level->malformed = true;
        *count = 0;
        return false;
    }
    return true;
}

static bool check_index(const struct level *level, InstallError *error)
{
    return !level->malformed ||
           install_fail(error, "internal", "level %d: its core index points outside itself", level->number);
}

/* The levels' places on the image FILE, from the table of contents. */
static bool read_places(FILE *file, struct place places[LEVEL_COUNT], InstallError *error)
{
    for (int number = 0; number < LEVEL_COUNT; number++) {
        uint8_t entry[4], header[8];

        if (!file_read_at(file, (uint64_t)DISC_TOC_SECTOR * ISO_SECTOR + TOC_LEVEL_TABLE + 8u * (unsigned)number,
                          entry, sizeof entry) ||
            !file_read_at(file, (uint64_t)read_u32_le(entry) * ISO_SECTOR + 8, header, sizeof header))
            return install_fail(error, "internal", "level %d's place on the disc cannot be read", number);
        places[number].header_sector = read_u32_le(entry);
        places[number].sector = read_u32_le(header);
        places[number].sectors = read_u32_le(header + 4);
    }
    return true;
}

/* Whether the levels' archives and headers keep apart, so a level's archive written in place leaves every other
   level's as the disc had it. */
static bool places_apart(const struct place places[LEVEL_COUNT])
{
    for (int a = 0; a < LEVEL_COUNT; a++) {
        for (int b = 0; b < LEVEL_COUNT; b++) {
            uint64_t start = places[b].sector, end = start + places[b].sectors;

            if (a != b && places[a].sector < end && start < (uint64_t)places[a].sector + places[a].sectors)
                return false;
            if (places[a].header_sector >= start && places[a].header_sector < end)
                return false;
        }
    }
    return true;
}

/* The archive's section table, checked to lie within its SIZE bytes. */
static bool read_sections(struct level *level, size_t size, InstallError *error)
{
    size_t first = size;

    if (size < 8 * SECTIONS)
        return install_fail(error, "internal", "level %d's archive is too short", level->number);
    for (int s = 0; s < SECTIONS; s++) {
        int32_t offset = (int32_t)read_u32_le(level->archive + 8 * s);
        int32_t length = (int32_t)read_u32_le(level->archive + 8 * s + 4);

        if (offset < 0 || length < 0 || (size_t)offset > size || (size_t)length > size - (size_t)offset)
            return install_fail(error, "internal", "level %d's archive has a section outside it", level->number);
        level->offsets[s] = (size_t)offset;
        level->sizes[s] = (size_t)length;
        if (length > 0 && (size_t)offset < first)
            first = (size_t)offset;
    }
    if (first < 8 * SECTIONS || level->sizes[SECTION_CORE_INDEX] < INDEX_HEADER)
        return install_fail(error, "internal", "level %d's archive is not one the game reads", level->number);
    level->head_size = first;
    return true;
}

/* Level NUMBER's archive, PLACE's sectors of it, its core decoded: copied from ARCHIVE, or read from FILE when
   ARCHIVE is NULL. */
static bool read_level(FILE *file, const uint8_t *archive, const struct place *place, int number, struct level *level,
                       InstallError *error)
{
    size_t size = (size_t)place->sectors * ISO_SECTOR, decoded = 0, expected;
    enum wad_error result;

    memset(level, 0, sizeof *level);
    level->number = number;
    level->archive = malloc(size ? size : 1);
    if (!level->archive)
        return out_of_memory(error);
    if (archive)
        memcpy(level->archive, archive, size);
    else if (!file_read_at(file, (uint64_t)place->sector * ISO_SECTOR, level->archive, size))
        return install_fail(error, "internal", "level %d's archive cannot be read", number);
    if (!read_sections(level, size, error))
        return false;
    if (!buffer_append(&level->index, level->archive + level->offsets[SECTION_CORE_INDEX],
                       level->sizes[SECTION_CORE_INDEX]) ||
        !buffer_append(&level->gs, level->archive + level->offsets[SECTION_GS_RAM], level->sizes[SECTION_GS_RAM]))
        return out_of_memory(error);
    expected = read_u32_le(level->index.bytes + INDEX_CORE_SIZE);
    if (!buffer_reserve(&level->core, expected + CORE_ROOM))
        return out_of_memory(error);
    result = wad_decompress(level->archive + level->offsets[SECTION_CORE_DATA], level->sizes[SECTION_CORE_DATA],
                            level->core.bytes, expected, &decoded);
    if (result != WAD_OK && result != WAD_TOO_LARGE)
        return install_fail(error, "internal", "level %d: the core data is no stream the game reads (%s)", number,
                            wad_error_text(result));
    if (result == WAD_TOO_LARGE || decoded != expected)
        return install_fail(error, "internal", "level %d: the core data does not decode to the size its index gives",
                            number);
    level->core.size = decoded;
    return true;
}

/* ---------------------------------------------------------------------------------------------- core offsets */

/* A core offset the index holds: where in the index, and its value. */
struct pointer {
    size_t at;
    int32_t value;
};

struct pointers {
    struct pointer *items;
    size_t count;
};

static void add_pointer(struct level *level, struct pointers *list, size_t at)
{
    int32_t value = index_s32(level, at);

    if (value > 0) {
        list->items[list->count].at = at;
        list->items[list->count++].value = value;
    }
}

/*
 * Every core offset the core index holds, in this order: the block pointers, each moby, tie and shrub class blob,
 * each gadget blob and each of Ratchet's 256 sequences (wrench's LevelCoreHeader and OpenRAC's core block bounds).
 * The caller frees LIST's items.
 */
static bool core_pointers(struct level *level, struct pointers *list, InstallError *error)
{
    uint32_t counts[4], offsets[4], sequences = index_u32(level, INDEX_SEQUENCES);
    size_t capacity = sizeof BLOCK_POINTERS + 256;

    for (int t = 0; t < 3; t++) {
        index_table(level, CLASS_TABLES[t].at, CLASS_TABLES[t].entry, &counts[t], &offsets[t]);
        capacity += counts[t];
    }
    index_table(level, INDEX_GADGETS, 16, &counts[3], &offsets[3]);
    capacity += counts[3];
    list->count = 0;
    list->items = malloc(capacity * sizeof *list->items);
    if (!list->items)
        return out_of_memory(error);
    for (size_t k = 0; k < sizeof BLOCK_POINTERS; k++)
        add_pointer(level, list, BLOCK_POINTERS[k]);
    for (int t = 0; t < 3; t++) {
        for (uint32_t i = 0; i < counts[t]; i++)
            add_pointer(level, list, offsets[t] + (size_t)CLASS_TABLES[t].entry * i);
    }
    for (uint32_t i = 0; i < counts[3]; i++)
        add_pointer(level, list, offsets[3] + 16u * i);
    for (uint32_t i = 0; sequences != 0 && i < 256; i++)
        add_pointer(level, list, sequences + 4u * i);
    if (!check_index(level, error)) {
        free(list->items);
        list->items = NULL;
        return false;
    }
    return true;
}

/* Every core offset of LIST at FROM or past it moved up by DELTA; returns how many. */
static size_t shift_pointers(struct level *level, const struct pointers *list, size_t from, size_t delta)
{
    size_t shifted = 0;

    for (size_t k = 0; k < list->count; k++) {
        if ((size_t)list->items[k].value >= from) {
            index_put(level, list->items[k].at, (uint32_t)list->items[k].value + (uint32_t)delta);
            shifted++;
        }
    }
    return shifted;
}

/*
 * Every texture whose pixels start at core offset AT or later moved up by DELTA (their offsets count from the
 * textures' base, not the core's start, so core_pointers leaves them out): the Charge Boots texture lies past the
 * moby class blobs, and a class that grows before it must move it. Returns how many moved.
 */
static size_t shift_textures(struct level *level, size_t at, size_t delta)
{
    int64_t base = index_u32(level, INDEX_TEXTURES_BASE);
    size_t moved = 0;

    for (size_t t = 0; t < sizeof TEXTURE_TABLES; t++) {
        uint32_t count, offset;

        index_table(level, TEXTURE_TABLES[t], TEXTURE_ENTRY, &count, &offset);
        for (uint32_t i = 0; i < count; i++) {
            int32_t value = index_s32(level, offset + (size_t)TEXTURE_ENTRY * i);

            if (base + value >= (int64_t)at) {
                index_put(level, offset + (size_t)TEXTURE_ENTRY * i, (uint32_t)value + (uint32_t)delta);
                moved++;
            }
        }
    }
    return moved;
}

/* The GS RAM upload list must be in order: the game uploads it entry after entry, advancing by each entry's size,
   and ignores the stored addresses, so an entry appended lands at its file offset. */
static bool check_uploads(struct level *level, InstallError *error)
{
    uint32_t count, offset;
    uint64_t cursor = 0;

    if (!index_table(level, INDEX_UPLOADS, UPLOAD_ENTRY, &count, &offset))
        return check_index(level, error);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry = level->index.bytes + offset + (size_t)UPLOAD_ENTRY * i;
        uint32_t psm = read_u32_le(entry), packed = read_u32_le(entry + 4);
        uint64_t pixels = (uint64_t)(packed & 0xFFFF) * (packed >> 16);

        if (read_u32_le(entry + 8) != cursor)
            return install_fail(error, "internal", "level %d: the GS RAM upload list is not sequential",
                                level->number);
        cursor += psm == 0x13 ? (pixels > 0x100 ? pixels : 0x100) : psm == 2 ? 0x200 : 0x400;
    }
    if (cursor != level->gs.size)
        return install_fail(error, "internal", "level %d: the GS RAM image is 0x%zx bytes but its upload list covers "
                            "0x%llx", level->number, level->gs.size, (unsigned long long)cursor);
    return true;
}

/*
 * Where the moby class blobs end in the core data: the first core offset past the last class's start, inside the
 * part of the core the engine keeps resident (+0x7C; Ratchet's sequences and the gadget blobs come after it, in
 * memory the engine reuses: a class placed there hung the load).
 */
static bool moby_classes_end(struct level *level, size_t *end, InstallError *error)
{
    uint32_t count, offset, textures_base = index_u32(level, INDEX_TEXTURES_BASE);
    uint32_t resident_end = index_u32(level, INDEX_RESIDENT_END);
    int64_t last = -1, first_after = -1;
    struct pointers list;

    index_table(level, INDEX_MOBY_CLASSES, CLASS_ENTRY, &count, &offset);
    for (uint32_t i = 0; i < count; i++) {
        int32_t start = index_s32(level, offset + (size_t)CLASS_ENTRY * i);

        if (start > 0 && start > last)
            last = start;
    }
    if (!check_index(level, error) || !core_pointers(level, &list, error))
        return false;
    for (size_t k = 0; k < list.count; k++) {
        if (last > 0 && list.items[k].value > last && (first_after < 0 || list.items[k].value < first_after))
            first_after = list.items[k].value;
    }
    free(list.items);
    if (first_after < 0 || !(textures_base < first_after && first_after <= resident_end))
        return install_fail(error, "internal", "level %d: unexpected core layout: textures at 0x%x, moby classes end "
                            "at 0x%llx, resident part ends at 0x%x", level->number, textures_base,
                            (unsigned long long)first_after, resident_end);
    *end = (size_t)first_after;
    return true;
}

/* ------------------------------------------------------------------------------------------------ menu fonts */

/*
 * The seed code's glyphs painted into the level's three menu fonts (effect textures 1, 2 and 3, 256 x 128 sheets
 * in the effect bank): the pause menu's Save and Load pages show a Randomizer save's seed, and a level draws text
 * from its own copies of the fonts, which have the same empty corner and palette as the title screen's. Each font's
 * glyphs are made from its sheet as the level had it.
 */
static bool paint_fonts(struct level *level, InstallError *error)
{
    static const size_t SHEET = GLYPH_FONT_W * GLYPH_FONT_H;
    uint32_t fx_table = index_u32(level, INDEX_FX_TEXTURES + 4), bank = index_u32(level, INDEX_FX_BANK);
    uint8_t *sheets = malloc(FONTS * (SHEET + PALETTE + GLYPH_CELLS_SIZE));
    size_t places[FONTS];
    char reason[160];

    if (!sheets)
        return out_of_memory(error);
    for (int f = 0; f < FONTS; f++) {
        size_t entry = fx_table + 16u * (unsigned)(f + 1);
        int64_t palette = (int64_t)bank + index_s32(level, entry), pixels = (int64_t)bank + index_s32(level, entry + 4);
        int32_t width = index_s32(level, entry + 8), height = index_s32(level, entry + 12);

        if (level->malformed) {
            free(sheets);
            return check_index(level, error);
        }
        if (width != GLYPH_FONT_W || height != GLYPH_FONT_H) {
            free(sheets);
            return install_fail(error, "internal", "level %d: effect texture %d is %dx%d, not a 256x128 font",
                                level->number, f + 1, (int)width, (int)height);
        }
        if (palette < 0 || pixels < 0 || (uint64_t)palette + PALETTE > level->core.size ||
            (uint64_t)pixels + SHEET > level->core.size) {
            free(sheets);
            return install_fail(error, "internal", "level %d: font %d lies outside the core", level->number, f + 1);
        }
        places[f] = (size_t)pixels;
        memcpy(sheets + f * SHEET, level->core.bytes + pixels, SHEET);
        memcpy(sheets + FONTS * SHEET + f * PALETTE, level->core.bytes + palette, PALETTE);
    }
    for (int f = 0; f < FONTS; f++) {
        uint8_t *cells = sheets + FONTS * (SHEET + PALETTE) + f * GLYPH_CELLS_SIZE;

        if (!glyph_cells(sheets + f * SHEET, sheets + FONTS * SHEET + f * PALETTE, cells, reason, sizeof reason)) {
            free(sheets);
            return install_fail(error, "internal", "level %d: %s", level->number, reason);
        }
        glyph_paint(sheets + f * SHEET, cells);
        memcpy(level->core.bytes + places[f], sheets + f * SHEET, SHEET);
    }
    free(sheets);
    return true;
}

/* ----------------------------------------------------------------------------------------------- the boots */

/* The Charge Boots: the class core in R&C1's format, and its one texture as Going Commando's level holds it. */
struct boots {
    uint8_t *core_class;
    size_t class_size;
    uint8_t *pixels;
    unsigned width, height;
    uint8_t palette[PALETTE];
};

/* Adds an entry at the table's end, copied to the index's end: the table at AT (its ENTRIES, COUNT of them, as
   they were read) and the new ADDED bytes. */
static bool copy_table(struct level *level, size_t at, const uint8_t *entries, uint32_t count, size_t entry,
                       const uint8_t *added, size_t added_count)
{
    size_t offset;

    if (!buffer_pad(&level->index, 16))
        return false;
    offset = level->index.size;
    if (!buffer_append(&level->index, entries, (size_t)count * entry) ||
        !buffer_append(&level->index, added, added_count * entry))
        return false;
    index_put(level, at, count + (uint32_t)added_count);
    index_put(level, at + 4, (uint32_t)offset);
    return true;
}

/* The class table grown in place by COUNT entries (ENTRIES) at its end, every index table after it moving down
   with its pointer. */
static bool grow_class_table(struct level *level, uint32_t classes, uint32_t classes_at, const uint8_t *entries,
                             size_t count)
{
    size_t grow_at = classes_at + (size_t)CLASS_ENTRY * classes, grow = (size_t)CLASS_ENTRY * count;

    for (size_t k = 0; k < sizeof INDEX_POINTERS; k++) {
        uint32_t value = index_u32(level, INDEX_POINTERS[k]);

        if (value >= grow_at)
            index_put(level, INDEX_POINTERS[k], value + (uint32_t)grow);
    }
    if (grow_at > level->index.size) {
        level->malformed = true;
        return true;
    }
    if (!buffer_insert(&level->index, grow_at, entries, grow))
        return false;
    index_put(level, INDEX_MOBY_CLASSES, classes + (uint32_t)count);
    return true;
}

/* A copy of the table at AT's entries as they stand, in a new buffer (NULL when out of memory). */
static uint8_t *table_copy(struct level *level, size_t at, size_t entry, uint32_t *count)
{
    uint32_t offset;
    uint8_t *copy;

    index_table(level, at, entry, count, &offset);
    copy = malloc((size_t)*count * entry + 1);
    if (copy && *count)
        memcpy(copy, level->index.bytes + offset, (size_t)*count * entry);
    return copy;
}

static void put_texture(uint8_t *entry, int32_t pixels, unsigned width, unsigned height, unsigned mips,
                        unsigned palette, const uint8_t *last)
{
    write_u32_le(entry, (uint32_t)pixels);
    write_u16_le(entry + 4, (uint16_t)width);
    write_u16_le(entry + 6, (uint16_t)height);
    write_u16_le(entry + 8, (uint16_t)mips);
    write_u16_le(entry + 10, (uint16_t)palette);
    memcpy(entry + 12, last, 4);
}

static void put_upload(uint8_t *entry, uint32_t psm, uint32_t packed_size, uint32_t address)
{
    write_u32_le(entry, psm);
    write_u32_le(entry + 4, packed_size);
    write_u32_le(entry + 8, address);
    write_u32_le(entry + 12, address);
}

/*
 * The Charge Boots class and its texture added to LEVEL, the one way the game accepted: the class blob and the
 * texture's pixels inserted where the moby class blobs end, the palette appended to GS RAM at a 0x100 boundary,
 * the class table grown in place, the texture table and upload list copied to the index's end. The texture is
 * type 1 (one mip level), as every texture of an item Ratchet carries or wears is.
 */
static bool add_boots(struct level *level, const struct boots *boots, size_t *insert_at, size_t *shifted,
                      InstallError *error)
{
    static const uint8_t NO_MATES[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t classes, classes_at, textures, uploads, textures_base = index_u32(level, INDEX_TEXTURES_BASE);
    uint8_t *texture_entries = table_copy(level, INDEX_MOBY_TEXTURES, TEXTURE_ENTRY, &textures);
    uint8_t *upload_entries = table_copy(level, INDEX_UPLOADS, UPLOAD_ENTRY, &uploads);
    uint8_t class_entry[CLASS_ENTRY], texture_entry[TEXTURE_ENTRY], upload_entry[UPLOAD_ENTRY];
    size_t tex_at, delta, palette_at = level->gs.size;
    struct pointers list;
    bool ok = false;

    if (!texture_entries || !upload_entries) {
        out_of_memory(error);
        goto out;
    }
    index_table(level, INDEX_MOBY_CLASSES, CLASS_ENTRY, &classes, &classes_at);
    for (uint32_t i = 0; i < classes && !level->malformed; i++) {
        if (index_s32(level, classes_at + (size_t)CLASS_ENTRY * i + 4) == BOOTS_CLASS) {
            install_fail(error, "internal", "level %d: class %d is already in it", level->number, BOOTS_CLASS);
            goto out;
        }
    }
    if (!check_index(level, error))
        goto out;
    if (classes + 1 > MAX_CLASSES || textures > MAX_SLOT) {
        install_fail(error, "internal", "level %d: no room: %u classes, %u moby textures", level->number, classes,
                     textures);
        goto out;
    }
    if (!check_uploads(level, error) || !moby_classes_end(level, insert_at, error) ||
        !core_pointers(level, &list, error))
        goto out;

    /* core data: the class blob and the pixels where the moby class blobs end, every core offset past them moved
       up; the textures' offsets count from their base, which stays put */
    tex_at = *insert_at + align(boots->class_size, SECTION_ALIGN);
    delta = tex_at - *insert_at + align((size_t)boots->width * boots->height, SECTION_ALIGN);
    *shifted = shift_pointers(level, &list, *insert_at, delta);
    free(list.items);
    *shifted += shift_textures(level, *insert_at, delta);       /* none on the retail disc */
    if (*insert_at > level->core.size || !buffer_insert(&level->core, *insert_at, NULL, delta)) {
        if (*insert_at > level->core.size)
            install_fail(error, "internal", "level %d: the moby classes end past the core", level->number);
        else
            out_of_memory(error);
        goto out;
    }
    memcpy(level->core.bytes + *insert_at, boots->core_class, boots->class_size);
    memcpy(level->core.bytes + tex_at, boots->pixels, (size_t)boots->width * boots->height);

    /* GS RAM: the palette */
    if (!buffer_append(&level->gs, boots->palette, PALETTE)) {
        out_of_memory(error);
        goto out;
    }

    /* core index: the class entry (texture slot NEXT, the list ended), the texture and the palette's upload */
    write_u32_le(class_entry, (uint32_t)*insert_at);
    write_u32_le(class_entry + 4, BOOTS_CLASS);
    memset(class_entry + 8, 0, 8);
    class_entry[16] = (uint8_t)textures;
    memset(class_entry + 17, 0xFF, 15);
    put_texture(texture_entry, (int32_t)(tex_at - textures_base), boots->width, boots->height, 1,
                (unsigned)(palette_at / 0x100), NO_MATES);
    put_upload(upload_entry, 0, 0, (uint32_t)palette_at);
    if (!grow_class_table(level, classes, classes_at, class_entry, 1) ||
        !copy_table(level, INDEX_UPLOADS, upload_entries, uploads, UPLOAD_ENTRY, upload_entry, 1) ||
        !copy_table(level, INDEX_MOBY_TEXTURES, texture_entries, textures, TEXTURE_ENTRY, texture_entry, 1) ||
        !buffer_pad(&level->index, 16)) {
        out_of_memory(error);
        goto out;
    }
    ok = check_index(level, error);
out:
    free(texture_entries);
    free(upload_entries);
    return ok;
}

/* ------------------------------------------------------------------------------------------------- the sign */

/*
 * Kalebo III's sign class replaced by BLOB in place. The new blob is larger, so its space grows by whole 0x100-byte
 * steps (every later block keeps its alignment), and every core offset and texture past it moves up by as much (the
 * Charge Boots texture among them); the class stays inside the resident part of the core, where the level had it.
 */
static bool replace_sign(struct level *level, const uint8_t *blob, size_t size, size_t *old, InstallError *error)
{
    uint32_t classes, classes_at, resident_end = index_u32(level, INDEX_RESIDENT_END);
    int64_t start = -1, end = -1;
    int found = 0;
    size_t region, delta;
    struct pointers list;

    index_table(level, INDEX_MOBY_CLASSES, CLASS_ENTRY, &classes, &classes_at);
    for (uint32_t i = 0; i < classes; i++) {
        if (index_s32(level, classes_at + (size_t)CLASS_ENTRY * i + 4) == SIGN_CLASS) {
            start = index_s32(level, classes_at + (size_t)CLASS_ENTRY * i);
            found++;
        }
    }
    if (!check_index(level, error))
        return false;
    if (found != 1 || start <= 0)
        return install_fail(error, "internal", "level %d has no class %d blob", level->number, SIGN_CLASS);
    if (!core_pointers(level, &list, error))
        return false;
    for (size_t k = 0; k < list.count; k++) {
        if (list.items[k].value > start && (end < 0 || list.items[k].value < end))
            end = list.items[k].value;
    }
    if (end < 0 || end > resident_end || (uint64_t)end > level->core.size) {
        free(list.items);
        return install_fail(error, "internal", "class %d at 0x%llx..0x%llx is not in the resident part (ends 0x%x)",
                            SIGN_CLASS, (unsigned long long)start, (unsigned long long)end, resident_end);
    }
    *old = (size_t)(end - start);
    region = size > *old ? *old + align(size - *old, SIGN_STEP) : *old;
    delta = region - *old;
    shift_pointers(level, &list, (size_t)end, delta);
    free(list.items);
    shift_textures(level, (size_t)end, delta);              /* the Charge Boots texture lies past the sign */
    if (!buffer_insert(&level->core, (size_t)end, NULL, delta))
        return out_of_memory(error);
    memcpy(level->core.bytes + start, blob, size);
    memset(level->core.bytes + start + size, 0, region - size);
    return check_index(level, error);
}

/* -------------------------------------------------------------------------------------------- extra classes */

/* A texture of a class copied from its level: its texture entry, pixels, palette and the palette's upload entry. */
struct class_texture {
    uint8_t entry[TEXTURE_ENTRY];
    uint8_t *pixels;
    size_t pixel_size;
    uint8_t palette[PALETTE];
    uint8_t upload[UPLOAD_ENTRY];
};

/* A class copied from its level: its blob, then its textures in the order of its slots. */
struct class_source {
    uint8_t *blob;
    size_t blob_size;
    struct class_texture textures[16];
    size_t texture_count;
};

static void class_source_free(struct class_source *source)
{
    free(source->blob);
    for (size_t t = 0; t < source->texture_count; t++)
        free(source->textures[t].pixels);
    memset(source, 0, sizeof *source);
}

static size_t texture_size(unsigned width, unsigned height, unsigned mips)
{
    size_t size = 0;

    for (unsigned i = 0; i < (mips > 1 ? mips : 1); i++)
        size += (size_t)(width >> i) * (height >> i);
    return size;
}

/* Class ID's texture in slot SLOT of LEVEL, with its pixels and its palette's place in GS RAM. */
static bool class_texture(struct level *level, int32_t id, unsigned slot, struct class_texture *texture,
                          InstallError *error)
{
    uint32_t textures, textures_at, uploads, uploads_at, base = index_u32(level, INDEX_TEXTURES_BASE);
    const uint8_t *entry;
    int64_t pixels;
    size_t palette_at;

    index_table(level, INDEX_MOBY_TEXTURES, TEXTURE_ENTRY, &textures, &textures_at);
    index_table(level, INDEX_UPLOADS, UPLOAD_ENTRY, &uploads, &uploads_at);
    if (!check_index(level, error))
        return false;
    if (slot >= textures)
        return install_fail(error, "internal", "class %d: texture slot %u is not in level %d", id, slot,
                            level->number);
    entry = level->index.bytes + textures_at + (size_t)TEXTURE_ENTRY * slot;
    memcpy(texture->entry, entry, TEXTURE_ENTRY);
    pixels = (int64_t)base + (int32_t)read_u32_le(entry);
    texture->pixel_size = texture_size(read_u16_le(entry + 4), read_u16_le(entry + 6), read_u16_le(entry + 8));
    if (pixels < 0 || (uint64_t)pixels + texture->pixel_size > level->core.size)
        return install_fail(error, "internal", "class %d: texture slot %u lies outside level %d's core", id, slot,
                            level->number);
    palette_at = (size_t)read_u16_le(entry + 10) * 0x100;
    for (uint32_t u = 0; u < uploads && !texture->pixels; u++) {
        const uint8_t *upload = level->index.bytes + uploads_at + (size_t)UPLOAD_ENTRY * u;

        if (read_u32_le(upload + 8) == palette_at && palette_at + PALETTE <= level->gs.size) {
            memcpy(texture->upload, upload, UPLOAD_ENTRY);
            memcpy(texture->palette, level->gs.bytes + palette_at, PALETTE);
            texture->pixels = malloc(texture->pixel_size + 1);
            if (!texture->pixels)
                return out_of_memory(error);
        }
    }
    if (!texture->pixels)
        return install_fail(error, "internal", "class %d: texture slot %u's palette at 0x%zx is not in GS RAM's "
                            "upload list", id, slot, palette_at);
    memcpy(texture->pixels, level->core.bytes + pixels, texture->pixel_size);
    return true;
}

/* Class ID's blob (up to the next class's start, or the resident part's end) and its textures, from LEVEL. */
static bool class_source(struct level *level, int32_t id, struct class_source *source, InstallError *error)
{
    uint32_t classes, classes_at, resident_end = index_u32(level, INDEX_RESIDENT_END);
    const uint8_t *entry = NULL;
    int64_t start, end = -1;

    memset(source, 0, sizeof *source);
    index_table(level, INDEX_MOBY_CLASSES, CLASS_ENTRY, &classes, &classes_at);
    for (uint32_t i = 0; i < classes && !entry; i++) {
        if (index_s32(level, classes_at + (size_t)CLASS_ENTRY * i + 4) == id)
            entry = level->index.bytes + classes_at + (size_t)CLASS_ENTRY * i;
    }
    if (!check_index(level, error))
        return false;
    if (!entry || (int32_t)read_u32_le(entry) <= 0)
        return install_fail(error, "internal", "class %d has no data in level %d", id, level->number);
    start = (int32_t)read_u32_le(entry);
    for (uint32_t i = 0; i < classes; i++) {
        int32_t other = (int32_t)read_u32_le(level->index.bytes + classes_at + (size_t)CLASS_ENTRY * i);

        if (other > start && (end < 0 || other < end))
            end = other;
    }
    if (end < 0)
        end = resident_end;
    if (end > (int64_t)level->core.size)
        end = (int64_t)level->core.size;
    source->blob_size = end > start ? (size_t)(end - start) : 0;
    source->blob = malloc(source->blob_size + 1);
    if (!source->blob)
        return out_of_memory(error);
    memcpy(source->blob, level->core.bytes + start, source->blob_size);
    for (int s = 16; s < 32 && entry[s] != 0xFF; s++) {
        /* ENTRY stays put: class_texture reads the index without changing it. A texture is counted once its pixels
           are its own, so class_source_free frees what was made. */
        struct class_texture *texture = &source->textures[source->texture_count];

        if (!class_texture(level, id, entry[s], texture, error))
            return false;
        source->texture_count++;
    }
    return true;
}

/* Whether LEVEL (its number) gets the extra class K at all. */
static bool gets_extra(int number, int k)
{
    for (size_t n = 0; EXTRA[k].one_planet && n < sizeof NEAR_THE_STACK / sizeof *NEAR_THE_STACK; n++) {
        if (NEAR_THE_STACK[n] == number)
            return false;
    }
    return true;
}

/* The extra classes LEVEL lacks, as indices into EXTRA: those it gets (WANTED), and those its class table has no
   room for (LEFT_OUT). */
static bool missing_classes(struct level *level, int wanted[EXTRA_CLASSES], size_t *wanted_count,
                            int left_out[EXTRA_CLASSES], size_t *left_count, InstallError *error)
{
    uint32_t classes, classes_at;
    int lacks[EXTRA_CLASSES];
    size_t lack_count = 0, split;
    int64_t room;

    index_table(level, INDEX_MOBY_CLASSES, CLASS_ENTRY, &classes, &classes_at);
    if (!check_index(level, error))
        return false;
    for (int k = 0; k < EXTRA_CLASSES; k++) {
        bool has = false;

        for (uint32_t i = 0; i < classes && !has; i++)
            has = (int32_t)read_u32_le(level->index.bytes + classes_at + (size_t)CLASS_ENTRY * i + 4) == EXTRA[k].id;
        if (!has && gets_extra(level->number, k))
            lacks[lack_count++] = k;
    }
    /* the first ROOM of them, ROOM counting from the end when the table is over full */
    room = (int64_t)MAX_CLASSES - classes;
    split = room >= 0 ? (room < (int64_t)lack_count ? (size_t)room : lack_count)
                      : (-room < (int64_t)lack_count ? lack_count - (size_t)-room : 0);
    *wanted_count = split;
    *left_count = lack_count - split;
    memcpy(wanted, lacks, split * sizeof *lacks);
    memcpy(left_out, lacks + split, (lack_count - split) * sizeof *lacks);
    for (size_t k = 0; k < *left_count; k++) {
        if (EXTRA[left_out[k]].id == INFOBOT_CLASS)
            return install_fail(error, "internal", "level %d: no room for the infobot class", level->number);
    }
    return true;
}

/* The sound block's spans ({s16 offset from the block, s16 count} each): the array runs up to the first span's
   data. */
static bool sound_spans(struct level *level, int16_t spans[][2], size_t *count, InstallError *error)
{
    uint32_t block_at = index_u32(level, INDEX_SOUND_BLOCK);
    int64_t data_at = -1;

    *count = 0;
    for (;;) {
        uint32_t word = index_u32(level, block_at + 4 * *count);
        int16_t offset = (int16_t)(word & 0xFFFF), entries = (int16_t)(word >> 16);

        if (!check_index(level, error))
            return false;
        spans[*count][0] = offset;
        spans[(*count)++][1] = entries;
        if (entries > 0 && (data_at < 0 || offset < data_at))
            data_at = offset;
        if (data_at >= 0 && (int64_t)(4 * *count) >= data_at)
            return true;
        if (*count > MAX_CLASSES + SPAN_FIXED)
            return install_fail(error, "internal", "level %d: the sound block's spans do not end", level->number);
    }
}

/*
 * The sound block rebuilt with a span for each of CLASSES classes after the two fixed ones (the slot table, whose
 * entries are 32 bytes, and the remap, 4 bytes an entry like the classes'): the spans the block has, then empty
 * ones. The level loader walks exactly SPAN_FIXED + CLASSES spans.
 */
static bool rebuilt_sound_block(struct level *level, size_t classes, struct buffer *out, InstallError *error)
{
    int16_t spans[MAX_CLASSES + SPAN_FIXED + 1][2];
    size_t at[MAX_CLASSES + SPAN_FIXED + 1], count, wanted = SPAN_FIXED + classes, array_size;
    int64_t block_at = index_u32(level, INDEX_SOUND_BLOCK);
    struct buffer data = {NULL, 0, 0};
    bool ok = false;

    if (!sound_spans(level, spans, &count, error))
        return false;
    while (count > wanted && spans[count - 1][0] == 0 && spans[count - 1][1] == 0)
        count--;                                            /* the array's padding, read as spans */
    if (count > wanted)
        return install_fail(error, "internal", "level %d: the sound block has %zu spans for %zu classes",
                            level->number, count, classes);
    for (size_t s = 0; s < count; s++) {
        int64_t size = (int64_t)spans[s][1] * (s == 0 ? 32 : 4), from = block_at + spans[s][0];

        at[s] = data.size;
        if (size > 0 && (from < 0 || (uint64_t)(from + size) > level->index.size)) {
            install_fail(error, "internal", "level %d: a sound span lies outside the index", level->number);
            goto out;
        }
        if ((size > 0 && !buffer_append(&data, level->index.bytes + from, (size_t)size)) || !buffer_pad(&data, 16)) {
            out_of_memory(error);
            goto out;
        }
    }
    array_size = align(4 * wanted, 16);
    out->size = 0;
    if (!buffer_reserve(out, array_size + data.size)) {
        out_of_memory(error);
        goto out;
    }
    memset(out->bytes, 0, array_size);
    for (size_t s = 0; s < wanted; s++) {
        size_t offset = array_size + (s < count ? at[s] : data.size);

        if (offset > 0x7FFF) {
            install_fail(error, "internal", "level %d: the sound block is too big for its 16-bit offsets",
                         level->number);
            goto out;
        }
        write_u16_le(out->bytes + 4 * s, (uint16_t)offset);
        write_u16_le(out->bytes + 4 * s + 2, (uint16_t)(s < count ? spans[s][1] : 0));
    }
    out->size = array_size;
    if (!buffer_append(out, data.bytes, data.size)) {
        out_of_memory(error);
        goto out;
    }
    ok = out->size < 0x8000 ||
         install_fail(error, "internal", "level %d: the sound block is too big for its 16-bit offsets", level->number);
out:
    free(data.bytes);
    return ok;
}

/* The insert of the extra classes WANTED (indices into EXTRA, their copies in SOURCES), laid out from core offset
   INSERT_AT: each class's blob, then its textures' pixels, 0x100 apart; with their class, texture and upload
   entries, and the palettes for GS RAM in UPLOADS' order. */
struct extra_insert {
    struct buffer core;
    uint8_t classes[EXTRA_CLASSES * CLASS_ENTRY];
    size_t class_count;
    uint8_t textures[(MAX_SLOT + 1) * TEXTURE_ENTRY];
    uint8_t uploads[(MAX_SLOT + 1) * UPLOAD_ENTRY];
    const uint8_t *palettes[MAX_SLOT + 1];
    size_t texture_count;
};

static bool lay_out_extras(struct level *level, const int *wanted, size_t count,
                           const struct class_source sources[EXTRA_CLASSES], size_t insert_at, uint32_t textures,
                           struct extra_insert *insert, InstallError *error)
{
    uint32_t base = index_u32(level, INDEX_TEXTURES_BASE);

    for (size_t c = 0; c < count; c++) {
        const struct class_source *source = &sources[wanted[c]];
        uint8_t *entry = insert->classes + CLASS_ENTRY * insert->class_count++;
        size_t blob_at = insert_at + insert->core.size;

        if (!buffer_append(&insert->core, source->blob, source->blob_size) || !buffer_pad(&insert->core, 0x100))
            return out_of_memory(error);
        write_u32_le(entry, (uint32_t)blob_at);
        write_u32_le(entry + 4, (uint32_t)EXTRA[wanted[c]].id);
        memset(entry + 8, 0, 8);
        memset(entry + 16, 0xFF, 16);
        for (size_t t = 0; t < source->texture_count; t++) {
            const struct class_texture *texture = &source->textures[t];
            size_t pixel_at = insert_at + insert->core.size, slot = textures + insert->texture_count;
            size_t palette_at = level->gs.size + PALETTE * insert->texture_count;

            if (slot > MAX_SLOT)
                return install_fail(error, "internal", "level %d: no room: %u moby textures", level->number,
                                    textures);
            if (palette_at / 0x100 > 0xFFFF)
                return install_fail(error, "internal", "level %d: GS RAM is full", level->number);
            if (!buffer_append(&insert->core, texture->pixels, texture->pixel_size) ||
                !buffer_pad(&insert->core, 0x100))
                return out_of_memory(error);
            put_texture(insert->textures + TEXTURE_ENTRY * insert->texture_count, (int32_t)(pixel_at - base),
                        read_u16_le(texture->entry + 4), read_u16_le(texture->entry + 6),
                        read_u16_le(texture->entry + 8), (unsigned)(palette_at / 0x100), texture->entry + 12);
            put_upload(insert->uploads + UPLOAD_ENTRY * insert->texture_count, read_u32_le(texture->upload),
                       read_u32_le(texture->upload + 4), (uint32_t)palette_at);
            insert->palettes[insert->texture_count++] = texture->palette;
            entry[16 + t] = (uint8_t)slot;
        }
    }
    return true;
}

/*
 * The extra classes LEVEL lacks added to it, as the Charge Boots are (add_boots), each class's palettes appended to
 * GS RAM; and the sound block rebuilt at the index's end with a span for every class (the boots' class had none).
 * *WANTED_COUNT is 0 when it lacks none.
 */
static bool add_extra_classes(struct level *level, const struct class_source sources[EXTRA_CLASSES],
                              int wanted[EXTRA_CLASSES], size_t *wanted_count, int left_out[EXTRA_CLASSES],
                              size_t *left_count, InstallError *error)
{
    uint32_t classes, classes_at, textures, textures_at, uploads;
    uint8_t *texture_entries = NULL, *upload_entries = NULL;
    struct extra_insert *insert = NULL;
    struct buffer block = {NULL, 0, 0};
    struct pointers list;
    size_t insert_at;
    bool ok = false;

    if (!missing_classes(level, wanted, wanted_count, left_out, left_count, error))
        return false;
    if (*wanted_count == 0)
        return true;
    index_table(level, INDEX_MOBY_CLASSES, CLASS_ENTRY, &classes, &classes_at);
    index_table(level, INDEX_MOBY_TEXTURES, TEXTURE_ENTRY, &textures, &textures_at);
    if (!check_index(level, error))
        return false;
    if (classes + *wanted_count > MAX_CLASSES)
        return install_fail(error, "internal", "level %d: no room: %u classes", level->number, classes);
    if (!check_uploads(level, error) || !moby_classes_end(level, &insert_at, error))
        return false;
    insert = calloc(1, sizeof *insert);
    if (!insert)
        return out_of_memory(error);
    if (!lay_out_extras(level, wanted, *wanted_count, sources, insert_at, textures, insert, error) ||
        !core_pointers(level, &list, error))
        goto out;
    shift_pointers(level, &list, insert_at, insert->core.size);
    free(list.items);
    shift_textures(level, insert_at, insert->core.size);
    /* the tables copied to the end are read now that their core offsets are shifted (the Charge Boots texture and
       the gadget blobs lie past the insertion) */
    texture_entries = table_copy(level, INDEX_MOBY_TEXTURES, TEXTURE_ENTRY, &textures);
    upload_entries = table_copy(level, INDEX_UPLOADS, UPLOAD_ENTRY, &uploads);
    if (!texture_entries || !upload_entries || insert_at > level->core.size ||
        !buffer_insert(&level->core, insert_at, insert->core.bytes, insert->core.size)) {
        if (insert_at > level->core.size)
            install_fail(error, "internal", "level %d: the moby classes end past the core", level->number);
        else
            out_of_memory(error);
        goto out;
    }
    for (size_t t = 0; t < insert->texture_count; t++) {
        if (!buffer_append(&level->gs, insert->palettes[t], PALETTE)) {
            out_of_memory(error);
            goto out;
        }
    }

    /* the index: the class table grown in place; the upload and texture tables copied to the end with their new
       entries; the sound block rebuilt there with a span per class */
    if (!grow_class_table(level, classes, classes_at, insert->classes, insert->class_count) ||
        !copy_table(level, INDEX_UPLOADS, upload_entries, uploads, UPLOAD_ENTRY, insert->uploads,
                    insert->texture_count) ||
        !copy_table(level, INDEX_MOBY_TEXTURES, texture_entries, textures, TEXTURE_ENTRY, insert->textures,
                    insert->texture_count)) {
        out_of_memory(error);
        goto out;
    }
    if (!check_index(level, error) || !rebuilt_sound_block(level, classes + insert->class_count, &block, error))
        goto out;
    if (!buffer_pad(&level->index, 16)) {
        out_of_memory(error);
        goto out;
    }
    index_put(level, INDEX_SOUND_BLOCK, (uint32_t)level->index.size);
    if (!buffer_append(&level->index, block.bytes, block.size) || !buffer_pad(&level->index, 16)) {
        out_of_memory(error);
        goto out;
    }
    ok = check_index(level, error);
out:
    free(block.bytes);
    free(texture_entries);
    free(upload_entries);
    if (insert)
        free(insert->core.bytes);
    free(insert);
    return ok;
}

/* ------------------------------------------------------------------------------------- the boots, from the discs */

/* Going Commando's level 1: its core index, GS RAM image and core data, decoded. */
struct rac2_level {
    uint8_t *index;
    size_t index_size;
    uint8_t *gs;
    size_t gs_size;
    uint8_t *core;
    size_t core_size;
};

static void rac2_level_free(struct rac2_level *level)
{
    free(level->index);
    free(level->gs);
    free(level->core);
}

/* SIZE bytes at AT of IMAGE, decoded when they are a WAD stream; NULL when they cannot be read. */
static uint8_t *rac2_section(FILE *image, uint64_t at, size_t size, size_t *out_size)
{
    uint8_t *stored = malloc(size ? size : 1), *decoded;

    if (!stored || !file_read_at(image, at, stored, size)) {
        free(stored);
        return NULL;
    }
    *out_size = size;
    if (size < 3 || memcmp(stored, "WAD", 3) != 0)
        return stored;
    decoded = disc_unwad(stored, size, out_size);
    free(stored);
    return decoded;
}

/* The data archive of G/LEVEL1.WAD: its first sector at +0x10 of the file, then a table of {offset, size} pairs, the
   core index the second section, GS RAM the third, the core data the tenth. */
static bool read_rac2_level(const char *rac2, struct rac2_level *level, InstallError *error)
{
    FILE *image = file_open(rac2, "rb");
    uint8_t head[0x14], slots[24 * 4];
    IsoEntry file = {0, 0, 0};
    uint64_t data = 0;
    bool read;

    memset(level, 0, sizeof *level);
    read = image && iso_find_path(image, RAC2_LEVEL, &file) &&
           file_read_at(image, (uint64_t)file.extent * ISO_SECTOR, head, sizeof head);
    if (read) {
        data = (uint64_t)file.extent * ISO_SECTOR + (uint64_t)read_u32_le(head + 0x10) * ISO_SECTOR;
        read = file_read_at(image, data, slots, sizeof slots);
    }
    if (read) {
        level->index = rac2_section(image, data + read_u32_le(slots + 2 * 4), read_u32_le(slots + 3 * 4),
                                    &level->index_size);
        level->gs = rac2_section(image, data + read_u32_le(slots + 4 * 4), read_u32_le(slots + 5 * 4),
                                 &level->gs_size);
        level->core = rac2_section(image, data + read_u32_le(slots + 18 * 4), read_u32_le(slots + 19 * 4),
                                   &level->core_size);
        read = level->index && level->gs && level->core && level->index_size >= INDEX_HEADER;
    }
    if (image)
        fclose(image);
    if (!read) {
        rac2_level_free(level);
        memset(level, 0, sizeof *level);
        return install_fail(error, "internal", "%s cannot be read from Going Commando's disc %s", RAC2_LEVEL, rac2);
    }
    return true;
}

/* The core index's class table entry for ID in Going Commando's LEVEL, or NULL. */
static const uint8_t *rac2_class_entry(const struct rac2_level *level, int32_t id, uint32_t *count, uint32_t *at)
{
    *count = read_u32_le(level->index + INDEX_MOBY_CLASSES);
    *at = read_u32_le(level->index + INDEX_MOBY_CLASSES + 4);
    if (*at > level->index_size || (level->index_size - *at) / CLASS_ENTRY < *count)
        return NULL;
    for (uint32_t i = 0; i < *count; i++) {
        const uint8_t *entry = level->index + *at + (size_t)CLASS_ENTRY * i;

        if ((int32_t)read_u32_le(entry + 4) == id)
            return entry;
    }
    return NULL;
}

/*
 * The Charge Boots' class core as Going Commando's level holds it (up to the next class's start), and its first
 * texture: 8-bit pixels from the core's textures and the palette from GS RAM as the GS reads it.
 */
static bool rac2_boots(const struct rac2_level *level, uint8_t **blob, size_t *blob_size, struct boots *boots,
                       InstallError *error)
{
    uint32_t count, at, textures_at = read_u32_le(level->index + INDEX_MOBY_TEXTURES + 4);
    const uint8_t *entry = rac2_class_entry(level, RAC2_BOOTS_CLASS, &count, &at), *texture;
    int64_t start = entry ? (int32_t)read_u32_le(entry) : 0, end = (int64_t)level->core_size, pixels;
    size_t palette_at;

    if (start <= 0 || (uint64_t)start > level->core_size)
        return install_fail(error, "internal", "Going Commando's level 1 has no moby class %d", RAC2_BOOTS_CLASS);
    for (uint32_t i = 0; i < count; i++) {
        int32_t other = (int32_t)read_u32_le(level->index + at + (size_t)CLASS_ENTRY * i);

        if (other > start && other < end)
            end = other;
    }
    *blob_size = (size_t)(end - start);
    *blob = malloc(*blob_size + 1);
    if (!*blob)
        return out_of_memory(error);
    memcpy(*blob, level->core + start, *blob_size);

    if (textures_at > level->index_size || level->index_size - textures_at < (size_t)TEXTURE_ENTRY * (entry[16] + 1u))
        return install_fail(error, "internal", "Going Commando's class %d has no texture", RAC2_BOOTS_CLASS);
    texture = level->index + textures_at + (size_t)TEXTURE_ENTRY * entry[16];
    boots->width = read_u16_le(texture + 4);
    boots->height = read_u16_le(texture + 6);
    pixels = (int64_t)read_u32_le(level->index + INDEX_TEXTURES_BASE) + (int32_t)read_u32_le(texture);
    palette_at = (size_t)read_u16_le(texture + 10) * 0x100;
    if ((boots->width & (boots->width - 1)) || (boots->height & (boots->height - 1)) || boots->width < 8 ||
        boots->height == 0)
        return install_fail(error, "internal", "R&C2 class %d's texture is %ux%u, not a power-of-two texture",
                            RAC2_BOOTS_CLASS, boots->width, boots->height);
    if (pixels < 0 || (uint64_t)pixels + (size_t)boots->width * boots->height > level->core_size ||
        palette_at + PALETTE > level->gs_size)
        return install_fail(error, "internal", "R&C2 class %d's texture lies outside its level", RAC2_BOOTS_CLASS);
    boots->pixels = malloc((size_t)boots->width * boots->height);
    if (!boots->pixels)
        return out_of_memory(error);
    memcpy(boots->pixels, level->core + pixels, (size_t)boots->width * boots->height);
    memcpy(boots->palette, level->gs + palette_at, PALETTE);
    return true;
}

/* Class ID and its textures as level NUMBER holds them on the PAL disc RAC1. */
static bool read_disc_class(const char *rac1, int number, int32_t id, struct class_source *source,
                            InstallError *error)
{
    FILE *disc = file_open(rac1, "rb");
    struct place places[LEVEL_COUNT];
    struct level level = {0};
    bool ok;

    if (!disc)
        return install_fail(error, "internal", "cannot read the PAL disc %s", rac1);
    if (number < 0 || number >= LEVEL_COUNT) {
        fclose(disc);
        return install_fail(error, "internal", "there is no level %d", number);
    }
    ok = read_places(disc, places, error) && read_level(disc, NULL, &places[number], number, &level, error) &&
         class_source(&level, id, source, error);
    fclose(disc);
    level_free(&level);
    return ok;
}

uint8_t *disc_class(const char *rac1, int number, int32_t id, size_t *size, size_t *texture_count,
                    InstallError *error)
{
    struct class_source source = {0};
    uint8_t *blob = NULL;

    if (read_disc_class(rac1, number, id, &source, error)) {
        blob = source.blob;
        *size = source.blob_size;
        *texture_count = source.texture_count;
        source.blob = NULL;
    }
    class_source_free(&source);
    return blob;
}

/* The Grindboots' class core, from Novalis's data on the PAL disc RAC1: the header bytes only R&C1 keeps. */
static bool grindboots(const char *rac1, struct class_source *source, InstallError *error)
{
    return read_disc_class(rac1, GRINDBOOTS_LEVEL, GRINDBOOTS_CLASS, source, error);
}

/* The R&C1 class made from the R&C2 class core BLOB: with the reference class REF's header bytes 0xa and 0xb, and
   the boot below the ankle hung on the ankle (joint 1) rather than the knee (joint 0), which R&C2's Charge Boots
   hang the whole boot on: blended between z -950 and -1250 in the boot's vertex units (the ankle sits at -1335,
   the ground at -1905), about where the Magneboots blend. */
static bool convert_boots(const uint8_t *blob, size_t size, const struct class_source *ref, struct boots *boots,
                          Job *job, InstallError *error)
{
    struct moby_rebind_counts counts = {0, 0, 0};
    struct moby_class *moby, *check = NULL;
    char reason[256];

    if (ref->blob_size < 0xC)
        return install_fail(error, "internal", "the Grindboots class is too short");
    moby = moby_read(blob, size, MOBY_GC, reason, sizeof reason);
    if (!moby)
        return install_fail(error, "internal", "Going Commando's Charge Boots: %s", reason);
    moby->rac1_byte_a = ref->blob[0xA];
    moby->rac1_byte_b = ref->blob[0xB];
    if (moby_rebind(moby, 0, 1, -950, -1250, &counts, reason, sizeof reason) == 0)
        boots->core_class = moby_write(moby, MOBY_RAC, &boots->class_size, reason, sizeof reason);
    if (boots->core_class)
        check = moby_read(boots->core_class, boots->class_size, MOBY_RAC, reason, sizeof reason);
    moby_free(moby);
    if (!check)
        return install_fail(error, "internal", "the Charge Boots class: %s", reason);
    moby_free(check);
    job_log(job, "Charge Boots: class %d, 0x%zx bytes, from R&C2 class %d (rebound: %d high LOD, %d low LOD and %d "
            "metal vertices)", BOOTS_CLASS, boots->class_size, RAC2_BOOTS_CLASS, counts.high, counts.low,
            counts.metal);
    return true;
}

/* The Charge Boots from the discs: Going Commando's class 3696 and its texture, converted to R&C1's format with
   R&C1's Grindboots class as the reference. */
static bool make_boots(const char *rac1, const char *rac2, struct boots *boots, Job *job, InstallError *error)
{
    struct rac2_level level;
    struct class_source ref = {0};
    uint8_t *blob = NULL;
    size_t blob_size = 0;
    bool ok;

    if (!read_rac2_level(rac2, &level, error))
        return false;
    ok = rac2_boots(&level, &blob, &blob_size, boots, error);
    rac2_level_free(&level);
    ok = ok && grindboots(rac1, &ref, error) && convert_boots(blob, blob_size, &ref, boots, job, error);
    class_source_free(&ref);
    free(blob);
    return ok;
}

/* ------------------------------------------------------------------------------------------------- the pass */

struct LevelsPlan {
    FILE *source;                       /* the levels as the disc has them, for the extra classes' sources */
    struct place places[LEVEL_COUNT];
    struct boots boots;
    bool with_boots;
    const uint8_t *sign;
    size_t sign_size;
    struct class_source sources[EXTRA_CLASSES];
    unsigned threads;
    Job *job;
    uint64_t done;
    uint64_t total;
};

/* Level NUMBER with everything but its extra classes, and what was done to it in DONE: its archive copied from
   ARCHIVE, or read from the plan's source when ARCHIVE is NULL. */
static bool prepare_level(LevelsPlan *plan, int number, const uint8_t *archive, struct level *level, char *done,
                          size_t capacity, InstallError *error)
{
    if (!read_level(plan->source, archive, &plan->places[number], number, level, error) ||
        !paint_fonts(level, error))
        return false;
    snprintf(done, capacity, "fonts");
    if (plan->with_boots) {
        size_t insert_at = 0, shifted = 0;

        if (!add_boots(level, &plan->boots, &insert_at, &shifted, error))
            return false;
        text_extend(done, capacity, ", boots class at 0x%zx (%zu offsets shifted)", insert_at, shifted);
    }
    if (number == SIGN_LEVEL) {
        size_t old = 0;

        if (!replace_sign(level, plan->sign, plan->sign_size, &old, error))
            return false;
        text_extend(done, capacity, ", sign 0x%zx -> 0x%zx bytes", old, plan->sign_size);
    }
    return true;
}

/* The extra classes copied from their levels as this patch leaves them before their own extra classes. */
static bool read_sources(LevelsPlan *plan, InstallError *error)
{
    struct level level = {0};
    char done[256];
    bool ok = true;

    for (int k = 0; k < EXTRA_CLASSES && ok; k++) {
        const struct class_source *source = &plan->sources[k];

        if (job_cancelled(plan->job))
            ok = install_fail(error, "cancelled", "cancelled");
        else if (level.archive == NULL || level.number != EXTRA[k].level) {
            level_free(&level);
            ok = prepare_level(plan, EXTRA[k].level, NULL, &level, done, sizeof done, error);
        }
        if (ok && class_source(&level, EXTRA[k].id, &plan->sources[k], error)) {
            job_log(plan->job, "class %d (%s) from level %d: 0x%zx bytes, %zu textures", (int)EXTRA[k].id,
                    EXTRA[k].name, EXTRA[k].level, source->blob_size, source->texture_count);
            job_progress(plan->job, "patch", ++plan->done, plan->total);
        } else {
            ok = false;
        }
    }
    level_free(&level);
    return ok;
}

/* The level's archive back from its sections, each at a 0x100 boundary after the head, padded to whole sectors,
   the core data replaced by STREAM; in a zeroed buffer of at least MINIMUM bytes. */
static uint8_t *assemble(const struct level *level, const uint8_t *stream, size_t stream_size, size_t minimum,
                         size_t *size)
{
    const uint8_t *data[SECTIONS];
    size_t lengths[SECTIONS], at = level->head_size;
    uint8_t *out;

    for (int s = 0; s < SECTIONS; s++) {
        data[s] = level->archive + level->offsets[s];
        lengths[s] = level->sizes[s];
    }
    data[SECTION_CORE_INDEX] = level->index.bytes;
    lengths[SECTION_CORE_INDEX] = level->index.size;
    data[SECTION_GS_RAM] = level->gs.bytes;
    lengths[SECTION_GS_RAM] = level->gs.size;
    data[SECTION_CORE_DATA] = stream;
    lengths[SECTION_CORE_DATA] = stream_size;
    for (int s = 0; s < SECTIONS; s++) {
        if (level->sizes[s])
            at = align(at, SECTION_ALIGN) + lengths[s];
    }
    *size = align(at, ISO_SECTOR);
    out = calloc(*size > minimum ? *size : minimum, 1);
    if (!out)
        return NULL;
    memcpy(out, level->archive, level->head_size);
    at = level->head_size;
    for (int s = 0; s < SECTIONS; s++) {
        if (!level->sizes[s]) {
            memset(out + 8 * s, 0, 8);
            continue;
        }
        at = align(at, SECTION_ALIGN);
        write_u32_le(out + 8 * s, (uint32_t)at);
        write_u32_le(out + 8 * s + 4, (uint32_t)lengths[s]);
        if (lengths[s])
            memcpy(out + at, data[s], lengths[s]);
        at += lengths[s];
    }
    return out;
}

/* LEVEL's archive with its core compressed again (on the thread pool, then checked by decoding it back, by the
   game's rules) and the index's sizes (+0x88, +0x8C) to match; in a zeroed buffer of at least MINIMUM bytes. The
   decoded core is freed. */
static uint8_t *encode_level(LevelsPlan *plan, struct level *level, size_t minimum, size_t *size,
                             InstallError *error)
{
    size_t core_size = level->core.size, stream_size = 0, decoded = 0;
    uint8_t *stream = malloc(wad_bound(core_size)), *check = NULL, *archive = NULL;

    if (stream)
        stream_size = wad_compress(level->core.bytes, core_size, stream, pool_run, &plan->threads);
    if (stream_size)
        check = malloc(core_size + 1);
    if (!check) {
        free(stream);
        out_of_memory(error);
        return NULL;
    }
    if (wad_decompress(stream, stream_size, check, core_size, &decoded) != WAD_OK || decoded != core_size ||
        memcmp(check, level->core.bytes, core_size) != 0) {
        install_fail(error, "internal", "level %d: the compressed core does not decode to the core", level->number);
    } else {
        free(check);
        check = NULL;
        free(level->core.bytes);
        memset(&level->core, 0, sizeof level->core);
        index_put(level, INDEX_STREAM_SIZE, (uint32_t)stream_size);
        index_put(level, INDEX_CORE_SIZE, (uint32_t)core_size);
        archive = assemble(level, stream, stream_size, minimum, size);
        if (!archive)
            out_of_memory(error);
    }
    free(check);
    free(stream);
    return archive;
}

/* Level NUMBER's new archive (*SIZE bytes, zeroed up to at least its sectors): prepared, given its extra classes
   and encoded, from ARCHIVE (its archive as the disc has it) or from the plan's source when ARCHIVE is NULL. What
   was done to it in DONE, and the extra classes its class table has no room for in NO_ROOM ("" when none). */
static uint8_t *make_archive(LevelsPlan *plan, int number, const uint8_t *archive, size_t *size, char *done,
                             size_t done_capacity, char *no_room, size_t no_room_capacity, InstallError *error)
{
    struct level level = {0};
    int wanted[EXTRA_CLASSES], left_out[EXTRA_CLASSES];
    int32_t ids[EXTRA_CLASSES];
    size_t wanted_count = 0, left_count = 0;
    char list[64];
    uint8_t *made = NULL;

    no_room[0] = 0;
    if (prepare_level(plan, number, archive, &level, done, done_capacity, error) &&
        add_extra_classes(&level, plan->sources, wanted, &wanted_count, left_out, &left_count, error)) {
        if (wanted_count) {
            for (size_t k = 0; k < wanted_count; k++)
                ids[k] = EXTRA[wanted[k]].id;
            id_list(list, sizeof list, ids, wanted_count);
            text_extend(done, done_capacity, ", classes %s", list);
            if (left_count) {
                for (size_t k = 0; k < left_count; k++)
                    ids[k] = EXTRA[left_out[k]].id;
                id_list(no_room, no_room_capacity, ids, left_count);
            }
        }
        made = encode_level(plan, &level, (size_t)plan->places[number].sectors * ISO_SECTOR, size, error);
    }
    level_free(&level);
    return made;
}

/* The log lines and the progress step for level NUMBER, written WHERE. */
static void level_done(LevelsPlan *plan, int number, const char *done, const char *no_room, size_t size,
                       const char *where)
{
    job_log(plan->job, "level %2d: %s; archive 0x%zx bytes, %s", number, done, size, where);
    if (no_room[0])
        job_log(plan->job, "  no room in level %d's class table for %s", number, no_room);
    job_progress(plan->job, "patch", ++plan->done, plan->total);
}

static bool is_left_out(const char *const *left_out, const char *id)
{
    for (; left_out && *left_out; left_out++) {
        if (strcmp(*left_out, id) == 0)
            return true;
    }
    return false;
}

LevelsPlan *levels_plan(FILE *source, const BuildFacts *facts, const uint8_t *sign, size_t size, Job *job,
                        InstallError *error)
{
    const char *rac1 = json_get_string(facts->discs, "rac1"), *rac2 = json_get_string(facts->discs, "rac2");
    LevelsPlan *plan = calloc(1, sizeof *plan);
    bool ok;

    if (!plan) {
        out_of_memory(error);
        return NULL;
    }
    *plan = (LevelsPlan){.source = source, .sign = sign, .sign_size = size, .threads = pool_cpus(), .job = job,
                         .total = EXTRA_CLASSES + LEVEL_COUNT};
    ok = rac1 || install_fail(error, "missing_disc", "the levels' patch needs the PAL disc");
    ok = ok && read_places(source, plan->places, error);
    if (ok && !places_apart(plan->places))
        ok = install_fail(error, "internal", "the levels' data overlap on the image");
    if (ok && rac2 && !is_left_out(facts->left_out, "charge_boots")) {
        ok = make_boots(rac1, rac2, &plan->boots, job, error);
        plan->with_boots = ok;
    }
    if (ok) {
        job_progress(job, "patch", 0, plan->total);
        ok = read_sources(plan, error);
    }
    if (!ok) {
        levels_plan_free(plan);
        return NULL;
    }
    return plan;
}

void levels_span(const LevelsPlan *plan, int number, uint32_t *first, uint32_t *count)
{
    const struct place *place = &plan->places[number];

    *first = place->header_sector;
    *count = place->sector + place->sectors - place->header_sector;
}

bool levels_produce(LevelsPlan *plan, int number, uint8_t *span, size_t size, size_t *archive_size,
                    InstallError *error)
{
    const struct place *place;
    size_t region, archive_at, made_size = 0;
    char done[512], no_room[64], where[64];
    uint32_t first, count;
    uint8_t *made;

    if (number < 0 || number >= LEVEL_COUNT)
        return install_fail(error, "internal", "there is no level %d", number);
    if (job_cancelled(plan->job))
        return install_fail(error, "cancelled", "cancelled");
    place = &plan->places[number];
    levels_span(plan, number, &first, &count);
    if (size != (size_t)count * ISO_SECTOR)
        return install_fail(error, "internal", "level %d's span is 0x%zx bytes, not its 0x%zx", number, size,
                            (size_t)count * ISO_SECTOR);
    region = (size_t)place->sectors * ISO_SECTOR;
    archive_at = (size_t)(place->sector - place->header_sector) * ISO_SECTOR;
    made = make_archive(plan, number, span + archive_at, &made_size, done, sizeof done, no_room, sizeof no_room,
                        error);
    if (!made)
        return false;
    if (made_size > region) {
        free(made);
        return install_fail(error, "internal", "level %d's archive (0x%zx bytes) does not fit its 0x%zx", number,
                            made_size, region);
    }
    /* in its own sectors; the header (the span's first sector) gets the archive's new length */
    memcpy(span + archive_at, made, region);
    free(made);
    write_u32_le(span + 8, place->sector);
    write_u32_le(span + 12, (uint32_t)(made_size / ISO_SECTOR));
    snprintf(where, sizeof where, "in place at sector %u", place->sector);
    level_done(plan, number, done, no_room, made_size, where);
    *archive_size = made_size;
    return true;
}

void levels_plan_free(LevelsPlan *plan)
{
    if (!plan)
        return;
    for (int k = 0; k < EXTRA_CLASSES; k++)
        class_source_free(&plan->sources[k]);
    free(plan->boots.core_class);
    free(plan->boots.pixels);
    free(plan);
}

/* ARCHIVE (SIZE bytes, zeroed up to at least PLACE's sectors) in the level's own sectors on IMAGE when it fits, the
   rest of them zeroed; else at the image's end, after the blank tail the archive appended before it (*APPENDED)
   keeps (the stream reader reads past an archive's end; the last archive's tail is the image's own). The level's
   header is pointed at it. */
static bool write_archive(IsoImage *image, const struct place *place, bool *appended, const uint8_t *archive,
                          size_t size, char *where, size_t capacity, InstallError *error)
{
    size_t region = (size_t)place->sectors * ISO_SECTOR;
    uint8_t field[8];
    uint32_t sector;

    if (size <= region) {
        sector = place->sector;
        iso_image_write(image, (uint64_t)sector * ISO_SECTOR, archive, region);
        snprintf(where, capacity, "in place at sector %u", sector);
    } else {
        if (*appended) {
            uint8_t *tail = calloc(ISO_TAIL_SECTORS, ISO_SECTOR);

            if (!tail)
                return out_of_memory(error);
            iso_image_append(image, tail, (size_t)ISO_TAIL_SECTORS * ISO_SECTOR);
            free(tail);
        }
        sector = iso_image_append(image, archive, size);
        *appended = true;
        snprintf(where, capacity, "moved to new sectors at %u", sector);
    }
    write_u32_le(field, sector);
    write_u32_le(field + 4, (uint32_t)(size / ISO_SECTOR));
    iso_image_write(image, (uint64_t)place->header_sector * ISO_SECTOR + 8, field, sizeof field);
    return !image->failed || image_failed(image, error);
}

bool patch_levels(IsoImage *image, const BuildFacts *facts, const uint8_t *sign, size_t size, Job *job,
                  InstallError *error)
{
    LevelsPlan *plan = levels_plan(image->file, facts, sign, size, job, error);
    bool ok = plan != NULL, appended = false;

    for (int number = 0; ok && number < LEVEL_COUNT; number++) {
        char done[512], no_room[64], where[64];
        size_t made_size = 0;
        uint8_t *made = NULL;

        if (job_cancelled(job))
            ok = install_fail(error, "cancelled", "cancelled");
        else
            ok = (made = make_archive(plan, number, NULL, &made_size, done, sizeof done, no_room, sizeof no_room,
                                      error)) != NULL &&
                 write_archive(image, &plan->places[number], &appended, made, made_size, where, sizeof where, error);
        if (ok)
            level_done(plan, number, done, no_room, made_size, where);
        free(made);
    }
    if (ok)
        job_log(job, "%d levels patched; image %u sectors", LEVEL_COUNT,
                image->sectors + (image->grew ? ISO_TAIL_SECTORS : 0u));
    levels_plan_free(plan);
    return ok;
}
