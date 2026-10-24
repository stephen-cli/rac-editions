/* rz3/rz5: the Randomizer in play: every check hands out the item the
 * seed put there, and the player is told what it was.
 *
 * What a check is in the game. Every gadget and weapon the game ever gives
 * goes through GiveItem (func_L00_002618D8: marks the item owned, raises
 * its ammo, shows its "You got the ..." banner, puts it on the quick select
 * and in the hand), from character dialogues, pickups and the Gadgetron
 * vendor alike; every infobot ends in UnlockPlanet (func_L00_00261848: the
 * planet's flag in D_0013DE48, the galaxy list, the "Infobot for Planet ...
 * acquired" banner); a Gold Bolt sets its spot's flag (D_0014BFC0[level][k])
 * and saves (GoldBoltUpdate, func_L01_00309078); the Zoomerator, the
 * Raritanium, the Codebot and the two Nanotech upgrades set a byte of the
 * global flags D_0013D490 (0, 1, 2, 4, 5). tools/gen_randomizer.py finds
 * every call of GiveItem, UnlockPlanet and ShowPlanetBanner in each
 * level's code and tools/overlay_patch.py (feature randomizer) sends them
 * to this file's hooks through the level's stubs (src/rz_hooks.s), with
 * memcard_Save's calls inside GoldBoltUpdate and the gold bolt count's
 * (count_nonzero_entries_up_to_40, func_002160E0, behind the Gold Weapons'
 * vendor). The item a check gives in retail names the check
 * (src/randomizer.c's tables: every item is given once), a Gold Bolt spot
 * its level and flag, an infobot the planet it unlocks.
 *
 * The collected view. The givers decide what to offer from what the
 * player owns: Helga sells the Swingshot while it is not owned, an infobot
 * deletes itself once its planet is unlocked, the Trespasser pickup once
 * the Trespasser is owned, the Nanotech vendor sells the second upgrade
 * once the first's flag is set. With the rewards shuffled, "owned" is the
 * wrong question: a Swingshot received elsewhere would make Helga keep
 * hers, and her check with it, forever; a planet unlocked by another check
 * would delete the infobot that was a check of its own. So while a giver's
 * moby updates (the moby update loop's call, MobyUpdateLoop func_00213C78
 * + 0x58, goes through ee_rz_moby_update; the givers' classes come from the
 * same scan, gen_rz.c), the three arrays show whether the giver's CHECK has
 * been collected instead: the inventory D_0013D5C8 for the items retail
 * gives, D_0013DE48 for the planets infobots unlock, D_0013D490's five
 * bytes for the special items; the real arrays come back when it returns,
 * with any other byte the giver changed kept. A grant inside the update
 * (the hooks below) puts the real arrays back for its duration. Consumers
 * (the Swingshot's targets, the Trespasser's locks, the galactic map, the
 * HelpDesk) update outside those mobys and see the truth. The special
 * items' givers write their flag under the view; a byte that turned 1 is
 * the check collected, and the Nanotech vendor's health change is undone.
 *
 * Receiving. A gadget or weapon: GiveItem, as retail. An infobot:
 * UnlockPlanet (retail's banner, through ShowPlanetBanner, which the hook
 * lets through). A Gold Bolt: the record's count, which the Gold Weapons'
 * vendor sees instead of the spots' flags, and the "Gold Bolt acquired"
 * banner (0x53B8). The Zoomerator, Raritanium and Codebot: their real flag
 * and retail's own banner for each; the Nanotech upgrades: the health
 * maximum (D_0015EEA0: 5 with the Premium, 8 with the Ultra) and the hero's
 * health, and their banners. The Charge Boots: the cheat toggle
 * (EE_CHEAT_CHARGE_BOOTS) with the cheat marked seen, and their name as the
 * banner. The record (src/randomizer_save.c) keeps what has been collected
 * and received, so a check gives its item once; a Gadgetron vendor entry
 * is also marked bought in the vendor's stock list (D_0015EED0, bit 0x40,
 * as GiveItem would) so that it leaves the list.
 *
 * Not yet: the bonus vendor entry and the mystery icons (rz4), so the
 * Charge Boots are kept out of the pool (EE_RANDOMIZER_CHARGE_BOOTS 0);
 * Batalia's one bolt that another class awards; the Orxon gates and the
 * Blarg Station bridge when the O2 Mask comes early (Panda291's PS3 patches
 * force them). */
#include "randomizer.h"

DECLARE_LVL(func_L00_002618D8);         /* GiveItem(id, flag) */
DECLARE_LVL(func_L00_00261848);         /* UnlockPlanet(planet) */
DECLARE_LVL(func_L00_00263DB0);         /* ShowPlanetBanner(planet) */
DECLARE_LVL(func_L00_00264DB8);         /* ShowBanner(text, ticks) */
DECLARE_LVL(func_0020BFC8);             /* memcard_Save(slot, flags) */
DECLARE_LVL(func_002160E0);             /* count_nonzero_entries_up_to_40: the Gold Bolts collected */
DECLARE_LVL(func_L00_0029B770);         /* VendorBuildItemList(gold) */
DECLARE_LVL(func_00205270);             /* LoadMobyClassResources(class, buffer) */
DECLARE_LVL(func_L00_0023A1A0);         /* HudHealthDraw(element) */
DECLARE_LVL(func_00200198);             /* GetIconFrame(icon, frame) */
DECLARE_LVL(func_00200468);             /* HudSprite(frame, x, y, w, h, alpha) */
DECLARE_LVL(func_00200650);             /* DrawHudSpriteTurned(frame, x, y, w, h, alpha) */
DECLARE_LVL(func_L00_0023BAB8);         /* HudFrame(element, frame, x, y, scale, alpha) */
DECLARE_LVL_DATA(vendor_state);         /* the vendor's record: the item just bought at +0x48, the list at +0xD0 */
DECLARE_LVL_DATA(vendor_items);         /* the level's item info table: the ammo price at +8 of each 0x18 */
DECLARE_LVL_DATA(vendor_sounds);        /* the level's item sound table: a word per item, stride 0x14 */
DECLARE_LVL_DATA(game_mode);            /* D_0015F6E8: 0 play, 2 cinematic, 3 pause */
DECLARE_LVL(func_0020D440);             /* InitMobyInstance(moby, class) */
DECLARE_LVL(func_L00_00251E30);         /* the item display's placement of a model */
DECLARE_LVL(func_001F9EC0);             /* matrix_mul_vec3(out, v, matrix): a vector turned by a matrix */
DECLARE_LVL(func_001F9BD8);             /* FastVecAdd(out, a, b) */
DECLARE_LVL_DATA(vendor_pose);          /* Vendor_InitItemDisplay's per-item offsets of the panel's model (D_001C8C20) */
DECLARE_LVL(func_001FE540);             /* msg_string(id) */
typedef void (*GiveFn)(s32, s32);
typedef void (*PlanetFn)(s32);
typedef void (*BannerFn)(s32, s32);
typedef s32 (*SaveFn)(s32, s32);
typedef s32 (*CountFn)(void);
typedef void (*UpdateFn)(void *);
typedef void (*ListFn)(s32);
typedef void (*InitMobyFn)(void *, s32);
typedef void (*PlaceFn)(void *);
typedef void (*RotateFn)(void *, const void *, const void *);
typedef void (*VecAddFn)(void *, const void *, const void *);
typedef const char *(*MsgFn)(s32);
typedef void (*ClassFn)(s32, s32);
typedef s32 (*HealthFn)(void *);
typedef s32 (*IconFn)(s32, s32);
typedef void (*SpriteFn)(s32, s32, s32, s32, s32, s32);
typedef void (*FrameFn)(void *, s32, s32, s32, s32, s32);
struct VendorEntry {                    /* the vendor's list: a purchase (selected 0) or a weapon's ammo (1) */
    s32 item, selected, state, unused, sound;
};

#define INVENTORY       ((volatile u8 *)0x0013D5C8)      /* owned, per item 0-36 */
#define HAD             ((volatile u8 *)0x0013D5F0)      /* has had, per item: GiveItem sets it; pickups and some characters test it */
#define PLANETS         ((volatile u8 *)0x0013DE48)      /* unlocked, per planet 0-19 */
#define FLAGS           ((volatile u8 *)0x0013D490)      /* the global flags' first bytes */
#define GOLD_FLAGS      ((volatile u8 *)0x0014BFC0)      /* [level * 4 + k] */
#define VENDOR_BOUGHT   0x48            /* the vendor's record: the item just bought, -1 none */
#define VENDOR_INDEX    0x58            /* the entry the cursor is on */
#define VENDOR_LIST     0xD0            /* the entries (struct VendorEntry), up to 16, and their count */
#define VENDOR_COUNT    0x210
#define VENDOR_LIST_MAX 16
#define ITEM_INFO_SIZE  0x18            /* the item info table's records: the ammo price at +8, 0 for an item without ammo */
#define ITEM_AMMO_PRICE 8
#define ITEM_SOUND_STRIDE 0x14
#define CURRENT_LEVEL   (*(volatile s32 *)0x0015EE84)
#define MAX_HEALTH      (*(volatile s32 *)0x0015EEA0)    /* gMaxNanotech: 4, 5 with the Premium, 8 with the Ultra */
#define HERO_HEALTH     (*(volatile s32 *)(0x0013F450 + 0x22A8))
#define CHEAT_SEEN      (*(volatile u8 *)(0x0015EEC0 + EE_CHEAT_ENTRY))
#define MOBY_CLASS(m)   (*(volatile u16 *)((u8 *)(m) + 0xA6))  /* oclass (include/moby.h) */
#define MOBY_PVARS(m)   (*(volatile u32 *)((u8 *)(m) + 0x78))  /* its pvars */
#define MOBY_TABLE      (*(volatile u32 *)0x00160058)    /* the level's moby table (D_L03_00160058 in Kerwan), 0x100 a moby */
#define MOBY_SIZE       0x100
#define KERWAN          3
#define HELGA_CLASS     890             /* QuestGiverUpdate, func_L03_002DBC38: Helga, who sells the Swingshot */
#define HELGA_LINK      0xD4            /* her pvars: the moby index of the rail lift she is linked to */
#define LIFT_CLASS      0x3F4           /* RailLiftUpdate, func_L03_002DE088: the lift back to the ship */
#define LIFT_AUTOSTART  0x8C            /* its pvars: start without waiting for the trigger volume */
#define SWINGSHOT_ID    12
#define CHARGE_BOOTS_ID 36              /* the edition's item (src/goodies.c, tools/gen_items.py) */
#define CHARGE_BOOTS_CLASS 2000         /* its model, R&C2's boots (tools/gen_boots.py), in every level's core */
#define CHICKEN_CLASS   270             /* the Morph-o-Ray's chicken (MorphorayChickenUpdate), in every level */
#define CHICKEN_SCALE   0.6f            /* on the item panel's stand: the chicken is tall for the box */
#define CHICKEN_LOWER   0.15f           /* and stands on its feet, so it comes down a little */
#define STAND_AWAY      40.0f           /* the stand (a second chicken) goes below the display's view; a scale of 0 is not safe
                                         * for the drawing (the Novalis vendor froze on opening with it) */
#define VENDOR_OBJ      0x20            /* the vendor's record: the display mobys, at +0x100 the panel's item, +0x200 its stand, +0x300 the hologram */
#define VENDOR_CAM      0x1C            /* and its camera record: the yaw at +0x48 */
#define MOBY_POS_Z      0x18
#define MOBY_SCALE      0x2C
#define MOBY_ROT        0x40            /* x, y, z at +0x40, +0x44, +0x48 */
#define MOBY_POS        0x10
#define POSE_SIZE       0x30            /* vendor_pose: an item's panel offset x, y, z and its price-line lift */
#define CAM_MATRIX      0xC0            /* the vendor camera's matrix, which turns the offsets into the world */
#define PERSUADER_OWNED (*(volatile u8 *)0x0013D5EB)
#define ITEM_ICON_BASE  0xEA60          /* the items' HUD icons, by item id */
#define TEXT_AMMO_PROMPT 0x4F5D         /* the item panel's line under an ammo entry's name */
#define MODE_PAUSE      3
#define REDACTED        "???"
#define REDACT_BUFFERS  4               /* strings handed out at once; the callers draw them at once */
#define REDACT_MAX      256
#define PAL_MODE        (*(volatile s32 *)0x0015EE80)
#define HERO_MODE       (*(volatile u8 *)(0x0013F450 + 0x20A4))  /* 1 Clank, 2 a vehicle */
#define HERO_STATE      (*(volatile s32 *)(0x0013F450 + 0x2084))
#define CLANK_HEALTH_MAX (*(volatile s32 *)(0x0013F450 + 0x22AC))
#define BONUS_PRICE     2500            /* the bonus vendor entry's price, and with the Persuader */
#define BONUS_PRICE_PERSUADER 1250
#define ITEM_PRICE      0               /* the item info table: the price, and the Persuader's at +4 */
#define ITEM_PRICE_PERSUADER 4
#define MOBY_UID(m)     (*(volatile u16 *)((u8 *)(m) + 0xB2))   /* its save id, the level instance's uid */
#define MOBY_NEXT(m)    (*(volatile u32 *)((u8 *)(m) + 0x28))   /* the update list's next moby */
#define MOBY_STATE(m)   (*(volatile s8 *)((u8 *)(m) + 0x20))    /* negative: dead */
#define LEVEL_TRIGGERS  ((volatile u8 *)0x0014C150)      /* per level 16 bytes: a level trigger done (0xFF) */
#define LEVEL_BITS      ((volatile u8 *)0x0014C290)      /* per level 256 bytes: the save bits by moby uid */
#define ORXON           10
#define BLARG           6
#define ITEMS           37
#define PLANET_COUNT    20
#define FLAG_BYTES      8
#define BANNER_TICKS    300             /* GiveItem's own banner shows scale_ticks(0x12C) */
#define TEXT_CHARGE_BOOTS 0x5406        /* "Charge Boots" (text/strings.tsv) */
#define LANGUAGES       8

/* The banners' words, from the game's text table (tools/gen_randomizer.py). */
extern const char *const ee_rz_item_names[ITEMS][LANGUAGES];        /* each item's name */
extern const char *const ee_rz_infobot_banners[18][LANGUAGES];      /* "Infobot for Planet Aridia acquired" per planet */
extern const char *const ee_rz_special_names[5][LANGUAGES];         /* Zoomerator, Raritanium, CodeBot, Premium, Ultra */
extern const char *const ee_rz_word_gold_bolt[LANGUAGES];           /* "Gold Bolt" */
#define TEMPLATE_MARK   '@'             /* where the item goes in EE_TEXT_RZ_FOUND, _GOT and _WON */
#define BANNER_MAX      160
#define NAME_MAX        80
const char *ee_msg_string_edition(s32 id);          /* src/text.c */
extern const int ee_text_first, ee_text_count;      /* build/modseg/gen_text.c */

/* The special items' flags (D_0013D490 bytes) and received bits. */
#define FLAG_ZOOMERATOR 0
#define FLAG_RARITANIUM 1
#define FLAG_CODEBOT    2
#define FLAG_PREMIUM    4
#define FLAG_ULTRA      5

/* tools/gen_randomizer.py: per level, the classes whose update gets the collected view, 0xFFFF last. */
extern const u16 *const ee_rz_classes[19];

static s32 active;                  /* the game in memory is a Randomizer save (refreshed by ee_rz_frame) */
volatile u32 ee_debug_rz_play[4];   /* PINE: active, views applied, grants, the last check collected + 1 */
static s32 active_level = -1;
static s32 suppress_banner;         /* a planet whose banner the next ShowPlanetBanner skips */
static s32 view_on;                 /* the collected view is in place */
static s32 pending_item;            /* the item a GiveItem of the edition's is giving: its banner is ours */
static s32 pending_check;           /* and the check it is given for */
static s32 banner_due;              /* a check was collected this frame: a giver's own banner after it is ours */
static s32 last_check, last_item;   /* that check and its item */
static s32 fixes_level = -1;        /* the level whose early-O2 fixes (o2_early_fixes) have run */
static s32 banner_shown;            /* GiveItem's banner call came while pending_item was set */
static s32 vendor_vanilla = -1;     /* a vendor's purchase was a check: the vendor's item, and */
static s32 vendor_received;         /* the game id of the reward, -1 for one that is no item */
static s32 granted = -1;            /* the check collected during the moby update under way, -1 none */
static s32 vendor_open;             /* the vendor's list was built and no gameplay frame has run since: its menu is up */
volatile u32 ee_redact_saved[3];    /* src/text_hook.s: the caller's return address, the id and the program */
static char redacted[REDACT_BUFFERS][REDACT_MAX];
static s32 redact_next;
extern const char *const ee_rz_word_infobot[LANGUAGES];
static char banner_text[BANNER_MAX];    /* the banner's text (EE_TEXT_RZ_BANNER, served by src/text.c) */
static u8 real_inventory[ITEMS];
static u8 real_had[ITEMS];
static u8 real_planets[PLANET_COUNT];
static u8 real_flags[FLAG_BYTES];

/* --- what is a check ---------------------------------------------------------- */

static s32 special_flag_of(s32 item)
{
    switch (item) {
    case RZ_ZOOMERATOR: return FLAG_ZOOMERATOR;
    case RZ_RARITANIUM: return FLAG_RARITANIUM;
    case RZ_CODEBOT: return FLAG_CODEBOT;
    case RZ_PREMIUM_NANOTECH: return FLAG_PREMIUM;
    case RZ_ULTRA_NANOTECH: return FLAG_ULTRA;
    default: return -1;
    }
}

static s32 item_of_flag(s32 flag)
{
    switch (flag) {
    case FLAG_ZOOMERATOR: return RZ_ZOOMERATOR;
    case FLAG_RARITANIUM: return RZ_RARITANIUM;
    case FLAG_CODEBOT: return RZ_CODEBOT;
    case FLAG_PREMIUM: return RZ_PREMIUM_NANOTECH;
    case FLAG_ULTRA: return RZ_ULTRA_NANOTECH;
    default: return RZ_ITEM_NONE;
    }
}

/* The health maximum the received upgrades give: 4, one more with the
 * Premium Nanotech, three more with the Ultra; 8 with both, as retail,
 * which only sells the Ultra after the Premium. */
static s32 health_maximum(void)
{
    return 4 + (ee_rz_received_special(FLAG_PREMIUM) ? 1 : 0) + (ee_rz_received_special(FLAG_ULTRA) ? 3 : 0);
}

/* --- the banners --------------------------------------------------------------
 * The game's banner texts belong to the level they are shown on (an item's
 * "You got the ..." is in its level's text block only, a planet's "Infobot
 * for Planet ... acquired" likewise), and a text not in the level's block
 * shows as "Paradox: This message does not exist". So the Randomizer's
 * banners are its own string, EE_TEXT_RZ_BANNER, built here from retail's
 * own banner texts in the game's language (tools/gen_randomizer.py takes
 * them from the disc's text table, the orange item names included): the
 * item's "You got the ...!", the planet's "Infobot for Planet ... acquired",
 * "Gold Bolt acquired n/40". */
static const char *word(const char *const words[LANGUAGES])
{
    s32 lang = g_language;
    const char *s = lang >= 0 && lang < LANGUAGES ? words[lang] : 0;
    return s ? s : words[0] ? words[0] : "";
}

static s32 put(char *out, s32 cap, s32 at, const char *s)
{
    while (*s && at < cap - 1) {
        out[at++] = *s++;
    }
    out[at] = 0;
    return at;
}

static s32 put_number(char *out, s32 cap, s32 at, s32 n)
{
    char digits[12];
    s32 k = 0;
    do {
        digits[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n > 0);
    while (k > 0 && at < cap - 1) {
        out[at++] = digits[--k];
    }
    out[at] = 0;
    return at;
}

const char *ee_rz_banner_text(void)
{
    return banner_text;
}

/* The banner's text into banner_text: TEMPLATE with NAME where its mark is. */
static void fill_template(const char *template, const char *name)
{
    s32 at = 0;
    while (*template && at < BANNER_MAX - 1) {
        if (*template == TEMPLATE_MARK) {
            at = put(banner_text, BANNER_MAX, at, name);
        } else {
            banner_text[at++] = *template;
        }
        template++;
    }
    banner_text[at] = 0;
}

/* How CHECK's reward is come by (RzHow). */
static s32 how_of(s32 check)
{
    const struct RzCheck *c = rz_check(check);
    return c != 0 ? c->how : RZ_HOW_GOT;    /* the bonus vendor entry has no table row */
}

/* The banner for ITEM received at CHECK, then the banner: the check's words
 * ("You found the ...!" picked up in the world, "You got the ...!" from a
 * character or a vendor, "You won a ...!" in a race) around the item's name
 * in the game's orange; a Gold Bolt names its count, "Gold Bolt (12/40)"; an
 * infobot shows retail's own banner for its planet instead. */
static void item_banner(s32 check, s32 item, s32 L)
{
    char name[NAME_MAX];
    s32 how = how_of(check);
    s32 text = how == RZ_HOW_WON ? EE_TEXT_RZ_WON : how == RZ_HOW_GOT ? EE_TEXT_RZ_GOT : EE_TEXT_RZ_FOUND;
    s32 special = -1, at = 0;
    switch (item) {
    case RZ_ZOOMERATOR:       special = 0; break;
    case RZ_RARITANIUM:       special = 1; break;
    case RZ_CODEBOT:          special = 2; break;
    case RZ_PREMIUM_NANOTECH: special = 3; break;
    case RZ_ULTRA_NANOTECH:   special = 4; break;
    default: break;
    }
    name[0] = 0;
    if (item >= RZ_INFOBOT_NOVALIS && item <= RZ_INFOBOT_VELDIN) {
        put(banner_text, BANNER_MAX, 0, word(ee_rz_infobot_banners[item - RZ_INFOBOT_NOVALIS]));
        LVL(L, func_L00_00264DB8, BannerFn)(EE_TEXT_RZ_BANNER, BANNER_TICKS);
        return;
    }
    if (item == RZ_GOLD_BOLT) {
        at = put(name, NAME_MAX, at, word(ee_rz_word_gold_bolt));
        at = put(name, NAME_MAX, at, " (");
        at = put_number(name, NAME_MAX, at, ee_rz_gold_received());
        at = put(name, NAME_MAX, at, "/");
        at = put_number(name, NAME_MAX, at, RZ_GOLD_BOLTS);
        put(name, NAME_MAX, at, ")");
    } else if (item == RZ_CHARGE_BOOTS) {
        put(name, NAME_MAX, 0, ee_msg_string_edition(TEXT_CHARGE_BOOTS));
    } else if (special >= 0) {
        put(name, NAME_MAX, 0, word(ee_rz_special_names[special]));
    } else if (rz_item_game_id(item) >= 0) {
        put(name, NAME_MAX, 0, word(ee_rz_item_names[rz_item_game_id(item)]));
    }
    fill_template(ee_msg_string_edition(text), name);
    LVL(L, func_L00_00264DB8, BannerFn)(EE_TEXT_RZ_BANNER, BANNER_TICKS);
}

/* --- the collected view ------------------------------------------------------- */

static void view_apply(void)
{
    s32 i, check;
    if (view_on) {
        return;
    }
    for (i = 0; i < ITEMS; i++) {
        real_inventory[i] = INVENTORY[i];
        real_had[i] = HAD[i];
        check = rz_check_for_vanilla(rz_item_of_game_id(i));
        if (check >= 0) {
            INVENTORY[i] = ee_rz_check_collected(check) ? 1 : 0;
            HAD[i] = INVENTORY[i];
        }
    }
    for (i = 0; i < PLANET_COUNT; i++) {
        real_planets[i] = PLANETS[i];
        check = i >= 1 && i <= 18 ? rz_check_for_vanilla(RZ_INFOBOT_NOVALIS + i - 1) : -1;
        if (check >= 0) {
            PLANETS[i] = ee_rz_check_collected(check) ? 1 : 0;
        }
    }
    for (i = 0; i < FLAG_BYTES; i++) {
        real_flags[i] = FLAGS[i];
        check = rz_check_for_vanilla(item_of_flag(i));
        if (check >= 0) {
            FLAGS[i] = ee_rz_check_collected(check) ? 1 : 0;
        }
    }
    view_on = 1;
}

/* The real arrays back: a byte the view did not replace keeps what the
 * giver did to it; a special item's byte that turned 1 is its check, which
 * gets its item; the Nanotech vendor's change of the health maximum goes. */
static void receive(s32 check, s32 item, s32 L);

static void view_restore(s32 L)
{
    s32 i, check;
    if (!view_on) {
        return;
    }
    view_on = 0;
    for (i = 0; i < ITEMS; i++) {
        if (rz_check_for_vanilla(rz_item_of_game_id(i)) < 0) {
            real_inventory[i] = INVENTORY[i];
            real_had[i] = HAD[i];
        }
        INVENTORY[i] = real_inventory[i];
        HAD[i] = real_had[i];
    }
    for (i = 0; i < PLANET_COUNT; i++) {
        check = i >= 1 && i <= 18 ? rz_check_for_vanilla(RZ_INFOBOT_NOVALIS + i - 1) : -1;
        if (check < 0) {
            real_planets[i] = PLANETS[i];
        }
        PLANETS[i] = real_planets[i];
    }
    for (i = 0; i < FLAG_BYTES; i++) {
        check = rz_check_for_vanilla(item_of_flag(i));
        if (check < 0) {
            real_flags[i] = FLAGS[i];
        } else if (FLAGS[i] && !ee_rz_check_collected(check)) {
            ee_rz_set_collected(check);
            FLAGS[i] = real_flags[i];
            if (i == FLAG_PREMIUM || i == FLAG_ULTRA) {
                MAX_HEALTH = health_maximum();
                HERO_HEALTH = MAX_HEALTH;
            }
            receive(check, ee_rz_layout_item(check), L);
            real_flags[i] = FLAGS[i];
            continue;
        }
        FLAGS[i] = real_flags[i];
    }
}

/* --- receiving ------------------------------------------------------------------
 * A gadget or weapon goes through GiveItem, whose own banner (its call of
 * ShowBanner, hooked below) shows ours while pending_item is set; an infobot
 * through UnlockPlanet, whose planet banner (ShowPlanetBanner, hooked) shows
 * ours too. The special items set their real flag (the Nanotech's as well:
 * the Items page lists the upgrades by them; the Nanotech vendor sees the
 * collected view instead) and the health maximum follows the upgrades. */
static void receive(s32 check, s32 item, s32 L)
{
    s32 was_on = view_on;
    s32 flag;
    if (was_on) {
        view_restore(L);
    }
    pending_check = check;
    if (item >= RZ_INFOBOT_NOVALIS && item <= RZ_INFOBOT_VELDIN) {
        pending_item = item;
        LVL(L, func_L00_00261848, PlanetFn)(item - RZ_INFOBOT_NOVALIS + 1);
        pending_item = RZ_ITEM_NONE;
    } else if (item == RZ_GOLD_BOLT) {
        ee_rz_add_gold();
        item_banner(check, item, L);
    } else if (item == RZ_CHARGE_BOOTS) {
        *(volatile u8 *)EE_CHEAT_CHARGE_BOOTS = 1;
        CHEAT_SEEN = 1;
        item_banner(check, item, L);
    } else if ((flag = special_flag_of(item)) >= 0) {
        ee_rz_set_received_special(flag);
        FLAGS[flag] = 1;
        if (flag == FLAG_PREMIUM || flag == FLAG_ULTRA) {
            MAX_HEALTH = health_maximum();
            HERO_HEALTH = MAX_HEALTH;
        }
        item_banner(check, item, L);
    } else if (rz_item_game_id(item) >= 0) {
        pending_item = item;
        banner_shown = 0;
        LVL(L, func_L00_002618D8, GiveFn)(rz_item_game_id(item), 1);
        pending_item = RZ_ITEM_NONE;
        if (!banner_shown) {
            item_banner(check, item, L);    /* GiveItem shows none for an item its level's table has no banner for */
        }
    }
    if (was_on) {
        view_apply();
    }
}

/* Check CHECK collected now: its item, once. */
static void collect(s32 check, s32 L)
{
    if (ee_rz_check_collected(check)) {
        return;
    }
    ee_rz_set_collected(check);
    ee_debug_rz_play[2]++;
    ee_debug_rz_play[3] = (u32)check + 1;
    granted = check;
    banner_due = 1;
    last_check = check;
    last_item = ee_rz_layout_item(check);
    receive(check, last_item, L);
}

/* --- the hooks ----------------------------------------------------------------- */

/* GiveItem(id, flag) in level L (src/rz_hooks.s). */
void ee_rz_give_item_c(s32 id, s32 flag, s32 L)
{
    s32 check = active ? rz_check_for_vanilla(rz_item_of_game_id(id)) : -1;
    if (check < 0) {
        LVL(L, func_L00_002618D8, GiveFn)(id, flag);
        return;
    }
    if (check == RZ_CHECKS_BASE) {
        vendor_vanilla = id;                /* the bonus entry: never a weapon demo */
        vendor_received = -1;
    } else if (rz_check(check) != 0 && rz_check(check)->kind == RZ_KIND_VENDOR) {
        vendor_vanilla = id;
        vendor_received = rz_item_game_id(ee_rz_layout_item(check));
    }
    collect(check, L);
}

/* Whether item K is really owned (the collected view may be in place). */
static s32 owned(s32 k)
{
    return view_on ? real_inventory[k] : INVENTORY[k];
}

/* VendorBuildItemList(gold) in level L (src/rz_hooks.s): retail's list, then
 * the Randomizer's rules on it. Retail lists the vendor's stock (the level's
 * weapon unbought, every weapon bought before: `D_0015EED0`, bit 0x40 bought)
 * as purchases and ammo, and ammo for every other weapon owned; a purchase
 * is the weapon itself, so owning it and buying it are one thing. Here they
 * are two: the purchase is the check, gone once collected (whether or not the
 * weapon came from elsewhere, and whatever the stock list says after a
 * reload), and ammo is for a weapon in hand only (the vendor's own weapon is
 * not, once its check gave something else), added for an owned weapon whose
 * purchase retail listed instead. The bonus vendor check is the Charge
 * Boots (the edition's item 36, 2,500 bolts written into the level's item
 * info record: the price, half of it with the Persuader, no ammo), listed
 * last by every vendor once the player has landed on the seed's planet (the
 * record remembers; ee_rz_level_init_c), as retail puts a planet's weapon
 * on sale everywhere from the visit on, while that check is not collected.
 * The cursor stays on an entry.
 *
 * The call right after a purchase (VendorBuyDialogUpdate) comes once the
 * bought item is noted in the vendor's record for the weapon demo played on
 * leaving, which equips the noted item (it sets the hero's pending item and
 * loads the class): with a shuffled reward that is the vendor's own weapon,
 * unowned and without ammo. The note is cleared unless the reward is that
 * weapon itself: no demo, as for an item that has none, and the hero keeps
 * what the check gave. */
void ee_rz_vendor_list_c(s32 gold, s32 L)
{
    u8 *vendor;
    struct VendorEntry *e;
    s32 *count, *index;
    s32 n, i, out, k;
    LVL(L, func_L00_0029B770, ListFn)(gold);
    if (!active) {
        return;
    }
    vendor_open = 1;
    vendor = LVL_DATA(L, vendor_state, u8 *);
    if (vendor_vanilla >= 0) {
        s32 *bought = (s32 *)(vendor + VENDOR_BOUGHT);
        if (*bought == vendor_vanilla && vendor_received != vendor_vanilla) {
            *bought = -1;
        }
        vendor_vanilla = -1;
    }
    e = (struct VendorEntry *)(vendor + VENDOR_LIST);
    count = (s32 *)(vendor + VENDOR_COUNT);
    index = (s32 *)(vendor + VENDOR_INDEX);
    n = *count;
    for (i = 0, out = 0; i < n; i++) {
        s32 keep;
        if (e[i].selected) {
            keep = owned(e[i].item);
        } else {
            s32 check = rz_check_for_vanilla(rz_item_of_game_id(e[i].item));
            keep = check < 0 || !ee_rz_check_collected(check);
        }
        if (keep) {
            e[out++] = e[i];
        }
    }
    for (k = 0; k < ITEMS && out < VENDOR_LIST_MAX; k++) {
        const u8 *info = LVL_DATA(L, vendor_items, const u8 *) + ITEM_INFO_SIZE * k;
        if (!owned(k) || *(const u16 *)(info + ITEM_AMMO_PRICE) == 0) {
            continue;
        }
        for (i = 0; i < out && !(e[i].item == k && e[i].selected); i++) {
        }
        if (i == out) {
            e[out].item = k;
            e[out].selected = 1;
            e[out].state = 0;
            e[out].unused = 0;
            e[out].sound = *(const s32 *)(LVL_DATA(L, vendor_sounds, const u8 *) + ITEM_SOUND_STRIDE * k);
            out++;
        }
    }
    if (ee_rz_bonus_seen() && !ee_rz_check_collected(RZ_CHECKS_BASE) && out < VENDOR_LIST_MAX) {
        u8 *info = LVL_DATA(L, vendor_items, u8 *) + ITEM_INFO_SIZE * CHARGE_BOOTS_ID;
        *(volatile s32 *)(info + ITEM_PRICE) = BONUS_PRICE;
        *(volatile s32 *)(info + ITEM_PRICE_PERSUADER) = BONUS_PRICE_PERSUADER;
        *(volatile u16 *)(info + ITEM_AMMO_PRICE) = 0;
        e[out].item = CHARGE_BOOTS_ID;
        e[out].selected = 0;
        e[out].state = 0;
        e[out].unused = 0;
        e[out].sound = *(const s32 *)(LVL_DATA(L, vendor_sounds, const u8 *) + ITEM_SOUND_STRIDE * CHARGE_BOOTS_ID);
        out++;
    }
    *count = out;
    if (*index >= out) {
        *index = out > 0 ? out - 1 : 0;
    }
}

/* --- the mystery entries ----------------------------------------------------------
 * A vendor's purchase entry is a check, not the item it is named after, so
 * it shows as a mystery: the icon strip gets a blue
 * tile with a question mark for every purchase entry (VendorDrawIconStrip
 * asks GetIconFrame for frame 0 of a purchase's icon, frame 2 of an ammo
 * entry's), the item panel names it "???" (its msg_string calls, but for the
 * ammo entry's own line), and the item display shows the Morph-o-Ray's
 * chicken instead of the item's model (Vendor_InitItemDisplay's
 * InitMobyInstance calls, the item and its stand both). The price stays the
 * entry's own. Ammo entries, which are the weapons owned, stay as they are. */
static s32 purchase_selected(s32 L)
{
    u8 *vendor = LVL_DATA(L, vendor_state, u8 *);
    s32 index = *(s32 *)(vendor + VENDOR_INDEX);
    const struct VendorEntry *e = (const struct VendorEntry *)(vendor + VENDOR_LIST);
    return index >= 0 && index < *(s32 *)(vendor + VENDOR_COUNT) && e[index].selected == 0;
}

s32 ee_rz_vendor_icon_c(s32 icon, s32 frame, s32 L)
{
    if (active && frame == 0 && icon >= ITEM_ICON_BASE && icon < ITEM_ICON_BASE + ITEMS) {
        return LVL(L, func_00200198, IconFn)(EE_ICON_MYSTERY, 0);
    }
    return LVL(L, func_00200198, IconFn)(icon, frame);
}

const char *ee_rz_vendor_text_c(s32 id, s32 L)
{
    if (active && id != TEXT_AMMO_PROMPT && purchase_selected(L)) {
        return ee_msg_string_edition(EE_TEXT_MYSTERY);
    }
    return LVL(L, func_001FE540, MsgFn)(id);
}

void ee_rz_vendor_model_c(void *moby, s32 cls, s32 L)
{
    if (active && purchase_selected(L)) {
        cls = CHICKEN_CLASS;
    }
    LVL(L, func_0020D440, InitMobyFn)(moby, cls);
}

/* The display's placement of a model, once its pose is set from the item's
 * own tables (which tilt a weapon for the box): the panel's chicken stands
 * upright and faces the camera, smaller and a little lower, and its stand
 * (the second chicken) is moved below the view; the hologram keeps the item's pose. */
void ee_rz_vendor_place_c(void *moby, s32 L)
{
    if (active && purchase_selected(L)) {
        u8 *vendor = LVL_DATA(L, vendor_state, u8 *);
        u8 *obj = *(u8 **)(vendor + VENDOR_OBJ);
        u8 *cam = *(u8 **)(vendor + VENDOR_CAM);
        u8 *m = (u8 *)moby;
        if (m == obj + 0x100) {
            /* retail puts the model at the panel's base point plus the item's own offset, which
             * centres that item's model (a weapon's origin is at its grip), lifted for the price
             * line once, twice with the Persuader's prices shown: the chicken, whose origin is at
             * its feet, goes to the base point instead */
            s32 item = ((const struct VendorEntry *)(vendor + VENDOR_LIST))[*(s32 *)(vendor + VENDOR_INDEX)].item;
            const f32 *off = (const f32 *)(LVL_DATA(L, vendor_pose, const u8 *) + POSE_SIZE * item);
            f32 back[4] __attribute__((aligned(16)));
            f32 world[4] __attribute__((aligned(16)));
            back[0] = -off[0];
            back[1] = -off[1];
            back[2] = -(off[2] + off[3] * (PERSUADER_OWNED ? 2.0f : 1.0f));
            back[3] = 0.0f;
            LVL(L, func_001F9EC0, RotateFn)(world, back, cam + CAM_MATRIX);
            *(volatile f32 *)(m + MOBY_POS) += world[0];
            *(volatile f32 *)(m + MOBY_POS + 4) += world[1];
            *(volatile f32 *)(m + MOBY_POS + 8) += world[2];
            *(volatile f32 *)(m + MOBY_ROT) = 0.0f;
            *(volatile f32 *)(m + MOBY_ROT + 4) = 0.0f;
            *(volatile f32 *)(m + MOBY_ROT + 8) = *(volatile f32 *)(cam + 0x48);
            *(volatile f32 *)(m + MOBY_SCALE) *= CHICKEN_SCALE;
            *(volatile f32 *)(m + MOBY_POS_Z) -= CHICKEN_LOWER;
        } else if (m == obj + 0x200) {
            *(volatile f32 *)(m + MOBY_POS_Z) -= STAND_AWAY;
        }
    }
    LVL(L, func_L00_00251E30, PlaceFn)(moby);
}

/* The hologram's placement each frame in VendorMenuUpdate, right after its
 * pose is copied from the item's table: the tilt that table gives a weapon
 * (x and y) laid the chicken on its back; it stands upright and keeps the
 * spin (z). The pose is rewritten every frame, so nothing accumulates. */
void ee_rz_vendor_holo_c(void *moby, s32 L)
{
    if (active && purchase_selected(L)) {
        *(volatile f32 *)((u8 *)moby + MOBY_ROT) = 0.0f;
        *(volatile f32 *)((u8 *)moby + MOBY_ROT + 4) = 0.0f;
    }
    LVL(L, func_L00_00251E30, PlaceFn)(moby);
}

/* The hologram's two per-item offsets in VendorMenuUpdate, both tuned to the
 * original weapon (FastVecAdd of a table entry): its place in the beam, and
 * the pivot its spin swings around, which had the chicken circling off the
 * beam's axis. The chicken goes without both. */
void ee_rz_vendor_holo_add_c(f32 *out, const f32 *a, const f32 *b, s32 L)
{
    if (active && purchase_selected(L)) {
        out[0] = a[0];
        out[1] = a[1];
        out[2] = a[2];
        out[3] = a[3];
        return;
    }
    LVL(L, func_001F9BD8, VecAddFn)(out, a, b);
}

/* msg_string in VendorMenuUpdate: the selected item's slogan for the
 * scrolling line ("BLASTER! POINT IT MORE OR LESS AT THE BAD GUYS..."),
 * which names the item retail sells there; a purchase entry has none (the
 * scroller moves on to the vendor's own adverts). */
const char *ee_rz_vendor_slogan_c(s32 id, s32 L)
{
    if (active && purchase_selected(L)) {
        return "";
    }
    return LVL(L, func_001FE540, MsgFn)(id);
}

/* --- the givers' prompts ----------------------------------------------------------
 * A character offering its check names the item retail has there ("Buy the
 * Swingshot for 1,000 bolts"); the Randomizer redacts that name: every
 * string msg_string hands out on the check's planet, while the check is not
 * collected, has the item's name replaced by "???" (an infobot check's
 * "Infobot"), and while the vendor's menu is up the names of every
 * uncollected vendor check's item too, since a vendor sells the stock of
 * every planet visited. The pause menu is exempt (it names the items owned),
 * as are the edition's own strings (its banners name the reward). */
static s32 name_length(const char *s)
{
    s32 n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

/* S with every occurrence of NAME replaced by REDACTED, into the next buffer; 0 if NAME is not in S. */
static const char *redact_name(const char *s, const char *name)
{
    char *out;
    s32 n = name_length(name), at = 0, i, j, found = 0;
    if (n == 0) {
        return 0;
    }
    for (i = 0; s[i]; i++) {
        for (j = 0; j < n && s[i + j] == name[j]; j++) {
        }
        if (j == n) {
            found = 1;
            break;
        }
    }
    if (!found) {
        return 0;
    }
    out = redacted[redact_next];
    redact_next = (redact_next + 1) % REDACT_BUFFERS;
    for (i = 0; s[i] && at < REDACT_MAX - 1;) {
        for (j = 0; j < n && s[i + j] == name[j]; j++) {
        }
        if (j == n) {
            at = put(out, REDACT_MAX, at, REDACTED);
            i += n;
        } else {
            out[at++] = s[i++];
        }
    }
    out[at] = 0;
    return out;
}

/* The name a check's giver uses for its item, in the game's language; 0 for a Gold Bolt spot. */
static const char *check_name(s32 check)
{
    const struct RzCheck *c = rz_check(check);
    s32 item, special = -1;
    if (c == 0) {
        return ee_msg_string_edition(TEXT_CHARGE_BOOTS);        /* the bonus vendor entry */
    }
    item = c->vanilla;
    if (c->kind == RZ_KIND_INFOBOT) {
        return word(ee_rz_word_infobot);
    }
    switch (item) {
    case RZ_ZOOMERATOR:       special = 0; break;
    case RZ_RARITANIUM:       special = 1; break;
    case RZ_CODEBOT:          special = 2; break;
    case RZ_PREMIUM_NANOTECH: special = 3; break;
    case RZ_ULTRA_NANOTECH:   special = 4; break;
    default: break;
    }
    if (special >= 0) {
        return word(ee_rz_special_names[special]);
    }
    return rz_item_game_id(item) >= 0 ? word(ee_rz_item_names[rz_item_game_id(item)]) : 0;
}

const char *ee_rz_redact_c(const char *s, s32 id, s32 L)
{
    s32 check, checks;
    if (s == 0 || !active || L < 0 || L >= 19 || (id >= ee_text_first && id < ee_text_first + ee_text_count)
        || *(volatile s32 *)LVL_DATA(L, game_mode, u8 *) == MODE_PAUSE) {
        return s;
    }
    checks = ee_rz_bonus_planet() != 0 ? RZ_CHECKS_MAX : RZ_CHECKS_BASE;
    for (check = 0; check < checks; check++) {
        const struct RzCheck *c = rz_check(check);
        s32 vendor = c == 0 || c->kind == RZ_KIND_VENDOR;
        const char *name, *out;
        if (ee_rz_check_collected(check) || (vendor ? !vendor_open : c->planet != L)) {
            continue;
        }
        name = check_name(check);
        if (name != 0 && (out = redact_name(s, name)) != 0) {
            s = out;
        }
    }
    return s;
}

/* --- the O2 Mask early ------------------------------------------------------------
 * Retail plays Orxon as Clank until the O2 Mask (the mask comes from
 * Pokitaru, whose infobot is in Orxon's Clank section), and the gates of
 * that section open for Clank and his Gadgebots only; Blarg Station's
 * drawbridge is lowered from the Clank side too. With the mask shuffled
 * early the player arrives as Ratchet and both would stay shut, so Panda291's
 * randomizer forces them on PS3 when the mask is owned: Orxon's level
 * triggers 1 and 4 done and its save bits of the gates and the infobot set
 * (bytes 30, 35, 102 and 103 of its 256), the gadgebot door (uid 83), the
 * main gate (673) and the two minor gates (812, 813) deleted; Blarg
 * Station's trigger 1 done, which spawns the bridge lowered. The same here,
 * once per visit, at the first moby update of the level, before any gate's
 * own first update reads them (ee_rz_moby_update_c). */
DECLARE_LVL(func_0020E6B8);             /* the first moby of the update list */
DECLARE_LVL(func_0020D678);             /* DeleteMoby */
DECLARE_LVL(func_L00_0023D9C0);         /* InitLevelResidentState: the level's load, a reload of the same level too */
typedef void *(*FirstMobyFn)(void);
typedef void (*VoidFn)(void);
typedef void (*DeleteFn)(void *);

static void delete_mobys_with_uid(s32 uid, s32 L)
{
    u8 *m = (u8 *)LVL(L, func_0020E6B8, FirstMobyFn)();
    while (m != 0) {
        u8 *next = (u8 *)MOBY_NEXT(m);
        if (MOBY_STATE(m) >= 0 && MOBY_UID(m) == uid) {
            LVL(L, func_0020D678, DeleteFn)(m);
        }
        m = next;
    }
}

/* InitLevelResidentState in level L (src/rz_hooks.s): a level load, which a
 * Load Game of a save on the same level is too, so the per-visit state
 * starts over (the early-O2 fixes ran once per level change before, and a
 * reload on Orxon brought the gates back). The record's state is read here
 * as well: the first gameplay frame, where ee_rz_frame reads it, comes
 * after the arrival, and the givers' first updates (where they decide to
 * stay or go) came before it with `active` still as the title screen left
 * it, so after a Load Game from the card every giver saw the real inventory
 * and stayed. Landing on the bonus vendor
 * check's planet puts the Charge Boots on sale everywhere. */
void ee_rz_level_init_c(s32 L)
{
    fixes_level = -1;
    vendor_vanilla = -1;
    banner_due = 0;
    granted = -1;
    active = ee_rz_active();
    active_level = L;
    ee_debug_rz_play[0] = (u32)active;
    if (active && L == ee_rz_bonus_planet()) {
        ee_rz_set_bonus_seen();
    }
    LVL(L, func_L00_0023D9C0, VoidFn)();
}

static void o2_early_fixes(s32 L)
{
    static const u16 orxon_gates[4] = { 83, 673, 812, 813 };
    s32 i;
    if (!INVENTORY[rz_item_game_id(RZ_O2_MASK)]) {
        return;
    }
    if (L == BLARG) {
        LEVEL_TRIGGERS[BLARG * 16 + 1] = 0xFF;
    } else if (L == ORXON) {
        LEVEL_TRIGGERS[ORXON * 16 + 1] = 0xFF;
        LEVEL_TRIGGERS[ORXON * 16 + 4] = 0xFF;
        LEVEL_BITS[ORXON * 256 + 30] |= 0x70;
        LEVEL_BITS[ORXON * 256 + 35] |= 0x02;
        LEVEL_BITS[ORXON * 256 + 102] |= 0x30;
        LEVEL_BITS[ORXON * 256 + 103] |= 0x01;
        for (i = 0; i < 4; i++) {
            delete_mobys_with_uid(orxon_gates[i], L);
        }
    }
}

/* UnlockPlanet(planet) in level L: arrivals (the level itself) and Novalis pass. */
void ee_rz_unlock_planet_c(s32 planet, s32 L)
{
    s32 check = active && planet != CURRENT_LEVEL && planet >= 2 && planet <= 18
                ? rz_check_for_vanilla(RZ_INFOBOT_NOVALIS + planet - 1) : -1;
    if (check < 0) {
        LVL(L, func_L00_00261848, PlanetFn)(planet);
        return;
    }
    suppress_banner = planet;
    collect(check, L);
}

/* ShowBanner(text, ticks) inside GiveItem, and in a giver's own function
 * (Al's "You got the Heli-Pack upgrade!" right after his GiveItem): the
 * Randomizer's banner for the item it is giving, or gave this frame. */
void ee_rz_item_banner_c(s32 text, s32 ticks, s32 L)
{
    if (active && pending_item != RZ_ITEM_NONE) {
        banner_shown = 1;
        item_banner(pending_check, pending_item, L);
        return;
    }
    if (active && banner_due) {
        banner_due = 0;
        item_banner(last_check, last_item, L);
        return;
    }
    LVL(L, func_L00_00264DB8, BannerFn)(text, ticks);
}

/* LoadMobyClassResources(class, buffer) in Vendor_InitItemDisplay: the
 * loader finds a gadget's class in the level's gadget directory and reads
 * it in; the Charge Boots' class is not there (R&C2's model, spliced into
 * every level's core, so already in memory), and the loader run off the end
 * of its directory froze the game when the cursor reached the entry. */
void ee_rz_vendor_class_c(s32 cls, s32 buffer, s32 L)
{
    if (cls == CHARGE_BOOTS_CLASS) {
        return;
    }
    LVL(L, func_00205270, ClassFn)(cls, buffer);
}

/* --- the health HUD with 7 orbs ----------------------------------------------------
 * HudHealthDraw lays the orbs out by their count: 8 (two rows of four) and 5
 * (three over two) have tables, anything else is the row of four, so the 7
 * of the Ultra Nanotech without the Premium drew seven orbs over four
 * places. The edition's drawer, in HudHealthDraw's place (its address in
 * Hud_RegisterHealthBank's lui/addiu), draws 7 as four over three, the
 * three centred under the four as retail centres the two under the three,
 * with the backing bars of a row of four (48) and of three (32); any other
 * count is retail's. The port of retail's drawing keeps its constants (the
 * element's fade over 8, the bars 32 tall 16 above the orbs, rows 32
 * apart) and its sprites. */
#define ORB_ICON        0x7536
#define BAR_ICON        0x7580
#define ORB_PHASES      0x1E
#define FADE_MAX        8.0f
#define BAR_HEIGHT      32
#define BAR_ABOVE       -16
#define ROW_STEP        32
#define HUD_Y_BASE      14

static s32 health_count(void)
{
    return HERO_MODE == 1 ? CLANK_HEALTH_MAX : MAX_HEALTH;
}

static f32 unit(f32 v)
{
    return v > 1.0f ? 1.0f : v < 0.0f ? 0.0f : v;
}

static void bar(s32 x0, s32 y, s32 half, s32 alpha, s32 tex, s32 tex2, s32 L)
{
    LVL(L, func_00200650, SpriteFn)(tex, x0 + half, y, BAR_HEIGHT, BAR_HEIGHT, alpha);
    LVL(L, func_00200468, SpriteFn)(tex2, x0 - half, y, half << 1, BAR_HEIGHT, alpha);
    LVL(L, func_00200468, SpriteFn)(tex, x0 - half - BAR_HEIGHT, y, BAR_HEIGHT, BAR_HEIGHT, alpha);
}

s32 ee_rz_health_draw_c(void *element, s32 L)
{
    static const s16 orbs7[14] = { -80, 0, -48, 0, -16, 0, 16, 0, -64, ROW_STEP, -32, ROW_STEP, 0, ROW_STEP };
    u8 *e = (u8 *)element;
    const u8 *fade = e + 0x70;
    const s16 *anim = *(const s16 *const *)(e + 0x80);
    s32 x0, y0, n, phase, alpha, bar_alpha, tex, tex2, i;
    f32 shown, strength;
    if (health_count() != 7 || HERO_MODE == 2 || HERO_STATE == 0x32 || fade[0] == 0) {
        return LVL(L, func_L00_0023A1A0, HealthFn)(element);
    }
    x0 = *(s32 *)(e + 0x50);
    y0 = HUD_Y_BASE + (PAL_MODE ? 0xA : 0x12);
    n = *(s32 *)(e + 0x78);
    shown = unit((f32)fade[0] / FADE_MAX);
    strength = unit((f32)fade[1] / FADE_MAX);
    alpha = (s32)(strength * 128.0f);
    bar_alpha = (s32)(20.0f * shown);
    phase = anim[0] >> 1;
    if (n == 0) {
        phase = ORB_PHASES;
    }
    tex = LVL(L, func_00200198, IconFn)(BAR_ICON, 1);
    tex2 = LVL(L, func_00200198, IconFn)(BAR_ICON, 0);
    bar(x0, y0 + BAR_ABOVE, (s32)(shown * 48.0f), bar_alpha, tex, tex2, L);
    bar(x0, y0 + BAR_ABOVE + ROW_STEP, (s32)(shown * 32.0f), bar_alpha, tex, tex2, L);
    for (i = 0; i < 7; i++) {
        s32 x = x0 + orbs7[2 * i], y = y0 + orbs7[2 * i + 1];
        LVL(L, func_L00_0023BAB8, FrameFn)(e, LVL(L, func_00200198, IconFn)(ORB_ICON, phase), x, y, 1, alpha);
        if (phase != ORB_PHASES) {
            s32 glow = (s32)((f32)(anim[1] >> 4) * strength);
            LVL(L, func_L00_0023BAB8, FrameFn)(e, LVL(L, func_00200198, IconFn)(ORB_ICON, ORB_PHASES), x, y, 1, alpha);
            LVL(L, func_00200468, SpriteFn)(LVL(L, func_00200198, IconFn)(ORB_ICON, ORB_PHASES + 1), x + 0xE, y - 0x11,
                                            0x22, 0x22, glow);
            phase = n == i + 1 ? ORB_PHASES : (phase + 6) % ORB_PHASES;
        }
    }
    return *(s32 *)(e + 0x58);
}

/* ShowPlanetBanner(planet): not for a planet whose unlock was a check just
 * redirected; the Randomizer's own banner for an infobot it is giving. */
void ee_rz_planet_banner_c(s32 planet, s32 L)
{
    if (active && planet == suppress_banner) {
        suppress_banner = 0;
        return;
    }
    if (active && pending_item != RZ_ITEM_NONE) {
        item_banner(pending_check, pending_item, L);
        return;
    }
    LVL(L, func_L00_00263DB0, PlanetFn)(planet);
}

/* Every memcard_Save(slot, flags) call in a level's code: the givers'
 * autosaves (a pickup's, Helga's, the Nanotech vendor's, a Gold Bolt spot's
 * inside GoldBoltUpdate once its flag is set) and the ship's at a planet
 * change. The save serialises the inventory, the planets and the flags the
 * moment it is called: inside a giver's update that is the collected view,
 * so the view comes off around it, or the card would get the vanilla items
 * as owned, which a load from the title screen then restored as a plain
 * game's inventory. A Gold Bolt spot whose flag is set and whose
 * check is not collected gives its item first. The record's own page is
 * not among what the autosave writes; ee_rz_record_autosave follows it. */
s32 ee_rz_save_c(s32 slot, s32 flags, s32 L)
{
    s32 was_on = view_on;
    s32 r;
    if (active) {
        s32 k;
        for (k = 0; k < 4; k++) {
            s32 check = rz_check_for_bolt(CURRENT_LEVEL, k);
            if (check >= 0 && GOLD_FLAGS[CURRENT_LEVEL * 4 + k] != 0 && !ee_rz_check_collected(check)) {
                collect(check, L);
            }
        }
    }
    if (was_on) {
        view_restore(L);
    }
    r = LVL(L, func_0020BFC8, SaveFn)(slot, flags);
    if (was_on) {
        view_apply();
    }
    return r;
}

/* The Gold Bolts collected: the record's count rather than the spots'. */
s32 ee_rz_gold_count_c(s32 L)
{
    if (active) {
        return ee_rz_gold_received();
    }
    return LVL(L, func_002160E0, CountFn)();
}

static s32 class_listed(s32 L, s32 cls)
{
    const u16 *list = L >= 0 && L < 19 ? ee_rz_classes[L] : 0;
    while (list != 0 && *list != 0xFFFF) {
        if (*list == (u16)cls) {
            return 1;
        }
        list++;
    }
    return 0;
}

/* The moby update loop's call of a moby's update, in level L, and the
 * level's load-time pass (MobyLoadTimeUpdatePass), which runs every moby's
 * update once before that loop's first run: a giver decides to stay or go
 * at its first update, so the pass needs the view too (a revisit of Orxon
 * brought the Magneboots back: the pickup decided there, unseen).
 *
 * Helga's purchase is the one place a check strands the player: her
 * platform on Kerwan is left by the Swingshot she sells, and the rail lift
 * back to the ship, which she is linked to, is only given its trigger volume
 * at the purchase (and started outright by her on a later visit, once the
 * Swingshot is owned and she goes). With her check giving something else,
 * the lift is started outright at the purchase too, as she starts it on
 * that later visit (her link, read before her update, which deletes her). */
void ee_rz_moby_update_c(void *moby, UpdateFn update, s32 L)
{
    s32 link = -1;
    if (L != active_level) {                /* a moby's update before the frame hook has seen the level */
        active = ee_rz_active();
        active_level = L;
    }

    if (L != fixes_level) {
        fixes_level = L;
        if (ee_rz_active()) {
            o2_early_fixes(L);
        }
    }
    if (!active || !class_listed(L, MOBY_CLASS(moby))) {
        update(moby);
        return;
    }
    if (L == KERWAN && MOBY_CLASS(moby) == HELGA_CLASS) {
        link = *(volatile s32 *)(MOBY_PVARS(moby) + HELGA_LINK);
    }
    ee_debug_rz_play[1]++;
    granted = -1;
    view_apply();
    update(moby);
    view_restore(L);
    if (link >= 0 && granted == rz_check_for_vanilla(RZ_SWINGSHOT) && !INVENTORY[SWINGSHOT_ID]) {
        u8 *lift = (u8 *)MOBY_TABLE + MOBY_SIZE * link;
        if (MOBY_CLASS(lift) == LIFT_CLASS) {
            *(volatile s32 *)(MOBY_PVARS(lift) + LIFT_AUTOSTART) = 1;
        }
    }
    granted = -1;
}

/* Each gameplay frame (src/charge_dash.c's frame hook): the record's state
 * for this level, refreshed when the level changes and now and then, and
 * the record's page to the card once it changed and the card is free. */
void ee_rz_frame(s32 L)
{
    static s32 countdown;
    banner_due = 0;
    vendor_open = 0;                        /* no gameplay frame runs while the vendor's menu is up */
    if (active) {
        ee_rz_record_autosave(L);
    }
    if (L != active_level || --countdown <= 0) {
        active_level = L;
        countdown = 120;
        active = ee_rz_active();
        ee_debug_rz_play[0] = (u32)active;
        suppress_banner = 0;
        vendor_vanilla = -1;
        view_on = 0;
    }
}
