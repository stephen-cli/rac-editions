/* The Gadgetron vendor's ammo dialog opens at the most ammo the player can
 * buy, rather than at 1.
 *
 * Choosing an ammo entry opens the buy dialog (VendorBuyDialogUpdate,
 * func_L00_0029CDE0; NTSC decomp FUN_L00_0029ba78). Its opening state sets
 * the quantity to 1 (D_L00_00161F30 in level 0, level-local) and, when the
 * player can afford one unit and the weapon is not full, lets left and
 * right change it: down to 1, and up to the smaller of the units the bolts
 * pay for and the units the weapon has room for. The dialog's draw
 * (func_L00_0029FD68) shows the quantity and its total price, and the
 * purchase (state 4) buys that many.
 *
 * The opening state calls func_L00_00251E30 (the display's placement of the
 * dialog's moby) once, after the quantity is set and before anything reads
 * it; tools/overlay_patch.py's vendor_ammo feature sends that call here
 * (src/hooks.s, ee_vendor_ammo_open), which makes the call and then sets the
 * quantity to that same upper bound. A weapon that is full, or one the
 * bolts cannot pay a unit of, keeps 1, which the dialog refuses as retail
 * does. Left still lowers the quantity to buy less. */
#include "modseg.h"

DECLARE_LVL(func_L00_0029CDE0);         /* VendorBuyDialogUpdate */
DECLARE_LVL(func_L00_00251E30);         /* the display's placement of a moby */
DECLARE_LVL_DATA(vendor_state);         /* the vendor's record (D_L00_001CA7C0 in level 0) */
DECLARE_LVL_DATA(vendor_items);         /* the level's item info table (D_L00_001C43B0 in level 0) */
DECLARE_LVL_DATA(vendor_quantity);      /* the buy dialog's quantity (D_L00_00161F30 in level 0) */
typedef void (*PlaceFn)(void *);

/* The vendor's record. */
#define VENDOR_GOLD     0x40            /* nonzero: the ammo price is the second one (+0xA), as retail picks it */
#define VENDOR_CURSOR   0x58            /* the entry the cursor is on */
#define VENDOR_ENTRIES  0xD0            /* entries of 0x14: the item id at +0, 1 at +4 for the item's ammo */
#define ENTRY_SIZE      0x14
#define ENTRY_AMMO      0x4

/* The item info table: a record of 0x18 per item id. */
#define ITEM_INFO_SIZE  0x18
#define ITEM_AMMO_PRICE 0x8             /* u16 */
#define ITEM_AMMO_PRICE_GOLD 0xA        /* u16 */
#define ITEM_AMMO_MAX   0xE             /* u16 */

#define AMMO_COUNTS     ((volatile s32 *)0x0013D530)   /* ammo held, an s32 per item id (NTSC weapon_ammo_counts) */

void ee_vendor_ammo_open_c(void *moby, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_0029CDE0, EE_SITE_VENDOR_AMMO);
    u8 *vendor = LVL_DATA(L, vendor_state, u8 *);
    u8 *entry;
    const u8 *info;
    s32 item, price, quantity, room;

    LVL(L, func_L00_00251E30, PlaceFn)(moby);

    entry = vendor + VENDOR_ENTRIES + ENTRY_SIZE * *(s32 *)(vendor + VENDOR_CURSOR);
    if (*(s32 *)(entry + ENTRY_AMMO) != 1) {
        return;
    }
    item = *(s32 *)entry;
    info = LVL_DATA(L, vendor_items, const u8 *) + ITEM_INFO_SIZE * item;
    price = *(const u16 *)(info + (*(s32 *)(vendor + VENDOR_GOLD) ? ITEM_AMMO_PRICE_GOLD : ITEM_AMMO_PRICE));
    if (price <= 0) {
        return;
    }
    quantity = g_bolts / price;
    room = *(const u16 *)(info + ITEM_AMMO_MAX) - AMMO_COUNTS[item];
    if (room < quantity) {
        quantity = room;
    }
    if (quantity >= 1) {
        *LVL_DATA(L, vendor_quantity, volatile s32 *) = quantity;
    }
}
