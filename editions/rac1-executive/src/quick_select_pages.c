/* q2: three quick-select pages.
 *
 * The ring. R&C1's quick select is HUD slot 3, shown by the hero's input
 * handler with HudShow(3, 0, QuickSelectInit, QuickSelectUpdate,
 * QuickSelectDraw, 0, 0). Init (func_L00_00238478) fills the level's eight
 * slot records (0x1C each: +0 icon id, +8..+0x14 the d-pad neighbours, +0x18
 * item id) from the eight saved item words at D_00141FA0 and the item
 * table (0x4C per record, icon id at +0x38). Update (func_L00_00238530)
 * picks a slot from the stick or the d-pad and, when Triangle is released,
 * makes the slot's item pending for the hero; Draw (func_L00_00238C30) draws
 * the 210x200 box: a darkening disc, the icons on a circle of radius 74 x
 * 1.05 around the box's centre (origin from func_L00_00236400 + (105, 100)),
 * the selected item's name and ammo. The edition's build points the HudShow
 * call's update and draw arguments at ee_qs_update and ee_qs_draw
 * (tools/gen_qs.py finds the site in each level; tools/overlay_patch.py).
 * They learn the level from their return address (src/qs_hooks.s): HudUpdate
 * and HudDraw call the slot's callbacks from one jalr each, at the same
 * offset in all 19 levels.
 *
 * Pages. The active page stays in the game's eight words, so everything
 * retail does with them (the ring, the pause page, the first-empty-slot
 * placement of a new weapon, the challenge-mode carry-over, the save) keeps
 * working on it, and a retail game shows the active page from an edition
 * save. The other two pages and the page number are in 24 bytes of the
 * saved statistics block that nothing reads or writes (src/modseg.h). R1
 * while the ring is open rotates to the next page, L1 to the previous:
 * active <- next <- previous <- active, and the slot records are refilled
 * as Init fills them; the selection keeps its place. The ring reopens on
 * the page it was left on (the number is saved). An item may sit on more
 * than one page; the game's within-ring uniqueness is kept per page by its
 * own code. Items no longer owned are dropped from the two stored pages
 * whenever they are about to be shown (the carry-over does that for the
 * active page itself). A new weapon goes to the first empty slot of the
 * active page as in retail, and failing that to the next page, then the
 * previous (ee_qs_autoslot_c, replacing the store at the end of the
 * acquisition function's search). While the ring is open L1 and R1 are
 * masked from the hero (src/strafe.c's pad hook, ee_qs_ring_open), so a
 * page flip is not also a crouch or a look.
 *
 * Prompts. Draw adds the L1 and R1 glyphs (font codes 0x14 and 0x15) left
 * and right of the box, fading with the ring.
 *
 * The pause page. The pause menu's Quick Select page has a ring widget
 * {update func_0021D7A0, draw func_00221B58 (draw_pause_ring), enter
 * func_0021D9C8, leave func_0021DA60}: enter copies the eight words into the
 * widget state (+0x30), leave copies them back; update steps the highlighted
 * slot with L1/R1 and assigns the item grid's item with Cross. Each level's
 * widget record points at that level's stubs (src/qs_hooks.s): the update
 * wrapper flips the page on Square, writing the state back to the words as
 * leave would and reloading it as enter does; the draw wrapper prints
 * "Page N" in the ring's centre. The page's button-prompt list becomes
 * {"[X] Equip", "[square] Next ring", "[triangle] Exit"}. */
#include "modseg.h"

DECLARE_LVL(func_L00_00238530);   /* QuickSelectUpdate(slot) */
DECLARE_LVL(func_L00_00238C30);   /* QuickSelectDraw(slot) */
DECLARE_LVL(func_L00_00236400);   /* the box origin from the slot's anchor flags: (slot, &x, &y) */
DECLARE_LVL(func_001F68E8);       /* font_print_large(x, y, colour, text, limit) */
DECLARE_LVL(func_001F6EA8);       /* font_print_center(x, y, colour, text, limit) */
DECLARE_LVL(func_001F4630);       /* setup_gif_paging(0) */
DECLARE_LVL(func_001F4748);       /* do_gif_paging() */
DECLARE_LVL(func_001FE540);       /* msg_string(id): the edition's hook answers its own ids */
DECLARE_LVL(func_0021D7A0);       /* the pause ring widget's update */
DECLARE_LVL(func_00221B58);       /* its draw, draw_pause_ring */
DECLARE_LVL(func_0022ED80);       /* menu sound(kind, id, moby) */
DECLARE_LVL(func_00209DC0);       /* the save-state reset */
DECLARE_LVL(func_00227DB0);       /* the challenge-mode carry-over: the hooked call site is in it */
DECLARE_LVL_DATA(hud_slots);      /* HUD slot table, 13 records of 0x90 */
DECLARE_LVL_DATA(qs_slots);       /* the ring's 8 slot records of 0x1C */
DECLARE_LVL_DATA(item_table);     /* 37 item records of 0x4C */

#define QS_ITEMS        ((volatile u32 *)0x00141FA0)
#define QS_NEXT         ((volatile u8 *)EE_QS_PAGE_NEXT)
#define QS_PREV         ((volatile u8 *)EE_QS_PAGE_PREV)
#define QS_PAGE         (*(volatile u8 *)EE_QS_PAGE_INDEX)
#define OWNED           ((volatile u8 *)0x0013D5C8)   /* per item, saved */
#define ITEM_COUNT      37
#define SLOTS           8
#define PAGES           3

#define PAD             0x0013CA40
#define PAD_HELD_RAW    (*(volatile u32 *)(PAD + 0x1C0))   /* held, unmasked: what the ring itself reads */
#define PAD_PRESSED_RAW (*(volatile u32 *)(PAD + 0x1C4))   /* pressed this frame, unmasked */
#define BTN_L1          0x04u
#define BTN_R1          0x08u
#define BTN_TRIANGLE    0x10u
#define BTN_SQUARE      0x80u

#define BOX_W           210     /* the ring box (slot + 0x58/0x5C, from QuickSelectInit) */
#define BOX_CX          105     /* the circle's centre in it */
#define BOX_CY          100
#define FADE_FRAMES     8       /* slot + 0x70 counts up to this while Triangle is held */
#define PROMPT_COLOUR   0x00FFA888u
#define GLYPH_MIN_X     2       /* the glyphs are 24 wide; the screen's left edge is 0 */
/* R1's place in the box: the same gap to the right-hand icon (its edge at box
 * x + 105 + 77.7 + 16) as the clamped L1 leaves to the left-hand one, about 3
 * pixels, measured on screen (owner's note: R1 sat about 13 out, L1 3). */
#define R1_GLYPH_X      (BOX_W - 9)

typedef void (*SlotFn)(void *);
typedef void (*OriginFn)(void *, s32 *, s32 *);
typedef void (*FontFn)(s32, s32, u32, const char *, s32);
typedef void (*GifFn)(s32);
typedef const char *(*MsgFn)(s32);
typedef s32 (*PauseFn)(void *);
typedef void (*SoundFn)(s32, s32, u32);
typedef void (*VoidFn)(void);

/* The page's button prompts: 12-byte rows {s16 string id, ...}, a zero id ends them. */
const u16 ee_qs_prompts[4 * 6] = {
    0x4EEC, 0, 0, 0, 0, 0,              /* "[X] Equip" */
    EE_TEXT_QS_SWITCH, 0, 0, 0, 0, 0,   /* "[square] Next ring" */
    0x4EE0, 0, 0, 0, 0, 0,              /* "[triangle] Exit" */
    0, 0, 0, 0, 0, 0,
};
static const char glyph_l1[2] = { 0x14, 0 };
static const char glyph_r1[2] = { 0x15, 0 };

/* The callbacks' entry points (src/qs_hooks.s): they pass the return address,
 * which names the level. The slot record's address does not: several levels
 * put their HUD slot table at the same address (0x17E4D0 in levels 6 and 8,
 * for one), and a first build that matched the record against the 19
 * tables took Batalia (level 8) for level 6 and called level 6's
 * QuickSelectUpdate in Batalia's program (owner's Challenge Mode save,
 * 2026-10-18: the ring never drew and the game froze). */
void ee_qs_update(void *slot);
void ee_qs_draw(void *slot);
DECLARE_LVL(func_L00_0023B610);   /* HudUpdate: calls the slot's update at + EE_SITE_HUD_SLOT_UPDATE */
DECLARE_LVL(func_001FFFB8);       /* HudDraw: calls the slot's draw at + EE_SITE_HUD_SLOT_DRAW */
#define EE_SITE_HUD_SLOT_UPDATE 0xFC   /* jalr $2 in all 19 levels' HudUpdate */
#define EE_SITE_HUD_SLOT_DRAW   0x5C   /* jalr $2 in all 19 levels' HudDraw */

s32 ee_qs_ring_open(s32 L)
{
    return *(volatile u32 *)(LVL_DATA(L, hud_slots, u32) + 3 * 0x90 + 0x14) == (u32)ee_qs_update;
}

/* Items no longer owned (or out of range) leave the stored pages; the page number stays 0-2. */
static void validate(void)
{
    s32 i;
    for (i = 0; i < SLOTS; i++) {
        if (QS_NEXT[i] >= ITEM_COUNT || !OWNED[QS_NEXT[i]]) {
            QS_NEXT[i] = 0;
        }
        if (QS_PREV[i] >= ITEM_COUNT || !OWNED[QS_PREV[i]]) {
            QS_PREV[i] = 0;
        }
    }
    if (QS_PAGE >= PAGES) {
        QS_PAGE = 0;
    }
}

/* DIR +1: the next page becomes active (R1); -1: the previous (L1). */
static void rotate(s32 dir)
{
    u8 active[SLOTS];
    s32 i;
    for (i = 0; i < SLOTS; i++) {
        active[i] = (u8)QS_ITEMS[i];
    }
    if (dir > 0) {
        for (i = 0; i < SLOTS; i++) {
            QS_ITEMS[i] = QS_NEXT[i];
            QS_NEXT[i] = QS_PREV[i];
            QS_PREV[i] = active[i];
        }
        QS_PAGE = (QS_PAGE + 1) % PAGES;
    } else {
        for (i = 0; i < SLOTS; i++) {
            QS_ITEMS[i] = QS_PREV[i];
            QS_PREV[i] = QS_NEXT[i];
            QS_NEXT[i] = active[i];
        }
        QS_PAGE = (QS_PAGE + PAGES - 1) % PAGES;
    }
}

/* The slot records from the active page, as QuickSelectInit fills them. */
static void refill(s32 L)
{
    volatile u8 *slot = LVL_DATA(L, qs_slots, volatile u8 *);
    const u8 *items = LVL_DATA(L, item_table, const u8 *);
    s32 i;
    for (i = 0; i < SLOTS; i++, slot += 0x1C) {
        u32 id = QS_ITEMS[i];
        *(volatile u32 *)(slot + 0x18) = id;
        *(volatile u32 *)slot = *(const u16 *)(items + id * 0x4C + 0x38);
    }
}

void ee_qs_update_c(void *slot, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_0023B610, EE_SITE_HUD_SLOT_UPDATE);
    u32 pressed = PAD_PRESSED_RAW;
    validate();
    if (PAD_HELD_RAW & BTN_TRIANGLE) {          /* not on the release frame, which equips */
        if (pressed & BTN_R1) {
            rotate(1);
            refill(L);
        } else if (pressed & BTN_L1) {
            rotate(-1);
            refill(L);
        }
    }
    LVL(L, func_L00_00238530, SlotFn)(slot);
}

/* The ghost rings (owner's stretch goal: faint, shrunken copies of the
 * neighbouring pages beside the ring) were built and tuned on 2026-10-18 and
 * taken out the same day at the owner's word: too busy (commits d0b0ce9,
 * 0ff040b). The ring stays at its retail place. */
void ee_qs_draw_c(void *slot, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_001FFFB8, EE_SITE_HUD_SLOT_DRAW);
    volatile u8 *s = (volatile u8 *)slot;
    s32 x, y, fade, min_x;
    u32 colour;
    LVL(L, func_L00_00238C30, SlotFn)(slot);
    fade = s[0x70];
    if (fade > FADE_FRAMES) {
        fade = FADE_FRAMES;
    }
    if (fade == 0) {
        return;
    }
    colour = ((u32)(fade * 0x80 / FADE_FRAMES) << 24) | PROMPT_COLOUR;
    x = *(volatile s32 *)(s + 0x50);
    y = *(volatile s32 *)(s + 0x54);
    LVL(L, func_L00_00236400, OriginFn)(slot, &x, &y);
    /* The ring sits at the screen's left edge (box x ~18): the L1 glyph is
     * kept on screen, which still clears the left icon (box x + 11). In 16:9
     * the ring's slot is moved out while it draws (src/widescreen.c), and the
     * clamp with it, so that L1 keeps its place beside the ring. */
    min_x = GLYPH_MIN_X + ee_ws_ring_shift();
    LVL(L, func_001F68E8, FontFn)(x - 26 < min_x ? min_x : x - 26, y + BOX_CY - 8, colour, glyph_l1, -1);
    LVL(L, func_001F68E8, FontFn)(x + R1_GLYPH_X, y + BOX_CY - 8, colour, glyph_r1, -1);
}

/* The pause page's ring widget, update: Square flips the page. S is the
 * widget state: +0x14 its moby (for the sound), +0x30 the eight slots,
 * +0x50 the highlighted one. Entered through the level's stub. */
s32 ee_qs_pause_update_c(u8 *s, s32 L)
{
    volatile u32 *slots = (volatile u32 *)(s + 0x30);
    s32 i;
    validate();
    if (PAD_PRESSED_RAW & BTN_SQUARE) {
        for (i = 0; i < SLOTS; i++) {
            QS_ITEMS[i] = slots[i];
        }
        rotate(1);
        for (i = 0; i < SLOTS; i++) {
            slots[i] = QS_ITEMS[i];
        }
        LVL(L, func_0022ED80, SoundFn)(1, 0x11, *(u32 *)(s + 0x14));   /* the slot-step blip */
    }
    return LVL(L, func_0021D7A0, PauseFn)(s);
}

/* Its draw: "Page N" in the ring's centre (+0x20/+0x24 are the widget's size). */
s32 ee_qs_pause_draw_c(u8 *s, s32 L)
{
    s32 r = LVL(L, func_00221B58, PauseFn)(s);
    const char *text = LVL(L, func_001FE540, MsgFn)(EE_TEXT_QS_PAGE + (QS_PAGE < PAGES ? QS_PAGE : 0));
    if (text) {
        LVL(L, func_001F4630, GifFn)(0);
        LVL(L, func_001F6EA8, FontFn)(*(s32 *)(s + 0x20) / 2, *(s32 *)(s + 0x24) / 2 - 8, 0x80000000u | PROMPT_COLOUR,
                                      text, -1);
        ((VoidFn)lvl_func_001F4748[L])();
    }
    return r;
}

/* A new weapon's slot: INDEX is the first empty slot of the active page the
 * game found, 8 when it is full (then retail placed nothing). */
void ee_qs_autoslot_c(s32 index, u32 item)
{
    s32 i;
    if (index < SLOTS) {
        QS_ITEMS[index] = item;
        return;
    }
    for (i = 0; i < SLOTS; i++) {
        if (QS_NEXT[i] == 0) {
            QS_NEXT[i] = (u8)item;
            return;
        }
    }
    for (i = 0; i < SLOTS; i++) {
        if (QS_PREV[i] == 0) {
            QS_PREV[i] = (u8)item;
            return;
        }
    }
}

/* The challenge-mode carry-over's save-state reset, with the pages kept
 * across it as the carry-over keeps the ring. */
void ee_qs_carry_hook_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_00227DB0, EE_SITE_QS_CARRY);
    u8 keep[24];
    s32 i;
    for (i = 0; i < 24; i++) {
        keep[i] = ((volatile u8 *)EE_QS_PAGE_NEXT)[i];
    }
    LVL(L, func_00209DC0, VoidFn)();
    for (i = 0; i < 24; i++) {
        ((volatile u8 *)EE_QS_PAGE_NEXT)[i] = keep[i];
    }
}
