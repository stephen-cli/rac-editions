/* q3: a setting that pauses the game while quick select is open, as in
 * R&C2 and R&C3, toggled from the pause menu's Options page.
 *
 * Pausing. The mode-0 frame (func_L00_00299250) runs each world-update
 * group only if its bit is set in the update gate word at
 * *(update_flags + 8): 1 hero, 2 mobys and the tick counter, 4 particles,
 * 8 camera (0x10 is the debug single-step mode). HUD update, help update,
 * sound and rendering run regardless. So while the quick-select ring is
 * open and the setting is on, those four bits are cleared, and restored
 * when it closes: the world freezes, the ring keeps animating and taking
 * input, music keeps playing, play time does not advance. The frame's
 * HudUpdate call is redirected here (tools/overlay_patch.py, through the
 * trampoline in src/hooks.s that passes the return address), so the state
 * is checked once per frame right after the HUD has processed the ring's
 * open and close. The ring is HUD slot 3; a slot is in use while its
 * update callback (slot record + 0x14, the one HudUpdate calls) is set,
 * and the ring's is QuickSelectUpdate (func_L00_00238530). The slot table
 * is level-local data, like the gate struct.
 *
 * Setting. Saved global flag 120 (0x13D508: chunk 5 of the save, 128
 * bytes from 0x13D490; indices 118-126 are unused by retail), so it is kept
 * per save file, off by default.
 *
 * Menu. The Options list gets one more row before Quit Game, which opens a
 * copy of the Subtitles page (a toggle-list widget over one entry
 * {label, flag*, on, off}) with our flag. These records are level-local
 * data (each level program has its own copy at its own addresses), so
 * tools/gen_menu.py emits one set per level into the resident segment and
 * the list widget's item pointer is patched per level. Labels reuse string
 * 20196 "Quick Select" until the text table can take new strings. */
#include "modseg.h"

DECLARE_LVL(func_L00_00299250);        /* the mode-0 frame: the hooked call site is in it */
DECLARE_LVL(func_L00_0023B610);        /* HudUpdate(int) */
DECLARE_LVL(func_L00_00238530);        /* QuickSelectUpdate: the ring's slot callback */
DECLARE_LVL_DATA(update_flags);        /* the gate struct; bits at +8 */
DECLARE_LVL_DATA(hud_slots);           /* HUD slot table, 13 records of 0x90 (level-local) */

#define QS_PAUSE_SETTING   (*(volatile u8 *)0x0013D508)
/* A slot's current update callback: HudUpdate calls record + 0x14 (set from HudShow's
 * fourth argument by apply_pending), zero when the slot is free. */
#define HUD_SLOT_UPDATE(L, n) (*(volatile u32 *)(LVL_DATA(L, hud_slots, u32) + (n) * 0x90 + 0x14))
#define GATE_BITS          0xFu

typedef void (*HudUpdateFn)(s32);

static u32 saved_bits;
static s32 paused;

void ee_hud_update_hook_c(s32 arg, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_00299250, EE_SITE_HUD_UPDATE);
    volatile u32 *gate;
    s32 open;

    LVL(L, func_L00_0023B610, HudUpdateFn)(arg);

    gate = (volatile u32 *)(LVL_DATA(L, update_flags, u32) + 8);
    open = HUD_SLOT_UPDATE(L, 3) == lvl_func_L00_00238530[L];
    if (open && QS_PAUSE_SETTING && !paused) {
        saved_bits = *gate & GATE_BITS;
        *gate &= ~GATE_BITS;
        paused = 1;
    } else if (paused && !(open && QS_PAUSE_SETTING)) {
        *gate |= saved_bits;
        paused = 0;
    }
}

/* The pause-menu additions (rows, page, widgets, entries) are level-local
 * data with pointers between them, so tools/gen_menu.py generates one copy
 * per level into build/modseg/gen_menu.c from each level's own records, and
 * tools/overlay_patch.py points each level's Options list at its copy. */
