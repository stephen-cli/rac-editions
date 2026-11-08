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
 * the givers decide to stay or go), the bytes of INVENTORY, HAD, PLANETS and
 * the special items' FLAGS that name a check of the planet being played show
 * whether that check has been collected instead. A giver's own reward is
 * always a check of its planet; anything else it tests is a prerequisite and
 * keeps the truth: Skid's agent on Aridia trades the Sonic Summoner once the
 * Zoomerator is owned (his dialog tests its flag, process_menu_entry kind 4),
 * the Rilgar race's host and the Gadgetron CEO on Kalebo III test the
 * Hoverboard, the Zoomerator and the Hoverboard being checks of other planets.
 * The real arrays come back when the update returns, with any other byte the
 * giver changed kept. A grant inside the update puts the real arrays back for
 * its duration. Everything else (the Swingshot's targets, the Trespasser's
 * locks, the galactic map, the HelpDesk) updates outside those mobys and sees
 * the truth. The special items' givers write their flag under the view: a
 * byte that turned 1 is the check collected (the Rilgar race's host,
 * func_L05_00317F80, sets the Zoomerator's at the win and shows "You won a
 * Platinum Zoomerator!" right after, which becomes the Randomizer's banner).
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
DECLARE_LVL(func_0020D678);             /* DeleteMoby(moby) */
DECLARE_LVL(func_L00_0023D9C0);         /* InitLevelResidentState: a level's load, a reload of the same level too */
typedef void (*GiveFn)(s32, s32);
typedef void (*PlanetFn)(s32);
typedef s32 (*SaveFn)(s32, s32);
typedef s32 (*CountFn)(void);
typedef void (*UpdateFn)(void *);
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
static s32 view_on;                     /* the collected view is in place, */
static s32 view_level;                  /* for the checks of this level */
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

/* The Nanotech vendor (NanotechVendorUpdate, func_L10_002E9718) sets the upgrade's flag at the purchase, and on a
 * later update autosaves and shows "Premium Nanotech acquired" (or the Ultra's) through ShowBanner, which would
 * replace the banner of the item its check gave. That ShowBanner call is hooked too (tools/gen_randomizer.py,
 * FLAG_BANNER_FUNCTIONS) and shows the Randomizer's banner for the check instead. */
#define TEXT_PREMIUM_BOUGHT 0x271C
#define TEXT_ULTRA_BOUGHT   0x271D
#define TEXT_HYDRODISPLACER_FOUND 0x177C  /* Blarg Station's "You've found the Hydrodisplacer!" (UpdateMoby_1028) */
static s32 nanotech_check = -1;         /* the Nanotech check bought, its banner still due; -1 none */

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

/* Whether the view replaces the byte of CHECK in level L: a check of that planet (-1, no check, is none). */
static s32 in_view(s32 check, s32 L)
{
    const struct RzCheck *row = rz_check(check);
    return row != 0 && row->planet == L;
}

static void view_apply(s32 L)
{
    s32 i, check;
    if (view_on) {
        return;
    }
    view_level = L;
    for (i = 0; i < ITEMS; i++) {
        real_inventory[i] = INVENTORY[i];
        real_had[i] = HAD[i];
        check = check_of_item_id(i);
        if (in_view(check, L)) {
            INVENTORY[i] = ee_rz_check_collected(check) ? 1 : 0;
            HAD[i] = INVENTORY[i];
        }
    }
    for (i = 0; i < PLANET_COUNT; i++) {
        real_planets[i] = PLANETS[i];
        check = check_of_planet(i);
        if (in_view(check, L)) {
            PLANETS[i] = ee_rz_check_collected(check) ? 1 : 0;
        }
    }
    for (i = 0; i < FLAG_BYTES; i++) {
        real_flags[i] = FLAGS[i];
        check = check_of_flag(i);
        if (in_view(check, L)) {
            FLAGS[i] = ee_rz_check_collected(check) ? 1 : 0;
        }
    }
    view_on = 1;
}

static void receive(s32 check, s32 item, s32 L);

/* Under the view: a special item's byte turned 1 and its check is not collected yet. */
static s32 flag_turned(void)
{
    s32 i, check;
    for (i = 0; i < FLAG_BYTES; i++) {
        check = check_of_flag(i);
        if (in_view(check, view_level) && FLAGS[i] && !ee_rz_check_collected(check)) {
            return 1;
        }
    }
    return 0;
}

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
        if (!in_view(check_of_item_id(i), view_level)) {
            real_inventory[i] = INVENTORY[i];
            real_had[i] = HAD[i];
        }
        INVENTORY[i] = real_inventory[i];
        HAD[i] = real_had[i];
    }
    for (i = 0; i < PLANET_COUNT; i++) {
        if (!in_view(check_of_planet(i), view_level)) {
            real_planets[i] = PLANETS[i];
        }
        PLANETS[i] = real_planets[i];
    }
    for (i = 0; i < FLAG_BYTES; i++) {
        check = check_of_flag(i);
        if (!in_view(check, view_level)) {
            real_flags[i] = FLAGS[i];
            FLAGS[i] = real_flags[i];
        } else if (FLAGS[i] && !ee_rz_check_collected(check)) {
            ee_rz_set_collected(check);
            FLAGS[i] = real_flags[i];
            if (i == FLAG_PREMIUM || i == FLAG_ULTRA) {
                set_health_maximum();
                nanotech_check = check;
            }
            receive(check, ee_rz_layout_item(check), L);
            real_flags[i] = FLAGS[i];
        } else {
            FLAGS[i] = real_flags[i];
        }
    }
}

/* --- receiving ------------------------------------------------------------------------------------------------- */

/* What Ratchet wears on his head: GiveItem(item, 1) puts a helmet there (the O2 Mask, the Pilot's Helmet). The
 * Pilot's Helmet pickup on Gaspar (Moby1290Update, func_L00_002E1850) also writes the Pilot's Helmet there itself
 * after its GiveItem, whatever that gave, so after a giver's update the slot goes back to what the grant left
 * unless the helmet is really owned. */
#define HEAD_GEAR       (*(volatile s32 *)0x00141510)
#define PILOTS_HELMET_ID 7
static s32 head_gear;                   /* the slot before the giver's update, or as the last grant left it */

/* ITEM, given for CHECK, with its banner. GiveItem's and UnlockPlanet's own banners are ours while pending_item is
 * set (ee_rz_item_banner_c, ee_rz_planet_banner_c); UnlockPlanet shows none for a planet already unlocked (the
 * Randomizer's banner comes instead). */
static void receive(s32 check, s32 item, s32 L)
{
    s32 was_on = view_on;
    s32 flag = flag_of_item(item);
    if (was_on) {
        view_restore(L);
    }
    pending_check = check;
    if (item >= RZ_INFOBOT_NOVALIS && item <= RZ_INFOBOT_VELDIN) {
        s32 planet = item - RZ_INFOBOT_NOVALIS + 1;
        s32 silent = PLANETS[planet] || planet == CURRENT_LEVEL;    /* UnlockPlanet shows no banner for these */
        pending_item = item;
        LVL(L, func_L00_00261848, PlanetFn)(planet);
        pending_item = RZ_ITEM_NONE;
        if (silent) {
            rz_show_item_banner(check, item, L);
        }
    } else if (item == RZ_GOLD_BOLT) {
        ee_rz_add_gold();
        rz_show_item_banner(check, item, L);
    } else if (item == RZ_CHARGE_BOOTS) {
        /* The boots are the cheat bytes, which are saved. A build without the boots (a seed made with them,
         * loaded there) pays a Bolt Cache instead and still sets the bytes, so a build with the boots gives
         * them to the save when it loads it; the check is collected either way and never pays twice. */
        *(volatile u8 *)EE_CHEAT_CHARGE_BOOTS = 1;
        CHEAT_SEEN = 1;
        if (!EE_HAS(EE_FEATURE_CHARGE_BOOTS)) {
            ee_add_bolts(BOLT_CACHE, L);
        }
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
    head_gear = HEAD_GEAR;
    if (was_on) {
        view_apply(L);
    }
}

/* CHECK collected now: its item, once. */
void rz_play_collect(s32 check, s32 L)
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
    rz_play_collect(check, L);
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
    rz_play_collect(check, L);
}

/* ShowBanner(text, ticks) inside GiveItem, and in a giver's own function (Al's "You got the Heli-Pack upgrade!"
 * right after his GiveItem, the Rilgar race's "You won a Platinum Zoomerator!" right after its flag, the Nanotech
 * vendor's after its autosave, Blarg Station's Hydrodisplacer sequence after its scene): the Randomizer's banner
 * for the item it is giving, or gave this frame, or the special item's check its giver has just set the flag of,
 * the Nanotech check bought, or the check of Blarg Station's Hydrodisplacer. */
void ee_rz_item_banner_c(s32 text, s32 ticks, s32 L)
{
    if (active && pending_item != RZ_ITEM_NONE) {
        pending_banner_shown = 1;
        rz_show_item_banner(pending_check, pending_item, L);
        return;
    }
    if (active && view_on && flag_turned()) {
        view_restore(L);                    /* collects the special item's check, with the Randomizer's banner */
        view_apply(L);
        return;
    }
    if (active && nanotech_check >= 0 && (text == TEXT_PREMIUM_BOUGHT || text == TEXT_ULTRA_BOUGHT)) {
        rz_show_item_banner(nanotech_check, ee_rz_layout_item(nanotech_check), L);
        nanotech_check = -1;
        return;
    }
    if (active && banner_due) {
        banner_due = 0;
        rz_show_item_banner(last_check, last_item, L);
        return;
    }
    if (active && text == TEXT_HYDRODISPLACER_FOUND) {
        s32 check = check_of_item_id(rz_item_game_id(RZ_HYDRODISPLACER));
        if (check >= 0) {
            rz_show_item_banner(check, ee_rz_layout_item(check), L);
            return;
        }
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
 * ee_page19_autosave (src/randomizer_save.c) does, from the next gameplay frame, and a page changed since the
 * last autosave (x1's death counts) is due once this one is done. */
s32 ee_rz_save_c(s32 slot, s32 flags, s32 L)
{
    s32 was_on = view_on;
    s32 result;
    if (active) {
        s32 spot;
        rz_world_before_save(L);            /* a live stand-in bolt's borrowed flag, before the spots are read */
        for (spot = 0; spot < GOLD_SPOTS; spot++) {
            s32 check = rz_check_for_bolt(CURRENT_LEVEL, spot);
            s32 taken = GOLD_FLAGS[CURRENT_LEVEL * GOLD_SPOTS + spot] != 0;
            if (check >= 0 && taken && !ee_rz_check_collected(check)) {
                rz_play_collect(check, L);
            }
        }
    }
    if (was_on) {
        view_restore(L);
    }
    result = LVL(L, func_0020BFC8, SaveFn)(slot, flags);
    if (was_on) {
        view_apply(L);
    }
    ee_page19_game_saved();
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
 * once per visit, at the level's first moby update, before any gate's own first update reads them. Orxon's landing
 * site also holds Clank (class 87, uid 9), who stands there for the visit retail plays as Clank and would stand
 * beside Ratchet, who carries him; he is deleted too. So is the gate into the room of Pokitaru's infobot (class
 * 353, uid 800): in retail the last gadgebot lock (uid 261, ten gadgebots) mans a turret (class 1346,
 * LinkedTurretUpdate) that shoots the Blarg ships and a wall, then opens that gate; the ships and the wall can
 * stay, and Ratchet leaves the room another way. The search covers every moby the level placed (the table
 * from MOBY_TABLE to MOBYS_PLACED_END), not the update list: a moby joins that list at its first update, and the
 * gadgebot door and Clank, too far from the landing site, had not had one yet.
 *
 * The Clank section's lab doors (class 1015, HeightGlideUpdate, func_L10_002DA468) open when a gadgebot lock
 * (class 1302, func_L06_00309170) or a pressure button (class 1031, func_L10_002DA678) moves them from state 2
 * (shut) to 3 (rising; 4 open). Both count as done once their level trigger is: a button every frame, a lock once,
 * at its second update, for the doors that are in state 2 by then. A door starts in state 0, reaching 2 at its
 * own first update, and the door behind lock 66 (uid 1) had not had it when its lock fired: it stayed shut for
 * good, and the Magneboots and Pokitaru's infobot behind it out of Ratchet's reach. So with the mask early every
 * lab door in state 2 is opened (open_lab_door). */
#define LAB_DOOR_CLASS  1015
#define DOOR_SHUT       2
#define DOOR_RISING     3

static void delete_mobys_with_uid(s32 uid, s32 L)
{
    u32 moby;
    for (moby = MOBY_TABLE(L); moby < MOBYS_PLACED_END(L); moby += MOBY_SIZE) {
        if (MOBY_STATE(moby) >= 0 && MOBY_UID(moby) == uid) {
            LVL(L, func_0020D678, DeleteFn)((void *)moby);
        }
    }
}

/* The O2 Mask is owned: Ratchet plays Orxon, and Blarg Station's Clank side is done. */
static s32 o2_early(void)
{
    return rz_play_owned(rz_item_game_id(RZ_O2_MASK));
}

/* Before a moby's update in level L: a shut lab door of Orxon's Clank section opens. */
static void open_lab_door(void *moby, s32 L)
{
    if (L == ORXON && MOBY_CLASS(moby) == LAB_DOOR_CLASS && MOBY_STATE(moby) == DOOR_SHUT && o2_early()) {
        MOBY_STATE(moby) = DOOR_RISING;
    }
}

static void o2_early_fixes(s32 L)
{
    static const u16 ORXON_REMOVED[6] = { 83, 673, 812, 813, 9, 800 };  /* the gates, Clank, the infobot room's gate */
    s32 i;
    if (!o2_early()) {
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
        for (i = 0; i < 6; i++) {
            delete_mobys_with_uid(ORXON_REMOVED[i], L);
        }
    }
}

/* Umbris's end. Its scene controller (class 436, func_L07_002F6F20) waits in state 8 for the Snagglebeast dead and
 * Ratchet in the hangar's volume (cuboid 29, which reaches some 13 units before the infobot), then hides the
 * infobot, plays two scenes and a movie, unlocks Batalia itself (UnlockPlanet(8), the check's giver), saves and
 * flies Ratchet to Batalia (func_0022F4A0). With the check holding another item, the item was given before Ratchet
 * reached it and the flight went to Batalia regardless. So the controller is held in state 8, and the hangar's
 * infobot is taken as a world infobot (src/randomizer_world.c). Retail lets Ratchet out of the pit and into the
 * hangar only on a later visit: the exit lift (class 1474) is placed only by a load with the boss's trigger 2 done
 * (its record's flags: 0x2 and not 0x1), and it and the hangar door (class 1128) read the level's triggers as the
 * level loaded them (mission_state), the lift showing and the door shutting for good once trigger 2 is done. So the
 * lift's record is placed at every load (ee_rz_place_mobys_c: flag 0x1, placed while the trigger is open too), the
 * lift hiding itself until its copy of trigger 2 is done; with the boss beaten, that copy is set at once, so it
 * appears and carries Ratchet up. The door's force-open word (pvars + 0x3C) makes it open after the boss, then and
 * on every later visit, so the infobot stays reachable; the bridge to the hangar (class 1129) extends without
 * waiting for the boss's state (its target, pvars + 0xB0, -1), as on a later visit the boss is gone. Retail's end
 * plays only for Batalia's own infobot. The two teleporter pads (class 1135), one at the top of the lift and one by
 * the ship, stay off until Ratchet stands in their cuboid 14 on the lift's landing (the arena's intro volume too).
 * With the boss beaten, a pad whose save bit is clear is switched on as that cuboid would: its save bit, so the pads
 * are on at every later visit, then the pad and its glow shown. (A pad's switching on also wakes its partner, state
 * 1 but hidden and unsaved, so the test is the bit's.) */
DECLARE_LVL_DATA(mission_state);        /* the level's triggers as the level loaded them */
DECLARE_LVL(func_L01_00309F00);         /* the teleporter pad's switching on: its save bit */
DECLARE_LVL(func_L01_0030AC10);         /* the teleporter pad and its glow shown */
typedef void (*PadFn)(void *);
#define UMBRIS              7
#define BATALIA_INFOBOT     (RZ_INFOBOT_NOVALIS + 8 - 1)
#define UMBRIS_SCENES_CLASS 436
#define UMBRIS_SCENES_END   8           /* waiting for the boss dead and Ratchet at the hangar */
#define UMBRIS_LIFT_CLASS   1474
#define UMBRIS_DOOR_CLASS   1128
#define UMBRIS_DOOR_FORCE   0x3C        /* its pvars: nonzero, opens once trigger 2 is done instead of shutting */
#define UMBRIS_BRIDGE_CLASS 1129
#define UMBRIS_BRIDGE_TARGET 0xB0       /* its pvars: the moby whose state extends it, -1 none */
#define UMBRIS_PAD_CLASS    1135
#define PAD_ON              1           /* its states: waiting for Ratchet */
#define PAD_OFF             5           /* hidden until Ratchet stands in its cuboid */
#define SNAGGLEBEAST_TRIGGER 2
#define UMBRIS_LIFT_UID     458
DECLARE_LVL_DATA(level_core);           /* the level's core data: the gameplay data at +4 */
DECLARE_LVL(func_001F99B0);             /* fill_transfer_words(words, value, count) */
typedef void (*FillFn)(void *, u32, s32);
#define GAMEPLAY_MOBYS      0x44        /* the gameplay data's offset of its moby records */
#define MOBYS_HEAD          0x10        /* their count at +0, the first record after */
#define RECORD_MISSION      1           /* a record's words: its size at 0, then its mission, */
#define RECORD_FLAGS        2           /* its flags (0x1 placed while the mission is open, 0x2 once done), */
#define RECORD_UID          3           /* its uid */
#define PLACE_WHILE_OPEN    0x1

/* Whether Umbris's end in level L is the Randomizer's: the Batalia infobot's check holds another item. */
static s32 umbris_end_moved(s32 L)
{
    s32 check = rz_check_for_vanilla(BATALIA_INFOBOT);
    return active && L == UMBRIS && check >= 0 && ee_rz_layout_item(check) != BATALIA_INFOBOT;
}

/* InitLevelRenderGlobals's clearing of the moby table of level L, right before it places the level's mobys from
 * their records (fill_transfer_words(table, value, count) at + 0xF18): Umbris's exit lift's record is placed while
 * the boss's trigger is open too, when its end is the Randomizer's. */
void ee_rz_place_mobys_c(void *table, u32 value, s32 count, s32 L)
{
    if (umbris_end_moved(L)) {
        u8 *gameplay = *(u8 **)(LVL_DATA(L, level_core, u8 *) + 4);
        u8 *records = gameplay + *(u32 *)(gameplay + GAMEPLAY_MOBYS);
        s32 records_count = *(s32 *)records;
        u32 *record = (u32 *)(records + MOBYS_HEAD);
        s32 i;
        for (i = 0; i < records_count; i++) {
            if (record[RECORD_UID] == UMBRIS_LIFT_UID && record[RECORD_MISSION] == SNAGGLEBEAST_TRIGGER) {
                record[RECORD_FLAGS] |= PLACE_WHILE_OPEN;
                break;
            }
            record = (u32 *)((u8 *)record + (record[0] & ~3u));
        }
    }
    LVL(L, func_001F99B0, FillFn)(table, value, count);
}

/* Whether a moby's save bit in level L is set. */
static s32 moby_saved(void *moby, s32 L)
{
    u32 uid = MOBY_UID(moby);
    return (LEVEL_BITS[L * 256 + (uid >> 3)] >> (uid & 7)) & 1;
}

/* Before a moby's update in level L: 1 when it must not run (Umbris's scene controller at its end). */
static s32 umbris_end(void *moby, s32 L)
{
    s32 boss_beaten;
    if (!umbris_end_moved(L)) {
        return 0;
    }
    boss_beaten = LEVEL_TRIGGERS[UMBRIS * 16 + SNAGGLEBEAST_TRIGGER] == 0xFF;
    switch (MOBY_CLASS(moby)) {
    case UMBRIS_SCENES_CLASS:
        return MOBY_STATE(moby) == UMBRIS_SCENES_END;
    case UMBRIS_LIFT_CLASS:
        if (boss_beaten) {
            LVL_DATA(L, mission_state, volatile u8 *)[SNAGGLEBEAST_TRIGGER] = 0xFF;
        }
        return 0;
    case UMBRIS_DOOR_CLASS:
        *(volatile s32 *)(MOBY_PVARS(moby) + UMBRIS_DOOR_FORCE) = 1;
        return 0;
    case UMBRIS_BRIDGE_CLASS:
        if (boss_beaten) {
            *(volatile s32 *)(MOBY_PVARS(moby) + UMBRIS_BRIDGE_TARGET) = -1;
        }
        return 0;
    case UMBRIS_PAD_CLASS:
        if (boss_beaten && MOBY_STATE(moby) != 0 && !moby_saved(moby, L)) {
            LVL(L, func_L01_00309F00, PadFn)(moby);
            LVL(L, func_L01_0030AC10, PadFn)(moby);
            if (MOBY_STATE(moby) == PAD_OFF) {
                MOBY_STATE(moby) = PAD_ON;
            }
        }
        return 0;
    }
    return 0;
}

/* --- the Missions page and the map ------------------------------------------------------------------------------ */

/* The pause menu's Missions page and map show a planet's missions (tools/gen_randomizer.py describes the tables).
 * Mission_UpdateObjectives (func_0020C7A0) marks each one hidden, open or done from its two conditions, which test
 * the planets unlocked, the items owned and the special items' flags, and a marker shows on the map while its
 * mission is open. With the rewards shuffled those are the wrong questions, as they are for the givers: "Buy
 * Heli-Pack from Al" would tick when a Heli-Pack came from elsewhere, and stay open after Al's check gave
 * something else. So the update runs under the collected view of the planet shown, as a giver's update does: a
 * condition naming one of that planet's checks' retail rewards reads whether the check is collected, and one
 * naming another planet's (Aridia's "Use your Swingshot", a prerequisite) keeps the truth. The predicates of the
 * type-7 conditions read the same bytes. EnterMenuMode, DrawMapScreen, MissionMenu_Init, LevelSelectMenu_Update and
 * initialize_pause_menu call the update; the statuses are worked out afresh each time and never saved. */
DECLARE_LVL(func_0020C7A0);             /* Mission_UpdateObjectives() */
DECLARE_LVL_DATA(map_state);            /* D_001A01F0 in the executable */
#define MAP_PLANET(L)   (LVL_DATA(L, map_state, volatile s32 *)[0x224 / 4])

/* For the PINE test scripts: updates; updates under the view; the planet of the last; the view already on. */
volatile u32 ee_debug_rz_mission[4];

s32 rz_play_map_planet(s32 L)
{
    return MAP_PLANET(L);
}

/* The check whose retail reward SOURCE names; -1 for none. */
static s32 check_of_source(const struct RzSource *source)
{
    switch (source->kind) {
    case RZ_SOURCE_PLANET:
        return check_of_planet(source->arg);
    case RZ_SOURCE_ITEM:
        return source->arg < ITEMS ? check_of_item_id(source->arg) : -1;
    case RZ_SOURCE_FLAG:
        return source->arg < FLAG_BYTES ? check_of_flag(source->arg) : -1;
    default:
        return -1;
    }
}

s32 rz_play_hidden_check(const struct RzSource *source, s32 planet)
{
    s32 check = check_of_source(source);
    return in_view(check, planet) && !ee_rz_check_collected(check) ? check : -1;
}

/* Mission_UpdateObjectives in level L's program. */
s32 ee_rz_mission_update_c(s32 L)
{
    s32 planet = MAP_PLANET(L);
    s32 result;
    ee_debug_rz_mission[0]++;
    ee_debug_rz_mission[2] = (u32)planet;
    if (!active || view_on) {               /* the view is on only inside a moby update, which no menu runs in */
        if (view_on) {
            ee_debug_rz_mission[3]++;
        }
        return LVL(L, func_0020C7A0, CountFn)();
    }
    ee_debug_rz_mission[1]++;
    view_apply(planet);
    result = LVL(L, func_0020C7A0, CountFn)();
    view_restore(L);
    return result;
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
    nanotech_check = -1;
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
static void start_helga_lift(s32 lift_index, s32 L)
{
    u8 *lift = (u8 *)MOBY_TABLE(L) + MOBY_SIZE * lift_index;
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
        rz_world_forget();
        if (active) {
            o2_early_fixes(L);
        }
    }
    if (active) {
        open_lab_door(moby, L);
        if (umbris_end(moby, L) || rz_world_before(moby, update, L)) {
            return;
        }
    }
    if (!active || !class_listed(L, MOBY_CLASS(moby))) {
        update(moby);
        if (active) {
            rz_world_after(moby, L);
        }
        return;
    }
    if (L == KERWAN && MOBY_CLASS(moby) == HELGA_CLASS) {
        helga_lift = *(volatile s32 *)(MOBY_PVARS(moby) + HELGA_LIFT);    /* read first: her update deletes her */
    }
    ee_debug_rz_play[1]++;
    granted = -1;
    head_gear = HEAD_GEAR;
    view_apply(L);
    update(moby);
    view_restore(L);
    if (HEAD_GEAR == PILOTS_HELMET_ID && !INVENTORY[PILOTS_HELMET_ID]) {
        HEAD_GEAR = head_gear;
    }
    rz_world_after(moby, L);
    if (helga_lift >= 0 && granted == rz_check_for_vanilla(RZ_SWINGSHOT) && !INVENTORY[SWINGSHOT_ID]) {
        start_helga_lift(helga_lift, L);
    }
    granted = -1;
}

/* Each frame of level L's program in any game mode (src/strafe.c's pad hook): the stand-ins' unpacked models let
 * go when the galactic map opens (src/randomizer_world.c). Title-screen programs (19, 20) have no stand-ins. */
void ee_rz_any_frame(s32 L)
{
    if (L >= 0 && L < LEVELS) {
        rz_world_any_frame(L);
    }
}

/* Each gameplay frame (src/charge_dash.c's frame hook): the save's twentieth page to the card when due (the
 * record's changes, x1's death counts; any save, Randomizer or not), and the record read again when the level
 * changes and now and then. No gameplay frame runs while the vendor's menu is up, so a frame closes it. */
void ee_rz_frame(s32 L)
{
    static s32 frames_left;
    banner_due = 0;
    rz_vendor_menu_closed();
    rz_world_frame(L);
    ee_page19_autosave(L);
    if (L != active_level || --frames_left <= 0) {
        frames_left = REFRESH_FRAMES;
        read_record(L);
        suppress_planet_banner = 0;
        rz_vendor_forget_purchase();
        view_on = 0;
    }
}
