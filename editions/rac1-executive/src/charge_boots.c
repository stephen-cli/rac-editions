/* Charge Boots (x2, first step): a new gadget on the Gadgets page.
 *
 * Item 36. The game's item records (0x4C bytes, 37 per level program) have
 * an unused 37th entry, a copy of the Devastator that no grid shows and no
 * vendor sells; the edition rewrites it per level as a copy of the
 * Grindboots record (equip slot 1, moby class 0xC3 attached to both feet)
 * with its own name, description and icon (tools/gen_items.py). The boots
 * grid of the Gadgets page gets a third cell. The per-item arrays the id
 * indexes (owned flags at 0x13D5C8, gold flags at 0x13E620) hold 40 bytes
 * (the challenge-mode reset copies them whole, 0x28), so id 36 is in range.
 *
 * Icon 0xEA84: two 32x32 frames (black, white when equipped) in the
 * resident segment, reached by two image pages appended to the HUD header
 * past the last bank's range, so the game neither relinks nor uploads them
 * with a bank and GetFrameTex fetches them from here on first use
 * (tools/gen_hud.py).
 *
 * The item is a cheat (owner's decision, 2026-10-18): locked until the
 * game's move-combo cheat detector matches the edition's sequence, which
 * like the retail cheats only registers once the game has been beaten
 * (Challenge Mode), and then unlocked for good in that save. The detector
 * (func_L00_00207418; NTSC decomp FUN_L00_00206e00) records a move id 15
 * ticks into a move (60 for a held crouch or glide) in a 16-entry ring at
 * hero + 0x21C4, compares the ring's tail with the 12 entries of a per-level
 * table (16 bytes each, zero-terminated; retail fills 0-4, 6 and 7) and on a
 * match toggles gCheats[i] (0x15EEB0 + i, saved with the file), sets
 * 0x15EEC0 + i and shows the cheat banner (text 0x4FBE on, 0x4FBF off). The
 * edition writes entry 8 (tools/overlay_patch.py): crouch for a second (7),
 * back flip (3) twice, Comet-Strike (4), Hyper-Strike (6) twice; the move
 * ids as the retail codes use them (entry 0, 1 2 3 3 4 5 4 6, is Big Head
 * Ratchet: side flip left, right, back flip twice, Comet-Strike, double
 * jump, Comet-Strike, Hyper-Strike). A match also marks the cheat seen
 * (0x15EEC0 + 8, saved: chunk 37), which is what the Goodies Cheats page
 * lists; the edition adds the Charge Boots row there (ee_cheats_enter_c),
 * an On/Off toggle of gCheats[8] like the retail rows (owner's request,
 * 2026-10-18). Every frame, and on the Cheats page as soon as the row is
 * toggled, the boots are owned while gCheats[8] is on and taken away while
 * it is off; once seen, entry 8 of the level's table is blanked
 * (ee_charge_cheat_frame) so the sequence only unlocks. The unlock shows the
 * game's own "cheat on" banner, like every cheat (owner's request). */
#include "modseg.h"

#define ITEM_OWNED(id)     (*(volatile u8 *)(0x0013D5C8 + (id)))   /* D_0013D5C8: owned items, by id */
#define ITEM_CHARGE_BOOTS  36
#define ITEM_CLANK         1           /* the back slot's "no pack" item */
#define SLOT_BOOTS         1
#define EQUIPPED(slot)       (*(volatile s32 *)(0x00141760 + 4 * (slot)))   /* D_00141760: the hero's equipped item per slot, saved */
#define HERO_PENDING(slot)   (*(volatile s32 *)(0x0013F450 + 0x20B8 + 4 * (slot)))   /* hero.pending_item */
#define HERO_SELECTED(slot)  (*(volatile s32 *)(0x0013F450 + 0x20D4 + 4 * (slot)))   /* hero.selected_item */

/* Item id 36 is also the hero's "temporary item" sentinel (0x24): the item
 * switch (hero_switch_item in the NTSC decomp) moves a pending id into
 * selected_item but writes the persistent equipped array only when the id
 * is not 0x24, so the Charge Boots were selected for a frame and the hero
 * recreated the previous boots from the equipped array. The sentinel's
 * other uses are all about slot 0 (the hand), so for the boots slot the
 * equipped id is set here whenever the hero has the Charge Boots pending
 * or selected; equipping anything else or nothing overwrites it as retail
 * does. */
/* The Foot Items box. The Gadgets page pairs each of its 14 widgets with a
 * sequence of the menu panel class 0x472 (one panel moby per widget) and
 * the sequence carries the box's size and place: the boots grid's (0x26) is
 * 1.43 wide, the Head Items grid's (0x25) 2.04. tools/gen_items.py builds
 * ee_panel_seq, the Head Items sequence with its root moved down to the
 * boots box's height, and the page word is patched to PANEL_SLOT; this
 * installs the sequence in the class's table (class +0x48, a pointer per
 * slot; count byte at +0xC) as soon as the class is resident, found
 * through the class tables (id -> slot -> data), or through a panel moby
 * while the menu is open. Installed once per level: a new level loads a
 * new class. */
extern const u32 ee_panel_seq[];
DECLARE_LVL_DATA(menu_state);          /* the pause menu state; the 14 panel mobys at +0x1B0 */
DECLARE_LVL_DATA(class_slot_by_id);    /* u8 per class id */
DECLARE_LVL_DATA(class_resources);     /* class data pointer per slot */
#define PANEL_CLASS  0x472
#define PANEL_SLOT   219
#define PANEL_SEQS   219               /* the retail class's sequence count */

static void install_panel_sequence(u8 *cls)
{
    volatile u32 *seqs = (volatile u32 *)(cls + 0x48);
    if (cls == 0 || cls[0xC] < PANEL_SEQS || seqs[PANEL_SLOT] == (u32)ee_panel_seq) {
        return;
    }
    if (0x48 + 4 * (PANEL_SLOT + 1) > *(u32 *)cls || (cls[0xC] > PANEL_SLOT && seqs[PANEL_SLOT] != 0)) {
        return;         /* no room before the packet table, or the slot is in use */
    }
    seqs[PANEL_SLOT] = (u32)ee_panel_seq;
    if (cls[0xC] <= PANEL_SLOT) {
        cls[0xC] = PANEL_SLOT + 1;
    }
}

static void panel_sequence(s32 L)
{
    u8 *slot_by_id = LVL_DATA(L, class_slot_by_id, u8 *);
    u32 *resources = LVL_DATA(L, class_resources, u32 *);
    u32 *panels = (u32 *)(LVL_DATA(L, menu_state, u8 *) + 0x1B0);
    u8 slot = slot_by_id[PANEL_CLASS];
    s32 i;
    if (slot != 0xFF && resources[slot] != 0) {
        install_panel_sequence((u8 *)resources[slot]);
    }
    for (i = 0; i < 14; i++) {
        u8 *m = (u8 *)panels[i];
        if (m != 0 && *(u16 *)(m + 0xA6) == PANEL_CLASS) {
            install_panel_sequence(*(u8 **)(m + 0x24));
            break;
        }
    }
}

#define CHEAT_ON             (*(volatile u8 *)EE_CHEAT_CHARGE_BOOTS)           /* gCheats[8]: the toggle */
#define CHEAT_SEEN           (*(volatile u8 *)(0x0015EEC0 + EE_CHEAT_ENTRY))   /* unlocked: listed on the Cheats page */
#define ITEM_NONE            0x26        /* hero_switch_item's "no item": what it makes pending to take boots off */
#define HERO_ITEM(slot)      (*(volatile s32 *)(0x0013F450 + 0x1090 + 0x50 * (slot) + 0x28))   /* hero.items[slot].item_id */
/* The pause menu's own equipped item per slot (NTSC menu_system.equipped,
 * menu state + 0x30): what the Gadgets page marks as worn and equips from. */
#define MENU_EQUIPPED(L, slot) (*(volatile s32 *)(LVL_DATA(L, menu_state, u8 *) + 0x30 + 4 * (slot)))

/* The boots follow the cheat's toggle. Called every gameplay frame and, so
 * that the Gadgets page shows the change at once, every frame the Cheats
 * page is open (the gameplay frame does not run while paused). */
static void sync_ownership(s32 L)
{
    /* A save from the one test build (2026-10-19) that made the boots item
     * 1, which is Clank; retail puts only 0, 28 or 29 in the boots slot. */
    if (EQUIPPED(SLOT_BOOTS) == ITEM_CLANK) {
        EQUIPPED(SLOT_BOOTS) = ITEM_CHARGE_BOOTS;
    }
    if (!CHEAT_ON) {
        /* Locked or switched off: not owned, not worn (saves from earlier
         * builds own and wear them). */
        ITEM_OWNED(ITEM_CHARGE_BOOTS) = 0;
        if (EQUIPPED(SLOT_BOOTS) == ITEM_CHARGE_BOOTS) {
            EQUIPPED(SLOT_BOOTS) = 0;
        }
        if (MENU_EQUIPPED(L, SLOT_BOOTS) == ITEM_CHARGE_BOOTS) {
            MENU_EQUIPPED(L, SLOT_BOOTS) = 0;
        }
        if (HERO_ITEM(SLOT_BOOTS) == ITEM_CHARGE_BOOTS || HERO_SELECTED(SLOT_BOOTS) == ITEM_CHARGE_BOOTS
            || HERO_PENDING(SLOT_BOOTS) == ITEM_CHARGE_BOOTS) {
            HERO_PENDING(SLOT_BOOTS) = ITEM_NONE;
        }
        return;
    }
    ITEM_OWNED(ITEM_CHARGE_BOOTS) = 1;
    if (HERO_PENDING(SLOT_BOOTS) == ITEM_CHARGE_BOOTS || HERO_SELECTED(SLOT_BOOTS) == ITEM_CHARGE_BOOTS) {
        EQUIPPED(SLOT_BOOTS) = ITEM_CHARGE_BOOTS;
    }
}

static void rewear_saved_boots(void);

void ee_charge_boots_frame(s32 L)
{
    panel_sequence(L);
    rewear_saved_boots();
    sync_ownership(L);
}

/* The cheat, at the start of every gameplay frame (before the hero update
 * and its detector): once seen, entry 8 of this level's table (each level
 * loads its own, gen_charge's cheat_table) is blanked, so the sequence
 * cannot switch the cheat off (the Cheats page does that). */
extern const u32 ee_charge_cheat_table[19];

void ee_charge_cheat_frame(s32 L)
{
    if (CHEAT_SEEN) {
        *(volatile u8 *)(ee_charge_cheat_table[L] + 16 * EE_CHEAT_ENTRY) = 0;
    }
}

/* Record 36 in the hoverboard races. The record the Charge Boots took is
 * not unused: it is the races' Devastator. The races' ammo pickup (class
 * 0x1D6; NTSC decomp ui/help/00237488.c) puts item 36 in Ratchet's hand
 * and adds to its ammo, and the hero then loads the record's class as the
 * gun (func_00205270). With the boots' class there, the loader found no
 * such gun among the level's, unpacked from address 0 and the game stopped
 * in the retail halt routine (func_L00_0024EC20, a trap) at the first
 * pickup (owner's report, 2026-10-19). No record is free (0 is "nothing",
 * 1 is Clank on the back), so while Ratchet is on the hoverboard (control
 * mode 0x16), or item 36 is in his hand, the record holds retail's words;
 * otherwise the Charge Boots' (worn boots: below). Checked at the start of every gameplay
 * frame, before the hero update in which a pickup equips. */
extern const u32 ee_item36_at[19];
extern const u32 ee_item36_words[19][2][19];
#define HERO_CONTROL_MODE  (*(volatile s32 *)(0x0013F450 + 0x208C))
#define CONTROL_HOVERBOARD 0x16
#define SLOT_HAND          0

/* Worn boots on the board. Item 36 in the boots slot reads the same record
 * as the hand's: with retail's words in it the worn boots were attached as
 * the Devastator is, near Ratchet's hand, during the race (owner,
 * 2026-10-19), and nothing is worn on the feet on the hoverboard. So on
 * the board the boots are taken off (made pending as "no item", as the
 * cheat's toggle does) and the record switches only once they are off,
 * unless a pickup has already put item 36 in the hand. They are put back
 * on (unless other boots have been put on meanwhile) once Ratchet is in
 * normal control, item 36 is out of his hand and the record holds the
 * boots' words again: the first build put them back the frame he left the
 * board, with the race's Devastator still in his hand and the record still
 * retail's, so the boots slot loaded a second Devastator into the hand
 * gadget's buffer; a second Ratchet appeared on the track and threw the
 * real one across the level (owner, 2026-10-19). Retail takes item 36 out
 * of the hand when the game is paused (func_00219C70).
 *
 * The pause menu shows the boots from the record (their preview, their
 * equip), and a pause can open before any gameplay frame has run since the
 * board was left (quitting the race): it showed the Devastator as the
 * Charge Boots (owner, 2026-10-19). So the mode-0 frame's two calls of the
 * pause entry (func_L00_00299250 + 0x36C, + 0x434) put the boots' words
 * back first; the next gameplay frame chooses again. */
#define ITEM_MAGNEBOOTS_ID 0x1C
#define ITEM_GRINDBOOTS_ID 0x1D
#define CONTROL_NORMAL     0
static s32 boots_parked;

static s32 boots_worn(void)
{
    return HERO_ITEM(SLOT_BOOTS) == ITEM_CHARGE_BOOTS || HERO_SELECTED(SLOT_BOOTS) == ITEM_CHARGE_BOOTS
        || HERO_PENDING(SLOT_BOOTS) == ITEM_CHARGE_BOOTS;
}

void ee_race_item_frame(s32 L)
{
    volatile u32 *rec = (volatile u32 *)ee_item36_at[L];
    s32 board = HERO_CONTROL_MODE == CONTROL_HOVERBOARD;
    s32 hand = EQUIPPED(SLOT_HAND) == ITEM_CHARGE_BOOTS
        || HERO_ITEM(SLOT_HAND) == ITEM_CHARGE_BOOTS || HERO_PENDING(SLOT_HAND) == ITEM_CHARGE_BOOTS
        || HERO_SELECTED(SLOT_HAND) == ITEM_CHARGE_BOOTS;
    s32 race = board || hand;
    const u32 *want;
    s32 i;
    if (board && boots_worn()) {
        HERO_PENDING(SLOT_BOOTS) = ITEM_NONE;
        boots_parked = 1;
        race = hand;
    } else if (boots_parked && !board && !hand && HERO_CONTROL_MODE == CONTROL_NORMAL
               && HERO_PENDING(SLOT_BOOTS) == 0 && rec[0] == ee_item36_words[L][1][0]) {
        if (ITEM_OWNED(ITEM_CHARGE_BOOTS) && HERO_ITEM(SLOT_BOOTS) != ITEM_MAGNEBOOTS_ID
            && HERO_ITEM(SLOT_BOOTS) != ITEM_GRINDBOOTS_ID) {
            HERO_PENDING(SLOT_BOOTS) = ITEM_CHARGE_BOOTS;
        }
        boots_parked = 0;
    }
    want = ee_item36_words[L][race ? 0 : 1];
    if (rec[0] != want[0]) {
        for (i = 0; i < 19; i++) {
            rec[i] = want[i];
        }
    }
}

DECLARE_LVL(func_L00_00299250);        /* the mode-0 frame */
DECLARE_LVL(func_00219C70);            /* the pause entry */
#define PAUSE_SITE_A 0x36C
#define PAUSE_SITE_B 0x434
typedef void (*PauseFn)(s32);

void ee_pause_entry_c(s32 arg, u32 ra)
{
    s32 L;
    volatile u32 *rec;
    s32 i;
    for (L = 0; L < 19; L++) {
        u32 f = lvl_func_L00_00299250[L];
        if (ra == f + PAUSE_SITE_A + 8 || ra == f + PAUSE_SITE_B + 8) {
            break;
        }
    }
    if (L == 19) {
        L = ee_level_of_call(ra, lvl_func_L00_00299250, PAUSE_SITE_A);   /* never expected */
    }
    rec = (volatile u32 *)ee_item36_at[L];
    if (HERO_ITEM(SLOT_HAND) != ITEM_CHARGE_BOOTS && HERO_PENDING(SLOT_HAND) != ITEM_CHARGE_BOOTS
        && HERO_SELECTED(SLOT_HAND) != ITEM_CHARGE_BOOTS) {   /* never under a race Devastator in the hand */
        for (i = 0; i < 19; i++) {
            rec[i] = ee_item36_words[L][1][i];
        }
    }
    LVL(L, func_00219C70, PauseFn)(arg);
    ee_rz_pause_open(L);                /* the Randomizer's Save and Load pages (src/goodies.c), once the entry built the panels */
}

/* Saves a retail game can load. The equipped array is saved (block 0x20)
 * and the hero builds each slot's moby from it: a retail game builds the
 * boots slot from its own record 36, the race Devastator (weapon slot,
 * class 157, which no level keeps resident), and stopped at once with an
 * edition save that wore the Charge Boots (PCSX2: "FQC = 0 on VIF FIFO
 * READ"; owner, 2026-10-20). So every save call (memcard_MakeWholeSave,
 * func_0020BA00: the pause menu's two, the challenge-mode carry-over's and
 * Veldin 2's ending, src/charge_hooks.s) writes the boots slot empty and
 * item 36 not owned, with EE_SAVE_BOOTS_WORN set in the saved statistics
 * block, and puts them back after. Ownership follows gCheats[8] every frame
 * anyway; on a load the flag puts the boots back on (rewear_saved_boots),
 * the way the race code does, once Ratchet is in normal control. The
 * quick select is turned to page 1 for the save the same way
 * (src/quick_select_pages.c), as a retail game keeps the saved ring's
 * page for good. */
DECLARE_LVL(func_0020BA00);            /* memcard_MakeWholeSave(out) */
DECLARE_LVL(func_00227C78);            /* its callers: + 0x64 */
DECLARE_LVL(func_00227D20);            /* + 0x3C */
DECLARE_LVL(func_00227DB0);            /* the challenge-mode carry-over: + 0x304 */
#define SAVE_WORN          (*(volatile u8 *)EE_SAVE_BOOTS_WORN)
#define SAVE_SITE_A        0x64
#define SAVE_SITE_B        0x3C
#define SAVE_SITE_CARRY    0x304
typedef void (*MakeSaveFn)(u8 *out);

void ee_save_c(u8 *out, u32 ra, s32 L)
{
    s32 worn = EQUIPPED(SLOT_BOOTS) == ITEM_CHARGE_BOOTS;
    u8 owned = ITEM_OWNED(ITEM_CHARGE_BOOTS);
    s32 qs_page;
    if (L < 0) {
        for (L = 0; L < 19; L++) {
            if (ra == lvl_func_00227C78[L] + SAVE_SITE_A + 8 || ra == lvl_func_00227D20[L] + SAVE_SITE_B + 8
                || ra == lvl_func_00227DB0[L] + SAVE_SITE_CARRY + 8) {
                break;
            }
        }
        if (L == 19) {
            L = ee_level_of_call(ra, lvl_func_00227C78, SAVE_SITE_A);   /* never expected */
        }
    }
    qs_page = ee_qs_turn_to_page(0);
    if (worn) {
        EQUIPPED(SLOT_BOOTS) = 0;
    }
    ITEM_OWNED(ITEM_CHARGE_BOOTS) = 0;
    SAVE_WORN = worn;
    LVL(L, func_0020BA00, MakeSaveFn)(out);
    SAVE_WORN = 0;
    ITEM_OWNED(ITEM_CHARGE_BOOTS) = owned;
    if (worn) {
        EQUIPPED(SLOT_BOOTS) = ITEM_CHARGE_BOOTS;
    }
    ee_qs_turn_to_page(qs_page);
}

static void rewear_saved_boots(void)
{
    if (!SAVE_WORN) {
        return;
    }
    if (!CHEAT_ON) {
        SAVE_WORN = 0;
        return;
    }
    if (HERO_CONTROL_MODE != CONTROL_NORMAL) {
        return;
    }
    if (EQUIPPED(SLOT_BOOTS) == 0 && HERO_PENDING(SLOT_BOOTS) == 0) {
        HERO_PENDING(SLOT_BOOTS) = ITEM_CHARGE_BOOTS;
    }
    SAVE_WORN = 0;
}

/* The worn boots' pose. A worn item moby is placed at a joint of Ratchet's
 * every frame, but only some items are posed from Ratchet's skeleton: the
 * item updater (func_L00_0020FC18) asks three predicates of each slot's
 * item, and the boots one (func_L00_0020DBD8: item - 0x1C < 2, i.e. the
 * Magneboots or the Grindboots) sends each boot moby through the pose
 * builder (func_L00_00209940), which writes Ratchet's leg (joint lists 2
 * and 3: knee, ankle, heel, ball, toe) into a per-boot frame buffer that
 * the moby then plays. Any other item keeps its class's own static frame,
 * so the Charge Boots (36) were drawn rigid on the knee whatever their
 * skin, the soles tilting with the shin through the floor (owner's report,
 * 2026-10-18; measured: the boot moby's frame pointer at + 0x6C pointed
 * into its class, the Magneboots' at the buffers 0x17D100/0x17D200 in
 * level 8). The predicate's slot_item call comes here (src/charge_hooks.s)
 * and the Charge Boots answer as the Magneboots, for that test only. */
DECLARE_LVL(func_L00_0020DBD8);        /* the boots predicate; its slot_item call is at + 8 */
DECLARE_LVL(func_L00_0020DB30);        /* slot_item(slot): the item in a hero slot */
#define ITEM_MAGNEBOOTS    0x1C
#define EE_SITE_BOOTS_POSE 0x8
typedef s32 (*SlotItem)(s32 slot);

s32 ee_boots_pose_item_c(s32 slot, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_0020DBD8, EE_SITE_BOOTS_POSE);
    s32 item = LVL(L, func_L00_0020DB30, SlotItem)(slot);
    return item == ITEM_CHARGE_BOOTS ? ITEM_MAGNEBOOTS : item;
}

/* The Goodies Cheats page: its list widget's "enter" callback is the row
 * builder (func_0021B138; NTSC decomp rebuild_configured_text_label_list),
 * which writes a row {name, &gCheats[id], "On", "Off", 0} for each seen
 * cheat of its id list {1, 3, 0, 7, 4, 6, 2} into the widget's list (13
 * rows of 0x14 bytes, gen_charge's cheats_list) and ends it with a zero
 * name. Each level's callback slot points at a stub that passes the level
 * (src/charge_hooks.s); the builder runs, then the Charge Boots row is
 * added once the cheat is seen. The page has no other per-row table (its
 * other widgets are the title and the button prompts). */
extern const u32 ee_charge_cheats_builder[19], ee_charge_cheats_list[19];
#define TEXT_ON              0x4F5A
#define TEXT_OFF             0x4F5B
#define TEXT_CHARGE_BOOTS    0x5406   /* text/strings.tsv: "Charge Boots" */
#define CHEAT_ROWS           13
struct cheat_row {
    s32 text;
    u8 *flag;
    s32 on, off;
    s32 value;
};
typedef s32 (*CheatsBuilder)(s32 a0, s32 a1, s32 a2, s32 a3);

s32 ee_cheats_enter_c(s32 a0, s32 a1, s32 a2, s32 a3, s32 L)
{
    s32 result = ((CheatsBuilder)ee_charge_cheats_builder[L])(a0, a1, a2, a3);
    struct cheat_row *row = (struct cheat_row *)ee_charge_cheats_list[L];
    s32 n = 0;
    if (!CHEAT_SEEN) {
        return result;
    }
    while (n < CHEAT_ROWS - 1 && row[n].text != 0) {
        n++;
    }
    if (n < CHEAT_ROWS - 1) {
        row[n].text = TEXT_CHARGE_BOOTS;
        row[n].flag = (u8 *)EE_CHEAT_CHARGE_BOOTS;
        row[n].on = TEXT_ON;
        row[n].off = TEXT_OFF;
        row[n].value = 0;
        row[n + 1].text = 0;
    }
    return result;
}

/* The page's "update" callback (the option list's, func_00221E60, which
 * flips a row's flag on Cross), through the per-level stubs ee_cheats_update_NN
 * (src/charge_hooks.s): the boots follow a toggle at once, so the Gadgets
 * page shows them, or no longer does, without leaving the pause menu. */
extern const u32 ee_charge_cheats_update[19];
typedef s32 (*CheatsUpdate)(s32 screen);

s32 ee_cheats_update_c(s32 screen, s32 L)
{
    s32 result = ((CheatsUpdate)ee_charge_cheats_update[L])(screen);
    sync_ownership(L);
    return result;
}
