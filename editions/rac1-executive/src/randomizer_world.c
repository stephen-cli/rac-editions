/* rz3: what the world shows for a check, and how it is taken (src/randomizer_play.h for the other files).
 *
 * The hosts. Three kinds of moby hold a check in the world: a Gold Bolt spot (GoldBoltUpdate, func_L01_00309078,
 * class 1134; its flag at pvars + 0), a world pickup (PICKUPS: the Trespasser, the Suck Cannon, the Hydrodisplacer,
 * the Pilot's Helmet, the Magneboots, the Raritanium, the Morph-o-Ray, the Bolt Grabber, the Codebot) and a world
 * infobot (InfobotUpdate, func_L00_002D7298, class 750; the planet it unlocks at pvars + 4), whose check is one of
 * the planet's own (the Kerwan train's infobot, Orxon's two, ...). A host whose check holds its own retail item
 * plays as retail. Any other item is shown by a stand-in, a moby of the item's model made by CreateMoby
 * (func_0020D348) and carried by the host's update: its position every frame, so it follows a host that moves (the
 * Kerwan infobot rides the train and then flies its paths), and the item's motion (MODELS): an infobot holds still,
 * as InfobotUpdate keeps one at rest (its home yaw, no tilt); anything else turns about its upright axis at a
 * quarter turn a second, the rate of the game's own pickups (func_L04_002E2DF0, Moby1290Update: rot.z +=
 * g_frame_time * pi/2). The host itself draws at scale 0. Its touch takes the check at once, as no item has a
 * cutscene that plays anywhere: a spot quietly (pvars + 0xC, the touch retail's quiet spots have, without Ratchet's
 * hold-up and the camera's cut), a pickup before its update can start its cutscene, an infobot as it turns to its
 * message (state 8), each with the Randomizer's banner and an autosave. Two kinds of item are the exception: a Gold
 * Bolt at a pickup or an infobot is a live bolt (carry_bolt), whose touch has the hold-up of every Gold Bolt, and a
 * weapon the vendor sells plays the vendor's demo of it once taken (start_weapon_demo).
 *
 * The models. A stand-in's class must be in the level's memory. Most are in every level: the infobot (spliced into
 * the five levels without it by tools/extra_classes.py), the Gold Bolt, and the gadgets Ratchet wears (the packs on
 * a Clank, as the Gadgets page shows them, the helmets, the boots, the Sonic Summoner, the Map-o-Matic, the Bolt
 * Grabber, the Persuader, the drone of the Drone Device, the Charge Boots). The weapons and the gadgets he holds live
 * compressed in each level's gadget directory; a stand-in unpacks its own copy into the free RAM above the effect pool
 * (gadget_ready). A few items exist on one planet only (the Hoverboard, the Zoomerator, the Nanotech cans, the
 * Codebot, the Raritanium); tools/extra_classes.py copies them into the other levels with the room. Where a model
 * is not in memory (those items on a level without the room, a weapon with no room left), the stand-in is the
 * Morph-o-Ray's chicken (class 270, in every level), as on the vendor's mystery entries.
 *
 * Bookkeeping. A stand-in names its host as its spawner (MOBY_PARENT) and carries STAND_IN_MARK at pvars +
 * STAND_IN_MARK_AT, a word no update of its class writes, as its own update never runs. A stand-in can be gone
 * without this code deleting it (on Aridia one 190 units from the hero had its slot cleared, and the slot then went
 * to another class), so it is checked before every use: one that no longer holds a live marked moby of that host is
 * forgotten, never written, and a new one is made at the host's next update. The host can move too: on Eudora,
 * with Ratchet some 80 units from the Suck Cannon pickup, the game moved the level's mobys down a slot, so the
 * stand-in still named the old address; a stand-in whose host moved is found by that old address and follows it.
 * Each stand-in is kept under its host's identity (host_key), which a move does not change. A stand-in not made (no
 * free moby: CreateMoby keeps a freed slot for two frames) is tried again at the next update, the host staying
 * hidden meanwhile. */
#include "randomizer_play.h"

DECLARE_LVL(func_0020D678);             /* DeleteMoby(moby) */
DECLARE_LVL(func_0020D348);             /* CreateMoby(class_id) */
DECLARE_LVL(func_L00_002512D8);         /* SetLevelTriggerDone(mission) */
DECLARE_LVL(func_L00_001FF860);         /* fast_atan2(dx, dy): the yaw of a direction */
DECLARE_LVL(func_L00_00251E30);         /* refresh_moby_spatial_bounds (func_0020ED48 in the executable): its
                                         * matrix and bounding sphere from its place, turn and scale */
DECLARE_LVL(func_00213DE0);             /* MobyBlendAnimation(moby, sequence, frame, blend frames) */
DECLARE_LVL(func_L01_00309848);         /* initialize_gold_bolt_data(moby): its glow's state, pvars +0x20..+0x5C */
DECLARE_LVL(func_L01_00309928);         /* update_gold_bolt_glow_rings(moby): the glow, each frame */
DECLARE_LVL_DATA(class_slot_by_id);     /* u8 per class id, 0xFF none */
DECLARE_LVL_DATA(class_resources);      /* per slot: the class's data, 0 not loaded */
DECLARE_LVL_DATA(game_mode);            /* D_0015F6E8: 0 play, 2 cinematic, 3 pause */
DECLARE_LVL_DATA(class_unpacked);       /* LoadMobyClassResources's last directory entry, */
DECLARE_LVL_DATA(class_decode_buffer);  /*   its decode buffer, */
DECLARE_LVL_DATA(level_buffers);        /*   and the level's buffer table (tools/modseg.py) */
DECLARE_LVL(func_00205270);             /* LoadMobyClassResources(class, decode buffer) */
typedef void (*DeleteFn)(void *);
typedef void *(*CreateFn)(s32);
typedef void (*MissionFn)(s32);
typedef void (*BoundsFn)(void *);
typedef f32 (*AtanFn)(f32, f32);
typedef void (*BlendFn)(void *, s32, s32, s32);
typedef void (*LoadFn)(s32, s32);

#define LEVELS          19
#define GOLD_CLASS      1134            /* GoldBoltUpdate, func_L01_00309078 */
#define GOLD_QUIET      0xC             /* its pvars: nonzero, the touch takes the bolt without the sequence */
#define GOLD_SPOTS      4               /* Gold Bolt spots per level */
#define INFOBOT_CLASS   750             /* InfobotUpdate, func_L00_002D7298 */
#define INFOBOT_PLANET  0x4             /* its pvars: the planet it unlocks */
#define INFOBOT_TOUCHED 8               /* its state once Ratchet reaches it: its message follows */
#define INFOBOT_DORMANT 1               /* its state while hidden, until the level wakes it (leave) */
#define CHICKEN_CLASS   270             /* the Morph-o-Ray's chicken (MorphorayChickenUpdate), in every level */

#define MOBY_SCALE_F(m) (*(volatile f32 *)((u8 *)(m) + MOBY_SCALE))
#define MOBY_ROT_F(m, axis) (*(volatile f32 *)((u8 *)(m) + MOBY_ROT + 4 * (axis)))
#define MOBY_DRAW_DIST(m) (*(volatile s16 *)((u8 *)(m) + 0x32))
#define MOBY_UPDATE_DIST(m) (*(volatile u8 *)((u8 *)(m) + 0x30))
#define MOBY_PARENT(m)  (*(void *volatile *)((u8 *)(m) + 0xB8))
#define MOBY_MISSION(m) (*(volatile u8 *)((u8 *)(m) + 0xB0))
#define MOBY_PCLASS(m)  (*(volatile u32 *)((u8 *)(m) + 0x24))
#define MOBY_FLAGS(m)   (*(volatile u16 *)((u8 *)(m) + 0x34))
#define FLAG_UNLISTED   0x2             /* left out of the visibility list (build_resident_visibility_list) */
#define FLAG_STILL      0x40            /* its animation is not advanced (MobyUpdateLoop) */
#define MOBY_PREV_SEQ(m) (*(volatile u8 *)((u8 *)(m) + 0x53))
#define CLASS_SEQ_COUNT 0xC             /* the class header's number of sequences */
#define LEVEL_MOBYS_END (*(volatile u32 *)0x0016005C)  /* the end of the level's placed mobys; spawned ones follow */
#define MOBYS_END       (*(volatile u32 *)0x00160068)  /* the end of the moby array, spawned mobys included */
#define MOBY_FREE_TAIL  0xFF            /* the state of the first slot of the array's free tail */
#define FRAME_TIME      (*(volatile f32 *)0x0015EE6C)  /* g_frame_time: a frame's seconds */
#define HERO_POS        ((volatile f32 *)(HERO_BASE + 0x80))
#define PI_F            3.1415927f
#define MODE_PLAY       0
#define MODE_CINEMATIC  2               /* an in-level cutscene */

#define STAND_IN_MARK   0x525A5349      /* the edition's word, at pvars + STAND_IN_MARK_AT (CreateMoby clears the */
#define STAND_IN_MARK_AT 0x7C           /* pvars); past every field GoldBoltUpdate and InfobotUpdate use */
#define BOLT_MARK       0x525A4742      /* the same place: a live Gold Bolt standing in, which runs GoldBoltUpdate */
#define BODY_MARK       0x525A4344      /*   and Clank, carrying a pack stand-in */
#define CLANK_CLASS     601             /* Clank, as the Gadgets page shows him with a pack (its pose moby 0x259) */
#define GOLD_ID         0x0             /* GoldBoltUpdate's pvars: its flag, D_0014BFC0[level * 4 + id] */
#define GOLD_HOME       0x10            /*   its rest position and yaw, set at its first update */
#define BORROWED_SPOT   3               /* the flag a live stand-in bolt uses, its value kept around its update */
#define STAND_INS       8

/* --- the models ---------------------------------------------------------------------------------------------- */

enum Motion {
    STILL,                              /* an infobot at rest */
    SPIN                                /* about the upright axis, a quarter turn a second */
};

/* Where a model's class is: in every level's memory, or only while the game has it unpacked or on its own planet. */
enum Source {
    RESIDENT,
    GADGET,                             /* the level's gadget directory: unpacked only while held or at the vendor */
    COPIED                              /* one planet's own, copied into the others by tools/extra_classes.py where
                                         * they have the room (not Blarg Station's: the chicken there) */
};

/* How a model is shown. */
#define ANIMATE         1               /* its sequence 1 plays, as on the Gadgets page: the pieces of a class skinned to
                                         * its joints draw nowhere, or shrunk, without a pose */
#define ON_CLANK        2               /* a pack: drawn on a Clank, in his space, as the Gadgets page shows it */
#define CENTRE          4               /* turned about its middle (its bounding sphere's centre), not its origin: a
                                         * weapon's is at its grip, the Persuader's where it sits on the harness */

struct ItemModel {
    u16 class_id;
    u8 motion;
    u8 source;
    f32 scale;                          /* times the class's own scale */
    u8 flags;
    u8 seq;                             /* the sequence ANIMATE plays: 0 for 1 */
    f32 roll;                           /* its own turn about x and y (rot.x, rot.y) to stand upright: for an item */
    f32 tilt;                           /* the vendor's hologram has no pose for (the Suck Cannon, the Morph-o-Ray,
                                         * the hand gadgets): most take the quarter turn about y the vendor gives
                                         * most weapons; the others stand as the hologram does */
};

/* Each item's model: its own class in the item records (the class the Gadgets page shows: +0x10 of each 0x4C-byte
 * record, gen_items.py's table), what the vendor shows for a weapon, the pickup or the cutscene prop for the rest.
 * The packs are drawn on Clank (601), each its record's class: the Heli-Pack's (0x25F) is its top rotor and the
 * rotors that take the place of Clank's hands, deployed by its sequence 6 as the Gadgets page plays it, the
 * Thruster-Pack's (0x260) his thrusters, the Hydro-Pack's (0x261) his hydro pods. The Map-o-Matic, the Bolt Grabber,
 * the Persuader and the boots are drawn larger: on Ratchet they are a few centimetres across (the Magneboots' bounding
 * sphere 0.27 units, the Trespasser pickup's 1.7). The three boots come out about 0.55 across, between the Taunter
 * (0.48) and the Pyrocitor (0.67): at 3 times, 0.8, they outsized every weapon. A Gold Bolt is a live bolt (carry_bolt), not a model. */
static const struct ItemModel MODELS[RZ_ITEM_KINDS] = {
    [RZ_HELI_PACK] = { 607, SPIN, RESIDENT, 1.0f, ANIMATE | ON_CLANK, 6 },
    [RZ_THRUSTER_PACK] = { 608, SPIN, RESIDENT, 1.0f, ANIMATE | ON_CLANK },
    [RZ_HYDRO_PACK] = { 609, SPIN, RESIDENT, 1.0f, ANIMATE | ON_CLANK },
    [RZ_SONIC_SUMMONER] = { 433, SPIN, RESIDENT, 1.0f },
    [RZ_O2_MASK] = { 1289, SPIN, RESIDENT, 1.0f },
    [RZ_PILOTS_HELMET] = { 1290, SPIN, RESIDENT, 1.0f },
    [RZ_TAUNTER] = { 175, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_HYDRODISPLACER] = { 1251, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_TRESPASSER] = { 188, SPIN, GADGET, 1.0f, CENTRE, 0, 0.0f, -1.5707964f },
    [RZ_METAL_DETECTOR] = { 585, SPIN, GADGET, 1.0f, CENTRE, 0, 0.0f, -1.5707964f },
    [RZ_MAGNEBOOTS] = { 173, SPIN, RESIDENT, 2.0f, CENTRE },
    [RZ_GRINDBOOTS] = { 195, SPIN, RESIDENT, 2.0f, CENTRE },
    [RZ_HOVERBOARD] = { 84, SPIN, COPIED, 1.0f, CENTRE },
    [RZ_HOLOGUISE] = { 483, SPIN, GADGET, 1.0f, CENTRE, 0, 0.0f, -1.5707964f },
    [RZ_PDA] = { 619, SPIN, GADGET, 1.0f, CENTRE, 0, 0.0f, -1.5707964f },
    [RZ_MAP_O_MATIC] = { 614, SPIN, RESIDENT, 3.0f, ANIMATE },
    [RZ_BOLT_GRABBER] = { 618, SPIN, RESIDENT, 3.0f, ANIMATE },
    [RZ_PERSUADER] = { 407, SPIN, RESIDENT, 4.0f, ANIMATE | CENTRE },
    [RZ_ZOOMERATOR] = { 1002, SPIN, COPIED, 1.0f, CENTRE },
    [RZ_RARITANIUM] = { 37, SPIN, COPIED, 1.0f, CENTRE },
    [RZ_CODEBOT] = { 1428, SPIN, COPIED, 1.0f, CENTRE },
    [RZ_PREMIUM_NANOTECH] = { 645, SPIN, COPIED, 2.5f, CENTRE, 0, 0.0f, -1.5707964f },
    [RZ_ULTRA_NANOTECH] = { 645, SPIN, COPIED, 2.5f, CENTRE, 0, 0.0f, -1.5707964f },
    [RZ_SWINGSHOT] = { 208, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_DRONE_DEVICE] = { 479, SPIN, RESIDENT, 1.0f },
    [RZ_SUCK_CANNON] = { 849, SPIN, GADGET, 1.0f, CENTRE, 0, 0.0f, -1.5707964f },
    [RZ_BOMB_GLOVE] = { 192, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_DEVASTATOR] = { 157, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_VISIBOMB] = { 163, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_BLASTER] = { 168, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_PYROCITOR] = { 176, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_MINE_GLOVE] = { 190, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_WALLOPER] = { 180, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_TESLA_CLAW] = { 177, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_GLOVE_OF_DOOM] = { 229, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_MORPH_O_RAY] = { 185, SPIN, GADGET, 1.0f, CENTRE, 0, 0.0f, -1.5707964f },
    [RZ_RYNO] = { 454, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_DECOY_GLOVE] = { 562, SPIN, GADGET, 1.0f, CENTRE },
    [RZ_CHARGE_BOOTS] = { 2000, SPIN, RESIDENT, 2.25f, ANIMATE | CENTRE },
};
static const struct ItemModel INFOBOT_MODEL = { INFOBOT_CLASS, STILL, RESIDENT, 1.0f };
static const struct ItemModel CHICKEN_MODEL = { CHICKEN_CLASS, SPIN, RESIDENT, 1.0f };

/* For the PINE test scripts (docs/DEBUGGING.md): a class every stand-in takes instead of its item's, 0 none; and
 * the live stand-in bolts: updates run, the last one's state after it, its host's check, the last borrow taken. */
volatile s32 ee_debug_rz_model;
volatile s32 ee_debug_rz_bolt[4];

static s32 is_infobot(s32 item)
{
    return item >= RZ_INFOBOT_NOVALIS && item <= RZ_INFOBOT_VELDIN;
}

static s32 class_in_memory(s32 class_id, s32 L)
{
    u8 slot = class_id >= 0 && class_id < 0x800 ? LVL_DATA(L, class_slot_by_id, u8 *)[class_id] : 0xFF;
    return slot != 0xFF && LVL_DATA(L, class_resources, u32 *)[slot] != 0;
}

/* --- the gadget directory's models ----------------------------------------------------------------------------
 *
 * The weapons and the gadgets Ratchet holds are not in a level's memory for good: its gadget directory keeps them
 * compressed, and LoadMobyClassResources (func_00205270) unpacks one into one of two 0x18000-byte decode buffers
 * when Ratchet takes it in hand or the vendor shows it, points the class's resource slot at it and maps its
 * materials onto textures the level keeps resident (so a model costs no texture memory). A stand-in needs its own
 * copy, which no later load overwrites. The free RAM for it is above the effect pool, the last block of the level's
 * allocations, which runs up toward the stack at the top of RAM: the edition's pool searches only its first 0x400
 * records (tools/overlay_patch.py effect_pool), and retail leaves 48 KB between its full pool and the top of RAM, so
 * everything from record 0x400 to STACK_ROOM below the top is unused. On most levels that is several hundred KB; on
 * Blarg Station some 34 KB. A model goes there through LoadMobyClassResources itself, its fifth buffer table entry
 * (the decode buffers' base) pointed at the free RAM for the call; its state (the entry last unpacked, which it
 * skips unpacking again, and the buffer it used) is put back after. The class's slot is pointed at the copy again
 * every frame its stand-in is carried: the vendor, showing that weapon, points it at its own buffer. A model with
 * no room is shown by the chicken. */
extern const u32 ee_rz_effect_pool[LEVELS];     /* tools/gen_randomizer.py: the word holding the pool's start */
extern const s32 ee_rz_gadget_count;
extern const u16 ee_rz_gadget_classes[];        /* the gadget directory's classes */
extern const u32 ee_rz_gadget_sizes[];          /*   and their sizes unpacked */

#define EFFECT_RECORD_SIZE  0x40
#define EFFECT_RECORDS_USED 0x400               /* tools/overlay_patch.py EFFECT_RECORDS */
#define RAM_END             0x02000000
#define STACK_ROOM          0x10000             /* left below the top of RAM: more than retail's 48 KB */
#define DECODE_BASE         4                   /* the level buffer table's entry: the decode buffers' base */
#define GADGET_MODELS       6

static struct {
    u16 class_id;
    u32 data;
} unpacked[GADGET_MODELS];
static s32 unpacked_count;
static u32 free_ram;                            /* the next free byte above the pool; 0 until a visit's first model */

static void forget_gadget_models(void)
{
    unpacked_count = 0;
    free_ram = 0;
}

static u32 gadget_size(s32 class_id)
{
    s32 i;
    for (i = 0; i < ee_rz_gadget_count; i++) {
        if (ee_rz_gadget_classes[i] == class_id) {
            return ee_rz_gadget_sizes[i];
        }
    }
    return 0;
}

/* CLASS_ID's model in memory as the stand-in's own, unpacked at the visit's first need: 1, or 0 for no room. */
static s32 gadget_ready(s32 class_id, s32 L)
{
    volatile u32 *resources = LVL_DATA(L, class_resources, volatile u32 *);
    volatile u32 *buffers = LVL_DATA(L, level_buffers, volatile u32 *);
    volatile s32 *last = LVL_DATA(L, class_unpacked, volatile s32 *);
    volatile s32 *buffer = LVL_DATA(L, class_decode_buffer, volatile s32 *);
    u8 slot = LVL_DATA(L, class_slot_by_id, u8 *)[class_id];
    u32 size = gadget_size(class_id);
    u32 saved_base;
    s32 saved_last, saved_buffer, i;
    if (slot == 0xFF || size == 0) {
        return 0;
    }
    for (i = 0; i < unpacked_count; i++) {
        if (unpacked[i].class_id == class_id) {
            resources[slot] = unpacked[i].data;
            return 1;
        }
    }
    if (free_ram == 0) {
        u32 pool = *(volatile u32 *)ee_rz_effect_pool[L];
        if (pool == 0) {
            return 0;
        }
        free_ram = (pool + EFFECT_RECORDS_USED * EFFECT_RECORD_SIZE + 0xF) & ~0xFu;
    }
    if (unpacked_count == GADGET_MODELS || free_ram + size > RAM_END - STACK_ROOM) {
        return 0;
    }
    saved_base = buffers[DECODE_BASE];
    saved_last = *last;
    saved_buffer = *buffer;
    buffers[DECODE_BASE] = free_ram;
    *last = -1;
    LVL(L, func_00205270, LoadFn)(class_id, 0);
    buffers[DECODE_BASE] = saved_base;
    *last = saved_last;
    *buffer = saved_buffer;
    unpacked[unpacked_count].class_id = (u16)class_id;
    unpacked[unpacked_count].data = free_ram;
    unpacked_count++;
    free_ram += (size + 0xF) & ~0xFu;
    return 1;
}

/* ITEM's model in level L: its own, or the chicken where its class is not in memory (a one-planet item elsewhere,
 * a gadget with no room to unpack it). */
static const struct ItemModel *model_of(s32 item, s32 L)
{
    const struct ItemModel *model = is_infobot(item) ? &INFOBOT_MODEL
                                  : item > 0 && item < RZ_ITEM_KINDS ? &MODELS[item] : 0;
    if (model == 0 || model->class_id == 0) {
        return &CHICKEN_MODEL;
    }
    if (model->source == GADGET ? !gadget_ready(model->class_id, L) : !class_in_memory(model->class_id, L)) {
        return &CHICKEN_MODEL;
    }
    return model;
}

/* --- the stand-ins ------------------------------------------------------------------------------------------- */

struct StandIn {
    u16 key;                            /* host_key; 0 a free entry */
    void *host;
    u8 *model;
    u8 *body;                           /* the Clank under a pack, 0 none */
    u32 mark;                           /* the model's: STAND_IN_MARK, or BOLT_MARK for a live bolt */
    u16 class_id;
    u8 motion;
    u8 flags;
    u8 lift_known;
    f32 lift;                           /* the host's own model's middle above its origin (host_lift) */
    const struct ItemModel *model_ref;  /* the model it shows */
};
static struct StandIn stand_ins[STAND_INS];
static u8 *demo_vendor;                 /* the vendor moby of a weapon demo under way (start_weapon_demo), 0 none */

void rz_world_forget(void)
{
    s32 i;
    for (i = 0; i < STAND_INS; i++) {
        stand_ins[i].key = 0;
    }
    forget_gadget_models();
    demo_vendor = 0;
}

static u32 mark_of(void *moby)
{
    u32 pvars = MOBY_PVARS(moby);
    return MOBY_STATE(moby) >= 0 && pvars != 0 ? *(volatile u32 *)(pvars + STAND_IN_MARK_AT) : 0;
}

/* A stand-in that never runs its class's update: a model or a Clank. */
static s32 marked(void *moby)
{
    u32 mark = mark_of(moby);
    return mark == STAND_IN_MARK || mark == BODY_MARK;
}

static s32 holds(u8 *m, void *host, u32 mark)
{
    return mark_of(m) == mark && MOBY_PARENT(m) == host;
}

/* The live moby with MARK naming HOST as its spawner, wherever it is now; 0 for none. */
static u8 *find_by_host(void *host, u32 mark)
{
    u32 moby;
    for (moby = LEVEL_MOBYS_END; moby < MOBYS_END && (u8)MOBY_STATE(moby) != MOBY_FREE_TAIL; moby += MOBY_SIZE) {
        if (holds((u8 *)moby, host, mark)) {
            return (u8 *)moby;
        }
    }
    return 0;
}

/* The entry of the host with KEY, made when there is none; 0 when all are taken. */
static struct StandIn *entry_of(u16 key)
{
    s32 i;
    struct StandIn *free_entry = 0;
    for (i = 0; i < STAND_INS; i++) {
        if (stand_ins[i].key == key) {
            return &stand_ins[i];
        }
        if (stand_ins[i].key == 0 && free_entry == 0) {
            free_entry = &stand_ins[i];
        }
    }
    if (free_entry != 0) {
        free_entry->key = key;
        free_entry->host = 0;
        free_entry->model = 0;
        free_entry->body = 0;
        free_entry->lift_known = 0;
    }
    return free_entry;
}

/* M, a part of a stand-in marked MARK, now hosted by HOST: 0 when the game has removed it; when the host moved to
 * HOST from OLD_HOST, the part is found by the old address and follows. */
static u8 *follow(u8 *m, u32 mark, void *old_host, void *host)
{
    if (m != 0 && holds(m, host, mark)) {
        return m;
    }
    m = 0;
    if (old_host != 0 && old_host != host) {
        m = find_by_host(old_host, mark);
        if (m != 0) {
            MOBY_PARENT(m) = host;
        }
    }
    return m;
}

/* E's stand-in, now hosted by HOST: 0 when there is none or the game has removed it. */
static u8 *live(struct StandIn *e, void *host)
{
    e->model = follow(e->model, e->mark, e->host, host);
    e->body = follow(e->body, BODY_MARK, e->host, host);
    e->host = host;
    return e->model;
}

static void drop(u16 key, void *host, s32 L)
{
    struct StandIn *e = entry_of(key);
    if (e == 0) {
        return;
    }
    if (live(e, host) != 0) {
        LVL(L, func_0020D678, DeleteFn)(e->model);
    }
    if (e->body != 0) {
        LVL(L, func_0020D678, DeleteFn)(e->body);
    }
    e->key = 0;
}

/* A new stand-in part of class CLASS_ID for HOST, marked MARK; 0 when there is no free moby. CreateMoby gives the
 * classes of Ratchet's attachments (Clank, the packs, the Map-o-Matic, the Persuader, the Charge Boots) flags the
 * visibility list skips (+0x34 bit 2: no bounding sphere, no matrix, drawn nowhere) and, for some, no animation
 * (bit 0x40); the Gadgets page sets its own models' flags outright, and a stand-in clears both. */
static u8 *make_part(s32 class_id, u32 mark, void *host, s32 L)
{
    u8 *m = LVL(L, func_0020D348, CreateFn)(class_id);
    if (m != 0) {
        *(volatile u32 *)(MOBY_PVARS(m) + STAND_IN_MARK_AT) = mark;
        MOBY_PARENT(m) = host;
        MOBY_UPDATE_DIST(m) = MOBY_UPDATE_DIST(host);
        MOBY_DRAW_DIST(m) = MOBY_DRAW_DIST(host);
        MOBY_ROT_F(m, 2) = MOBY_ROT_F(host, 2);
        MOBY_FLAGS(m) &= ~(FLAG_UNLISTED | FLAG_STILL);
    }
    return m;
}

/* The vendor's hologram of an item: its offset, then its turn about x and y (rot.x, rot.y), which stand a weapon's
 * model upright (most lie along x, and turn a quarter about y); rot.z spins. 0x30 an item id (D_L00_001C9C30 in level
 * 0; func_L00_0029D988's VendorMenuUpdate poses the hologram from it). */
DECLARE_LVL_DATA(vendor_holo_pose);
#define HOLO_POSE_SIZE  0x30
#define HOLO_POSE_ROT   0x10
#define HOLO_POSE_ITEMS 37

/* M, ITEM's own model, turned to stand by its own turn, or as the vendor's hologram stands it (an item with an entry
 * there: the Hydrodisplacer has one, though the vendor never sells it). */
static void pose(u8 *m, s32 item, const struct ItemModel *model, s32 L)
{
    s32 id = rz_item_game_id(item);
    if (model->roll == 0.0f && model->tilt == 0.0f && id >= 0 && id < HOLO_POSE_ITEMS) {
        const f32 *rot = (const f32 *)(LVL_DATA(L, vendor_holo_pose, const u8 *) + HOLO_POSE_SIZE * id + HOLO_POSE_ROT);
        MOBY_ROT_F(m, 0) = rot[0];
        MOBY_ROT_F(m, 1) = rot[1];
    } else {
        MOBY_ROT_F(m, 0) = model->roll;
        MOBY_ROT_F(m, 1) = model->tilt;
    }
}

/* Sequence SEQ playing on M, as the Gadgets page keeps one on its models (update_menu_preview_animation_pose): set
 * again whenever another has taken over, when the class has it. The moby update loop advances the animation of
 * every moby it lists, a stand-in too. */
static void animate(u8 *m, s32 seq, s32 L)
{
    u8 *pclass = (u8 *)MOBY_PCLASS(m);
    if (pclass != 0 && pclass[CLASS_SEQ_COUNT] > seq && MOBY_PREV_SEQ(m) != seq) {
        LVL(L, func_00213DE0, BlendFn)(m, seq, 0, 0);
    }
}

static f32 wrap(f32 angle)
{
    if (angle > PI_F) {
        angle -= 2.0f * PI_F;
    } else if (angle < -PI_F) {
        angle += 2.0f * PI_F;
    }
    return angle;
}

/* The stand-in's turn this frame. Every angle stays within -pi..pi, as the game keeps them: a yaw grown past that
 * broke the moby's matrix, and it drew nowhere or as stray polygons over the menus. */
static void move(u8 *m, s32 motion, void *host)
{
    s32 axis;
    switch (motion) {
    case STILL:
        if (MOBY_CLASS(host) == INFOBOT_CLASS) {
            for (axis = 0; axis < 3; axis++) {
                MOBY_ROT_F(m, axis) = MOBY_ROT_F(host, axis);    /* an infobot host turns as it flies */
            }
        }
        break;
    case SPIN:
        MOBY_ROT_F(m, 2) = wrap(MOBY_ROT_F(m, 2) + FRAME_TIME * (PI_F / 2.0f));
        break;
    }
}

/* A Gold Bolt at a pickup or a world infobot is a live bolt: a moby of the bolt's class running GoldBoltUpdate,
 * so that it turns and bobs as a spot does and its touch has Ratchet's hold-up and the camera's cut. The moby
 * update loop (MobyUpdateLoop, func_00213C78) runs the mobys of the visibility list, which it builds from their
 * bounding spheres; a moby refreshes its own after its update moves it (refresh_moby_spatial_bounds), so a new one
 * gets its sphere here, or it would never be listed and never run. Its flag is
 * BORROWED_SPOT's, whose byte is cleared for its update and given its value back after (borrow); the touch sets it
 * and saves, and the save credits the host's check instead (rz_world_before_save). At rest it is kept at its host,
 * which may move (the Kerwan infobot); GoldBoltUpdate moves it to Ratchet for the hold-up, from its state 2. The
 * hold-up puts Ratchet 2.5 units from the bolt along its rest yaw (home.w), which a level's own spots face to open
 * ground; the stand-in's faces Ratchet, so he lands on the side he came from (facing anywhere, Gaspar's platform
 * edge put him over the lava). */
#define GOLD_RESTING    1
#define GOLD_HOLDING    2               /* Ratchet holds it up */
#define UPDATE_ALWAYS   0xFF            /* an update distance the visibility list takes as no limit, as spawners set:
                                         * with its host's, the bolt stopped updating during the hold-up, its
                                         * animation unfinished and Ratchet held in it; from the start, its first
                                         * update ran on the galactic map, far from Ratchet, and crashed there */
static void carry_bolt(struct StandIn *e, void *host, s32 L)
{
    u8 *m = live(e, host);
    if (m == 0) {
        e->mark = BOLT_MARK;
        m = e->model = make_part(GOLD_CLASS, BOLT_MARK, host, L);
        if (m == 0) {
            return;
        }
        *(volatile s32 *)(MOBY_PVARS(m) + GOLD_ID) = BORROWED_SPOT;
        *(volatile f32 *)(m + MOBY_POS) = *(volatile f32 *)((u8 *)host + MOBY_POS);
        *(volatile f32 *)(m + MOBY_POS + 4) = *(volatile f32 *)((u8 *)host + MOBY_POS + 4);
        *(volatile f32 *)(m + MOBY_POS_Z) = *(volatile f32 *)((u8 *)host + MOBY_POS_Z) + e->lift;
        LVL(L, func_L00_00251E30, BoundsFn)(m);
        e->class_id = GOLD_CLASS;
    }
    if (MOBY_STATE(m) == GOLD_RESTING) {
        volatile f32 *home = (volatile f32 *)(MOBY_PVARS(m) + GOLD_HOME);
        home[0] = *(volatile f32 *)((u8 *)host + MOBY_POS);
        home[1] = *(volatile f32 *)((u8 *)host + MOBY_POS + 4);
        home[2] = *(volatile f32 *)((u8 *)host + MOBY_POS_Z) + e->lift;
        home[3] = LVL(L, func_L00_001FF860, AtanFn)(HERO_POS[0] - home[0], HERO_POS[1] - home[1]);
        *(volatile f32 *)(m + MOBY_POS) = home[0];
        *(volatile f32 *)(m + MOBY_POS + 4) = home[1];
    }
}

#define CENTRE_MAX      8.0f            /* the most a model's middle is from its origin, in units */

/* How far above its origin HOST's own model has its middle, from its bounding sphere (+0x00, floats in 1/1024 units)
 * at its own place, turn and scale: the stand-in stands there, as the item it replaces would. A pickup's origin can
 * be on the floor under its model (Aridia's Trespasser: its middle 1.84 units up), a Gold Bolt's and an infobot's is
 * their middle. Read before the host is first drawn at scale 0; 0 for a host already hidden. */
static f32 host_lift(void *host, s32 L)
{
    volatile f32 *sphere = (volatile f32 *)host;
    f32 lift;
    if (MOBY_SCALE_F(host) <= 0.0f) {
        return 0.0f;
    }
    LVL(L, func_L00_00251E30, BoundsFn)(host);
    lift = sphere[2] / 1024.0f - *(volatile f32 *)((u8 *)host + MOBY_POS_Z);
    return sphere[3] > 0.0f && lift < CENTRE_MAX && lift > -CENTRE_MAX ? lift : 0.0f;
}

/* PART placed where HOST's model has its middle (LIFT above its origin), turned as MODEL turns. */
static void place(u8 *part, const u8 *model, void *host, f32 lift)
{
    s32 axis;
    *(volatile f32 *)(part + MOBY_POS) = *(volatile f32 *)((u8 *)host + MOBY_POS);
    *(volatile f32 *)(part + MOBY_POS + 4) = *(volatile f32 *)((u8 *)host + MOBY_POS + 4);
    *(volatile f32 *)(part + MOBY_POS_Z) = *(volatile f32 *)((u8 *)host + MOBY_POS_Z) + lift;
    for (axis = 0; axis < 3 && part != model; axis++) {
        MOBY_ROT_F(part, axis) = MOBY_ROT_F(model, axis);
    }
}

/* M placed so that its middle is where HOST's model has its middle: its bounding sphere (+0x00, floats in 1/1024 units, which the moby update
 * loop refreshes from its last place) tells how far its middle is from its origin, a frame late as it turns. */
static void centre(u8 *m, void *host, f32 lift)
{
    volatile f32 *sphere = (volatile f32 *)m;
    f32 offset[3];
    s32 axis;
    for (axis = 0; axis < 3; axis++) {
        offset[axis] = sphere[3] > 0.0f ? sphere[axis] / 1024.0f - *(volatile f32 *)(m + MOBY_POS + 4 * axis) : 0.0f;
        if (offset[axis] > CENTRE_MAX || offset[axis] < -CENTRE_MAX) {
            offset[axis] = 0.0f;            /* a sphere not yet refreshed from this place */
        }
    }
    place(m, m, host, lift);
    for (axis = 0; axis < 3; axis++) {
        *(volatile f32 *)(m + MOBY_POS + 4 * axis) -= offset[axis];
    }
}

/* After the update of HOST (KEY), which shows ITEM: the stand-in, made when there is none, stands where the host's
 * own model would (host_lift) and moves as the item does, a pack with its Clank, and the host draws at scale 0; once the host is gone, so is
 * the stand-in. */
static void carry(u16 key, void *host, s32 item, s32 L)
{
    const struct ItemModel *model;
    struct StandIn *e;
    u8 *m;
    s32 class_id;
    if (MOBY_STATE(host) < 0) {             /* collected, or deleted at its first update as collected before */
        drop(key, host, L);
        return;
    }
    e = entry_of(key);
    if (e == 0) {
        return;
    }
    if (!e->lift_known) {
        e->lift = host_lift(host, L);
        e->lift_known = 1;
    }
    MOBY_SCALE_F(host) = 0.0f;
    if (item == RZ_GOLD_BOLT) {
        carry_bolt(e, host, L);
        return;
    }
    model = model_of(item, L);
    class_id = ee_debug_rz_model > 0 && class_in_memory(ee_debug_rz_model, L) ? ee_debug_rz_model : model->class_id;
    m = live(e, host);
    if (m != 0 && e->class_id != class_id) {    /* the debug class changed */
        LVL(L, func_0020D678, DeleteFn)(m);
        m = e->model = 0;
        if (e->body != 0) {
            LVL(L, func_0020D678, DeleteFn)(e->body);
            e->body = 0;
        }
    }
    if (m == 0) {
        e->mark = STAND_IN_MARK;
        m = e->model = make_part(class_id, STAND_IN_MARK, host, L);
        if (m == 0) {
            return;
        }
        MOBY_SCALE_F(m) *= model->scale;
        if (model == &MODELS[item] && class_id == model->class_id) {    /* not the chicken, nor a debug class */
            pose(m, item, model, L);
        }
        if (class_id == CHICKEN_CLASS) {
            LVL(L, func_L01_00309848, BoundsFn)(m);
        }
        e->class_id = (u16)class_id;
        e->motion = model->motion;
        e->flags = model->flags;
        if (model == &MODELS[item] && model->source == GADGET) {
            e->flags |= ANIMATE;            /* sequence 1 from the start, as the vendor and the Gadgets page show
                                             * them: a gadget's class plays its draw (sequence 0) when made, and its
                                             * sound with it, over and over (the Metal Detector's equip sound) */
        }
        e->model_ref = model;
    }
    if ((e->flags & ON_CLANK) && e->body == 0 && class_in_memory(CLANK_CLASS, L)) {
        e->body = make_part(CLANK_CLASS, BODY_MARK, host, L);
    }
    if (e->flags & CENTRE) {
        centre(m, host, e->lift);
    } else {
        place(m, m, host, e->lift);
    }
    move(m, e->motion, host);
    if (e->class_id == CHICKEN_CLASS) {     /* a Gold Bolt's glow: the hosts' own (pickups, infobots) shine round
                                             * the others, and the chicken's own update never runs */
        LVL(L, func_L01_00309928, BoundsFn)(m);
    }
    if (e->flags & ANIMATE) {
        animate(m, e->model_ref->seq ? e->model_ref->seq : 1, L);
    }
    if (e->body != 0) {
        place(e->body, m, host, e->lift);
        animate(e->body, 1, L);
    }
}

/* --- the hosts ----------------------------------------------------------------------------------------------- */

/* A host's identity for its stand-in, kept when the game moves the host to another slot. */
#define KEY_SPOT(spot)      ((u16)(0x100 + (spot)))
#define KEY_PICKUP          ((u16)0x200)
#define KEY_INFOBOT(planet) ((u16)(0x300 + (planet)))

/* The world pickups: an item lying in a level, which on its touch starts a cutscene showing the item (and, for the
 * Trespasser and the Hydrodisplacer, Ratchet's hold-up after it), then gives it, saves and deletes itself. With
 * another item for its check, the touch is taken before the pickup's update can see it: the check collected, the
 * pickup's mission trigger done as its own touch does, the autosave, the pickup and its stand-in deleted. The test
 * is the pickup's own (its update's distance to the hero, from its idle state 1; the Raritanium has no states), a
 * little wider so that it always comes first. The Map-o-Matic's giver is a character (WatcherUpdate), not a
 * pickup. Radii from each update's disassembly (2026-10-25). */
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

/* A check another item than its retail one holds, not collected yet: the host shows a stand-in. */
static s32 shuffled(s32 check, s32 own_item)
{
    return check >= 0 && !ee_rz_check_collected(check) && ee_rz_layout_item(check) != own_item;
}

static const struct PickupSite *pickup_site(void *moby, s32 L)
{
    u32 i;
    for (i = 0; i < PICKUP_SITES; i++) {
        if (PICKUPS[i].level == L && PICKUPS[i].class_id == (u16)MOBY_CLASS(moby)) {
            return &PICKUPS[i];
        }
    }
    return 0;
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

/* --- the vendor's weapon demo ---------------------------------------------------------------------------------
 *
 * A weapon found in the world plays the demo the vendor plays for a weapon bought: on leaving, the vendor's menu
 * (VendorMenuUpdate) runs its exit (state 2, 40 frames), then, for an item with a demo scene
 * (vendor_item_cutscene_index, -1 none: the weapons the vendor sells), start_vendor_exit_cutscene (func_L00_0029C070)
 * places the scene in front of the vendor moby, puts the weapon in Ratchet's hand and streams the scene, which
 * state 3 plays; at its end close_vendor_menu puts Ratchet back in front of the vendor and returns to play. The demo
 * is the vendor's: it plays from the vendor's mode only, for a vendor the menu was opened on (its mode word +0x40
 * 0; the remote vendor the game opens with no moby never plays one). So the find opens the vendor's menu on a vendor
 * moby of its own (class 11, every level has it), made at the find and drawn at scale 0, and puts the menu straight
 * into its exit with the weapon noted (+0x48, the item just bought). The vendor moby is deleted once play resumes. */
DECLARE_LVL(func_L00_0029BA20);         /* open_vendor_menu(vendor moby) */
DECLARE_LVL_DATA(vendor_state);         /* the vendor's record (D_L00_001CA7C0 in level 0) */
DECLARE_LVL_DATA(vendor_demos);         /* per item id, its demo scene, -1 none */
typedef void (*OpenFn)(void *);

#define VENDOR_CLASS        11
#define VENDOR_MENU_STATE   0x0         /* the record: the menu's state, 2 its exit, */
#define VENDOR_MENU_FRAMES  0x4         /*   the frames spent in it, */
#define VENDOR_NOTED        0x48        /*   the item just bought (the demo's), -1 none */
#define VENDOR_EXIT         2


/* An item the vendor sells: the weapons and the Taunter, the Drone Device. The demo table's entries past the
 * vendor's items (the gadgets' ids, 28 on) are other data, which reads as scene 0, the Bomb Glove's. */
static s32 vendor_item(s32 item)
{
    return (item >= RZ_SUCK_CANNON && item <= RZ_DECOY_GLOVE) || item == RZ_TAUNTER || item == RZ_DRONE_DEVICE;
}

/* The demo of ITEM, found at POS in level L: 1 if it starts, 0 for an item without one (the RYNO has none). */
static s32 start_weapon_demo(s32 item, const f32 *pos, s32 L)
{
    s32 id = rz_item_game_id(item);
    u8 *vendor_record = LVL_DATA(L, vendor_state, u8 *);
    u8 *m;
    if (!vendor_item(item) || LVL_DATA(L, vendor_demos, s32 *)[id] < 0 || demo_vendor != 0
        || !class_in_memory(VENDOR_CLASS, L)) {
        return 0;
    }
    m = LVL(L, func_0020D348, CreateFn)(VENDOR_CLASS);
    if (m == 0) {
        return 0;
    }
    *(volatile f32 *)(m + MOBY_POS) = pos[0];
    *(volatile f32 *)(m + MOBY_POS + 4) = pos[1];
    *(volatile f32 *)(m + MOBY_POS_Z) = pos[2];
    MOBY_ROT_F(m, 2) = LVL(L, func_L00_001FF860, AtanFn)(HERO_POS[0] - pos[0], HERO_POS[1] - pos[1]);
    MOBY_SCALE_F(m) = 0.0f;
    LVL(L, func_L00_00251E30, BoundsFn)(m);
    LVL(L, func_L00_0029BA20, OpenFn)(m);
    *(volatile s32 *)(vendor_record + VENDOR_MENU_STATE) = VENDOR_EXIT;
    *(volatile s32 *)(vendor_record + VENDOR_MENU_FRAMES) = 0;
    *(volatile s32 *)(vendor_record + VENDOR_NOTED) = id;
    demo_vendor = m;
    return 1;
}

/* Each gameplay frame: a demo's vendor moby goes once play has resumed. */
void rz_world_frame(s32 L)
{
    if (demo_vendor != 0) {
        if (MOBY_CLASS(demo_vendor) == VENDOR_CLASS && MOBY_STATE(demo_vendor) >= 0) {
            LVL(L, func_0020D678, DeleteFn)(demo_vendor);
        }
        demo_vendor = 0;
    }
}

/* ITEM was found at HOST, in level L: a weapon's demo, if it has one. */
static void found(s32 item, void *host, s32 L)
{
    f32 pos[3];
    pos[0] = *(volatile f32 *)((u8 *)host + MOBY_POS);
    pos[1] = *(volatile f32 *)((u8 *)host + MOBY_POS + 4);
    pos[2] = *(volatile f32 *)((u8 *)host + MOBY_POS_Z);
    start_weapon_demo(item, pos, L);
}

/* HOST's check taken at once: its mission trigger done (as a pickup's own touch does), the check collected with
 * its banner, the autosave, the host and its stand-in deleted; a weapon's demo then. */
static void take(void *host, u16 key, s32 check, s32 L)
{
    if (MOBY_MISSION(host) != 0xFF) {
        LVL(L, func_L00_002512D8, MissionFn)(MOBY_MISSION(host));
    }
    rz_play_collect(check, L);
    ee_rz_save_c(0, -1, L);
    drop(key, host, L);
    LVL(L, func_0020D678, DeleteFn)(host);
    found(ee_rz_layout_item(check), host, L);   /* the deleted moby's record keeps its place */
}

/* A world infobot's check: one of level L's own, unlocking the planet at its pvars; -1 for any other infobot. */
static s32 infobot_check(void *moby, s32 L)
{
    s32 planet = *(volatile s32 *)(MOBY_PVARS(moby) + INFOBOT_PLANET);
    s32 check = planet >= 1 && planet <= 18 ? rz_check_for_vanilla(RZ_INFOBOT_NOVALIS + planet - 1) : -1;
    const struct RzCheck *row = rz_check(check);
    return row != 0 && row->planet == L ? check : -1;
}

/* The check of HOST, a pickup or a world infobot; -1 for none. */
static s32 host_check(void *host, s32 L)
{
    const struct PickupSite *site;
    if (MOBY_CLASS(host) == INFOBOT_CLASS) {
        return infobot_check(host, L);
    }
    site = pickup_site(host, L);
    return site != 0 ? rz_check_for_vanilla(site->item) : -1;
}

/* A live stand-in bolt's update under way: its borrowed flag's byte and value, and the check its touch is for. */
static struct {
    s32 active;
    s32 index;
    u8 saved;
    s32 check;
    s32 taken;
} borrow;

/* memcard_Save from a live stand-in bolt's update (GoldBoltUpdate saves once it has set its flag): the flag's byte
 * gets its own value back before the save writes it, and the host's check is collected (src/randomizer_play.c's
 * save hook calls this first). */
void rz_world_before_save(s32 L)
{
    if (borrow.active && GOLD_FLAGS[borrow.index] != 0 && !borrow.taken) {
        GOLD_FLAGS[borrow.index] = borrow.saved;
        borrow.taken = 1;
        rz_play_collect(borrow.check, L);
    }
}

/* The key HOST, a pickup or a world infobot, keeps its stand-in under. */
static u16 host_key(void *host)
{
    if (MOBY_CLASS(host) == INFOBOT_CLASS) {
        return KEY_INFOBOT(*(volatile s32 *)(MOBY_PVARS(host) + INFOBOT_PLANET));
    }
    return KEY_PICKUP;
}

/* Clank at BOLT, as near as GoldBoltUpdate's touch wants Ratchet. GoldBoltUpdate takes no touch from Clank (only in
 * hero modes 0 and 3): retail puts no Gold Bolt where Clank goes, but a pickup or an infobot of a Clank section (on
 * Orxon before the O2 Mask) can hold one. */
#define HERO_CLANK      1
#define GOLD_REACH      3.0f            /* GoldBoltUpdate's touch: within 3 units, */
#define GOLD_HEIGHT     2.0f            /*   2 in height */

static s32 clank_touches(void *bolt)
{
    volatile f32 *pos = (volatile f32 *)((u8 *)bolt + MOBY_POS);
    f32 dx = HERO_POS[0] - pos[0];
    f32 dy = HERO_POS[1] - pos[1];
    f32 dz = HERO_POS[2] - pos[2];
    return HERO_MODE == HERO_CLANK && MOBY_STATE(bolt) == GOLD_RESTING && HERO_HEALTH > 0
           && dx * dx + dy * dy + dz * dz < GOLD_REACH * GOLD_REACH && dz < GOLD_HEIGHT && -dz < GOLD_HEIGHT;
}

/* A live stand-in bolt's update, its flag borrowed; once its touch took it, the host goes too. Clank's touch takes
 * it at once, as a pickup holding another item is taken: Ratchet's hold-up is not his. */
static void bolt_update(void *bolt, void (*update)(void *), s32 L)
{
    void *host = MOBY_PARENT(bolt);
    s32 check;
    if (*(volatile s32 *)LVL_DATA(L, game_mode, u8 *) != MODE_PLAY) {
        return;
    }
    check = host_check(host, L);
    ee_debug_rz_bolt[2] = check;
    if (check < 0 || ee_rz_check_collected(check)) {
        LVL(L, func_0020D678, DeleteFn)(bolt);
        return;
    }
    if (clank_touches(bolt)) {
        take(host, host_key(host), check, L);
        return;
    }
    borrow.index = L * GOLD_SPOTS + BORROWED_SPOT;
    borrow.saved = GOLD_FLAGS[borrow.index];
    borrow.check = check;
    borrow.taken = 0;
    borrow.active = 1;
    GOLD_FLAGS[borrow.index] = 0;
    update(bolt);
    if (MOBY_STATE(bolt) >= GOLD_HOLDING) {
        MOBY_UPDATE_DIST(bolt) = UPDATE_ALWAYS;
    }
    ee_debug_rz_bolt[0]++;
    ee_debug_rz_bolt[1] = MOBY_STATE(bolt);
    ee_debug_rz_bolt[3] = borrow.taken;
    rz_world_before_save(L);                /* a flag set without a save, should GoldBoltUpdate ever do that */
    GOLD_FLAGS[borrow.index] = borrow.saved;
    borrow.active = 0;
    if (borrow.taken) {
        if (MOBY_MISSION(host) != 0xFF) {
            LVL(L, func_L00_002512D8, MissionFn)(MOBY_MISSION(host));
        }
        LVL(L, func_0020D678, DeleteFn)(host);
    }
}

/* Play or an in-level cutscene, where the stand-ins are kept. The galactic map runs the level's moby updates too;
 * with the stand-ins kept there, leaving Oltanis crashed the game on the map every time (an R5900 jump to an address
 * made of text), and the same flight without them did not. There a stand-in only keeps its own update from running. */
static s32 in_level(s32 L)
{
    s32 mode = *(volatile s32 *)LVL_DATA(L, game_mode, u8 *);
    return mode == MODE_PLAY || mode == MODE_CINEMATIC;
}

/* Before the update of MOBY in level L: 1 when this file ran it or the update must not run. A stand-in never runs
 * its class's update, a live stand-in bolt runs GoldBoltUpdate with a borrowed flag; a Gold Bolt spot holding
 * another item takes its touch quietly and carries its stand-in; a pickup holding another item is taken on its
 * touch, or, holding a Gold Bolt, stands still and hidden, its own touch test never run, while the bolt waits. */
s32 rz_world_before(void *moby, void (*update)(void *), s32 L)
{
    s32 check;
    if (marked(moby)) {
        return 1;
    }
    if (!in_level(L)) {
        return 0;
    }
    if (mark_of(moby) == BOLT_MARK) {
        bolt_update(moby, update, L);
        return 1;
    }
    if (MOBY_CLASS(moby) == GOLD_CLASS) {
        s32 spot = *(volatile s32 *)MOBY_PVARS(moby);
        check = spot >= 0 && spot < GOLD_SPOTS ? rz_check_for_bolt(L, spot) : -1;
        if (!shuffled(check, RZ_GOLD_BOLT)) {
            update(moby);
            return 1;
        }
        *(volatile s32 *)(MOBY_PVARS(moby) + GOLD_QUIET) = 1;
        update(moby);
        if (ee_rz_check_collected(check)) {     /* taken by this update: its autosave collected the check */
            found(ee_rz_layout_item(check), moby, L);
        }
        carry(KEY_SPOT(spot), moby, ee_rz_layout_item(check), L);
        return 1;
    }
    {
        const struct PickupSite *site = pickup_site(moby, L);
        if (site != 0) {
            check = rz_check_for_vanilla(site->item);
            if (shuffled(check, site->item) && ee_rz_layout_item(check) == RZ_GOLD_BOLT) {
                carry(KEY_PICKUP, moby, RZ_GOLD_BOLT, L);
                return 1;
            }
            if (shuffled(check, site->item) && pickup_touched(moby, site)) {
                take(moby, KEY_PICKUP, check, L);
                return 1;
            }
        }
    }
    return 0;
}

/* A dormant infobot. Its first update (state 0) hides it (+0x34 bits 0x41) in state 1 unless its pvars + 0 is set,
 * and the level wakes it later: Blarg Station has two Rilgar infobots, one locked in the warship's room and one at
 * the warship's shuttle, which wakes once Ratchet is back from the self-destruct. Both are their planet's check, kept
 * under the same key: a dormant one shows nothing, and leaves the stand-in to the other unless it carried it. */
static void leave(u16 key, void *host, s32 L)
{
    s32 i;
    for (i = 0; i < STAND_INS; i++) {
        if (stand_ins[i].key == key && stand_ins[i].host == host) {
            drop(key, host, L);
        }
    }
}

/* After the update of MOBY in level L: a pickup or a world infobot holding another item carries its stand-in, and
 * an infobot that turned to its message is taken instead; a dormant infobot shows none. */
void rz_world_after(void *moby, s32 L)
{
    const struct PickupSite *site;
    s32 check;
    if (!in_level(L)) {
        return;
    }
    if (MOBY_CLASS(moby) == INFOBOT_CLASS) {
        s32 planet = *(volatile s32 *)(MOBY_PVARS(moby) + INFOBOT_PLANET);
        check = infobot_check(moby, L);
        if (!shuffled(check, RZ_INFOBOT_NOVALIS + planet - 1)) {
            return;
        }
        if (MOBY_STATE(moby) == INFOBOT_DORMANT) {
            leave(KEY_INFOBOT(planet), moby, L);
            return;
        }
        if (MOBY_STATE(moby) == INFOBOT_TOUCHED) {
            take(moby, KEY_INFOBOT(planet), check, L);
            return;
        }
        carry(KEY_INFOBOT(planet), moby, ee_rz_layout_item(check), L);
        return;
    }
    site = pickup_site(moby, L);
    if (site != 0) {
        check = rz_check_for_vanilla(site->item);
        if (shuffled(check, site->item)) {
            carry(KEY_PICKUP, moby, ee_rz_layout_item(check), L);
        }
    }
}
