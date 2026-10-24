/* The edition's HUD icons, added to the HUD header as each level loads.
 *
 * HUD icons come from the HUD header: an icon table {id, frame count,
 * first frame}, frame references {palette page, image page} and two page
 * tables {source address, GS block, log2 width, log2 height}. Each level's
 * data archive carries its own copy of the header (and of the banks); the
 * level loader (func_002032D0, load_hud_banks) allocates ALIGN64(size)
 * bytes, copies the archive's header there and points the HUD arena
 * (D_0019A4E8 + 0x1C..0x28) at the four tables through the header's own
 * offsets (+0x04..+0x10). The copies differ between levels (some levels
 * carry extra icons) and most have no room to grow in the archive, so the
 * edition does not patch them on the disc: the loader's copy call is
 * redirected here (tools/overlay_patch.py, feature charge_boots), the
 * retail header is copied as before, and the four tables are rebuilt in a
 * resident buffer with the edition's icon added; the header's offsets are
 * then made to point at the rebuilt tables (an offset is added to the
 * header's address, so it may reach anywhere in memory). The arena, the
 * bank linking and GetFrameTex only ever go through those pointers.
 *
 * The icon's two image pages are inserted at the end of bank 1's page
 * range (header +0x38; bank 1 holds the menu icons), the later banks'
 * pages, their frame references and the range ends shifting by two.
 * Being inside a counted range matters: SetupGifPaging(0) (func_001F4630)
 * clears the GS block of every image page below the last range end
 * before each menu draw, since GetFrameTex's allocations are transient,
 * and a page past the ranges kept a stale block and showed whatever
 * texture had been uploaded there since (the first build's symptom:
 * other icons and font glyphs). Pages in bank 1's range get the bank's
 * address added by LinkHudBank(1, bank) once per load, so the loader's
 * call to it is redirected here as well: the linking is done as retail
 * does it, then the two pages are pointed at the pixels in this segment.
 * Every gadget icon's two frames share palette pages 5 (black glyph) and
 * 6 (white, while equipped), tile colour included; the extra levels'
 * palettes come after those. The tables are rebuilt at every level load.
 *
 * The Sound page's orange Movie volume bar (src/movie_volume.c) is icon
 * EE_ICON_MOVIE_BAR, one frame: the image of the slider icon's green bar
 * (icon 0xE99E frame 8; its blue bar, frame 9, is another image under the
 * same palette page) with a palette of its own, the bar's palette with the
 * bar's entries turned orange (tools/gen_hud.py). That palette page is
 * inserted at the end of bank 1's palette range the same way as the icon's
 * image pages, for the same reasons: later palettes, their frame
 * references and the range ends shift by one, and LinkHudBank(1) is
 * followed by pointing the page at the palette in this segment. The
 * Randomizer save badge (src/goodies.c) is EE_ICON_RANDOMIZER the same way:
 * the Load Game list's Challenge Mode R (icon 0xE99E frame 4) under a
 * palette page of its own, the badge's palette with its gold turned green,
 * inserted after the bar's. */
#include "modseg.h"

extern const unsigned char ee_icon_charge_boots[2048];   /* tools/gen_hud.py: two 32x32 frames */
extern const unsigned char ee_palette_movie_bar[1024];   /* tools/gen_hud.py: 256 RGBA entries, the game's CSM1 order */
extern const unsigned char ee_palette_randomizer_badge[1024];   /* tools/gen_hud.py: the R badge's palette, its gold green */
extern const unsigned char ee_icon_mystery[1024];        /* tools/gen_hud.py: the vendor's mystery tile, one 32x32 frame */
extern const unsigned char ee_palette_mystery[1024];     /* and its palette (src/randomizer_play.c) */

#define ICON_CHARGE_BOOTS   0xEA84
#define PALETTE_BLACK       5
#define PALETTE_WHITE       6
#define ICON_LOG2           5        /* 32x32 */
#define ICON_BANK           1        /* the menu icon bank */
#define ICON_SLIDER         0xE99E   /* the Sound page's slider icon */
#define SLIDER_GREEN_BAR    8        /* its frame for the sound effects bar */
#define SLIDER_R_BADGE      4        /* its frame for the Load Game list's Challenge Mode R */
#define NEW_ICONS           4        /* the Charge Boots, the movie bar, the Randomizer badge, the mystery tile */
#define NEW_FRAMES          5        /* two, one, one, one */
#define NEW_PALETTES        3        /* the bar's, the badge's, the tile's */
#define NEW_IMAGES          3        /* the Charge Boots', the tile's */

struct IconRow { u16 id, count, start; u8 flags, pad; };
struct FrameRef { s16 palette, image; };
struct Page { u32 source; u16 gs_block; u8 wlog2, hlog2; };
struct HudArena {
    u8 pad0[0x18];
    u8 *header;                 /* 0x18: the loader's copy of the level's HUD header */
    struct IconRow *icons;      /* 0x1C */
    struct FrameRef *frames;    /* 0x20 */
    struct Page *image_pages;   /* 0x24 */
    struct Page *palette_pages; /* 0x28 */
};
/* The arena (D_0019A4E8 in the executable) is above the resident core, so it
 * has a different address in every level: the link hook learns its level
 * from its call site and reads the arena's address from the level table
 * (tools/modseg.py LEVEL_DATA hud_arena, from the loader's own lui/addiu). */
DECLARE_LVL(func_002032D0);             /* load_hud_banks: the hooked call sites are in it */
DECLARE_LVL_DATA(hud_arena);            /* the arena: header pointer at +0x18, tables at +0x1C.. */
/* The title screen's two programs load their HUD with their own copies of the
 * loader, hooked the same way (tools/gen_frontend.py), so the Sound page's
 * movie bar is there too. */
#define ARENA_OF(L) (*(volatile struct HudArena *)LVL_DATA(L, hud_arena, u8 *))
#define HEADER_PAL_ENDS(h)   ((s32 *)((h) + 0x14))   /* palette page range ends per bank */
#define HEADER_IMG_ENDS(h)   ((s32 *)((h) + 0x34))   /* image page range ends per bank: 8 words, then the bank sizes and flags at +0x54 */
#define IMG_ENDS             8
#define HEADER_BANKS(h)      ((u32 *)((h) + 0x74))   /* linked bank addresses */

#define TABLES_BYTES 0x1800
static u8 ee_hud_tables[TABLES_BYTES] __attribute__((aligned(16)));
static s32 ee_icon_pages;        /* index of the icon's first image page, or -1 */
static s32 ee_bar_palette;       /* index of the movie bar's palette page, or -1; the badge's follows it */

static u8 *copy(u8 *dst, const u8 *src, s32 n)
{
    s32 i;
    for (i = 0; i < n; i++) {
        dst[i] = src[i];
    }
    return dst + n;
}

void ee_hud_header_copy(u8 *dst, const u8 *src, s32 size)
{
    u32 *off = (u32 *)(dst + 4);         /* +4 icons, +8 frames, +0xC palette pages, +0x10 image pages */
    s32 *img_ends = HEADER_IMG_ENDS(dst);
    u16 *counts = (u16 *)dst;
    const struct IconRow *icons;
    const struct FrameRef *refs;
    const struct Page *pages;
    s32 *pal_ends = HEADER_PAL_ENDS(dst);
    s32 n_icons, n_frames, n_pal, n_img, at, pal_at, bar, badge, need, i;
    u8 *p = ee_hud_tables;
    struct IconRow *row;
    struct FrameRef *ref;
    struct Page *page;
    u32 o_icons, o_frames, o_pal, o_img;

    copy(dst, src, size);
    ee_icon_pages = -1;
    ee_bar_palette = -1;

    icons = (const struct IconRow *)(dst + off[0]);
    bar = -1;                             /* the green bar's frame */
    badge = -1;                           /* the R badge's */
    for (n_icons = 0; icons[n_icons].id != 0xFFFF; n_icons++) {
        if (icons[n_icons].id == ICON_SLIDER && icons[n_icons].count > SLIDER_GREEN_BAR) {
            bar = icons[n_icons].start + SLIDER_GREEN_BAR;
            badge = icons[n_icons].start + SLIDER_R_BADGE;
        }
    }
    n_frames = (off[2] - off[1]) / 4;
    n_pal = (off[3] - off[2]) / 8;
    n_img = img_ends[4];
    at = img_ends[ICON_BANK];             /* the new image pages go here */
    pal_at = pal_ends[ICON_BANK];         /* and the new palette page here */
    need = 8 * (n_icons + NEW_ICONS + 1) + 4 * (n_frames + NEW_FRAMES) + 8 * (n_pal + NEW_PALETTES)
           + 8 * (n_img + NEW_IMAGES);
    if (need > TABLES_BYTES || at <= 0 || at > n_img || pal_at <= 0 || pal_at > n_pal || bar < 0 || bar >= n_frames) {
        return;                           /* keep the retail tables */
    }

    o_icons = (u32)p - (u32)dst;
    p = copy(p, dst + off[0], 8 * n_icons);
    row = (struct IconRow *)p;
    row->id = ICON_CHARGE_BOOTS;
    row->count = 2;
    row->start = (u16)n_frames;
    row->flags = 0;
    row->pad = 0;
    row++;
    row->id = EE_ICON_MOVIE_BAR;
    row->count = 1;
    row->start = (u16)(n_frames + 2);
    row->flags = 0;
    row->pad = 0;
    row++;
    row->id = EE_ICON_RANDOMIZER;
    row->count = 1;
    row->start = (u16)(n_frames + 3);
    row->flags = 0;
    row->pad = 0;
    row++;
    row->id = EE_ICON_MYSTERY;
    row->count = 1;
    row->start = (u16)(n_frames + 4);
    row->flags = 0;
    row->pad = 0;
    row++;
    row->id = 0xFFFF;
    row->count = 0;
    row->start = 0;
    row->flags = 0;
    row->pad = 0;
    p = (u8 *)(row + 1);

    o_frames = (u32)p - (u32)dst;
    refs = (const struct FrameRef *)(dst + off[1]);
    ref = (struct FrameRef *)p;
    for (i = 0; i < n_frames; i++) {
        ref[i].palette = refs[i].palette >= pal_at ? refs[i].palette + NEW_PALETTES : refs[i].palette;
        ref[i].image = refs[i].image >= at ? refs[i].image + NEW_IMAGES : refs[i].image;
    }
    ref[n_frames].palette = PALETTE_BLACK;
    ref[n_frames].image = (s16)at;
    ref[n_frames + 1].palette = PALETTE_WHITE;
    ref[n_frames + 1].image = (s16)(at + 1);
    ref[n_frames + 2].palette = (s16)pal_at;
    ref[n_frames + 2].image = ref[bar].image;
    ref[n_frames + 3].palette = (s16)(pal_at + 1);
    ref[n_frames + 3].image = ref[badge].image;
    ref[n_frames + 4].palette = (s16)(pal_at + 2);
    ref[n_frames + 4].image = (s16)(at + 2);
    p = (u8 *)(ref + n_frames + NEW_FRAMES);

    o_pal = (u32)p - (u32)dst;
    pages = (const struct Page *)(dst + off[2]);
    page = (struct Page *)p;
    for (i = 0; i < pal_at; i++) {
        page[i] = pages[i];
    }
    page[pal_at].source = 0x80000000;      /* unlinked; set to the palettes after LinkHudBank(1) */
    page[pal_at].gs_block = 0;
    page[pal_at].wlog2 = 0;
    page[pal_at].hlog2 = 0;
    page[pal_at + 1] = page[pal_at];
    page[pal_at + 2] = page[pal_at];
    for (i = pal_at; i < n_pal; i++) {
        page[i + NEW_PALETTES] = pages[i];
    }
    p = (u8 *)(page + n_pal + NEW_PALETTES);
    for (i = ICON_BANK; i < IMG_ENDS; i++) {      /* the palette range ends, 8 words before the image ones */
        if (pal_ends[i] >= pal_at) {
            pal_ends[i] += NEW_PALETTES;
        }
    }

    o_img = (u32)p - (u32)dst;
    pages = (const struct Page *)(dst + off[3]);
    page = (struct Page *)p;
    for (i = 0; i < at; i++) {
        page[i] = pages[i];
    }
    page[at].source = 0x80000000;          /* unlinked; set to the pixels after LinkHudBank(1) */
    page[at].gs_block = 0;
    page[at].wlog2 = ICON_LOG2;
    page[at].hlog2 = ICON_LOG2;
    page[at + 1] = page[at];
    page[at + 2] = page[at];
    for (i = at; i < n_img; i++) {
        page[i + NEW_IMAGES] = pages[i];
    }
    for (i = ICON_BANK; i < IMG_ENDS; i++) {      /* only the range ends: the bank sizes and flags follow them */
        if (img_ends[i] >= at) {
            img_ends[i] += NEW_IMAGES;
        }
    }

    off[0] = o_icons;
    off[1] = o_frames;
    off[2] = o_pal;
    off[3] = o_img;
    counts[0] = (u16)(n_icons + NEW_ICONS);
    counts[1] = (u16)(n_frames + NEW_FRAMES);
    ee_icon_pages = at;
    ee_bar_palette = pal_at;
}

/* LinkHudBank(bank, address), as retail's func_001FF7F0: records the
 * bank's address and adds it to the source offsets of the bank's palette
 * and image pages, once. Then the edition's pages get their real source. */
void ee_hud_link_bank_c(s32 bank, u32 addr, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_002032D0, EE_SITE_HUD_LINK_BANK);
    u8 *header = ARENA_OF(L).header;
    u32 *slot = HEADER_BANKS(header) + bank;
    s32 *pal_ends = HEADER_PAL_ENDS(header);
    s32 *img_ends = HEADER_IMG_ENDS(header);
    struct Page *pal = ARENA_OF(L).palette_pages;
    struct Page *img = ARENA_OF(L).image_pages;
    s32 i, start, limit;

    if (*slot != 0) {
        return;
    }
    addr = (addr + 0xF) & ~0xFu;
    *slot = addr;
    start = bank != 0 ? pal_ends[bank - 1] : 0;
    limit = pal_ends[bank];
    for (i = start; i < limit; i++) {
        pal[i].source = (pal[i].source & 0x7FFFFFFF) + addr;
    }
    start = bank != 0 ? img_ends[bank - 1] : 0;
    limit = img_ends[bank];
    for (i = start; i < limit; i++) {
        img[i].source = (img[i].source & 0x7FFFFFFF) + addr;
    }
    if (bank == ICON_BANK && ee_icon_pages >= 0) {
        img[ee_icon_pages].source = (u32)ee_icon_charge_boots;
        img[ee_icon_pages + 1].source = (u32)ee_icon_charge_boots + 1024;
        img[ee_icon_pages + 2].source = (u32)ee_icon_mystery;
    }
    if (bank == ICON_BANK && ee_bar_palette >= 0) {
        pal[ee_bar_palette].source = (u32)ee_palette_movie_bar;
        pal[ee_bar_palette + 1].source = (u32)ee_palette_randomizer_badge;
        pal[ee_bar_palette + 2].source = (u32)ee_palette_mystery;
    }
}
