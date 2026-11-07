#include "fill.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytes.h"
#include "disc.h"
#include "files.h"
#include "iso.h"
#include "wad.h"

/* entries of R&C1's table of contents (disc.h) */
#define TOC_ALL_TEXT 0x1528         /* the text table: {sector, sectors} */
#define TOC_HUD_HEADER 0x14F8       /* the HUD header */
#define TOC_HUD_BANKS 0x1500        /* the HUD's 5 banks of pages, WAD-compressed */
#define TOC_TITLE 0x14E8            /* the title screen's world, its menu fonts among its textures */

/* The text table's sub-tables are the game's languages: 0 English, 2 French, 3 German, 4 Spanish, 5 Italian. */
#define TEXT_TABLES 8

struct Filler {
    const Json *discs;
    uint8_t *text;                  /* R&C1's text table, read once */
    size_t text_size;
    uint8_t *rac2_index;            /* Going Commando's level 1: its core index and decoded core data, read once */
    size_t rac2_index_size;
    uint8_t *rac2_core;
    size_t rac2_core_size;
};

static bool failed(char *error, size_t capacity, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error, capacity, format, arguments);
    va_end(arguments);
    return false;
}

Filler *filler_new(const Json *discs)
{
    Filler *filler = calloc(1, sizeof *filler);
    if (filler != NULL) {
        filler->discs = discs;
    }
    return filler;
}

void filler_free(Filler *filler)
{
    if (filler != NULL) {
        free(filler->text);
        free(filler->rac2_index);
        free(filler->rac2_core);
        free(filler);
    }
}

/*
 * Text SID of language TABLE, from the text table: each sub-table is {u32 count, u32 size, entry[count]} with an
 * entry {u32 offset from the sub-table, u32 id, u32, u32}, its string NUL-ended. Entries are not sorted, and
 * where an id is listed twice the later one counts. NULL when it is not there.
 */
static const char *text_string(const Filler *filler, unsigned table, uint32_t sid, size_t *length)
{
    const uint8_t *raw = filler->text;
    size_t size = filler->text_size;
    if (table >= TEXT_TABLES || size < 4 * TEXT_TABLES) {
        return NULL;
    }
    size_t start = read_u32_le(raw + 4 * table);
    if (start > size - 8) {
        return NULL;
    }
    size_t count = read_u32_le(raw + start);
    for (size_t i = count; i-- > 0;) {
        size_t entry = start + 8 + 16 * i;
        if (entry > size - 16 || read_u32_le(raw + entry + 4) != sid) {
            continue;
        }
        size_t at = start + read_u32_le(raw + entry);
        const uint8_t *end = at < size ? memchr(raw + at, 0, size - at) : NULL;
        if (end == NULL) {
            return NULL;
        }
        *length = (size_t)(end - (raw + at));
        return (const char *)raw + at;
    }
    return NULL;
}

/* ee_rz_words: the strings the recipe names as [language, text id], each NUL-ended, in order. */
static bool fill_words(Filler *filler, const Json *fill, uint8_t *out, size_t size, char *error, size_t capacity)
{
    const char *rac1 = json_get_string(filler->discs, "rac1");
    if (filler->text == NULL) {
        filler->text = rac1 != NULL ? disc_read_toc_file(rac1, TOC_ALL_TEXT, &filler->text_size) : NULL;
        if (filler->text == NULL) {
            return failed(error, capacity, "the text table cannot be read from %s", rac1 != NULL ? rac1 : "no disc");
        }
    }
    const Json *recipe = json_get(fill, "recipe");
    if (recipe == NULL || recipe->type != JSON_ARRAY) {
        return failed(error, capacity, "words without a recipe");
    }
    size_t used = 0;
    for (size_t i = 0; i < recipe->count; i++) {
        const Json *step = &recipe->items[i];
        if (step->type != JSON_ARRAY || step->count != 2 || !step->items[0].is_integer || !step->items[1].is_integer) {
            return failed(error, capacity, "the words' recipe step %zu is not [language, text id]", i);
        }
        size_t length;
        const char *string = text_string(filler, (unsigned)step->items[0].integer, (uint32_t)step->items[1].integer,
                                         &length);
        if (string == NULL) {
            return failed(error, capacity, "text 0x%llX of language %lld is not in the disc's text table",
                          (unsigned long long)step->items[1].integer, (long long)step->items[0].integer);
        }
        if (used + length + 1 > size) {
            return failed(error, capacity, "the words outgrow their hole of %zu bytes", size);
        }
        memcpy(out + used, string, length);
        out[used + length] = 0;
        used += length + 1;
    }
    if (used != size) {
        return failed(error, capacity, "%zu bytes made, the hole holds %zu", used, size);
    }
    return true;
}

/* The HUD header's 5 banks of pages, decompressed when first needed. */
#define HUD_BANKS 5

/*
 * The HUD header's tables (OpenRAC's games/rac1/docs/ASSETS.md, tools/gen_hud.py): an icon table {u16 id, u16
 * frame count, u16 first frame, u8, u8} ended by id 0xFFFF; frames {s16 palette page, s16 image page}; and page
 * tables {u32 source address, u16 GS block, u8 log2 width, u8 log2 height} whose addresses are offsets into the
 * bank whose index range holds the page (the ranges' ends at header + 0x14 for palettes, + 0x34 for images).
 */
typedef struct {
    uint8_t *header;
    size_t header_size;
    uint8_t *bank_streams[HUD_BANKS];
    size_t bank_stream_sizes[HUD_BANKS];
    uint8_t *banks[HUD_BANKS];
    size_t bank_sizes[HUD_BANKS];
} Hud;

static void hud_free(Hud *hud)
{
    free(hud->header);
    for (int b = 0; b < HUD_BANKS; b++) {
        free(hud->bank_streams[b]);
        free(hud->banks[b]);
    }
    memset(hud, 0, sizeof *hud);
}

static bool hud_open(Hud *hud, const char *rac1)
{
    memset(hud, 0, sizeof *hud);
    hud->header = disc_read_toc_file(rac1, TOC_HUD_HEADER, &hud->header_size);
    bool read = hud->header != NULL && hud->header_size >= 0x54;
    for (int b = 0; read && b < HUD_BANKS; b++) {
        hud->bank_streams[b] = disc_read_toc_file(rac1, TOC_HUD_BANKS + 8 * (uint32_t)b, &hud->bank_stream_sizes[b]);
        read = hud->bank_streams[b] != NULL;
    }
    return read;
}

/* The bytes of page INDEX of the pages at TABLE whose bank ranges end at ENDS; NULL when out of range. */
static const uint8_t *hud_page(Hud *hud, uint32_t table, uint32_t ends, size_t index, size_t *size, unsigned *width,
                               unsigned *height, bool image)
{
    const uint8_t *header = hud->header;
    size_t entry = table + 8 * index;
    if (entry + 8 > hud->header_size) {
        return NULL;
    }
    int bank = 0;
    while (bank < HUD_BANKS && !((int64_t)index < (int32_t)read_u32_le(header + ends + 4 * (uint32_t)bank))) {
        bank++;
    }
    if (bank == HUD_BANKS) {
        return NULL;
    }
    if (hud->banks[bank] == NULL) {
        hud->banks[bank] = disc_unwad(hud->bank_streams[bank], hud->bank_stream_sizes[bank], &hud->bank_sizes[bank]);
        if (hud->banks[bank] == NULL) {
            return NULL;
        }
    }
    unsigned log_width = header[entry + 6];
    unsigned log_height = header[entry + 7];
    *width = 1u << log_width;
    *height = 1u << log_height;
    *size = image ? (size_t)1 << (log_width + log_height) : 1024;
    size_t offset = read_u32_le(header + entry) & 0x7FFFFFFF;
    if (offset + *size > hud->bank_sizes[bank]) {
        return NULL;
    }
    return hud->banks[bank] + offset;
}

/* The pixels of frame FRAME of icon ICON (8 bits each), with their palette page (1024 bytes, RGBA in the GS's
   CSM1 order); false when the header has no such frame. */
static bool hud_frame(Hud *hud, unsigned icon, unsigned frame, const uint8_t **pixels, size_t *pixel_count,
                      unsigned *width, unsigned *height, const uint8_t **palette)
{
    const uint8_t *header = hud->header;
    uint32_t icons = read_u32_le(header + 4);
    uint32_t frames = read_u32_le(header + 8);
    uint32_t palettes = read_u32_le(header + 12);
    uint32_t images = read_u32_le(header + 16);
    for (size_t at = icons; at + 8 <= hud->header_size && read_u16_le(header + at) != 0xFFFF; at += 8) {
        if (read_u16_le(header + at) != icon) {
            continue;
        }
        if (frame >= read_u16_le(header + at + 2)) {
            return false;
        }
        size_t frame_at = frames + 4 * ((size_t)read_u16_le(header + at + 4) + frame);
        if (frame_at + 4 > palettes) {
            return false;
        }
        int palette_page = (int16_t)read_u16_le(header + frame_at);
        int image_page = (int16_t)read_u16_le(header + frame_at + 2);
        unsigned palette_width;
        unsigned palette_height;
        size_t palette_size;
        if (palette_page < 0 || image_page < 0) {
            return false;
        }
        *pixels = hud_page(hud, images, 0x34, (size_t)image_page, pixel_count, width, height, true);
        *palette = hud_page(hud, palettes, 0x14, (size_t)palette_page, &palette_size, &palette_width,
                            &palette_height, false);
        return *pixels != NULL && *palette != NULL;
    }
    return false;
}

/* Palette index I's place in a page stored in the GS's CSM1 order: bits 3 and 4 swapped. */
static unsigned csm1(unsigned i)
{
    return ((i >> 3) & 1) != ((i >> 4) & 1) ? i ^ 0x18 : i;
}

#define ICON_SLIDER 0xE99E
#define SLIDER_GREEN_BAR 8          /* the Sound page's green volume bar */
#define BAR_GREEN_WHITE 224.0       /* the green bar's red channel at its white centre (0 at its green edges) */
#define ICON_BADGE_FRAME 4          /* the Load Game list's Challenge Mode "R" badge, 16 x 16 */

/*
 * ee_palette_movie_bar: the green bar's palette page with every entry the bar uses orange, each keeping its place
 * in the bar's fade from its white centre line (the red channel measures it) to its green edges.
 * ee_palette_randomizer_badge: the R badge's palette page with its gold entries the green of the same brightness.
 * As tools/gen_hud.py makes them; Python's round() is round-half-even, as nearbyint is.
 */
static bool fill_hud_palette(Filler *filler, const char *symbol, uint8_t *out, size_t size, char *error,
                             size_t capacity)
{
    static const double ORANGE[3] = {236, 112, 0};
    static const double HIGHLIGHT[3] = {234, 228, 214};
    static const double BADGE_GREEN[3] = {46, 196, 62};
    bool bar = strcmp(symbol, "ee_palette_movie_bar") == 0;
    if (!bar && strcmp(symbol, "ee_palette_randomizer_badge") != 0) {
        return failed(error, capacity, "no HUD palette of that name");
    }
    if (size != 1024) {
        return failed(error, capacity, "1024 bytes made, the hole holds %zu", size);
    }
    const char *rac1 = json_get_string(filler->discs, "rac1");
    Hud hud;
    if (rac1 == NULL || !hud_open(&hud, rac1)) {
        hud_free(&hud);
        return failed(error, capacity, "the HUD's files cannot be read from %s", rac1 != NULL ? rac1 : "no disc");
    }
    const uint8_t *pixels;
    const uint8_t *palette;
    size_t count;
    unsigned width;
    unsigned height;
    if (!hud_frame(&hud, ICON_SLIDER, bar ? SLIDER_GREEN_BAR : ICON_BADGE_FRAME, &pixels, &count, &width, &height,
                   &palette)) {
        hud_free(&hud);
        return failed(error, capacity, "icon 0x%X has no frame %d", ICON_SLIDER,
                      bar ? SLIDER_GREEN_BAR : ICON_BADGE_FRAME);
    }
    if (!bar && (width != 16 || height != 16)) {
        hud_free(&hud);
        return failed(error, capacity, "the R badge is %ux%u, not 16x16", width, height);
    }
    memcpy(out, palette, 1024);
    bool used[256] = {false};
    for (size_t p = 0; p < count; p++) {
        used[pixels[p]] = true;
    }
    for (unsigned i = 0; i < 256; i++) {
        if (!used[i]) {
            continue;
        }
        uint8_t *entry = out + csm1(i) * 4;
        double rgb[3] = {entry[0], entry[1], entry[2]};
        double made[3];
        if (bar) {
            if (entry[0] == 0 && entry[1] == 0 && entry[2] == 0) {
                continue;           /* the black margin above and below the bar */
            }
            if (entry[1] < (entry[0] > entry[2] ? entry[0] : entry[2])) {
                hud_free(&hud);
                return failed(error, capacity, "palette entry %u (%u, %u, %u) is not the green bar's", i, entry[0],
                              entry[1], entry[2]);
            }
            double k = rgb[0] / BAR_GREEN_WHITE < 1.0 ? rgb[0] / BAR_GREEN_WHITE : 1.0;
            for (int c = 0; c < 3; c++) {
                made[c] = nearbyint(ORANGE[c] * (1 - k) + HIGHLIGHT[c] * k);
            }
        } else {
            if (entry[0] < entry[2] + 40 || entry[0] < 100) {
                continue;           /* black, the R's greys, the rim's shadow */
            }
            double lum = (double)(299 * entry[0] + 587 * entry[1] + 114 * entry[2]) / 1000;
            double k = lum / 255 < 1.0 ? lum / 255 : 1.0;
            /* the green at this brightness: dark greens toward black, bright ones toward white */
            for (int c = 0; c < 3; c++) {
                made[c] = lum < 180 ? nearbyint(BADGE_GREEN[c] * k * 1.15)
                                    : nearbyint(BADGE_GREEN[c] + (255 - BADGE_GREEN[c]) * (lum - 180) / 75);
                made[c] = made[c] < 0 ? 0 : made[c] > 255 ? 255 : made[c];
            }
        }
        for (int c = 0; c < 3; c++) {
            entry[c] = (uint8_t)made[c];
        }
    }
    hud_free(&hud);
    return true;
}

/*
 * ee_glyph_cells: the Reuse Seed box's glyphs, as tools/gen_glyphs.py draws them, for each of the game's three
 * menu fonts (the title screen's effect textures 1, 2 and 3: 256 x 128 8-bit sheets). Seven 24 x 16 cells each:
 * the D-pad's four arms (up, right, down, left), drawn in the face buttons' greys, then copies of Cross, Circle and
 * Square moved right to the shoulder buttons' centre. The cells' places in the sheet's free corner must be blank.
 */
#define CELL_W 24
#define CELL_H 16
#define GLYPHS 7
#define BUTTON_V 0x60               /* the button glyphs' row */
#define ARM 13                      /* the arm's box, 13 x 13, at ARM_X, ARM_Y in the cell */
#define ARM_X 6
#define ARM_Y 1
#define FACE_SHIFT 3
#define FONT_W 256
#define FONT_H 128

typedef struct {
    unsigned u;
    unsigned v;
    bool arm;
    unsigned which;                 /* an arm's direction (0 up, 1 right, 2 down, 3 left), or a face button's u */
} Glyph;

static const Glyph GLYPH_CELLS[GLYPHS] = {
    {0xD0, 0x00, true, 0}, {0xE8, 0x00, true, 1}, {0xD0, 0x10, true, 2}, {0xE8, 0x10, true, 3},
    {0xD0, 0x20, false, 0x00}, {0xE8, 0x20, false, 0x48}, {0xD0, 0x30, false, 0x18},
};

/* Whether (X, Y) is in the arm for DIRECTION: the down arm is a square with a roof, its point at the top facing
   the pad's centre, and the others are it turned. */
static bool in_arm(int x, int y, unsigned direction)
{
    if (x < 0 || y < 0 || x >= ARM || y >= ARM) {
        return false;
    }
    const int half = ARM / 2;
    const int last = ARM - 1;
    int dx = x;
    int dy = y;
    if (direction == 0) {
        dy = last - y;
    } else if (direction == 1) {
        dx = y;
        dy = x;
    } else if (direction == 3) {
        dx = y;
        dy = last - x;
    }
    return dy > half || abs(dx - half) <= dy;
}

/* The index of the opaque grey closest to GREY (the palette's other colours are the buttons' symbols). COLOURS is
   RGBA in index order, alpha 0..255. */
static uint8_t nearest_grey(const uint8_t colours[256][4], int grey)
{
    int best = 0;
    long best_distance = -1;
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = colours[i];
        int high = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
        int low = c[0] < c[1] ? (c[0] < c[2] ? c[0] : c[2]) : (c[1] < c[2] ? c[1] : c[2]);
        if (c[3] < 0xE0 || high - low > 4) {
            continue;
        }
        long distance = 0;
        for (int k = 0; k < 3; k++) {
            distance += (long)(c[k] - grey) * (c[k] - grey);
        }
        if (best_distance < 0 || distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    return (uint8_t)best;
}

/* The arm for DIRECTION, shaded like the face buttons: an outer rim lit at the top left and dark at the bottom
   right, an inner rim, a dark face. */
static void draw_arm(uint8_t cell[CELL_H][CELL_W], const uint8_t colours[256][4], unsigned direction)
{
    uint8_t face = nearest_grey(colours, 46);
    uint8_t light = nearest_grey(colours, 75);
    uint8_t shade = nearest_grey(colours, 32);
    uint8_t rim_lit = nearest_grey(colours, 100);
    uint8_t rim_dark = nearest_grey(colours, 22);
    for (int y = 0; y < ARM; y++) {
        for (int x = 0; x < ARM; x++) {
            if (!in_arm(x, y, direction)) {
                continue;
            }
            bool out_right = !in_arm(x + 1, y, direction);
            bool out_left = !in_arm(x - 1, y, direction);
            bool out_down = !in_arm(x, y + 1, direction);
            bool out_up = !in_arm(x, y - 1, direction);
            uint8_t colour;
            if (out_right || out_left || out_down || out_up) {
                colour = out_left || out_up ? rim_lit : rim_dark;
            } else if (!in_arm(x - 1, y - 1, direction) || !in_arm(x - 2, y, direction) ||
                       !in_arm(x, y - 2, direction)) {
                colour = light;
            } else if (!in_arm(x + 1, y + 1, direction) || !in_arm(x + 2, y, direction) ||
                       !in_arm(x, y + 2, direction)) {
                colour = shade;
            } else {
                colour = face;
            }
            cell[ARM_Y + y][ARM_X + x] = colour;
        }
    }
}

static bool fill_glyphs(Filler *filler, uint8_t *out, size_t size, char *error, size_t capacity)
{
    if (size != 3 * GLYPHS * CELL_H * CELL_W) {
        return failed(error, capacity, "%d bytes made, the hole holds %zu", 3 * GLYPHS * CELL_H * CELL_W, size);
    }
    const char *rac1 = json_get_string(filler->discs, "rac1");
    size_t stored_size = 0;
    uint8_t *stored = rac1 != NULL ? disc_read_toc_file(rac1, TOC_TITLE, &stored_size) : NULL;
    if (stored == NULL) {
        return failed(error, capacity, "the title screen's data cannot be read from %s",
                      rac1 != NULL ? rac1 : "no disc");
    }
    /* the title screen's lump, WAD-compressed on the disc */
    size_t lump_size = stored_size;
    uint8_t *lump = stored;
    if (stored_size >= 3 && memcmp(stored, "WAD", 3) == 0) {
        lump = disc_unwad(stored, stored_size, &lump_size);
        free(stored);
        if (lump == NULL) {
            return failed(error, capacity, "the title screen's data does not decompress");
        }
    }
    /* its header's words: 1 the data's start, 0x17 the effect textures' table, 0x1A their bank */
    size_t base = read_u32_le(lump + 4);
    size_t table = read_u32_le(lump + 4 * 0x17);
    size_t bank = read_u32_le(lump + 4 * 0x1A);
    for (unsigned font = 1; font <= 3; font++) {
        size_t entry = table + 16 * font;
        if (entry + 16 > lump_size) {
            free(lump);
            return failed(error, capacity, "the title screen's font table is outside its data");
        }
        size_t palette_at = base + bank + read_u32_le(lump + entry);
        size_t pixels_at = base + bank + read_u32_le(lump + entry + 4);
        uint32_t width = read_u32_le(lump + entry + 8);
        uint32_t height = read_u32_le(lump + entry + 12);
        if (width != FONT_W || height != FONT_H) {
            free(lump);
            return failed(error, capacity, "font %u is %u x %u, not the 256 x 128 sheet expected", font, width,
                          height);
        }
        if (palette_at + 1024 > lump_size || pixels_at + FONT_W * FONT_H > lump_size) {
            free(lump);
            return failed(error, capacity, "font %u is outside the title screen's data", font);
        }
        const uint8_t *pixels = lump + pixels_at;
        /* the palette as the GS reads it (CSM1), alpha doubled to 0..255 */
        uint8_t colours[256][4];
        for (unsigned i = 0; i < 256; i++) {
            const uint8_t *entry_bytes = lump + palette_at + 4 * csm1(i);
            memcpy(colours[i], entry_bytes, 3);
            colours[i][3] = (uint8_t)(entry_bytes[3] * 2 > 255 ? 255 : entry_bytes[3] * 2);
        }
        uint8_t blank = pixels[0xFF];
        for (int g = 0; g < GLYPHS; g++) {
            const Glyph *glyph = &GLYPH_CELLS[g];
            for (int y = 0; y < CELL_H; y++) {
                for (int x = 0; x < CELL_W; x++) {
                    if (pixels[(glyph->v + (unsigned)y) * FONT_W + glyph->u + (unsigned)x] != blank) {
                        free(lump);
                        return failed(error, capacity, "the font's cell at u 0x%X, v 0x%X is not empty", glyph->u,
                                      glyph->v);
                    }
                }
            }
            uint8_t (*cell)[CELL_W] = (uint8_t (*)[CELL_W])(out + ((font - 1) * GLYPHS + (unsigned)g) * CELL_H * CELL_W);
            memset(cell, blank, CELL_H * CELL_W);
            if (glyph->arm) {
                draw_arm(cell, (const uint8_t (*)[4])colours, glyph->which);
            } else {
                for (int y = 0; y < CELL_H; y++) {
                    memcpy(&cell[y][FACE_SHIFT], pixels + (BUTTON_V + (unsigned)y) * FONT_W + glyph->which,
                           CELL_W - FACE_SHIFT);
                }
            }
        }
    }
    free(lump);
    return true;
}

/*
 * Going Commando's animations in R&C1's layout (common/rac2_anim.py). Going Commando keeps Ratchet's sequences
 * in each level's core data as R&C1 does, but packs their frames; the engine expands a frame at animation time
 * (the hand-written routine at 0x308B20 of SCUS_972.68) into the very layout R&C1 stores, and unpack_frame is
 * that routine transcribed. The gun-arm classes keep theirs unpacked, in R&C1's layout already.
 */
#define RAC2_LEVEL "G/LEVEL1.WAD"
#define RATCHET_JOINTS 111          /* Going Commando's Ratchet, as R&C1's; the sequences do not carry it */
#define SEQUENCE_HEAD 0x1C          /* a sequence's header: bounding sphere, counts, triggers, rate override */

static int32_t read_s32(const uint8_t *bytes)
{
    return (int32_t)read_u32_le(bytes);
}

static int read_s16(const uint8_t *bytes)
{
    return (int16_t)read_u16_le(bytes);
}

static int wrap16(int value)
{
    return (int)(int16_t)(uint16_t)(value & 0xFFFF);
}

/* Going Commando's level 1: the data archive's table holds (offset, size) pairs, the core index the second
   section, the core data the tenth, WAD-compressed. */
static bool read_rac2_level(Filler *filler, char *error, size_t capacity)
{
    if (filler->rac2_core != NULL) {
        return true;
    }
    const char *rac2 = json_get_string(filler->discs, "rac2");
    FILE *image = rac2 != NULL ? file_open(rac2, "rb") : NULL;
    if (image == NULL) {
        return failed(error, capacity, "Going Commando's disc cannot be opened");
    }
    IsoEntry level;
    uint8_t head[0x60];
    uint8_t slots[24 * 4];
    bool read = iso_find_path(image, RAC2_LEVEL, &level) &&
                file_read_at(image, (uint64_t)level.extent * ISO_SECTOR, head, sizeof head);
    uint64_t data = 0;
    if (read) {
        /* the archive's header: the data's first sector at + 0x10 */
        data = (uint64_t)level.extent * ISO_SECTOR + (uint64_t)read_u32_le(head + 0x10) * ISO_SECTOR;
        read = file_read_at(image, data, slots, sizeof slots);
    }
    uint8_t *stored = NULL;
    if (read) {
        filler->rac2_index_size = read_u32_le(slots + 3 * 4);
        size_t stored_size = read_u32_le(slots + 19 * 4);
        filler->rac2_index = malloc(filler->rac2_index_size > 0 ? filler->rac2_index_size : 1);
        stored = malloc(stored_size > 0 ? stored_size : 1);
        read = filler->rac2_index != NULL && stored != NULL &&
               file_read_at(image, data + read_u32_le(slots + 2 * 4), filler->rac2_index, filler->rac2_index_size) &&
               file_read_at(image, data + read_u32_le(slots + 18 * 4), stored, stored_size);
        if (read && stored_size >= 3 && memcmp(stored, "WAD", 3) == 0) {
            filler->rac2_core = disc_unwad(stored, stored_size, &filler->rac2_core_size);
            read = filler->rac2_core != NULL;
            if (!read) {
                free(stored);
                fclose(image);
                free(filler->rac2_index);
                filler->rac2_index = NULL;
                return failed(error, capacity, "%s: the level's core data does not decompress", RAC2_LEVEL);
            }
        } else if (read) {
            filler->rac2_core = stored;
            filler->rac2_core_size = stored_size;
            stored = NULL;
        }
    }
    free(stored);
    fclose(image);
    if (!read) {
        free(filler->rac2_index);
        free(filler->rac2_core);
        filler->rac2_index = NULL;
        filler->rac2_core = NULL;
        return failed(error, capacity, "%s cannot be read from Going Commando's disc", RAC2_LEVEL);
    }
    return true;
}

/* START's end: the next of ENDS (the offsets, with repeats, and the data's size) in sorted order after START's first
   place among them, as rac2_anim.py takes it. */
static size_t next_start(const size_t *starts, size_t count, size_t start, size_t data_size)
{
    /* the sorted list's element after START's first occurrence: the smallest start above START, unless START
       is listed twice, when it is START itself */
    size_t repeats = 0;
    size_t next = data_size;
    for (size_t i = 0; i < count; i++) {
        if (starts[i] == start) {
            repeats++;
        } else if (starts[i] > start && starts[i] < next) {
            next = starts[i];
        }
    }
    return repeats > 1 ? start : next;
}

/* A growing byte buffer for a sequence's frames. */
typedef struct {
    uint8_t *bytes;
    size_t length;
    size_t capacity;
    bool failed;
} Bytes;

static void bytes_add(Bytes *buffer, const void *data, size_t size)
{
    if (buffer->failed) {
        return;
    }
    if (buffer->length + size > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity * 2 : 4096;
        while (capacity < buffer->length + size) {
            capacity *= 2;
        }
        uint8_t *grown = realloc(buffer->bytes, capacity);
        if (grown == NULL) {
            buffer->failed = true;
            return;
        }
        buffer->bytes = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->bytes + buffer->length, data, size);
    buffer->length += size;
}

static void bytes_add_u16(Bytes *buffer, unsigned value)
{
    uint8_t bytes[2];
    write_u16_le(bytes, (uint16_t)value);
    bytes_add(buffer, bytes, 2);
}

/* The frame at FRAME of the packed sequence SEQ (SIZE bytes; its packed data at SPECIAL), expanded to R&C1's
   layout and added to OUT; false when it runs outside the sequence. */
static bool unpack_frame(const uint8_t *seq, size_t size, size_t special, size_t frame, Bytes *out)
{
    const size_t joints = RATCHET_JOINTS;
    /* every read below stays inside SEQ: its parts lie within the sequence's last 64 KB at most */
    if (special + 4 + 6 * joints + 2 > size || frame + 4 > size) {
        return false;
    }
    uint32_t packed = read_u32_le(seq + special);
    size_t base = special + 4;
    size_t thing = special + 4 + 6 * joints;
    unsigned scale_count = seq[thing];
    unsigned translation_count = seq[thing + 1];
    size_t scales = thing + 2;
    size_t translations = scales + 8 * scale_count;
    unsigned inverse_rate = read_u16_le(seq + frame);
    int header_word = read_s16(seq + frame + 2);
    size_t p1 = frame + 4;
    size_t p2 = frame + 4 + (packed & 0x3FF);
    size_t p3 = frame + 4 + ((packed >> 10) & 0x7FF);
    size_t p4 = frame + 4 + (packed >> 21);
    size_t quadwords = (joints + scale_count + translation_count + 1) >> 1;
    size_t expected = 16 + 16 * quadwords;
    size_t started = out->length;

    /* the header: f32 rate, s16, u16 quadwords, u16 quaternion bytes, u16 scale count, u16 translation offset,
       u16 translation count */
    float rate = inverse_rate != 0 ? (float)(1.0 / inverse_rate) : 0.0f;
    uint32_t rate_bits;
    memcpy(&rate_bits, &rate, 4);
    uint8_t rate_bytes[4];
    write_u32_le(rate_bytes, rate_bits);
    bytes_add(out, rate_bytes, 4);
    bytes_add_u16(out, (unsigned)header_word & 0xFFFF);
    bytes_add_u16(out, (unsigned)quadwords);
    bytes_add_u16(out, (unsigned)(8 * joints));
    bytes_add_u16(out, scale_count);
    bytes_add_u16(out, (unsigned)(8 * joints + 8 * scale_count));
    bytes_add_u16(out, translation_count);

    /* 10-bit values, three to a 32-bit word, low bits first */
    int shift = 22;
    size_t bits_at = p1;
    int xyz[RATCHET_JOINTS][3];
    for (size_t j = 0; j < joints; j++) {
        for (int c = 0; c < 3; c++) {
            int value = read_s16(seq + base + 6 * j + 2 * (size_t)c);
            unsigned mode = (unsigned)value & 3;
            if (mode == 1) {
                if (p3 >= size) {
                    return false;
                }
                value += (int8_t)seq[p3++] * 4;
            } else if (mode == 2) {
                if (bits_at + 4 > size) {
                    return false;
                }
                uint32_t top = (uint32_t)(read_u32_le(seq + bits_at) << shift) >> 22;
                int delta = (top & 0x200) != 0 ? (int)top - 0x400 : (int)top;
                shift -= 10;
                if (shift <= 0) {
                    shift = 22;
                    bits_at += 4;
                }
                value += delta * 4;
            } else if (mode == 3) {
                if (p2 + 2 > size) {
                    return false;
                }
                value = read_s16(seq + p2);
                p2 += 2;
            }
            xyz[j][c] = wrap16(value);
        }
    }
    /* w = sqrt(1 - |xyz|^2), signed by one bit per joint, in 1/32768 */
    if (p4 + (joints + 7) / 8 > size) {
        return false;
    }
    for (size_t j = 0; j < joints; j++) {
        int64_t square = (int64_t)xyz[j][0] * xyz[j][0] + (int64_t)xyz[j][1] * xyz[j][1] +
                         (int64_t)xyz[j][2] * xyz[j][2];
        double rest = 1.0 - (double)square / (double)(1 << 30);
        int w = rest > 0 ? (int)(sqrt(rest) * 32768.0) : 0;
        w = w < 0x7FFF ? w : 0x7FFF;
        if ((seq[p4 + j / 8] >> (j % 8)) & 1) {
            w = -w;
        }
        bytes_add_u16(out, (unsigned)xyz[j][0] & 0xFFFF);
        bytes_add_u16(out, (unsigned)xyz[j][1] & 0xFFFF);
        bytes_add_u16(out, (unsigned)xyz[j][2] & 0xFFFF);
        bytes_add_u16(out, (unsigned)w & 0xFFFF);
    }
    /* the scale and translation records: from their base records, two flag bits per component */
    size_t flags = p4 + (joints + 7) / 8;
    for (int list = 0; list < 2; list++) {
        size_t count = list == 0 ? scale_count : translation_count;
        size_t at = list == 0 ? scales : translations;
        for (size_t i = 0; i < count; i++) {
            size_t record = at + 8 * i;
            if (record + 8 > size || flags >= size) {
                return false;
            }
            unsigned flag = seq[flags++];
            for (int c = 0; c < 3; c++) {
                int value = read_s16(seq + record + 2 * (size_t)c);
                unsigned mode = (flag >> (2 * c)) & 3;
                if (mode == 1) {
                    if (flags >= size) {
                        return false;
                    }
                    value += (int8_t)seq[flags++] * 4;
                } else if (mode == 2) {
                    if (flags + 2 > size) {
                        return false;
                    }
                    value = (int8_t)seq[flags + 1] * 256 + seq[flags];
                    flags += 2;
                }
                bytes_add_u16(out, (unsigned)wrap16(value) & 0xFFFF);
            }
            bytes_add_u16(out, read_u16_le(seq + record + 6));
        }
    }
    static const uint8_t ZEROS[16] = {0};
    bytes_add(out, ZEROS, (16 - (out->length - started) % 16) % 16);
    return !out->failed && out->length - started == expected;
}

/* Ratchet's sequence SLOT in R&C1's layout: the seven header words (bounding sphere, counts, 0, the rate override)
   into HEAD, the frames one after another into FRAMES. */
static bool ratchet_sequence(Filler *filler, unsigned slot, uint8_t head[SEQUENCE_HEAD], Bytes *frames, char *error,
                             size_t capacity)
{
    if (!read_rac2_level(filler, error, capacity)) {
        return false;
    }
    const uint8_t *index = filler->rac2_index;
    const uint8_t *core = filler->rac2_core;
    size_t table = filler->rac2_index_size >= 0x7C ? read_u32_le(index + 0x78) : SIZE_MAX;
    if (slot > 255 || table > filler->rac2_index_size || filler->rac2_index_size - table < 256 * 4) {
        return failed(error, capacity, "Going Commando's Ratchet has no sequence table");
    }
    size_t starts[256];
    size_t count = 0;
    for (size_t i = 0; i < 256; i++) {
        size_t start = read_u32_le(index + table + 4 * i);
        if (start != 0) {
            starts[count++] = start;
        }
    }
    size_t start = read_u32_le(index + table + 4 * slot);
    if (start == 0) {
        return failed(error, capacity, "Going Commando's Ratchet has no sequence 0x%X", slot);
    }
    size_t end = next_start(starts, count, start, filler->rac2_core_size);
    if (end > filler->rac2_core_size || start + SEQUENCE_HEAD > end) {
        return failed(error, capacity, "sequence 0x%X is outside the level's data", slot);
    }
    const uint8_t *seq = core + start;
    size_t size = end - start;
    unsigned frame_count = seq[0x10];
    unsigned sound_count = seq[0x11];
    unsigned trigger_count = seq[0x12];
    unsigned last_count = seq[0x13];
    size_t special = SEQUENCE_HEAD + 4 * (size_t)frame_count + 4 * (size_t)trigger_count;
    if (special > size) {
        return failed(error, capacity, "sequence 0x%X is cut short", slot);
    }
    for (unsigned k = 0; k < frame_count; k++) {
        uint32_t frame = read_u32_le(seq + SEQUENCE_HEAD + 4 * k);
        if (frame >> 28 != 0xF) {
            return failed(error, capacity, "not a packed sequence (frame flags)");
        }
        if (!unpack_frame(seq, size, special, frame & 0x0FFFFFFF, frames)) {
            return failed(error, capacity, "sequence 0x%X frame %u does not expand", slot, k);
        }
    }
    memcpy(head, seq, 16);
    write_u32_le(head + 16, frame_count | sound_count << 8 | last_count << 24);
    write_u32_le(head + 20, 0);
    write_u32_le(head + 24, read_u32_le(seq + 0x18));
    return true;
}

/* Sequence SLOT of Going Commando's moby class CLASS_ID (the gun arms: 1 right, 2 left), stored unpacked: its
   0x1C-byte header into HEAD, its frames into FRAMES. The core index's class table (+0x18: count, offset) has
   32-byte entries {core offset, class id}; a class ends where the next begins. */
static bool arm_sequence(Filler *filler, int class_id, unsigned slot, uint8_t head[SEQUENCE_HEAD], Bytes *frames,
                         char *error, size_t capacity)
{
    if (!read_rac2_level(filler, error, capacity)) {
        return false;
    }
    const uint8_t *index = filler->rac2_index;
    size_t count = read_u32_le(index + 0x18);
    size_t table = read_u32_le(index + 0x1C);
    if (table > filler->rac2_index_size || count > (filler->rac2_index_size - table) / 32) {
        return failed(error, capacity, "Going Commando's class table is outside its index");
    }
    size_t *starts = malloc((count > 0 ? count : 1) * sizeof *starts);
    if (starts == NULL) {
        return failed(error, capacity, "out of memory");
    }
    size_t distinct = 0;
    size_t start = 0;
    bool found = false;
    for (size_t i = 0; i < count; i++) {
        int32_t offset = read_s32(index + table + 32 * i);
        if (!found && read_s32(index + table + 32 * i + 4) == class_id) {
            start = (size_t)(offset > 0 ? offset : 0);
            found = offset > 0;
        }
        if (offset > 0) {
            starts[distinct++] = (size_t)offset;
        }
    }
    /* the classes' starts are a set in rac2_anim.py: the next start above this one, else the data's end */
    size_t end = filler->rac2_core_size;
    for (size_t i = 0; i < distinct; i++) {
        if (starts[i] > start && starts[i] < end) {
            end = starts[i];
        }
    }
    free(starts);
    if (!found || end > filler->rac2_core_size || start + 0x48 > end) {
        return failed(error, capacity, "Going Commando has no class %d", class_id);
    }
    const uint8_t *cls = filler->rac2_core + start;
    size_t size = end - start;
    if (slot >= cls[0x0C] || 0x48 + 4 * (size_t)slot + 4 > size) {
        return failed(error, capacity, "the class has %u sequences, no 0x%X", cls[0x0C], slot);
    }
    size_t seq = read_u32_le(cls + 0x48 + 4 * slot);
    if (seq + SEQUENCE_HEAD > size) {
        return failed(error, capacity, "class %d's sequence 0x%X is outside the class", class_id, slot);
    }
    unsigned frame_count = cls[seq + 0x10];
    for (unsigned k = 0; k < frame_count; k++) {
        if (seq + SEQUENCE_HEAD + 4 * (size_t)k + 4 > size) {
            return failed(error, capacity, "class %d's sequence 0x%X is cut short", class_id, slot);
        }
        uint32_t frame = read_u32_le(cls + seq + SEQUENCE_HEAD + 4 * k);
        if (frame >> 28 == 0xF) {
            return failed(error, capacity, "class %d's sequence 0x%X is packed", class_id, slot);
        }
        if ((size_t)frame + 8 > size) {
            return failed(error, capacity, "class %d's sequence 0x%X frame %u is outside the class", class_id, slot, k);
        }
        size_t frame_size = 16 + 16 * (size_t)read_u16_le(cls + frame + 6);
        if (frame + frame_size > size) {
            return failed(error, capacity, "class %d's sequence 0x%X frame %u is outside the class", class_id, slot, k);
        }
        bytes_add(frames, cls + frame, frame_size);
    }
    memcpy(head, cls + seq, SEQUENCE_HEAD);
    return !frames->failed || failed(error, capacity, "out of memory");
}

/* A Ratchet or gun-arm sequence's part: "frames" (the frames one after another) or "head" (the seven words). */
static bool fill_sequence(Filler *filler, const Json *fill, bool arm, uint8_t *out, size_t size, char *error,
                          size_t capacity)
{
    int64_t slot = json_get_integer(fill, "slot", -1);
    const char *part = json_get_string(fill, "part");
    if (slot < 0 || slot > 255 || part == NULL) {
        return failed(error, capacity, "a sequence hole without a slot or part");
    }
    uint8_t head[SEQUENCE_HEAD];
    Bytes frames = {0};
    bool made = arm ? arm_sequence(filler, (int)json_get_integer(fill, "class", -1), (unsigned)slot, head, &frames,
                                   error, capacity)
                    : ratchet_sequence(filler, (unsigned)slot, head, &frames, error, capacity);
    if (made) {
        bool frames_part = strcmp(part, "frames") == 0;
        const uint8_t *bytes = frames_part ? frames.bytes : head;
        size_t length = frames_part ? frames.length : SEQUENCE_HEAD;
        if (length != size) {
            made = failed(error, capacity, "%zu bytes made, the hole holds %zu", length, size);
        } else {
            memcpy(out, bytes, size);
        }
    }
    free(frames.bytes);
    return made;
}

bool filler_fill(Filler *filler, const char *symbol, const Json *fill, uint8_t *out, size_t size, char *error,
                 size_t error_capacity)
{
    const char *kind = json_get_string(fill, "kind");
    bool made;
    char reason[512] = "";
    if (kind != NULL && strcmp(kind, "words") == 0) {
        made = fill_words(filler, fill, out, size, reason, sizeof reason);
    } else if (kind != NULL && strcmp(kind, "hud_palette") == 0) {
        made = fill_hud_palette(filler, symbol, out, size, reason, sizeof reason);
    } else if (kind != NULL && strcmp(kind, "glyphs") == 0) {
        made = fill_glyphs(filler, out, size, reason, sizeof reason);
    } else if (kind != NULL && (strcmp(kind, "ratchet_sequence") == 0 || strcmp(kind, "arm_sequence") == 0)) {
        made = fill_sequence(filler, fill, strcmp(kind, "arm_sequence") == 0, out, size, reason, sizeof reason);
    } else {
        made = failed(reason, sizeof reason, "unknown hole kind %s", kind != NULL ? kind : "(none)");
    }
    if (!made) {
        snprintf(error, error_capacity, "%s: %s", symbol, reason);
    }
    return made;
}
