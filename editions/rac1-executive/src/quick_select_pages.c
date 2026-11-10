/* q2: three quick-select pages.
 *
 * The ring. R&C1's quick select is HUD slot 3, shown by the hero's input
 * handler with HudShow(3, 0, QuickSelectInit, QuickSelectUpdate,
 * QuickSelectDraw, 0, 0). Init (initialize_eight_slot_hud_selector) fills the level's eight
 * slot records (0x1C each: +0 icon id, +8..+0x14 the d-pad neighbours, +0x18
 * item id) from the eight saved item words at D_00141FA0 and the item
 * table (0x4C per record, icon id at +0x38). Update (update_hud_item_selector_selection_and_visibility)
 * picks a slot from the stick or the d-pad and, when Triangle is released,
 * makes the slot's item pending for the hero; Draw (draw_hud_item_selector_panel) draws
 * the 210x200 box: a darkening disc, the icons on a circle of radius 74 x
 * 1.05 around the box's centre (origin from hud_screen_position + (105, 100)),
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
 * the page it was left on, across levels too, but a save is written on page
 * 1 (ee_qs_turn_to_page), so a loaded game, retail or edition, starts on
 * page 1. An item may sit on more
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
 * {update update_menu_item_slots, draw draw_pause_ring, enter
 * load_selection_list, leave pause_stage_sprite_lists}: enter copies the eight words into the
 * widget state (+0x30), leave copies them back; update steps the highlighted
 * slot with L1/R1 and assigns the item grid's item with Cross. Each level's
 * widget record points at that level's stubs (src/qs_hooks.s): the update
 * wrapper flips the page on Square, writing the state back to the words as
 * leave would and reloading it as enter does; the draw wrapper prints
 * "Page N" in the ring's centre. The page's button-prompt list becomes
 * {"[X] Equip", "[square] Next ring", "[triangle] Exit"}. */
#include "modseg.h"

DECLARE_LVL(update_hud_item_selector_selection_and_visibility);   /* QuickSelectUpdate(slot) */
DECLARE_LVL(draw_hud_item_selector_panel);   /* QuickSelectDraw(slot) */
DECLARE_LVL(hud_screen_position);            /* the box origin from the slot's anchor flags: (slot, &x, &y) */
DECLARE_LVL(FontPrintLarge);                 /* FontPrintLarge(x, y, colour, text, limit) */
DECLARE_LVL(FontPrintCenter);                /* FontPrintCenter(x, y, colour, text, limit) */
DECLARE_LVL(SetupGifPaging);                 /* SetupGifPaging(0) */
DECLARE_LVL(DoGifPaging);
DECLARE_LVL(msg_string);                     /* msg_string(id): the edition's hook answers its own ids */
DECLARE_LVL(update_menu_item_slots);         /* the pause ring widget's update */
DECLARE_LVL(draw_pause_ring);                /* its draw, draw_pause_ring */
DECLARE_LVL(Moby_PlaySound);                 /* menu sound(kind, id, moby) */
DECLARE_LVL(func_00209DC0);                  /* the save-state reset */
DECLARE_LVL(func_00227DB0);                  /* the challenge-mode carry-over: the hooked call site is in it */
DECLARE_LVL_DATA(hud_slots);                 /* HUD slot table, 13 records of 0x90 */
DECLARE_LVL_DATA(qs_slots);                  /* the ring's 8 slot records of 0x1C */
DECLARE_LVL_DATA(item_table);                /* 37 item records of 0x4C */

#define QS_NEXT         ((volatile u8 *)EE_QS_PAGE_NEXT)
#define QS_PREV         ((volatile u8 *)EE_QS_PAGE_PREV)
#define QS_PAGE         (*(volatile u8 *)EE_QS_PAGE_INDEX)
#define ITEM_COUNT      37
#define SLOTS           8
#define PAGES           3

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
DECLARE_LVL(update_hud_elements);   /* HudUpdate: calls the slot's update at + EE_SITE_HUD_SLOT_UPDATE */
DECLARE_LVL(update_hud);            /* HudDraw: calls the slot's draw at + EE_SITE_HUD_SLOT_DRAW */
#define EE_SITE_HUD_SLOT_UPDATE 0xFC   /* jalr $2 in all 19 levels' HudUpdate */
#define EE_SITE_HUD_SLOT_DRAW   0x5C   /* jalr $2 in all 19 levels' HudDraw */

s32 ee_qs_ring_open(s32 L)
{
    u32 update = *(volatile u32 *)(LVL_DATA(L, hud_slots, u32) + 3 * 0x90 + 0x14);
    /* The pages' update, or retail's ring. */
    return update == (u32)ee_qs_update
           || update == LVL(L, update_hud_item_selector_selection_and_visibility, u32);
}

/* Items no longer owned (or out of range) leave the stored pages; the page number stays 0-2. */
static void validate(void)
{
    s32 i;
    for (i = 0; i < SLOTS; i++) {
        if (QS_NEXT[i] >= ITEM_COUNT || !item_available[QS_NEXT[i]]) {
            QS_NEXT[i] = 0;
        }
        if (QS_PREV[i] >= ITEM_COUNT || !item_available[QS_PREV[i]]) {
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
        active[i] = (u8)quick_select_items[i];
    }
    if (dir > 0) {
        for (i = 0; i < SLOTS; i++) {
            quick_select_items[i] = QS_NEXT[i];
            QS_NEXT[i] = QS_PREV[i];
            QS_PREV[i] = active[i];
        }
        QS_PAGE = (QS_PAGE + 1) % PAGES;
    } else {
        for (i = 0; i < SLOTS; i++) {
            quick_select_items[i] = QS_PREV[i];
            QS_PREV[i] = QS_NEXT[i];
            QS_NEXT[i] = active[i];
        }
        QS_PAGE = (QS_PAGE + PAGES - 1) % PAGES;
    }
}

/* Makes PAGE (0-2) the active page and returns the one that was. Every save
 * is written on page 1 (src/charge_boots.c, ee_save_c): a retail game knows
 * only the eight words, so a save written on another page would leave the
 * retail ring on that page for good. The slot records are left alone; the
 * ring is closed while saving and the caller turns back before it reopens. */
s32 ee_qs_turn_to_page(s32 page)
{
    s32 was;
    validate();
    was = QS_PAGE;
    while (QS_PAGE != page) {
        rotate(1);
    }
    return was;
}

/* The slot records from the active page, as QuickSelectInit fills them. */
static void refill(s32 L)
{
    volatile u8 *slot = LVL_DATA(L, qs_slots, volatile u8 *);
    const u8 *items = LVL_DATA(L, item_table, const u8 *);
    s32 i;
    for (i = 0; i < SLOTS; i++, slot += 0x1C) {
        u32 id = quick_select_items[i];
        *(volatile u32 *)(slot + 0x18) = id;
        *(volatile u32 *)slot = *(const u16 *)(items + id * 0x4C + 0x38);
    }
}

void ee_qs_update_c(void *slot, u32 ra)
{
    s32 L = ee_level_of_call(ra, LVL_TABLE(update_hud_elements), EE_SITE_HUD_SLOT_UPDATE);
    u32 pressed = g_controller_state.pressed_unmasked;
    validate();
    if (g_controller_state.held_unmasked & BTN_TRIANGLE) {          /* not on the release frame, which equips */
        if (pressed & BTN_R1) {
            rotate(1);
            refill(L);
        } else if (pressed & BTN_L1) {
            rotate(-1);
            refill(L);
        }
    }
    LVL(L, update_hud_item_selector_selection_and_visibility, SlotFn)(slot);
}

/* The ghost rings (owner's stretch goal: faint, shrunken copies of the
 * neighbouring pages beside the ring) were built and tuned on 2026-10-18 and
 * taken out the same day at the owner's word: too busy (commits d0b0ce9,
 * 0ff040b). The ring stays at its retail place. */
void ee_qs_draw_c(void *slot, u32 ra)
{
    s32 L = ee_level_of_call(ra, LVL_TABLE(update_hud), EE_SITE_HUD_SLOT_DRAW);
    volatile u8 *s = (volatile u8 *)slot;
    s32 x, y, fade, min_x;
    u32 colour;
    LVL(L, draw_hud_item_selector_panel, SlotFn)(slot);
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
    LVL(L, hud_screen_position, OriginFn)(slot, &x, &y);
    /* The ring sits at the screen's left edge (box x ~18): the L1 glyph is
     * kept on screen, which still clears the left icon (box x + 11). In 16:9
     * the ring's slot is moved out while it draws (src/widescreen.c), and the
     * clamp with it, so that L1 keeps its place beside the ring. */
    min_x = GLYPH_MIN_X + ee_ws_ring_shift();
    LVL(L, FontPrintLarge, FontFn)(x - 26 < min_x ? min_x : x - 26, y + BOX_CY - 8, colour, glyph_l1, -1);
    LVL(L, FontPrintLarge, FontFn)(x + R1_GLYPH_X, y + BOX_CY - 8, colour, glyph_r1, -1);
}

/* The pause page's ring, update: Square flips the page. RING is its box
 * (MenuScreen.data.slots: the eight slots' items and the highlighted slot;
 * its moby is where the sound plays). Entered through the level's stub. */
s32 ee_qs_pause_update_c(MenuScreen *ring, s32 L)
{
    s32 *slots = ring->data.slots.items;
    s32 i;
    validate();
    if (g_controller_state.pressed_unmasked & BTN_SQUARE) {
        for (i = 0; i < SLOTS; i++) {
            quick_select_items[i] = slots[i];
        }
        rotate(1);
        for (i = 0; i < SLOTS; i++) {
            slots[i] = quick_select_items[i];
        }
        LVL(L, Moby_PlaySound, SoundFn)(1, 0x11, ring->moby);    /* the slot-step blip */
    }
    return LVL(L, update_menu_item_slots, PauseFn)(ring);
}

/* Its draw: "Page N" in the ring's centre. */
s32 ee_qs_pause_draw_c(MenuScreen *ring, s32 L)
{
    s32 r = LVL(L, draw_pause_ring, PauseFn)(ring);
    const char *text = LVL(L, msg_string, MsgFn)(EE_TEXT_QS_PAGE + (QS_PAGE < PAGES ? QS_PAGE : 0));
    if (text) {
        LVL(L, SetupGifPaging, GifFn)(0);
        LVL(L, FontPrintCenter, FontFn)(ring->width / 2, ring->height / 2 - 8, 0x80000000u | PROMPT_COLOUR, text, -1);
        ((VoidFn)LVL_TABLE(DoGifPaging)[L])();
    }
    return r;
}

/* A new weapon's slot: INDEX is the first empty slot of the active page the
 * game found, 8 when it is full (then retail placed nothing). */
void ee_qs_autoslot_c(s32 index, u32 item)
{
    s32 i;
    if (index < SLOTS) {
        quick_select_items[index] = item;
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
 * across it as the carry-over keeps the ring. The 24 bytes also cover the
 * Movie volume and the control layout, which follow the pages.
 *
 * The player's options are kept too. The reset (func_00209DC0) restores
 * the disc's initial save, which holds every option block, and keeps only
 * the stereo setting and the two volumes (0x15EEE8-0x15EEF0) across it;
 * the carry-over then puts back the cheat bytes (gCheats, which hold the
 * edition's 16:9, Strafing and Quick Select pause rows) and turns the
 * HelpDesk off. So retail's Challenge Mode starts with the Camera page's
 * three settings and the subtitles back at their defaults; the edition
 * keeps them as the player set them. */
#define CAMERA_OPTIONS      (&invert52)  /* save ids 0x1A, 0x19, 0x1B: invert52, opt_cam_invert, opt_cam_yaw_rate */
#define CAMERA_OPTION_WORDS 3

void ee_qs_carry_hook_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, LVL_TABLE(func_00227DB0), EE_SITE_QS_CARRY);
    u8 keep[24];
    s32 camera[CAMERA_OPTION_WORDS];
    u8 subtitles = subtitles_enabled;
    s32 i;
    for (i = 0; i < 24; i++) {
        keep[i] = ((volatile u8 *)EE_QS_PAGE_NEXT)[i];
    }
    for (i = 0; i < CAMERA_OPTION_WORDS; i++) {
        camera[i] = CAMERA_OPTIONS[i];
    }
    LVL(L, func_00209DC0, VoidFn)();
    for (i = 0; i < 24; i++) {
        ((volatile u8 *)EE_QS_PAGE_NEXT)[i] = keep[i];
    }
    for (i = 0; i < CAMERA_OPTION_WORDS; i++) {
        CAMERA_OPTIONS[i] = camera[i];
    }
    subtitles_enabled = subtitles;
}
