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
void ee_charge_boots_frame(void)
{
    ITEM_OWNED(ITEM_CHARGE_BOOTS) = 1;
    if (HERO_PENDING(SLOT_BOOTS) == ITEM_CHARGE_BOOTS || HERO_SELECTED(SLOT_BOOTS) == ITEM_CHARGE_BOOTS) {
        EQUIPPED(SLOT_BOOTS) = ITEM_CHARGE_BOOTS;
    }
}
