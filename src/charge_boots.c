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
 * Until the Challenge Mode reward exists, the item is simply owned: the
 * flag is set every frame from the HUD update hook. Equipping it attaches
 * the Grindboots model; it grants nothing else yet. */
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

void ee_charge_boots_frame(s32 L)
{
    ITEM_OWNED(ITEM_CHARGE_BOOTS) = 1;
    panel_sequence(L);
    if (HERO_PENDING(SLOT_BOOTS) == ITEM_CHARGE_BOOTS || HERO_SELECTED(SLOT_BOOTS) == ITEM_CHARGE_BOOTS) {
        EQUIPPED(SLOT_BOOTS) = ITEM_CHARGE_BOOTS;
    }
}
