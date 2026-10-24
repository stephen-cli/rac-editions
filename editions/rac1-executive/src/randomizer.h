/* rz1: the randomizer's logic: what is shuffled, where it can go, and the
 * seed code (src/randomizer.c). Plain C over the typedefs in modseg.h and
 * nothing of the game's, so a host compiler runs it too
 * (tests/randomizer_test.c).
 *
 * The pool and the rules are Panda291's R&C1 Archipelago world (worlds/RAC1
 * in github.com/Panda291/Archipelago: data/Items.py, data/Locations.py,
 * Logic.py, ItemPool.py), transcribed check by check. Every planet is gated
 * by its infobot, Novalis is the starting planet and the Bomb Glove the
 * starting weapon, Drek is the goal and not a check. 94 checks hold 94
 * items: 17 infobots, 37 gadgets and weapons, 40 Gold Bolts. With the
 * Charge Boots in the build they join the pool as a dead end that nothing
 * needs, and a bonus vendor entry on a seed-chosen planet is the 95th
 * check (rz5, rz4). */
#ifndef RANDOMIZER_H
#define RANDOMIZER_H

#include "modseg.h"

/* The logic version in every seed code: bump it on any change to the
 * rules, the pool or the algorithm, so a shared code gives the same game. */
#define RZ_LOGIC_VERSION    2
#define RZ_VERSION_MAX      15          /* 4 bits */
#define RZ_WEAPON_MODES     4           /* 2 bits reserved; only RZ_WEAPON_VANILLA is implemented */
#define RZ_WEAPON_VANILLA   0
#define RZ_SEED_LIMIT       3349357     /* 11^8 / 64: the seeds a code can carry */
#define RZ_CODE_LENGTH      9           /* 8 data presses and a check press */
#define RZ_CODE_BUTTONS     11

#define RZ_PLANETS          19          /* 0 Veldin's tutorial (no checks) .. 18 Veldin */
#define RZ_CHECKS_BASE      94
#define RZ_CHECKS_MAX       95          /* with the bonus vendor check */
#define RZ_ITEMS_BASE       94
#define RZ_ITEMS_MAX        95          /* with the Charge Boots */
#define RZ_GOLD_BOLTS       40

/* Items. The infobots come first, planet by planet, then the gadgets and
 * weapons; those 56 are the logical items, one bit each of a u64. The Gold
 * Bolts and the Charge Boots are items too but gate nothing. */
enum RzItem {
    RZ_INFOBOT_NOVALIS = 1,             /* RZ_INFOBOT_NOVALIS + planet - 1 for planet 1..18 */
    RZ_INFOBOT_VELDIN = 18,
    RZ_HELI_PACK, RZ_THRUSTER_PACK, RZ_HYDRO_PACK, RZ_SONIC_SUMMONER, RZ_O2_MASK, RZ_PILOTS_HELMET,
    RZ_TAUNTER, RZ_HYDRODISPLACER, RZ_TRESPASSER, RZ_METAL_DETECTOR, RZ_MAGNEBOOTS, RZ_GRINDBOOTS,
    RZ_HOVERBOARD, RZ_HOLOGUISE, RZ_PDA, RZ_MAP_O_MATIC, RZ_BOLT_GRABBER, RZ_PERSUADER,
    RZ_ZOOMERATOR, RZ_RARITANIUM, RZ_CODEBOT, RZ_PREMIUM_NANOTECH, RZ_ULTRA_NANOTECH, RZ_SWINGSHOT,
    RZ_DRONE_DEVICE,
    RZ_SUCK_CANNON, RZ_BOMB_GLOVE, RZ_DEVASTATOR, RZ_VISIBOMB, RZ_BLASTER, RZ_PYROCITOR,
    RZ_MINE_GLOVE, RZ_WALLOPER, RZ_TESLA_CLAW, RZ_GLOVE_OF_DOOM, RZ_MORPH_O_RAY, RZ_RYNO,
    RZ_DECOY_GLOVE,
    RZ_LOGICAL_ITEMS,                   /* 57: items 1..56 have a bit */
    RZ_GOLD_BOLT = RZ_LOGICAL_ITEMS,
    RZ_CHARGE_BOOTS,
    RZ_ITEM_KINDS
};
#define RZ_ITEM_NONE 0

/* What a check is in the game: how its reward is handed over (rz3, rz4). */
enum RzCheckKind {
    RZ_KIND_INFOBOT,                    /* an infobot found or bought */
    RZ_KIND_GADGET,                     /* a gadget or weapon from a character, a chest or a prize */
    RZ_KIND_VENDOR,                     /* a Gadgetron vendor entry */
    RZ_KIND_GOLD_BOLT,
    RZ_KIND_BONUS_VENDOR                /* the extra vendor entry of a build with the Charge Boots */
};

/* How the reward is come by, which words the banner uses (src/randomizer_text.c). */
enum RzHow {
    RZ_HOW_FOUND,                       /* picked up in the world: "You found the ...!" */
    RZ_HOW_GOT,                         /* given or sold by a character, a vendor entry: "You got the ...!" */
    RZ_HOW_WON                          /* a hoverboard race's prize: "You won a ...!" */
};

struct RzCheck {
    u8 planet;                          /* 1..18 */
    u8 kind;
    u8 vanilla;                         /* the item retail gives there (RzItem), for reference */
    u8 how;                             /* RzHow */
    u64 all;                            /* every bit needed, the planet's infobot included */
    u64 any[3];                         /* OR groups, each needing one of its bits; 0 ends the list */
    u8 spot;                            /* a Gold Bolt's flag, D_0014BFC0[planet][spot] (its moby's pvars+0); 0 otherwise */
};

struct RzLayout {
    u32 seed;
    u8 version;
    u8 weapon_mode;
    u8 with_boots;                      /* the pool has the Charge Boots and the bonus vendor check */
    u8 checks;                          /* RZ_CHECKS_BASE or RZ_CHECKS_MAX */
    u8 bonus_planet;                    /* the bonus vendor check's planet, 1..18 */
    u8 bonus_price;                     /* 0: 2,500 bolts, 1: 5,000 */
    u8 attempts;                        /* fills tried before one passed the sweep */
    u8 pad;
    u8 item[RZ_CHECKS_MAX];             /* RzItem per check */
};

/* The tables. rz_check(i) for i < RZ_CHECKS_BASE; the bonus check's record
 * is built from the layout. */
const struct RzCheck *rz_check(s32 index);
const char *rz_item_name(s32 item);                 /* English, for tests and debug output */
u64 rz_item_bit(s32 item);                          /* 0 for a Gold Bolt or the Charge Boots */
s32 rz_item_is_progression(s32 item);
u64 rz_drek_rule(void);                             /* the goal's bits */
/* The game's inventory id (0-35) of a gadget or weapon; -1 for other kinds. */
s32 rz_item_game_id(s32 item);
s32 rz_item_of_game_id(s32 game_id);                /* the RzItem of an inventory id; RZ_ITEM_NONE for none */
s32 rz_check_for_vanilla(s32 item);                 /* the check whose retail reward is ITEM; -1 for none */
/* The check of LEVEL's Gold Bolt with flag SPOT (0-3); -1 for none. */
s32 rz_check_for_bolt(s32 level, s32 spot);

/* The layout for SEED: deterministic, the same every time. Returns 0 if no
 * fill passed the sweep (which the tests say never happens). */
s32 rz_generate(u32 seed, s32 with_boots, struct RzLayout *out);

/* Everything collected from the start, every check reached and Drek's rule
 * met: 1 if so. */
s32 rz_layout_complete(const struct RzLayout *layout);

/* Seed codes. A code is 9 presses of the 11 buttons, as digits 0..10: Up,
 * Right, Down, Left, Cross, Circle, Square, L1, L2, R1, R2. The first 8 are
 * seed * 64 + weapon_mode * 16 + version in base 11, most significant
 * first; the 9th makes the weighted sum (1 * d0 + 2 * d1 + ... + 9 * d8) a
 * multiple of 11, which catches every single wrong press and every swap of
 * two neighbouring presses. rz_code_decode gives 0 for a code that fails
 * the check or carries an unsupported version or weapon mode. */
void rz_code_encode(u32 seed, s32 version, s32 weapon_mode, u8 digits[RZ_CODE_LENGTH]);
s32 rz_code_decode(const u8 digits[RZ_CODE_LENGTH], u32 *seed, s32 *version, s32 *weapon_mode);

#endif
