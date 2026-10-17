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

void ee_charge_boots_frame(void)
{
    ITEM_OWNED(ITEM_CHARGE_BOOTS) = 1;
}
