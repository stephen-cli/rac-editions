/* rz1: the randomizer's logic (src/randomizer.h).
 *
 * The fill is the assumed fill Archipelago itself uses (fill_restrictive),
 * which is what Panda291's rules were tested with: the progression items
 * are placed one at a time in random order, each into a check reachable
 * with the items still to be placed plus what the start gives, as if the
 * player already had all of them; so every item placed is reachable without
 * itself, and the layout never needs an item from behind its own lock. The
 * other items (the weapons and gadgets nothing needs, the Gold Bolts, the
 * Charge Boots) then scatter over the checks left. A sweep from the start
 * collecting everything reachable confirms every check and Drek's rule;
 * should it fail, the fill runs again from the next state of the seed's
 * generator, so a seed always gives the same layout.
 *
 * Rules are bit tests over one u64 of logical items: a check needs every
 * bit of `all` (its planet's infobot included) and one bit of each `any`
 * group. The groups are Panda291's ORs: improved jump is Heli-Pack or
 * Thruster-Pack, an explosive is the Bomb Glove or the Devastator, long
 * range is the Blaster, the Devastator, the Visibomb or the RYNO. Vendor
 * entries over 7,500 bolts take the Metal Detector as a stand-in for
 * having the bolts, as his do. */
#include "randomizer.h"

#define BIT(item)       ((u64)1 << (item))
#define P(planet)       BIT(RZ_INFOBOT_NOVALIS + (planet) - 1)

#define JUMP            (BIT(RZ_HELI_PACK) | BIT(RZ_THRUSTER_PACK))
#define EXPLOSIVE       (BIT(RZ_BOMB_GLOVE) | BIT(RZ_DEVASTATOR))
#define LONG_RANGE      (BIT(RZ_BLASTER) | BIT(RZ_DEVASTATOR) | BIT(RZ_VISIBOMB) | BIT(RZ_RYNO))
#define HELI            BIT(RZ_HELI_PACK)
#define THRUSTER        BIT(RZ_THRUSTER_PACK)
#define HYDRO           BIT(RZ_HYDRO_PACK)
#define SWING           BIT(RZ_SWINGSHOT)
#define GRIND           BIT(RZ_GRINDBOOTS)
#define MAGNET          BIT(RZ_MAGNEBOOTS)
#define DISPLACER       BIT(RZ_HYDRODISPLACER)
#define O2              BIT(RZ_O2_MASK)
#define TRESPASS        BIT(RZ_TRESPASSER)
#define DETECTOR        BIT(RZ_METAL_DETECTOR)
#define HELMET          BIT(RZ_PILOTS_HELMET)
#define HOLOGUISE       BIT(RZ_HOLOGUISE)
#define BOARD           BIT(RZ_HOVERBOARD)
/* Veldin's rule, shared by its checks and the goal (Logic.py veldin_global_rule). */
#define VELDIN_ALL      (P(18) | TRESPASS | MAGNET | DISPLACER | THRUSTER)

#define NOVALIS 1
#define ARIDIA 2
#define KERWAN 3
#define EUDORA 4
#define RILGAR 5
#define BLARG 6
#define UMBRIS 7
#define BATALIA 8
#define GASPAR 9
#define ORXON 10
#define POKITARU 11
#define HOVEN 12
#define GEMLIK 13
#define OLTANIS 14
#define QUARTU 15
#define KALEBO 16
#define FLEET 17
#define VELDIN 18

#define INFOBOT RZ_KIND_INFOBOT
#define GADGET  RZ_KIND_GADGET
#define VENDOR  RZ_KIND_VENDOR
#define BOLT    RZ_KIND_GOLD_BOLT
#define GOT     RZ_HOW_GOT                  /* how: 0 is found in the world */
#define WON     RZ_HOW_WON
#define GB      RZ_GOLD_BOLT
#define IB(planet) (RZ_INFOBOT_NOVALIS + (planet) - 1)

/* data/Locations.py, in its order (location ids 1..94), with Logic.py's rules. */
static const struct RzCheck checks[RZ_CHECKS_BASE] = {
    /* Novalis */
    { NOVALIS, INFOBOT, IB(ARIDIA), GOT, P(1), { 0, 0, 0 } },                     /* 1 the plumber's infobot */
    { NOVALIS, INFOBOT, IB(KERWAN), GOT, P(1), { 0, 0, 0 } },                     /* 2 the mayor's infobot */
    { NOVALIS, VENDOR, RZ_PYROCITOR, GOT, P(1), { 0, 0, 0 } },                    /* 3 vendor: Pyrocitor */
    { NOVALIS, BOLT, GB, 0, P(1), { 0, 0, 0 } },                                /* 4 sewer */
    { NOVALIS, BOLT, GB, 0, P(1), { EXPLOSIVE, 0, 0 } },                        /* 5 caves */
    { NOVALIS, BOLT, GB, 0, P(1) | HYDRO, { 0, 0, 0 } },                        /* 6 underwater caves */
    /* Aridia */
    { ARIDIA, GADGET, RZ_HOVERBOARD, GOT, P(2), { 0, 0, 0 } },                    /* 7 save Skid McMarx */
    { ARIDIA, GADGET, RZ_TRESPASSER, 0, P(2) | SWING, { 0, 0, 0 } },            /* 8 find the Trespasser */
    { ARIDIA, GADGET, RZ_SONIC_SUMMONER, GOT, P(2) | BIT(RZ_ZOOMERATOR), { 0, 0, 0 } },  /* 9 trade the Zoomerator */
    { ARIDIA, BOLT, GB, 0, P(2) | SWING, { 0, 0, 0 } },                         /* 10 below the Trespasser */
    { ARIDIA, BOLT, GB, 0, P(2), { JUMP, 0, 0 } },                              /* 11 the island */
    { ARIDIA, BOLT, GB, 0, P(2) | MAGNET, { 0, 0, 0 } },                        /* 12 by the mortar turret */
    { ARIDIA, BOLT, GB, 0, P(2), { EXPLOSIVE, 0, 0 } },                         /* 13 the sandshark area */
    /* Kerwan */
    { KERWAN, GADGET, RZ_SWINGSHOT, GOT, P(3), { 0, 0, 0 } },                     /* 14 Helga's Swingshot */
    { KERWAN, GADGET, RZ_HELI_PACK, GOT, P(3), { 0, 0, 0 } },                     /* 15 Al's Heli-Pack */
    { KERWAN, INFOBOT, IB(EUDORA), 0, P(3), { JUMP, 0, 0 } },                   /* 16 after the train */
    { KERWAN, VENDOR, RZ_BLASTER, GOT, P(3), { 0, 0, 0 } },                       /* 17 vendor: Blaster */
    { KERWAN, BOLT, GB, 0, P(3) | HELI, { 0, 0, 0 } },                          /* 18 below the ship */
    { KERWAN, BOLT, GB, 0, P(3), { JUMP, 0, 0 } },                              /* 19 the train station */
    { KERWAN, BOLT, GB, 0, P(3) | HELI, { 0, 0, 0 } },                          /* 20 the lone tower */
    /* Eudora */
    { EUDORA, INFOBOT, IB(BLARG), GOT, P(4) | SWING | TRESPASS, { JUMP, 0, 0 } },      /* 21 the henchman (UnlockPlanet(6)) */
    { EUDORA, GADGET, RZ_SUCK_CANNON, 0, P(4) | HELI, { 0, 0, 0 } },           /* 22 the Suck Cannon */
    { EUDORA, VENDOR, RZ_GLOVE_OF_DOOM, GOT, P(4) | DETECTOR, { 0, 0, 0 } },      /* 23 vendor: Glove of Doom */
    { EUDORA, BOLT, GB, 0, P(4) | HELI, { 0, 0, 0 } },                          /* 24 by the ship */
    /* Rilgar */
    { RILGAR, INFOBOT, IB(UMBRIS), GOT, P(5) | SWING | DISPLACER, { JUMP, 0, 0 } },    /* 25 the bouncer, Captain Qwark (UnlockPlanet(7)) */
    { RILGAR, GADGET, RZ_ZOOMERATOR, WON, P(5) | BOARD, { JUMP, 0, 0 } },        /* 26 the hoverboard race */
    { RILGAR, VENDOR, RZ_MINE_GLOVE, GOT, P(5) | DETECTOR, { 0, 0, 0 } },         /* 27 vendor: Mine Glove */
    { RILGAR, VENDOR, RZ_RYNO, GOT, P(5) | DETECTOR, { JUMP, 0, 0 } },            /* 28 the RYNO */
    { RILGAR, BOLT, GB, 0, P(5), { JUMP, 0, 0 } },                              /* 29 the maze */
    { RILGAR, BOLT, GB, 0, P(5) | SWING | DISPLACER | O2, { JUMP, 0, 0 } },     /* 30 the waterworks */
    /* Blarg Tactical Research Station */
    { BLARG, GADGET, RZ_HYDRODISPLACER, 0, P(6) | TRESPASS, { 0, 0, 0 } },     /* 31 as Clank */
    { BLARG, INFOBOT, IB(RILGAR), 0, P(6), { 0, 0, 0 } },                       /* 32 the exploding ship (UnlockPlanet(5)) */
    { BLARG, GADGET, RZ_GRINDBOOTS, GOT, P(6) | SWING, { 0, 0, 0 } },            /* 33 the scientist's Grindboots */
    { BLARG, VENDOR, RZ_TAUNTER, GOT, P(6), { 0, 0, 0 } },                        /* 34 vendor: Taunter */
    { BLARG, BOLT, GB, 0, P(6) | O2 | TRESPASS, { 0, 0, 0 } },                  /* 35 outside, as Ratchet */
    { BLARG, BOLT, GB, 0, P(6) | SWING, { 0, 0, 0 } },                          /* 36 the swarmer nest */
    /* Umbris */
    { UMBRIS, INFOBOT, IB(BATALIA), 0, P(7) | SWING | HELI | DISPLACER, { 0, 0, 0 } },   /* 37 the Snagglebeast */
    { UMBRIS, BOLT, GB, 0, P(7) | SWING | HELI, { 0, 0, 0 } },                  /* 38 the pressure plate puzzle */
    { UMBRIS, BOLT, GB, 0, P(7) | SWING | HELI | DISPLACER, { 0, 0, 0 } },      /* 39 jumping down onto it */
    /* Batalia */
    { BATALIA, VENDOR, RZ_DEVASTATOR, GOT, P(8) | DETECTOR, { 0, 0, 0 } },        /* 40 vendor: Devastator */
    { BATALIA, INFOBOT, IB(GASPAR), 0, P(8) | GRIND, { 0, 0, 0 } },             /* 41 after the grind rail */
    { BATALIA, INFOBOT, IB(ORXON), GOT, P(8), { 0, 0, 0 } },                      /* 42 the commander */
    { BATALIA, GADGET, RZ_METAL_DETECTOR, GOT, P(8) | MAGNET, { 0, 0, 0 } },      /* 43 the plumber's Metal Detector */
    { BATALIA, BOLT, GB, 0, P(8), { JUMP, 0, 0 } },                             /* 44 the cliffside */
    { BATALIA, BOLT, GB, 0, P(8), { 0, 0, 0 } },                                /* 45 above the Trespasser lock */
    /* Gaspar */
    { GASPAR, VENDOR, RZ_WALLOPER, GOT, P(9) | DETECTOR, { 0, 0, 0 } },           /* 46 vendor: Walloper */
    { GASPAR, GADGET, RZ_PILOTS_HELMET, 0, P(9), { 0, 0, 0 } },                 /* 47 the Pilot's Helmet */
    { GASPAR, BOLT, GB, 0, P(9) | SWING, { 0, 0, 0 } },                         /* 48 the Swingshot side path */
    { GASPAR, BOLT, GB, 0, P(9), { JUMP, 0, 0 } },                              /* 49 through the volcano */
    /* Orxon */
    { ORXON, VENDOR, RZ_VISIBOMB, GOT, P(10) | O2 | DETECTOR, { 0, 0, 0 } },      /* 50 vendor: Visibomb */
    { ORXON, INFOBOT, IB(POKITARU), 0, P(10), { 0, 0, 0 } },                    /* 51 the Clank section */
    { ORXON, INFOBOT, IB(HOVEN), 0, P(10) | O2 | HELI | SWING | MAGNET, { 0, 0, 0 } },  /* 52 as Ratchet */
    { ORXON, GADGET, RZ_MAGNEBOOTS, 0, P(10), { 0, 0, 0 } },                    /* 53 the Clank section's Magneboots */
    { ORXON, GADGET, RZ_PREMIUM_NANOTECH, GOT, P(10) | O2 | HELI, { 0, 0, 0 } },  /* 54 Premium Nanotech */
    { ORXON, GADGET, RZ_ULTRA_NANOTECH, GOT, P(10) | O2 | HELI | DETECTOR, { 0, 0, 0 } },  /* 55 Ultra Nanotech */
    { ORXON, BOLT, GB, 0, P(10) | O2, { 0, 0, 0 } },                            /* 56 the Clank section, as Ratchet */
    { ORXON, BOLT, GB, 0, P(10) | O2 | BIT(RZ_VISIBOMB) | HELI | SWING | MAGNET, { 0, 0, 0 } },  /* 57 with the Visibomb */
    /* Pokitaru */
    { POKITARU, VENDOR, RZ_DECOY_GLOVE, GOT, P(11) | DETECTOR, { 0, 0, 0 } },     /* 58 vendor: Decoy Glove */
    { POKITARU, GADGET, RZ_O2_MASK, GOT, P(11) | HELMET | THRUSTER, { 0, 0, 0 } },     /* 59 destroy the ships */
    { POKITARU, GADGET, RZ_PERSUADER, GOT, P(11) | BIT(RZ_RARITANIUM) | TRESPASS | DISPLACER, { 0, 0, 0 } },  /* 60 the sewer */
    { POKITARU, GADGET, RZ_THRUSTER_PACK, GOT, P(11), { 0, 0, 0 } },              /* 61 buy the Thruster-Pack */
    { POKITARU, BOLT, GB, 0, P(11) | SWING | THRUSTER, { 0, 0, 0 } },           /* 62 the waterfalls */
    /* Hoven */
    { HOVEN, VENDOR, RZ_DRONE_DEVICE, GOT, P(12) | DETECTOR, { 0, 0, 0 } },       /* 63 vendor: Drone Device */
    { HOVEN, INFOBOT, IB(GEMLIK), 0, P(12), { LONG_RANGE, JUMP, 0 } },          /* 64 the turret's ship */
    { HOVEN, GADGET, RZ_HYDRO_PACK, GOT, P(12) | DISPLACER, { 0, 0, 0 } },        /* 65 buy the Hydro-Pack */
    { HOVEN, GADGET, RZ_RARITANIUM, 0, P(12) | SWING, { JUMP, 0, 0 } },         /* 66 the Raritanium */
    { HOVEN, BOLT, GB, 0, P(12) | DISPLACER, { 0, 0, 0 } },                     /* 67 the Hydrodisplacer section */
    { HOVEN, BOLT, GB, 0, P(12), { 0, 0, 0 } },                                 /* 68 the moving wall jump */
    /* Gemlik Base */
    { GEMLIK, INFOBOT, IB(OLTANIS), 0, P(13) | MAGNET | TRESPASS | SWING, { JUMP, LONG_RANGE, 0 } },  /* 69 Captain Qwark */
    { GEMLIK, BOLT, GB, 0, P(13) | BIT(RZ_VISIBOMB) | TRESPASS, { JUMP, 0, 0 } },  /* 70 shoot the tower */
    /* Oltanis */
    { OLTANIS, VENDOR, RZ_TESLA_CLAW, GOT, P(14) | DETECTOR, { 0, 0, 0 } },       /* 71 vendor: Tesla Claw */
    { OLTANIS, INFOBOT, IB(QUARTU), 0, P(14) | GRIND, { 0, 0, 0 } },            /* 72 after the grind rail */
    { OLTANIS, GADGET, RZ_PDA, GOT, P(14) | MAGNET, { 0, 0, 0 } },                /* 73 Steve's PDA */
    { OLTANIS, GADGET, RZ_MORPH_O_RAY, 0, P(14) | SWING, { 0, 0, 0 } },         /* 74 the Morph-o-Ray */
    { OLTANIS, BOLT, GB, 0, P(14) | GRIND | SWING, { 0, 0, 0 } },               /* 75 along the main path */
    { OLTANIS, BOLT, GB, 0, P(14) | MAGNET, { 0, 0, 0 } },                      /* 76 down the ice on the Magneboot path */
    { OLTANIS, BOLT, GB, 0, P(14) | MAGNET, { 0, 0, 0 } },                      /* 77 the ledge on the Magneboot path */
    { OLTANIS, BOLT, GB, 0, P(14) | GRIND | SWING | MAGNET, { 0, 0, 0 } },      /* 78 after every objective */
    /* Quartu */
    { QUARTU, INFOBOT, IB(KALEBO), 0, P(15) | SWING, { 0, 0, 0 } },             /* 79 after the Giant Clank fight */
    { QUARTU, GADGET, RZ_BOLT_GRABBER, 0, P(15) | HYDRO | O2, { 0, 0, 0 } },    /* 80 after the underwater section */
    { QUARTU, INFOBOT, IB(FLEET), GOT, P(15) | HOLOGUISE | SWING | THRUSTER, { 0, 0, 0 } },  /* 81 Clank's mother */
    { QUARTU, BOLT, GB, 0, P(15) | HOLOGUISE | SWING | THRUSTER, { 0, 0, 0 } }, /* 82 behind Clank's mother */
    { QUARTU, BOLT, GB, 0, P(15) | BIT(RZ_CODEBOT) | SWING, { 0, 0, 0 } },      /* 83 behind the Codebot door */
    /* Kalebo III */
    { KALEBO, GADGET, RZ_HOLOGUISE, WON, P(16) | BOARD | SWING | GRIND, { 0, 0, 0 } },  /* 84 the hoverboard race */
    { KALEBO, GADGET, RZ_MAP_O_MATIC, 0, P(16) | GRIND, { 0, 0, 0 } },          /* 85 after the grind rail */
    { KALEBO, BOLT, GB, 0, P(16) | GRIND, { 0, 0, 0 } },                        /* 86 on the grind rail */
    { KALEBO, BOLT, GB, 0, P(16) | GRIND, { 0, 0, 0 } },                        /* 87 the employee break room */
    /* Drek's Fleet */
    { FLEET, INFOBOT, IB(VELDIN), 0, P(17) | MAGNET | HELMET | HOLOGUISE | SWING, { 0, 0, 0 } },  /* 88 the main ship */
    { FLEET, GADGET, RZ_CODEBOT, 0, P(17) | O2 | HYDRO, { 0, 0, 0 } },          /* 89 after the water section */
    { FLEET, BOLT, GB, 0, P(17) | O2 | HYDRO, { 0, 0, 0 } },                    /* 90 in the water section */
    { FLEET, BOLT, GB, 0, P(17) | MAGNET | HELMET | HOLOGUISE | SWING, { 0, 0, 0 } },  /* 91 the robot guards' side path */
    /* Veldin */
    { VELDIN, BOLT, GB, 0, VELDIN_ALL | BIT(RZ_TAUNTER), { 0, 0, 0 } },         /* 92 the Taunter and the horny toad */
    { VELDIN, BOLT, GB, 0, VELDIN_ALL, { 0, 0, 0 } },                           /* 93 the platforms */
    { VELDIN, BOLT, GB, 0, VELDIN_ALL | GRIND | SWING, { 0, 0, 0 } },           /* 94 after the short grind rail */
};

static const char *const item_names[RZ_ITEM_KINDS] = {
    "(nothing)",
    "Novalis infobot", "Aridia infobot", "Kerwan infobot", "Eudora infobot", "Rilgar infobot",
    "Blarg Station infobot", "Umbris infobot", "Batalia infobot", "Gaspar infobot", "Orxon infobot",
    "Pokitaru infobot", "Hoven infobot", "Gemlik Base infobot", "Oltanis infobot", "Quartu infobot",
    "Kalebo III infobot", "Drek's Fleet infobot", "Veldin infobot",
    "Heli-Pack", "Thruster-Pack", "Hydro-Pack", "Sonic Summoner", "O2 Mask", "Pilot's Helmet",
    "Taunter", "Hydrodisplacer", "Trespasser", "Metal Detector", "Magneboots", "Grindboots",
    "Hoverboard", "Hologuise", "PDA", "Map-o-Matic", "Bolt Grabber", "Persuader",
    "Zoomerator", "Raritanium", "Codebot", "Premium Nanotech", "Ultra Nanotech", "Swingshot",
    "Drone Device",
    "Suck Cannon", "Bomb Glove", "Devastator", "Visibomb Gun", "Blaster", "Pyrocitor",
    "Mine Glove", "Walloper", "Tesla Claw", "Glove of Doom", "Morph-o-Ray", "RYNO",
    "Decoy Glove",
    "Gold Bolt", "Charge Boots",
};

/* ItemPool.py get_classification: the items whose rules gate something. */
static const u8 progression[] = {
    RZ_HELI_PACK, RZ_THRUSTER_PACK, RZ_HYDRO_PACK, RZ_SWINGSHOT, RZ_MAGNEBOOTS, RZ_GRINDBOOTS,
    RZ_HYDRODISPLACER, RZ_TAUNTER, RZ_O2_MASK, RZ_PILOTS_HELMET, RZ_TRESPASSER, RZ_HOLOGUISE,
    RZ_CODEBOT, RZ_RARITANIUM, RZ_HOVERBOARD, RZ_ZOOMERATOR, RZ_BOMB_GLOVE, RZ_DEVASTATOR,
    RZ_VISIBOMB, RZ_METAL_DETECTOR,
};

/* The game's inventory ids (the item records, 0-36; src/goodies.c's Charge
 * Boots are 36) of the gadgets and weapons, by RzItem; -1 for the items the
 * game keeps as flags (Zoomerator, Raritanium, Codebot, the Nanotech). */
static const s8 game_ids[RZ_ITEM_KINDS] = {
    -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,        /* the infobots */
    2, 3, 4, 5, 6, 7, 14, 22, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,              /* Heli-Pack .. Persuader */
    -1, -1, -1, -1, -1,                                                            /* Zoomerator .. Ultra Nanotech */
    12, 24,                                                                        /* Swingshot, Drone Device */
    9, 10, 11, 13, 15, 16, 17, 18, 19, 20, 21, 23, 25,                             /* Suck Cannon .. Decoy Glove */
    -1, 36,                                                                        /* Gold Bolt; the Charge Boots are item 36 */
};

#define START_ITEMS     (P(NOVALIS) | BIT(RZ_BOMB_GLOVE))
#define MAX_ATTEMPTS    64

s32 rz_item_game_id(s32 item)
{
    return item > 0 && item < RZ_ITEM_KINDS ? game_ids[item] : -1;
}

s32 rz_item_of_game_id(s32 game_id)
{
    s32 item;
    for (item = 1; item < RZ_ITEM_KINDS; item++) {
        if (game_ids[item] == game_id && game_id >= 0) {
            return item;
        }
    }
    return RZ_ITEM_NONE;
}

s32 rz_check_for_vanilla(s32 item)
{
    s32 i;
    if (item <= RZ_ITEM_NONE || item == RZ_GOLD_BOLT) {
        return -1;
    }
    if (item == RZ_CHARGE_BOOTS) {
        return RZ_CHECKS_BASE;              /* the bonus vendor check, of a layout with the boots */
    }
    for (i = 0; i < RZ_CHECKS_BASE; i++) {
        if (checks[i].vanilla == item) {
            return i;
        }
    }
    return -1;
}

s32 rz_check_for_bolt(s32 level, s32 spot)
{
    s32 i, spots_seen = 0;
    for (i = 0; i < RZ_CHECKS_BASE; i++) {
        if (checks[i].kind == RZ_KIND_GOLD_BOLT && checks[i].planet == level) {
            if (spots_seen == spot) {
                return i;
            }
            spots_seen++;
        }
    }
    return -1;
}

/* A level's Gold Bolt spots' rules, conjoined (randomizer.h). */
static struct RzCheck bolt_rules[RZ_PLANETS];
static s32 bolt_rules_ready;

static void prepare_bolt_rules(void)
{
    s32 i, group, slot;
    for (i = 0; i < RZ_PLANETS; i++) {
        bolt_rules[i].all = 0;
        bolt_rules[i].any[0] = 0;
        bolt_rules[i].any[1] = 0;
        bolt_rules[i].any[2] = 0;
    }
    for (i = 0; i < RZ_CHECKS_BASE; i++) {
        struct RzCheck *merged = &bolt_rules[checks[i].planet];
        if (checks[i].kind != RZ_KIND_GOLD_BOLT) {
            continue;
        }
        merged->all |= checks[i].all;
        for (group = 0; group < 3 && checks[i].any[group] != 0; group++) {
            for (slot = 0; slot < 3 && merged->any[slot] != 0 && merged->any[slot] != checks[i].any[group]; slot++) {
            }
            if (slot < 3) {
                merged->any[slot] = checks[i].any[group];
            }
        }
    }
    bolt_rules_ready = 1;
}

const struct RzCheck *rz_check(s32 index)
{
    return index >= 0 && index < RZ_CHECKS_BASE ? &checks[index] : 0;
}

const char *rz_item_name(s32 item)
{
    return item > 0 && item < RZ_ITEM_KINDS ? item_names[item] : item_names[0];
}

u64 rz_item_bit(s32 item)
{
    return item > 0 && item < RZ_LOGICAL_ITEMS ? BIT(item) : 0;
}

s32 rz_item_is_progression(s32 item)
{
    s32 i;
    if (item >= RZ_INFOBOT_NOVALIS && item <= RZ_INFOBOT_VELDIN) {
        return 1;
    }
    for (i = 0; i < (s32)(sizeof(progression) / sizeof(progression[0])); i++) {
        if (progression[i] == item) {
            return 1;
        }
    }
    return 0;
}

u64 rz_drek_rule(void)
{
    return VELDIN_ALL | SWING;
}

/* --- The generator ----------------------------------------------------------
 * xorshift32 over a state scrambled from the seed and the attempt (a
 * splitmix step), so attempt 1 is unrelated to attempt 0. */
struct Rng {
    u32 state;
};

static u32 scramble(u32 x)
{
    x += 0x9E3779B9u;
    x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
    x = (x ^ (x >> 13)) * 0xC2B2AE35u;
    return x ^ (x >> 16);
}

static void rng_seed(struct Rng *rng, u32 seed, s32 attempt)
{
    rng->state = scramble(seed * 0x2545F491u + (u32)attempt * 0x7F4A7C15u);
    if (rng->state == 0) {
        rng->state = 0x6A09E667u;
    }
}

/* xorshift32 (Marsaglia's 13, 17, 5). */
static u32 rng_next(struct Rng *rng)
{
    u32 state = rng->state;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    rng->state = state;
    return state;
}

static s32 rng_below(struct Rng *rng, s32 limit)
{
    return (s32)((rng_next(rng) >> 8) % (u32)limit);
}

/* Fisher-Yates. */
static void shuffle(struct Rng *rng, u8 *items, s32 count)
{
    s32 i;
    for (i = count - 1; i > 0; i--) {
        s32 other = rng_below(rng, i + 1);
        u8 held = items[i];
        items[i] = items[other];
        items[other] = held;
    }
}

static s32 rule_met(const struct RzCheck *check, u64 have)
{
    s32 group;
    if (check->kind == RZ_KIND_GOLD_BOLT) {
        if (!bolt_rules_ready) {
            prepare_bolt_rules();
        }
        check = &bolt_rules[check->planet];
    }
    if ((have & check->all) != check->all) {
        return 0;
    }
    for (group = 0; group < 3 && check->any[group] != 0; group++) {
        if ((have & check->any[group]) == 0) {
            return 0;
        }
    }
    return 1;
}

/* The bonus vendor check: its planet's infobot is its only rule. */
static void bonus_check(const struct RzLayout *layout, struct RzCheck *out)
{
    out->planet = layout->bonus_planet;
    out->kind = RZ_KIND_BONUS_VENDOR;
    out->how = RZ_HOW_GOT;
    out->vanilla = RZ_ITEM_NONE;
    out->all = P(layout->bonus_planet);
    out->any[0] = 0;
    out->any[1] = 0;
    out->any[2] = 0;
}

static const struct RzCheck *check_of(const struct RzLayout *layout, s32 index, struct RzCheck *bonus)
{
    if (index < RZ_CHECKS_BASE) {
        return &checks[index];
    }
    bonus_check(layout, bonus);
    return bonus;
}

/* Everything reachable from HAVE: reached[i] set for each check whose rule
 * is met, with the items found there collected as it goes; the final HAVE
 * is returned. */
static u64 sweep(const struct RzLayout *layout, u64 have, u8 *reached)
{
    struct RzCheck bonus;
    s32 i, changed;
    for (i = 0; i < layout->checks; i++) {
        reached[i] = 0;
    }
    do {
        changed = 0;
        for (i = 0; i < layout->checks; i++) {
            if (!reached[i] && rule_met(check_of(layout, i, &bonus), have)) {
                reached[i] = 1;
                have |= rz_item_bit(layout->item[i]);
                changed = 1;
            }
        }
    } while (changed);
    return have;
}

/* One fill: 0 if an item found no check. */
static s32 fill(struct RzLayout *layout, struct Rng *rng)
{
    u8 progression[RZ_ITEMS_MAX], rest[RZ_ITEMS_MAX], reached[RZ_CHECKS_MAX], candidates[RZ_CHECKS_MAX];
    s32 progression_count = 0, rest_count = 0, i, item;

    for (i = 0; i < layout->checks; i++) {
        layout->item[i] = RZ_ITEM_NONE;
    }
    for (item = RZ_INFOBOT_NOVALIS + 1; item <= RZ_INFOBOT_VELDIN; item++) {   /* Novalis is the start */
        progression[progression_count++] = (u8)item;
    }
    for (item = RZ_INFOBOT_VELDIN + 1; item < RZ_LOGICAL_ITEMS; item++) {
        if (item == RZ_BOMB_GLOVE) {
            continue;                                                        /* the starting weapon */
        }
        if (rz_item_is_progression(item)) {
            progression[progression_count++] = (u8)item;
        } else {
            rest[rest_count++] = (u8)item;
        }
    }
    for (i = 0; i < RZ_GOLD_BOLTS; i++) {
        rest[rest_count++] = RZ_GOLD_BOLT;
    }
    if (layout->with_boots) {
        rest[rest_count++] = RZ_CHARGE_BOOTS;
    }
    if (progression_count + rest_count != layout->checks) {
        return 0;                                                            /* the tables disagree */
    }
    shuffle(rng, progression, progression_count);
    shuffle(rng, rest, rest_count);

    while (progression_count > 0) {
        u64 have = START_ITEMS;
        s32 candidate_count = 0;
        item = progression[--progression_count];
        for (i = 0; i < progression_count; i++) {
            have |= BIT(progression[i]);                                            /* assumed: still to be placed */
        }
        sweep(layout, have, reached);
        for (i = 0; i < layout->checks; i++) {
            if (reached[i] && layout->item[i] == RZ_ITEM_NONE) {
                candidates[candidate_count++] = (u8)i;
            }
        }
        if (candidate_count == 0) {
            return 0;
        }
        layout->item[candidates[rng_below(rng, candidate_count)]] = (u8)item;
    }
    for (i = 0; i < layout->checks; i++) {
        if (layout->item[i] == RZ_ITEM_NONE) {
            layout->item[i] = rest[--rest_count];
        }
    }
    return rest_count == 0;
}

s32 rz_layout_complete(const struct RzLayout *layout)
{
    u8 reached[RZ_CHECKS_MAX];
    u64 have = sweep(layout, START_ITEMS, reached);
    s32 i;
    for (i = 0; i < layout->checks; i++) {
        if (!reached[i]) {
            return 0;
        }
    }
    return (have & rz_drek_rule()) == rz_drek_rule();
}

s32 rz_generate(u32 seed, s32 with_boots, struct RzLayout *out)
{
    struct Rng rng;
    s32 attempt;
    u8 *bytes = (u8 *)out;
    for (attempt = 0; attempt < (s32)sizeof(*out); attempt++) {
        bytes[attempt] = 0;                 /* the checks past `checks` too, so layouts compare whole */
    }
    out->seed = seed;
    out->version = RZ_LOGIC_VERSION;
    out->weapon_mode = RZ_WEAPON_VANILLA;
    out->with_boots = with_boots != 0;
    out->checks = with_boots ? RZ_CHECKS_MAX : RZ_CHECKS_BASE;
    out->pad = 0;
    for (attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        rng_seed(&rng, seed, attempt);
        out->bonus_planet = (u8)(1 + rng_below(&rng, RZ_PLANETS - 1));
        out->bonus_price = 0;               /* 2,500 bolts; the record keeps the field */
        out->attempts = (u8)(attempt + 1);
        if (fill(out, &rng) && rz_layout_complete(out)) {
            return 1;
        }
    }
    return 0;
}

/* --- Seed codes ------------------------------------------------------------- */

void rz_code_encode(u32 seed, s32 version, s32 weapon_mode, u8 digits[RZ_CODE_LENGTH])
{
    u32 value = seed * 64 + (u32)weapon_mode * 16 + (u32)version;
    s32 i, sum = 0;
    for (i = RZ_CODE_LENGTH - 2; i >= 0; i--) {
        digits[i] = (u8)(value % RZ_CODE_BUTTONS);
        value /= RZ_CODE_BUTTONS;
    }
    for (i = 0; i < RZ_CODE_LENGTH - 1; i++) {
        sum += (i + 1) * digits[i];
    }
    /* 9 * d8 = -sum (mod 11); 9's inverse mod 11 is 5 */
    digits[RZ_CODE_LENGTH - 1] = (u8)(((RZ_CODE_BUTTONS - sum % RZ_CODE_BUTTONS) * 5) % RZ_CODE_BUTTONS);
}

s32 rz_code_decode(const u8 digits[RZ_CODE_LENGTH], u32 *seed, s32 *version, s32 *weapon_mode)
{
    u32 value = 0;
    s32 i, sum = 0;
    for (i = 0; i < RZ_CODE_LENGTH; i++) {
        if (digits[i] >= RZ_CODE_BUTTONS) {
            return 0;
        }
        sum += (i + 1) * digits[i];
    }
    if (sum % RZ_CODE_BUTTONS != 0) {
        return 0;
    }
    for (i = 0; i < RZ_CODE_LENGTH - 1; i++) {
        value = value * RZ_CODE_BUTTONS + digits[i];
    }
    *version = (s32)(value & 15);
    *weapon_mode = (s32)((value >> 4) & 3);
    *seed = value >> 6;
    return *version == RZ_LOGIC_VERSION && *weapon_mode == RZ_WEAPON_VANILLA && *seed < RZ_SEED_LIMIT;
}
