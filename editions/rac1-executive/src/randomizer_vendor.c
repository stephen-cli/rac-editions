/* rz3/rz4/rz5: the Gadgetron vendor in a Randomizer save: what it lists,
 * and how a purchase that is a check looks (src/randomizer_play.h for the
 * other files).
 *
 * The list. Retail's vendor (VendorBuildItemList, func_L00_0029B770) lists
 * its stock (the level's weapon unbought, every weapon bought before:
 * D_0015EED0, bit 0x40 bought) as purchases, and ammo for every weapon
 * owned; a purchase is the weapon itself, so owning it and buying it are one
 * thing. In a Randomizer save they are two: a purchase is a check, listed
 * until it is collected (whether or not the weapon came from elsewhere, and
 * whatever the stock list says after a reload), and ammo is for a weapon in
 * hand only. The bonus vendor check, the Charge Boots, is listed last by
 * every vendor once the player has landed on the seed's planet.
 *
 * The mystery entries. A purchase entry is a check, not the item it is
 * named after, so it shows as a mystery at the entry's own price: a blue
 * question-mark tile on the icon strip (EE_ICON_MYSTERY, tools/gen_hud.py),
 * "???" for its name, and the Morph-o-Ray's chicken on the item panel and in
 * the hologram instead of the weapon's model. Ammo entries, the weapons
 * owned, look as in retail. */
#include "randomizer_play.h"

DECLARE_LVL(func_L00_0029B770);         /* VendorBuildItemList(gold) */
DECLARE_LVL(func_00205270);             /* LoadMobyClassResources(class, buffer) */
DECLARE_LVL(func_0020D440);             /* InitMobyInstance(moby, class) */
DECLARE_LVL(func_L00_00251E30);         /* the item display's placement of a model, once its pose is set */
DECLARE_LVL(func_001F9EC0);             /* matrix_mul_vec3(out, vector, matrix) */
DECLARE_LVL(func_001F9BD8);             /* FastVecAdd(out, a, b) */
DECLARE_LVL_DATA(vendor_state);         /* the vendor's record (D_L00_001CA7C0 in level 0) */
DECLARE_LVL_DATA(vendor_items);         /* the level's item info table */
DECLARE_LVL_DATA(vendor_sounds);        /* the level's item sound table */
DECLARE_LVL_DATA(vendor_pose);          /* the item panel's per-item model offsets (D_001C8C20 in level 0) */
typedef void (*ListFn)(s32);
typedef void (*ClassFn)(s32, s32);
typedef void (*InitMobyFn)(void *, s32);
typedef void (*PlaceFn)(void *);
typedef void (*VectorFn)(void *, const void *, const void *);

/* The vendor's record. */
#define VENDOR_CAMERA   0x1C            /* its camera's record: the yaw at +0x48, the matrix at +0xC0 */
#define VENDOR_DISPLAY  0x20            /* the display's mobys: */
#define DISPLAY_PANEL   0x100           /*   the item on the panel, */
#define DISPLAY_STAND   0x200           /*   its stand, */
#define DISPLAY_HOLOGRAM 0x300          /*   the hologram over the vendor */
#define VENDOR_BOUGHT   0x48            /* the item just bought, for the weapon demo on leaving; -1 none */
#define VENDOR_CURSOR   0x58            /* the entry the cursor is on */
#define VENDOR_ENTRIES  0xD0            /* struct VendorEntry[VENDOR_ENTRIES_MAX] */
#define VENDOR_COUNT    0x210
#define VENDOR_ENTRIES_MAX 16
#define CAMERA_YAW      0x48
#define CAMERA_MATRIX   0xC0

struct VendorEntry {
    s32 item;                           /* the item id */
    s32 ammo;                           /* 0 a purchase, 1 the weapon's ammo */
    s32 state;
    s32 unused;
    s32 sound;
};

/* The item info table: a record per item id. */
#define ITEM_INFO_SIZE  0x18
#define ITEM_PRICE      0
#define ITEM_PRICE_PERSUADER 4
#define ITEM_AMMO_PRICE 8               /* u16, 0 for an item without ammo */
#define ITEM_SOUND_STRIDE 0x14          /* the sound table: a word per item id */
#define ITEM_POSE_SIZE  0x30            /* vendor_pose: an item's offset x, y, z, then its lift for the price line */

#define BONUS_PRICE     2500            /* the Charge Boots entry */
#define BONUS_PRICE_PERSUADER 1250
#define CHARGE_BOOTS_CLASS 2000         /* R&C2's boots (tools/gen_boots.py), spliced into every level's core */
#define ITEM_ICON_BASE  0xEA60          /* the items' HUD icons: ITEM_ICON_BASE + item id */
#define TEXT_AMMO_PROMPT 0x4F5D         /* the item panel's line under an ammo entry's name */

/* The chicken on the item panel. */
#define CHICKEN_CLASS   270             /* the Morph-o-Ray's chicken (MorphorayChickenUpdate), in every level */
#define CHICKEN_SCALE   0.6f            /* a weapon-sized chicken is too tall for the panel */
#define CHICKEN_LOWER   0.15f           /* its origin is at its feet, so it comes down a little */
#define WEAPON_SPECK    0.02f           /* the weapon's model in the stand's slot, at 2% of its size: see
                                         * ee_rz_vendor_model_c; a scale of 0 froze the game on opening */

static s32 purchase_item = -1;          /* a check was bought: the vendor's item id, */
static s32 reward_item;                 /* and the item id the check gave, -1 for one that is no item */
static s32 menu_open;                   /* the list was built and no gameplay frame has run since */

void rz_vendor_note_purchase(s32 vendor_item_id, s32 reward_item_id)
{
    purchase_item = vendor_item_id;
    reward_item = reward_item_id;
}

void rz_vendor_forget_purchase(void)
{
    purchase_item = -1;
}

void rz_vendor_menu_closed(void)
{
    menu_open = 0;
}

s32 rz_vendor_menu_open(void)
{
    return menu_open;
}

/* --- the list ------------------------------------------------------------------------------------------------ */

static struct VendorEntry *entries(u8 *vendor)
{
    return (struct VendorEntry *)(vendor + VENDOR_ENTRIES);
}

static s32 sound_of(s32 item, s32 L)
{
    return *(const s32 *)(LVL_DATA(L, vendor_sounds, const u8 *) + ITEM_SOUND_STRIDE * item);
}

static void set_entry(struct VendorEntry *entry, s32 item, s32 ammo, s32 L)
{
    entry->item = item;
    entry->ammo = ammo;
    entry->state = 0;
    entry->unused = 0;
    entry->sound = sound_of(item, L);
}

/* A purchase stays while its check is uncollected; ammo only for a weapon really owned. */
static s32 entry_stays(const struct VendorEntry *entry)
{
    s32 check;
    if (entry->ammo) {
        return rz_play_owned(entry->item);
    }
    check = rz_check_for_vanilla(rz_item_of_game_id(entry->item));
    return check < 0 || !ee_rz_check_collected(check);
}

static s32 ammo_listed(const struct VendorEntry *list, s32 count, s32 item)
{
    s32 i;
    for (i = 0; i < count; i++) {
        if (list[i].item == item && list[i].ammo) {
            return 1;
        }
    }
    return 0;
}

/* VendorBuildItemList(gold) in level L (src/rz_hooks.s): retail's list, then the Randomizer's rules on it.
 *
 * The call right after a purchase (from VendorBuyDialogUpdate) comes once the bought item is noted in the
 * vendor's record for the weapon demo played on leaving, which equips the noted item: with a shuffled reward
 * that is the vendor's own weapon, unowned and without ammo. The note is cleared unless the reward is that
 * weapon itself, so there is no demo, as for an item that has none, and the hero keeps what the check gave. */
void ee_rz_vendor_list_c(s32 gold, s32 L)
{
    u8 *vendor;
    struct VendorEntry *list;
    s32 *count;
    s32 *cursor;
    s32 retail_count, i, kept, item;
    LVL(L, func_L00_0029B770, ListFn)(gold);
    if (!rz_play_active()) {
        return;
    }
    menu_open = 1;
    vendor = LVL_DATA(L, vendor_state, u8 *);
    if (purchase_item >= 0) {
        s32 *bought = (s32 *)(vendor + VENDOR_BOUGHT);
        if (*bought == purchase_item && reward_item != purchase_item) {
            *bought = -1;
        }
        purchase_item = -1;
    }
    list = entries(vendor);
    count = (s32 *)(vendor + VENDOR_COUNT);
    cursor = (s32 *)(vendor + VENDOR_CURSOR);

    retail_count = *count;
    kept = 0;
    for (i = 0; i < retail_count; i++) {
        if (entry_stays(&list[i])) {
            list[kept++] = list[i];
        }
    }
    /* ammo for every weapon owned that retail listed as a purchase instead */
    for (item = 0; item < ITEMS && kept < VENDOR_ENTRIES_MAX; item++) {
        const u8 *info = LVL_DATA(L, vendor_items, const u8 *) + ITEM_INFO_SIZE * item;
        s32 has_ammo = *(const u16 *)(info + ITEM_AMMO_PRICE) != 0;
        if (rz_play_owned(item) && has_ammo && !ammo_listed(list, kept, item)) {
            set_entry(&list[kept++], item, 1, L);
        }
    }
    /* the bonus vendor check, priced in the level's item info record */
    if (ee_rz_bonus_seen() && !ee_rz_check_collected(RZ_CHECKS_BASE) && kept < VENDOR_ENTRIES_MAX) {
        u8 *info = LVL_DATA(L, vendor_items, u8 *) + ITEM_INFO_SIZE * CHARGE_BOOTS_ID;
        *(volatile s32 *)(info + ITEM_PRICE) = BONUS_PRICE;
        *(volatile s32 *)(info + ITEM_PRICE_PERSUADER) = BONUS_PRICE_PERSUADER;
        *(volatile u16 *)(info + ITEM_AMMO_PRICE) = 0;
        set_entry(&list[kept++], CHARGE_BOOTS_ID, 0, L);
    }
    *count = kept;
    if (*cursor >= kept) {
        *cursor = kept > 0 ? kept - 1 : 0;
    }
}

/* LoadMobyClassResources(class, buffer) in Vendor_InitItemDisplay: the loader finds a gadget's class in the
 * level's gadget directory and reads it in. The Charge Boots' class is not there (it is spliced into every
 * level's core, so already in memory), and the loader running off the end of its directory froze the game when
 * the cursor reached the entry. */
void ee_rz_vendor_class_c(s32 class_id, s32 buffer, s32 L)
{
    if (class_id == CHARGE_BOOTS_CLASS) {
        return;
    }
    LVL(L, func_00205270, ClassFn)(class_id, buffer);
}

/* --- the mystery entries ------------------------------------------------------------------------------------- */

/* The cursor is on a purchase entry, in a Randomizer save. */
static s32 mystery_selected(s32 L)
{
    u8 *vendor;
    s32 cursor;
    if (!rz_play_active()) {
        return 0;
    }
    vendor = LVL_DATA(L, vendor_state, u8 *);
    cursor = *(s32 *)(vendor + VENDOR_CURSOR);
    return cursor >= 0 && cursor < *(s32 *)(vendor + VENDOR_COUNT) && !entries(vendor)[cursor].ammo;
}

/* GetIconFrame in VendorDrawIconStrip (func_00239A00): frame 0 of an item's icon is a purchase entry's (an ammo
 * entry's is frame 2), which becomes the mystery tile. */
s32 ee_rz_vendor_icon_c(s32 icon, s32 frame, s32 L)
{
    s32 item_icon = icon >= ITEM_ICON_BASE && icon < ITEM_ICON_BASE + ITEMS;
    if (rz_play_active() && frame == 0 && item_icon) {
        return LVL(L, func_00200198, IconFn)(EE_ICON_MYSTERY, 0);
    }
    return LVL(L, func_00200198, IconFn)(icon, frame);
}

/* msg_string in VendorDrawItemPanel (func_00239CF8): the selected purchase's name is "???". */
const char *ee_rz_vendor_text_c(s32 id, s32 L)
{
    if (id != TEXT_AMMO_PROMPT && mystery_selected(L)) {
        return ee_msg_string_edition(EE_TEXT_MYSTERY);
    }
    return LVL(L, func_001FE540, MsgFn)(id);
}

/* msg_string in VendorMenuUpdate (func_L00_0029D988): the selected item's slogan for the scrolling line
 * ("BLASTER! POINT IT MORE OR LESS AT THE BAD GUYS..."), which names the weapon; a purchase entry has none, and
 * the scroller moves on to the vendor's own adverts. */
const char *ee_rz_vendor_slogan_c(s32 id, s32 L)
{
    if (mystery_selected(L)) {
        return "";
    }
    return LVL(L, func_001FE540, MsgFn)(id);
}

/* InitMobyInstance in Vendor_InitItemDisplay, for the panel's model, its stand and the hologram in turn.
 *
 * The panel's model and the hologram are the chicken. The stand's slot gets the weapon itself, drawn as a speck
 * (ee_rz_vendor_place_c), because the game needs a model of the weapon's class drawn during the visit: on a PS2
 * (never in PCSX2) leaving the vendor froze the game whenever neither display model was the weapon, with a
 * chicken in both places or a chicken and the stand; with the weapon in either place it left fine. The vendor
 * loads the weapon's class into the gadget buffer on opening and Ratchet's own back on leaving, and drawing a
 * model of the loaded class does bookkeeping that reload relies on. The stand itself is not missed: the chicken
 * stands on the panel's floor. */
static s32 entry_weapon_class;         /* the entry's weapon class, from the panel's call for the stand's */

void ee_rz_vendor_model_c(void *moby, s32 class_id, s32 L)
{
    if (mystery_selected(L)) {
        u8 *display = *(u8 **)(LVL_DATA(L, vendor_state, u8 *) + VENDOR_DISPLAY);
        if ((u8 *)moby == display + DISPLAY_PANEL) {
            entry_weapon_class = class_id;
            class_id = CHICKEN_CLASS;
        } else if ((u8 *)moby == display + DISPLAY_STAND) {
            if (entry_weapon_class > 0) {
                class_id = entry_weapon_class;
            }
        } else {
            class_id = CHICKEN_CLASS;
        }
    }
    LVL(L, func_0020D440, InitMobyFn)(moby, class_id);
}

static volatile f32 *moby_float(u8 *moby, s32 offset)
{
    return (volatile f32 *)(moby + offset);
}

/* Retail puts the panel's model at the panel's base point plus the item's own offset (vendor_pose), which centres
 * that weapon's model (its origin is at the grip), lifted for the price line once, twice when the Persuader's
 * prices show too. The chicken's origin is at its feet, so it goes back to the base point: the item's offset,
 * turned into the world by the vendor camera's matrix, is taken off again. */
static void undo_item_offset(u8 *panel, s32 item, const u8 *camera, s32 L)
{
    const f32 *offset = (const f32 *)(LVL_DATA(L, vendor_pose, const u8 *) + ITEM_POSE_SIZE * item);
    f32 back[4] __attribute__((aligned(16)));
    f32 world[4] __attribute__((aligned(16)));
    f32 price_lift = offset[3] * (PERSUADER_OWNED ? 2.0f : 1.0f);
    back[0] = -offset[0];
    back[1] = -offset[1];
    back[2] = -(offset[2] + price_lift);
    back[3] = 0.0f;
    LVL(L, func_001F9EC0, VectorFn)(world, back, camera + CAMERA_MATRIX);
    *moby_float(panel, MOBY_POS) += world[0];
    *moby_float(panel, MOBY_POS + 4) += world[1];
    *moby_float(panel, MOBY_POS + 8) += world[2];
}

/* The display's placement of a model in Vendor_InitItemDisplay, once its pose is set from the item's tables (which
 * tilt a weapon for the panel): the panel's chicken stands upright facing the camera, smaller and a little lower;
 * the weapon in the stand's slot shrinks to a speck. */
void ee_rz_vendor_place_c(void *moby, s32 L)
{
    if (mystery_selected(L)) {
        u8 *vendor = LVL_DATA(L, vendor_state, u8 *);
        u8 *display = *(u8 **)(vendor + VENDOR_DISPLAY);
        u8 *camera = *(u8 **)(vendor + VENDOR_CAMERA);
        u8 *model = (u8 *)moby;
        if (model == display + DISPLAY_PANEL) {
            s32 item = entries(vendor)[*(s32 *)(vendor + VENDOR_CURSOR)].item;
            undo_item_offset(model, item, camera, L);
            *moby_float(model, MOBY_ROT) = 0.0f;
            *moby_float(model, MOBY_ROT + 4) = 0.0f;
            *moby_float(model, MOBY_ROT + 8) = *(volatile f32 *)(camera + CAMERA_YAW);
            *moby_float(model, MOBY_SCALE) *= CHICKEN_SCALE;
            *moby_float(model, MOBY_POS_Z) -= CHICKEN_LOWER;
        } else if (model == display + DISPLAY_STAND) {
            *moby_float(model, MOBY_SCALE) *= WEAPON_SPECK;
        }
    }
    LVL(L, func_L00_00251E30, PlaceFn)(moby);
}

/* The hologram's placement each frame in VendorMenuUpdate, right after its pose is copied from the item's table:
 * the tilt that table gives a weapon (x and y) laid the chicken on its back; it stands upright and keeps the spin
 * (z). The pose is rewritten every frame, so nothing builds up. */
void ee_rz_vendor_holo_c(void *moby, s32 L)
{
    if (mystery_selected(L)) {
        *moby_float((u8 *)moby, MOBY_ROT) = 0.0f;
        *moby_float((u8 *)moby, MOBY_ROT + 4) = 0.0f;
    }
    LVL(L, func_L00_00251E30, PlaceFn)(moby);
}

/* The hologram's two per-item offsets in VendorMenuUpdate (FastVecAdd of a table entry), both tuned to the weapon:
 * its place in the beam, and the pivot its spin swings around, which had the chicken circling off the beam's axis.
 * The chicken goes without both. */
void ee_rz_vendor_holo_add_c(f32 *out, const f32 *base, const f32 *item_offset, s32 L)
{
    if (mystery_selected(L)) {
        out[0] = base[0];
        out[1] = base[1];
        out[2] = base[2];
        out[3] = base[3];
        return;
    }
    LVL(L, func_001F9BD8, VectorFn)(out, base, item_offset);
}
