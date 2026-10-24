/* rz3/rz5: the Randomizer's checks in play. Every check hands out the item
 * the seed put there, and the givers offer their check, not their retail
 * item (src/randomizer_play.h for the other files).
 *
 * What a check is in the game. Every gadget and weapon the game gives goes
 * through GiveItem (func_L00_002618D8: marks the item owned and "had",
 * raises its ammo, shows its "You got the ..." banner, puts it on the quick
 * select and in the hand), from characters, pickups and the Gadgetron vendor
 * alike; every infobot ends in UnlockPlanet (func_L00_00261848: the planet's
 * byte in PLANETS, the galaxy list, the "Infobot for Planet ... acquired"
 * banner); a Gold Bolt sets its spot's byte in GOLD_FLAGS and saves
 * (GoldBoltUpdate, func_L01_00309078); the Zoomerator, the Raritanium, the
 * Codebot and the two Nanotech upgrades set a byte of FLAGS. The item a check
 * gives in retail names the check (src/randomizer.c: every item is given
 * once), a Gold Bolt spot its level and flag, an infobot the planet it
 * unlocks. tools/gen_randomizer.py finds every call of those functions in
 * each level's code, and tools/overlay_patch.py (feature randomizer) sends
 * them to the hooks here through the level's stubs (src/rz_hooks.s, which
 * pass the level, L).
 *
 * The collected view. The givers decide what to offer from what the player
 * owns: Helga sells the Swingshot while it is not owned, an infobot deletes
 * itself once its planet is unlocked, the Magneboots pickup once the
 * Magneboots are "had", the Nanotech vendor sells the second upgrade once
 * the first's flag is set. With the rewards shuffled, owning is the wrong
 * question: a Swingshot received elsewhere would make Helga keep hers, and
 * her check with it, for good; a planet unlocked by another check would
 * delete the infobot that is a check of its own. So while a giver's update
 * runs (the moby update loop's call, and the level's load-time pass, where
 * the givers decide to stay or go), INVENTORY, HAD, PLANETS and the special
 * items' FLAGS show whether each check has been collected instead. The real
 * arrays come back when it returns, with any other byte the giver changed
 * kept. A grant inside the update puts the real arrays back for its
 * duration. Everything else (the Swingshot's targets, the Trespasser's locks,
 * the galactic map, the HelpDesk) updates outside those mobys and sees the
 * truth. The special items' givers write their flag under the view: a byte
 * that turned 1 is the check collected.
 *
 * Receiving. A gadget or weapon: GiveItem, with the Randomizer's banner in
 * place of retail's (src/randomizer_text.c). An infobot: UnlockPlanet. A
 * Gold Bolt: the record's count, which the Gold Weapons' vendor sees instead
 * of the spots' flags. The Zoomerator, Raritanium and Codebot: their flag.
 * The Nanotech upgrades: their flag and the health maximum, 4 orbs plus one
 * for the Premium and three for the Ultra. The Charge Boots: the cheat
 * switched on. The record (src/randomizer_save.c) keeps what has been
 * collected and received, so a check gives its item once. */
#include "randomizer_play.h"

DECLARE_LVL(func_L00_002618D8);         /* GiveItem(item_id, flag) */
DECLARE_LVL(func_L00_00261848);         /* UnlockPlanet(planet) */
DECLARE_LVL(func_L00_00263DB0);         /* ShowPlanetBanner(planet) */
DECLARE_LVL(func_0020BFC8);             /* memcard_Save(slot, flags) */
DECLARE_LVL(func_002160E0);             /* count_nonzero_entries_up_to_40: the Gold Bolts collected */
DECLARE_LVL(func_0020E6B8);             /* the first moby of the update list */
DECLARE_LVL(func_0020D678);             /* DeleteMoby(moby) */
DECLARE_LVL(func_L00_0023D9C0);         /* InitLevelResidentState: a level's load, a reload of the same level too */
typedef void (*GiveFn)(s32, s32);
typedef void (*PlanetFn)(s32);
typedef s32 (*SaveFn)(s32, s32);
typedef s32 (*CountFn)(void);
typedef void (*UpdateFn)(void *);
typedef void *(*FirstMobyFn)(void);
typedef void (*DeleteFn)(void *);

#define PLANET_COUNT    20
#define LEVELS          19
#define GOLD_SPOTS      4               /* Gold Bolt spots per level */
#define REFRESH_FRAMES  120             /* how often ee_rz_frame reads the record again */

/* Helga on Kerwan and her lift (ee_rz_moby_update_c). */
#define KERWAN          3
#define HELGA_CLASS     890             /* QuestGiverUpdate, func_L03_002DBC38: Helga, who sells the Swingshot */
#define HELGA_LIFT      0xD4            /* her pvars: the moby index of the rail lift she is linked to */
#define LIFT_CLASS      0x3F4           /* RailLiftUpdate, func_L03_002DE088: the lift back to the ship */
#define LIFT_AUTOSTART  0x8C            /* its pvars: start without waiting for the trigger volume */
#define SWINGSHOT_ID    12

/* The early-O2 fixes (o2_early_fixes). */
#define ORXON           10
#define BLARG           6
#define LEVEL_TRIGGERS  ((volatile u8 *)0x0014C150)  /* per level 16 bytes: a level trigger done (0xFF) */
#define LEVEL_BITS      ((volatile u8 *)0x0014C290)  /* per level 256 bytes: the save bits by moby uid */

/* tools/gen_randomizer.py: per level, the classes whose update gets the collected view, 0xFFFF last. */
extern const u16 *const ee_rz_classes[LEVELS];

/* For the PINE test scripts (docs/DEBUGGING.md): active; views applied; grants; the last check collected + 1. */
volatile u32 ee_debug_rz_play[4];

static s32 active;                      /* the game in memory is a Randomizer save */
static s32 active_level = -1;           /* the level `active` was read for */
static s32 fixes_level = -1;            /* the level whose early-O2 fixes have run on this visit */
static s32 view_on;                     /* the collected view is in place */
static u8 real_inventory[ITEMS];        /* the real arrays while the view is on */
static u8 real_had[ITEMS];
static u8 real_planets[PLANET_COUNT];
static u8 real_flags[FLAG_BYTES];

/* The banners: the Randomizer's own replaces retail's after a check. */
static s32 pending_item = RZ_ITEM_NONE; /* the item a GiveItem or UnlockPlanet of the edition's is giving, */
static s32 pending_check;               /* and its check */
static s32 pending_banner_shown;        /* GiveItem showed its banner, which was ours */
static s32 suppress_planet_banner;      /* a planet whose retail banner the next ShowPlanetBanner skips */
static s32 banner_due;                  /* a check was collected this frame: a giver's own banner after it is ours */
static s32 last_check;                  /* that check, */
static s32 last_item;                   /* and its item */
static s32 granted = -1;                /* the check collected during the moby update under way, -1 none */

s32 rz_play_active(void)
{
    return active;
}

s32 rz_play_owned(s32 item_id)
{
    return view_on ? real_inventory[item_id] : INVENTORY[item_id];
}

/* --- the special items ----------------------------------------------------------------------------------------- */

static s32 flag_of_item(s32 item)
{
    switch (item) {
    case RZ_ZOOMERATOR:
        return FLAG_ZOOMERATOR;
    case RZ_RARITANIUM:
        return FLAG_RARITANIUM;
    case RZ_CODEBOT:
        return FLAG_CODEBOT;
    case RZ_PREMIUM_NANOTECH:
        return FLAG_PREMIUM;
    case RZ_ULTRA_NANOTECH:
        return FLAG_ULTRA;
    default:
        return -1;
    }
}

static s32 item_of_flag(s32 flag)
{
    switch (flag) {
    case FLAG_ZOOMERATOR:
        return RZ_ZOOMERATOR;
    case FLAG_RARITANIUM:
        return RZ_RARITANIUM;
    case FLAG_CODEBOT:
        return RZ_CODEBOT;
    case FLAG_PREMIUM:
        return RZ_PREMIUM_NANOTECH;
    case FLAG_ULTRA:
        return RZ_ULTRA_NANOTECH;
    default:
        return RZ_ITEM_NONE;
    }
}

/* The health maximum the received upgrades give: 4, one more with the Premium Nanotech, three more with the Ultra;
 * 8 with both, as retail, which only sells the Ultra after the Premium. */
static void set_health_maximum(void)
{
    MAX_HEALTH = 4 + (ee_rz_received_special(FLAG_PREMIUM) ? 1 : 0) + (ee_rz_received_special(FLAG_ULTRA) ? 3 : 0);
    HERO_HEALTH = MAX_HEALTH;
}

/* --- the collected view ---------------------------------------------------------------------------------------- */

/* The check whose retail reward is item id ITEM_ID's, planet PLANET's infobot, or flag byte FLAG's item; -1. */
static s32 check_of_item_id(s32 item_id)
{
    return rz_check_for_vanilla(rz_item_of_game_id(item_id));
}

static s32 check_of_planet(s32 planet)
{
    return planet >= 1 && planet <= 18 ? rz_check_for_vanilla(RZ_INFOBOT_NOVALIS + planet - 1) : -1;
}

static s32 check_of_flag(s32 flag)
{
    return rz_check_for_vanilla(item_of_flag(flag));
}

static void view_apply(void)
{
    s32 i, check;
    if (view_on) {
        return;
    }
    for (i = 0; i < ITEMS; i++) {
        real_inventory[i] = INVENTORY[i];
        real_had[i] = HAD[i];
        check = check_of_item_id(i);
        if (check >= 0) {
            INVENTORY[i] = ee_rz_check_collected(check) ? 1 : 0;
            HAD[i] = INVENTORY[i];
        }
    }
    for (i = 0; i < PLANET_COUNT; i++) {
        real_planets[i] = PLANETS[i];
        check = check_of_planet(i);
        if (check >= 0) {
            PLANETS[i] = ee_rz_check_collected(check) ? 1 : 0;
        }
    }
    for (i = 0; i < FLAG_BYTES; i++) {
        real_flags[i] = FLAGS[i];
        check = check_of_flag(i);
        if (check >= 0) {
            FLAGS[i] = ee_rz_check_collected(check) ? 1 : 0;
        }
    }
    view_on = 1;
}

static void receive(s32 check, s32 item, s32 L);

/* The real arrays back. A byte the view did not replace keeps what the giver did to it. A special item's byte that
 * turned 1 under the view is its check collected, which gives its item (the Nanotech vendor's own change of the
 * health maximum goes with the view). */
static void view_restore(s32 L)
{
    s32 i, check;
    if (!view_on) {
        return;
    }
    view_on = 0;
    for (i = 0; i < ITEMS; i++) {
        if (check_of_item_id(i) < 0) {
            real_inventory[i] = INVENTORY[i];
            real_had[i] = HAD[i];
        }
        INVENTORY[i] = real_inventory[i];
        HAD[i] = real_had[i];
    }
    for (i = 0; i < PLANET_COUNT; i++) {
        if (check_of_planet(i) < 0) {
            real_planets[i] = PLANETS[i];
        }
        PLANETS[i] = real_planets[i];
    }
    for (i = 0; i < FLAG_BYTES; i++) {
        check = check_of_flag(i);
        if (check < 0) {
            real_flags[i] = FLAGS[i];
            FLAGS[i] = real_flags[i];
        } else if (FLAGS[i] && !ee_rz_check_collected(check)) {
            ee_rz_set_collected(check);
            FLAGS[i] = real_flags[i];
            if (i == FLAG_PREMIUM || i == FLAG_ULTRA) {
                set_health_maximum();
            }
            receive(check, ee_rz_layout_item(check), L);
            real_flags[i] = FLAGS[i];
        } else {
            FLAGS[i] = real_flags[i];
        }
    }
}

/* --- receiving ------------------------------------------------------------------------------------------------- */

/* ITEM, given for CHECK, with its banner. GiveItem's and UnlockPlanet's own banners are ours while pending_item is
 * set (ee_rz_item_banner_c, ee_rz_planet_banner_c). */
static void receive(s32 check, s32 item, s32 L)
{
    s32 was_on = view_on;
    s32 flag = flag_of_item(item);
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
        rz_show_item_banner(check, item, L);
    } else if (item == RZ_CHARGE_BOOTS) {
        *(volatile u8 *)EE_CHEAT_CHARGE_BOOTS = 1;
        CHEAT_SEEN = 1;
        rz_show_item_banner(check, item, L);
    } else if (flag >= 0) {
        ee_rz_set_received_special(flag);
        FLAGS[flag] = 1;                    /* the Items page lists the upgrades by their flag */
        if (flag == FLAG_PREMIUM || flag == FLAG_ULTRA) {
            set_health_maximum();
        }
        rz_show_item_banner(check, item, L);
    } else if (rz_item_game_id(item) >= 0) {
        pending_item = item;
        pending_banner_shown = 0;
        LVL(L, func_L00_002618D8, GiveFn)(rz_item_game_id(item), 1);
        pending_item = RZ_ITEM_NONE;
        if (!pending_banner_shown) {
            rz_show_item_banner(check, item, L);    /* GiveItem shows none for an item its level has no banner for */
        }
    }
    if (was_on) {
        view_apply();
    }
}

/* CHECK collected now: its item, once. */
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

/* --- the grants' hooks ----------------------------------------------------------------------------------------- */

/* GiveItem(item_id, flag) in level L. A vendor check also tells the vendor what it gave (randomizer_vendor.c). */
void ee_rz_give_item_c(s32 item_id, s32 flag, s32 L)
{
    s32 check = active ? check_of_item_id(item_id) : -1;
    const struct RzCheck *row;
    if (check < 0) {
        LVL(L, func_L00_002618D8, GiveFn)(item_id, flag);
        return;
    }
    row = rz_check(check);
    if (check == RZ_CHECKS_BASE) {
        rz_vendor_note_purchase(item_id, -1);       /* the bonus entry: never a weapon demo */
    } else if (row != 0 && row->kind == RZ_KIND_VENDOR) {
        rz_vendor_note_purchase(item_id, rz_item_game_id(ee_rz_layout_item(check)));
    }
    collect(check, L);
}

/* UnlockPlanet(planet) in level L: an arrival (the level itself) and Novalis pass as retail. */
void ee_rz_unlock_planet_c(s32 planet, s32 L)
{
    s32 is_check = active && planet != CURRENT_LEVEL && planet >= 2 && planet <= 18;
    s32 check = is_check ? check_of_planet(planet) : -1;
    if (check < 0) {
        LVL(L, func_L00_00261848, PlanetFn)(planet);
        return;
    }
    suppress_planet_banner = planet;
    collect(check, L);
}

/* ShowBanner(text, ticks) inside GiveItem, and in a giver's own function (Al's "You got the Heli-Pack upgrade!"
 * right after his GiveItem): the Randomizer's banner for the item it is giving, or gave this frame. */
void ee_rz_item_banner_c(s32 text, s32 ticks, s32 L)
{
    if (active && pending_item != RZ_ITEM_NONE) {
        pending_banner_shown = 1;
        rz_show_item_banner(pending_check, pending_item, L);
        return;
    }
    if (active && banner_due) {
        banner_due = 0;
        rz_show_item_banner(last_check, last_item, L);
        return;
    }
    LVL(L, func_L00_00264DB8, BannerFn)(text, ticks);
}

/* ShowPlanetBanner(planet): none for a planet whose unlock was a check just redirected; the Randomizer's own banner
 * for an infobot it is giving. */
void ee_rz_planet_banner_c(s32 planet, s32 L)
{
    if (active && planet == suppress_planet_banner) {
        suppress_planet_banner = 0;
        return;
    }
    if (active && pending_item != RZ_ITEM_NONE) {
        rz_show_item_banner(pending_check, pending_item, L);
        return;
    }
    LVL(L, func_L00_00263DB0, PlanetFn)(planet);
}

/* Every memcard_Save(slot, flags) call in a level's code: the givers' autosaves (a pickup's, Helga's, the Nanotech
 * vendor's, a Gold Bolt spot's inside GoldBoltUpdate once its flag is set) and the ship's at a planet change.
 *
 * The save serialises the inventory, the planets and the flags the moment it is called: inside a giver's update
 * that is the collected view, so the view comes off around it, or the card would get the retail items as owned,
 * which a load from the title screen then restored as the save's inventory. A Gold Bolt spot whose flag is set
 * and whose check is not collected gives its item first. The autosave does not write the record's page;
 * ee_rz_record_autosave (src/randomizer_save.c) does, from the next gameplay frame. */
s32 ee_rz_save_c(s32 slot, s32 flags, s32 L)
{
    s32 was_on = view_on;
    s32 result;
    if (active) {
        s32 spot;
        for (spot = 0; spot < GOLD_SPOTS; spot++) {
            s32 check = rz_check_for_bolt(CURRENT_LEVEL, spot);
            s32 taken = GOLD_FLAGS[CURRENT_LEVEL * GOLD_SPOTS + spot] != 0;
            if (check >= 0 && taken && !ee_rz_check_collected(check)) {
                collect(check, L);
            }
        }
    }
    if (was_on) {
        view_restore(L);
    }
    result = LVL(L, func_0020BFC8, SaveFn)(slot, flags);
    if (was_on) {
        view_apply();
    }
    return result;
}

/* The Gold Bolts collected, for the Gold Weapons' vendor: the record's count rather than the spots'. */
s32 ee_rz_gold_count_c(s32 L)
{
    if (active) {
        return ee_rz_gold_received();
    }
    return LVL(L, func_002160E0, CountFn)();
}

/* --- the O2 Mask early ----------------------------------------------------------------------------------------- */

/* Retail plays Orxon as Clank until the O2 Mask (Pokitaru's infobot is in Orxon's Clank section), and the gates of
 * that section open for Clank and his Gadgebots only; Blarg Station's drawbridge is lowered from the Clank side
 * too. With the mask early the player arrives as Ratchet and both would stay shut, so Panda291's randomizer forces
 * them on PS3 when the mask is owned: Orxon's level triggers 1 and 4 done, its save bits of the gates and the
 * infobot set (bytes 30, 35, 102 and 103 of its 256), the gadgebot door (uid 83), the main gate (673) and the two
 * minor gates (812, 813) deleted; Blarg Station's trigger 1 done, which spawns the bridge lowered. The same here,
 * once per visit, at the level's first moby update, before any gate's own first update reads them. */
static void delete_mobys_with_uid(s32 uid, s32 L)
{
    u8 *moby = (u8 *)LVL(L, func_0020E6B8, FirstMobyFn)();
    while (moby != 0) {
        u8 *next = (u8 *)MOBY_NEXT(moby);
        if (MOBY_STATE(moby) >= 0 && MOBY_UID(moby) == uid) {
            LVL(L, func_0020D678, DeleteFn)(moby);
        }
        moby = next;
    }
}

static void o2_early_fixes(s32 L)
{
    static const u16 ORXON_GATES[4] = { 83, 673, 812, 813 };
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
            delete_mobys_with_uid(ORXON_GATES[i], L);
        }
    }
}

/* --- the level and the frame ----------------------------------------------------------------------------------- */

/* Read the record for level L: whether this is a Randomizer save. */
static void read_record(s32 L)
{
    active = ee_rz_active();
    active_level = L;
    ee_debug_rz_play[0] = (u32)active;
}

/* InitLevelResidentState in level L: a level's load, which a Load Game of a save on the same level is too, so the
 * visit's state starts over (the early-O2 fixes run again on a reload of Orxon). The record is read here: the
 * givers' first updates, where they decide to stay or go, come before the first gameplay frame. Landing on the
 * bonus vendor check's planet puts the Charge Boots on sale everywhere. */
void ee_rz_level_init_c(s32 L)
{
    fixes_level = -1;
    banner_due = 0;
    granted = -1;
    rz_vendor_forget_purchase();
    read_record(L);
    if (active && L == ee_rz_bonus_planet()) {
        ee_rz_set_bonus_seen();
    }
    LVL(L, func_L00_0023D9C0, VoidFn)();
}

static s32 class_listed(s32 L, s32 class_id)
{
    const u16 *list = L >= 0 && L < LEVELS ? ee_rz_classes[L] : 0;
    while (list != 0 && *list != 0xFFFF) {
        if (*list == (u16)class_id) {
            return 1;
        }
        list++;
    }
    return 0;
}

/* Helga's purchase is the one place a check strands the player: her platform on Kerwan is left by the Swingshot she
 * sells, and the rail lift back to the ship, which she is linked to, only gets its trigger volume at the purchase
 * (and is started outright by her on a later visit, once the Swingshot is owned and she goes). With her check
 * giving something else, the lift is started outright at the purchase too. */
static void start_helga_lift(s32 lift_index)
{
    u8 *lift = (u8 *)MOBY_TABLE + MOBY_SIZE * lift_index;
    if (MOBY_CLASS(lift) == LIFT_CLASS) {
        *(volatile s32 *)(MOBY_PVARS(lift) + LIFT_AUTOSTART) = 1;
    }
}

/* A moby's update, in level L: the moby update loop's call (MobyUpdateLoop + 0x58) and the level's load-time pass
 * (MobyLoadTimeUpdatePass + 0xC8), which runs every moby's update once before that loop's first run. The givers'
 * updates run under the collected view. */
void ee_rz_moby_update_c(void *moby, UpdateFn update, s32 L)
{
    s32 helga_lift = -1;
    if (L != active_level) {                /* an update before the level's init hook has run */
        read_record(L);
    }
    if (L != fixes_level) {
        fixes_level = L;
        if (active) {
            o2_early_fixes(L);
        }
    }
    if (!active || !class_listed(L, MOBY_CLASS(moby))) {
        update(moby);
        return;
    }
    if (L == KERWAN && MOBY_CLASS(moby) == HELGA_CLASS) {
        helga_lift = *(volatile s32 *)(MOBY_PVARS(moby) + HELGA_LIFT);    /* read first: her update deletes her */
    }
    ee_debug_rz_play[1]++;
    granted = -1;
    view_apply();
    update(moby);
    view_restore(L);
    if (helga_lift >= 0 && granted == rz_check_for_vanilla(RZ_SWINGSHOT) && !INVENTORY[SWINGSHOT_ID]) {
        start_helga_lift(helga_lift);
    }
    granted = -1;
}

/* Each gameplay frame (src/charge_dash.c's frame hook): the record's page to the card once it changed, and the
 * record read again when the level changes and now and then. No gameplay frame runs while the vendor's menu is
 * up, so a frame closes it. */
void ee_rz_frame(s32 L)
{
    static s32 frames_left;
    banner_due = 0;
    rz_vendor_menu_closed();
    if (active) {
        ee_rz_record_autosave(L);
    }
    if (L != active_level || --frames_left <= 0) {
        frames_left = REFRESH_FRAMES;
        read_record(L);
        suppress_planet_banner = 0;
        rz_vendor_forget_purchase();
        view_on = 0;
    }
}
