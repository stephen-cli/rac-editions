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
DECLARE_LVL(func_0020D348);             /* CreateMoby(class_id) */
DECLARE_LVL(func_L00_0023D9C0);         /* InitLevelResidentState: a level's load, a reload of the same level too */
typedef void (*GiveFn)(s32, s32);
typedef void (*PlanetFn)(s32);
typedef s32 (*SaveFn)(s32, s32);
typedef s32 (*CountFn)(void);
typedef void (*UpdateFn)(void *);
typedef void *(*FirstMobyFn)(void);
typedef void (*DeleteFn)(void *);
typedef void *(*CreateFn)(s32);

#define PLANET_COUNT    20
#define LEVELS          19
#define GOLD_SPOTS      4               /* Gold Bolt spots per level */
#define REFRESH_FRAMES  120             /* how often ee_rz_frame reads the record again */

/* A Gold Bolt spot whose item is an infobot shows an infobot (carry_stand_in): a moby of the infobot class, which
 * every level has (tools/extra_classes.py splices it into the five without it), made at the spot by CreateMoby so
 * that its animation state is the class's own, stands in for the bolt. Its own update is never run (on those five
 * levels the class has none): the spot's carries it, turning it with its yaw kept within -pi..pi as the game keeps
 * its angles (a yaw that grew past them broke its matrix, and it drew nowhere or as stray polygons over the menus).
 * The bolt keeps GoldBoltUpdate, its check and its sparkle, drawn at scale 0; its touch takes it at once, the way the
 * spots retail marks quiet do, without Ratchet's hold-up and the camera's cut, and the infobot banner follows
 * (receive). Swapping the bolt's own class pointer instead left its animation state the bolt's, and MobyProc looped
 * on it. */
#define GOLD_CLASS      1134            /* GoldBoltUpdate, func_L01_00309078 */
#define INFOBOT_CLASS   750
#define GOLD_QUIET      0xC             /* its pvars: nonzero, the touch takes the bolt without the sequence */
#define MOBY_SCALE_F(m) (*(volatile f32 *)((u8 *)(m) + MOBY_SCALE))
#define MOBY_YAW(m)     (*(volatile f32 *)((u8 *)(m) + MOBY_ROT + 8))
#define MOBY_DRAW_DIST(m) (*(volatile s16 *)((u8 *)(m) + 0x32))
#define MOBY_UPDATE_DIST(m) (*(volatile u8 *)((u8 *)(m) + 0x30))
#define STAND_IN_TURN   0.02f           /* the stand-in's yaw a frame */
#define PI_F            3.1415927f

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

/* Start skips cutscenes and movies (rz: Challenge Mode's skip). The in-level
 * cutscene update (CutsceneModeUpdate, func_L00_0029A300) and the movie
 * player (read_mpeg, func_0023B740) let Start alone end a cutscene when the
 * Challenge Mode flag D_0015EFA0 is set, the game has been completed
 * (D_0015EF20 > 0), a level is loading or no level is loaded; otherwise all
 * four shoulder buttons must be held with it. tools/overlay_patch.py
 * (cutscene_skip) points each program's load of D_0015EF20 there at this
 * word, which holds that count's test or a Randomizer save, so Challenge
 * Mode's other perks (the doubled bolts, the cheat codes) stay its own. It
 * is set as each level loads, before any cutscene of the level runs, and
 * whenever the record is read again. */
#define COMPLETES       (*(volatile s32 *)0x0015EF20)
volatile s32 ee_cutscene_skip;
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
    ee_cutscene_skip = COMPLETES != 0 || active;
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

static s32 is_infobot(s32 item)
{
    return item >= RZ_INFOBOT_NOVALIS && item <= RZ_INFOBOT_VELDIN;
}

/* The stand-ins of this visit: one per Gold Bolt spot, then one for the level's world pickup (PICKUPS); 0 none.
 * Each names its host as its spawner (MOBY_PARENT). A stand-in can be gone without this code deleting it (on
 * Aridia one 190 units from the hero had its slot cleared, and the slot then went to another class), so it is
 * checked before every use: a slot that no longer holds a live infobot of that host is forgotten, never written,
 * and a new stand-in is made at the host's next update. */
#define PICKUP_STAND_IN GOLD_SPOTS
#define STAND_INS       (GOLD_SPOTS + 1)
#define MOBY_PARENT(m)  (*(void *volatile *)((u8 *)(m) + 0xB8))
static u8 *stand_in[STAND_INS];
static void *stand_in_host[STAND_INS];
static f32 host_scale[STAND_INS];       /* the host's own scale, from before it was drawn at 0 */

static void forget_stand_ins(void)
{
    s32 i;
    for (i = 0; i < STAND_INS; i++) {
        stand_in[i] = 0;
        stand_in_host[i] = 0;
        host_scale[i] = 0.0f;
    }
}

static s32 holds_stand_in(u8 *m, void *host)
{
    return MOBY_CLASS(m) == INFOBOT_CLASS && MOBY_STATE(m) >= 0 && MOBY_PARENT(m) == host;
}

/* Stand-in I of HOST, or 0 when there is none or the game has removed it. */
static u8 *live_stand_in(s32 i, void *host)
{
    u8 *m = stand_in[i];
    if (m != 0 && !holds_stand_in(m, host)) {
        stand_in[i] = 0;
        m = 0;
    }
    return m;
}

static s32 is_stand_in(void *moby)
{
    s32 i;
    for (i = 0; i < STAND_INS; i++) {
        if (stand_in[i] == (u8 *)moby && holds_stand_in((u8 *)moby, stand_in_host[i])) {
            return 1;
        }
    }
    return 0;
}

static void drop_stand_in(s32 i, void *host, s32 L)
{
    u8 *m = live_stand_in(i, host);
    if (m != 0) {
        LVL(L, func_0020D678, DeleteFn)(m);
        stand_in[i] = 0;
    }
}

DECLARE_LVL_DATA(class_slot_by_id);     /* u8 per class id, 0xFF none */
DECLARE_LVL_DATA(class_resources);      /* per slot: the class's data, 0 not loaded */

static s32 has_infobot_class(s32 L)
{
    u8 slot = LVL_DATA(L, class_slot_by_id, u8 *)[INFOBOT_CLASS];
    return slot != 0xFF && LVL_DATA(L, class_resources, u32 *)[slot] != 0;
}

/* After the update of MOBY, a moby showing an infobot: once it is gone, so is stand-in I; while it is there, the
 * stand-in, made when there is none and drawn from as far as MOBY is, stands where it is and turns, and MOBY's own
 * model is drawn at scale 0. Without a stand-in (a level without the infobot's class, an image without
 * tools/extra_classes.py's splice, or no free moby), MOBY keeps its model. */
static void carry_stand_in(s32 i, void *moby, s32 L)
{
    u8 *m;
    f32 yaw;
    if (MOBY_STATE(moby) < 0) {             /* collected, or deleted at its first update as collected before */
        drop_stand_in(i, moby, L);
        return;
    }
    m = live_stand_in(i, moby);
    if (m == 0 && has_infobot_class(L)) {
        m = LVL(L, func_0020D348, CreateFn)(INFOBOT_CLASS);
        stand_in[i] = m;
        stand_in_host[i] = moby;
        if (m != 0) {
            MOBY_PARENT(m) = moby;
            MOBY_UPDATE_DIST(m) = MOBY_UPDATE_DIST(moby);
            MOBY_DRAW_DIST(m) = MOBY_DRAW_DIST(moby);
            MOBY_YAW(m) = MOBY_YAW(moby);
        }
    }
    if (m == 0) {
        if (MOBY_SCALE_F(moby) == 0.0f && host_scale[i] != 0.0f) {
            MOBY_SCALE_F(moby) = host_scale[i];
        }
        return;
    }
    *(volatile f32 *)(m + MOBY_POS) = *(volatile f32 *)((u8 *)moby + MOBY_POS);
    *(volatile f32 *)(m + MOBY_POS + 4) = *(volatile f32 *)((u8 *)moby + MOBY_POS + 4);
    *(volatile f32 *)(m + MOBY_POS_Z) = *(volatile f32 *)((u8 *)moby + MOBY_POS_Z);
    yaw = MOBY_YAW(m) + STAND_IN_TURN;
    if (yaw > PI_F) {
        yaw -= 2.0f * PI_F;
    }
    MOBY_YAW(m) = yaw;
    if (MOBY_SCALE_F(moby) != 0.0f) {
        host_scale[i] = MOBY_SCALE_F(moby);
        MOBY_SCALE_F(moby) = 0.0f;
    }
}

/* A Gold Bolt spot's update. With an infobot for its item, the spot takes the touch quietly and shows a stand-in
 * (GOLD_CLASS above). */
static void gold_spot_update(void *moby, UpdateFn update, s32 L)
{
    s32 spot = *(volatile s32 *)MOBY_PVARS(moby);
    s32 check = rz_check_for_bolt(L, spot);
    if (check < 0 || !is_infobot(ee_rz_layout_item(check)) || spot < 0 || spot >= GOLD_SPOTS) {
        update(moby);
        return;
    }
    *(volatile s32 *)(MOBY_PVARS(moby) + GOLD_QUIET) = 1;
    update(moby);
    carry_stand_in(spot, moby, L);
}

/* The world pickups: an item lying in a level, which on its touch starts a cutscene showing the item (and, for the
 * Trespasser and the Hydrodisplacer, Ratchet's hold-up after it), then gives it, saves and deletes itself. With an
 * infobot for its check, the touch is taken here, before the pickup's update can see it: the check collected (the
 * infobot's banner), the pickup's mission trigger done as its own touch does, the autosave, the pickup and its
 * stand-in deleted; until then an infobot stands in for the pickup as for a Gold Bolt (GOLD_CLASS above). The test
 * is the pickup's own (its update's distance to the hero,
 * from its idle state 1; the Raritanium has no states), a little wider so that it always comes first. The Map-o-
 * Matic's giver is a character (WatcherUpdate), not a pickup. Radii from each update's disassembly (2026-10-25). */
struct PickupSite {
    u8 level;
    u8 item;                                /* the vanilla item, which names the check */
    u16 class_id;
    f32 radius;
    u8 stateless;
};
static const struct PickupSite PICKUPS[] = {
    { 2, RZ_TRESPASSER, 1005, 1.0f, 0 },    /* Moby1005Update */
    { 4, RZ_SUCK_CANNON, 1120, 3.0f, 0 },   /* func_L04_002E2DF0 */
    { 6, RZ_HYDRODISPLACER, 1016, 1.0f, 0 }, /* Moby1005Update */
    { 9, RZ_PILOTS_HELMET, 1290, 3.0f, 0 }, /* Moby1290Update */
    { 10, RZ_MAGNEBOOTS, 18, 5.0f, 0 },     /* SpinningPickupUpdate */
    { 12, RZ_RARITANIUM, 37, 2.0f, 1 },     /* Moby37Update */
    { 14, RZ_MORPH_O_RAY, 1354, 1.0f, 0 },  /* CollectiblePickupUpdate */
    { 15, RZ_BOLT_GRABBER, 1388, 3.0f, 0 }, /* Moby1388Update */
    { 17, RZ_CODEBOT, 1428, 3.0f, 0 },      /* Moby1428Update */
};
#define PICKUP_SITES    (sizeof(PICKUPS) / sizeof(PICKUPS[0]))
#define PICKUP_IDLE     1
#define PICKUP_SLACK    0.5f                /* the touch test's margin over the pickup's own */
#define PICKUP_HEIGHT   2.5f                /* the most height between Ratchet and the pickup (theirs: 2) */
#define HERO_POS        ((volatile f32 *)(HERO_BASE + 0x80))
#define MOBY_MISSION(m) (*(volatile u8 *)((u8 *)(m) + 0xB0))
DECLARE_LVL(func_L00_002512D8);         /* SetLevelTriggerDone(mission) */
typedef void (*MissionFn)(s32);

/* MOBY's check if it is level L's world pickup and its check gives an infobot not collected yet; -1 otherwise. */
static s32 pickup_check(void *moby, s32 L, const struct PickupSite **site)
{
    u32 i;
    for (i = 0; i < PICKUP_SITES; i++) {
        if (PICKUPS[i].level == L && PICKUPS[i].class_id == (u16)MOBY_CLASS(moby)) {
            s32 check = rz_check_for_vanilla(PICKUPS[i].item);
            if (check >= 0 && is_infobot(ee_rz_layout_item(check)) && !ee_rz_check_collected(check)) {
                *site = &PICKUPS[i];
                return check;
            }
            return -1;
        }
    }
    return -1;
}

static s32 pickup_touched(void *moby, const struct PickupSite *site)
{
    volatile f32 *pos = (volatile f32 *)((u8 *)moby + MOBY_POS);
    f32 dx = HERO_POS[0] - pos[0];
    f32 dy = HERO_POS[1] - pos[1];
    f32 dz = HERO_POS[2] - pos[2];
    f32 r = site->radius + PICKUP_SLACK;
    if (!site->stateless && MOBY_STATE(moby) != PICKUP_IDLE) {
        return 0;
    }
    return dx * dx + dy * dy < r * r && dz < PICKUP_HEIGHT && -dz < PICKUP_HEIGHT && HERO_HEALTH > 0;
}

static void take_pickup(void *moby, s32 check, s32 L)
{
    if (MOBY_MISSION(moby) != 0xFF) {
        LVL(L, func_L00_002512D8, MissionFn)(MOBY_MISSION(moby));
    }
    collect(check, L);
    ee_rz_save_c(0, -1, L);
    drop_stand_in(PICKUP_STAND_IN, moby, L);
    LVL(L, func_0020D678, DeleteFn)(moby);
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
    s32 pickup = 0;
    if (L != active_level) {                /* an update before the level's init hook has run */
        read_record(L);
    }
    if (L != fixes_level) {
        fixes_level = L;
        forget_stand_ins();
        if (active) {
            o2_early_fixes(L);
        }
    }
    if (active && MOBY_CLASS(moby) == GOLD_CLASS) {
        gold_spot_update(moby, update, L);
        return;
    }
    if (MOBY_CLASS(moby) == INFOBOT_CLASS && is_stand_in(moby)) {
        return;                             /* carried by its spot's or its pickup's update */
    }
    if (active) {
        const struct PickupSite *site;
        s32 check = pickup_check(moby, L, &site);
        if (check >= 0 && pickup_touched(moby, site)) {
            take_pickup(moby, check, L);
            return;
        }
        pickup = check >= 0;
    }
    if (!active || !class_listed(L, MOBY_CLASS(moby))) {
        update(moby);
        if (pickup) {
            carry_stand_in(PICKUP_STAND_IN, moby, L);
        }
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
    if (pickup) {
        carry_stand_in(PICKUP_STAND_IN, moby, L);
    }
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
