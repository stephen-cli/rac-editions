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
 * jump, Comet-Strike, Hyper-Strike). Every frame (ee_charge_cheat_frame)
 * the boots are owned when gCheats[8] is set and taken away otherwise, and
 * once set, entry 8 of the level's table is blanked so a second entry of
 * the sequence does nothing (no "cheat off"); the banner of the unlock is
 * the edition's own text. */
#include "modseg.h"

#define ITEM_OWNED(id)     (*(volatile u8 *)(0x0013D5C8 + (id)))   /* D_0013D5C8: owned items, by id */
#define ITEM_CHARGE_BOOTS  36
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

#define CHEAT_UNLOCKED       (*(volatile u8 *)EE_CHEAT_CHARGE_BOOTS)
#define ITEM_NONE            0x26        /* hero_switch_item's "no item": what it makes pending to take boots off */
#define HERO_ITEM(slot)      (*(volatile s32 *)(0x0013F450 + 0x1090 + 0x50 * (slot) + 0x28))   /* hero.items[slot].item_id */

void ee_charge_boots_frame(s32 L)
{
    panel_sequence(L);
    if (!CHEAT_UNLOCKED) {
        /* Locked: not owned, not worn (saves from earlier builds own and
         * wear them). */
        ITEM_OWNED(ITEM_CHARGE_BOOTS) = 0;
        if (EQUIPPED(SLOT_BOOTS) == ITEM_CHARGE_BOOTS) {
            EQUIPPED(SLOT_BOOTS) = 0;
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

/* The cheat, at the start of every gameplay frame (before the hero update
 * and its detector): once unlocked, entry 8 of this level's table (each
 * level loads its own, gen_charge's cheat_table) is blanked, so the
 * sequence cannot switch the cheat off; and the state at the frame's start
 * tells the banner hook whether this frame's "cheat on" is the unlock. */
extern const u32 ee_charge_cheat_table[19], ee_charge_banner_site[19], ee_charge_show_banner[19];
#define TEXT_CHEAT_ON          0x4FBE
#define TEXT_BOOTS_UNLOCKED    0x5409   /* text/strings.tsv */
typedef void (*ShowBanner)(s32 text, s32 time);
static u8 unlocked_at_frame_start;

void ee_charge_cheat_frame(s32 L)
{
    unlocked_at_frame_start = CHEAT_UNLOCKED;
    if (CHEAT_UNLOCKED) {
        *(volatile u8 *)(ee_charge_cheat_table[L] + 16 * EE_CHEAT_ENTRY) = 0;
    }
}

/* The detector's banner call (func_L00_00207418): the unlock shows the
 * edition's text instead of the generic "cheat on". */
void ee_charge_banner_c(s32 text, s32 time, u32 ra)
{
    s32 L = ee_level_of_call(ra, ee_charge_banner_site, 0);
    if (text == TEXT_CHEAT_ON && CHEAT_UNLOCKED && !unlocked_at_frame_start) {
        text = TEXT_BOOTS_UNLOCKED;
    }
    ((ShowBanner)ee_charge_show_banner[L])(text, time);
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
