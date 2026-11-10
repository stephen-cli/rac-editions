/* The Randomizer in play, shared by its four source files:
 *
 *   src/randomizer_play.c    the checks: the hooks that turn a retail grant into the seed's item, the collected
 *                            view the givers see, the autosaves, the early-O2 fixes
 *   src/randomizer_world.c   what the world shows for a check (the stand-ins of Gold Bolt spots, world pickups and
 *                            world infobots holding another item) and how it is taken
 *   src/randomizer_text.c    the banners that name what was received, and the givers' prompts with the item's
 *                            name hidden
 *   src/randomizer_vendor.c  the Gadgetron vendor's list, and its mystery entries (the tile, "???", the chicken)
 *   src/randomizer_map.c     the pause map's reward markers, the mystery tile until their check is collected
 *   src/randomizer_hud.c     the health display's 7 orbs
 *
 * The record behind it all (the layout, the checks collected) is src/randomizer_save.c; the logic that made the
 * layout is src/randomizer.c. */
#ifndef RANDOMIZER_PLAY_H
#define RANDOMIZER_PLAY_H

#include "modseg.h"
#include "randomizer.h"

/* --- the game's memory (src/game.h) -------------------------------------------------------------------------- */

#define PERSUADER_OWNED item_available[0x23]

#define ITEMS           37              /* the game's item ids, 0-36 (36: the edition's Charge Boots) */
#define CHARGE_BOOTS_ID 36              /* src/goodies.c, tools/gen_items.py */
#define TEXT_CHARGE_BOOTS 0x5406        /* "Charge Boots" (text/strings.tsv) */
#define TEXT_BOLT_CACHE 0x5426          /* "Bolt Cache": the Charge Boots' check in a build without them */
#define BOLT_CACHE      2500            /* its bolts, the bonus vendor entry's price */

/* The special items' bytes in gSpecialItems. */
#define FLAG_BYTES      8
#define FLAG_ZOOMERATOR 0
#define FLAG_RARITANIUM 1
#define FLAG_CODEBOT    2
#define FLAG_PREMIUM    4
#define FLAG_ULTRA      5

/* --- a level's mobys: MOBY(address) in src/game.h; save_id is the level instance's uid --------------------- */

/* The level's moby array, MOBY_SIZE apart: a record each level program has at its own address (tools/modseg.py),
 * the table first, then the end of the mobys the level placed (spawned ones follow), and at + 0x10 the end of the
 * array. */
DECLARE_LVL_DATA(mobys_placed_end);
DECLARE_LVL_DATA(mobys_end);
#define MOBYS_PLACED_END(L) (*LVL_DATA(L, mobys_placed_end, volatile u32 *))
#define MOBY_TABLE(L)       (LVL_DATA(L, mobys_placed_end, volatile u32 *)[-1])
#define MOBYS_END(L)        (*LVL_DATA(L, mobys_end, volatile u32 *))

/* --- the game's functions, as the hooks call them (each program has its own copy: LVL) ------------------------ */

DECLARE_LVL(GetIconFrame);   /* GetIconFrame(icon, frame) */
DECLARE_LVL(msg_string);     /* msg_string(id) */
DECLARE_LVL(ShowBanner);     /* ShowBanner(text, ticks) */
typedef s32 (*IconFn)(s32, s32);
typedef const char *(*MsgFn)(s32);
typedef void (*BannerFn)(s32, s32);
typedef void (*VoidFn)(void);

const char *ee_msg_string_edition(s32 id);          /* src/text.c */

/* --- the Missions page and the map (tools/gen_randomizer.py's tables) ------------------------------------------ */

/* What a mission condition names, as Mission_CheckCondition (check_mission_condition) tests it: a planet unlocked, an
 * item owned (its "had" byte, type 3, names the same item), a special item's flag byte. */
#define RZ_SOURCE_PLANET    1
#define RZ_SOURCE_ITEM      2
#define RZ_SOURCE_FLAG      6
#define RZ_MISSION_SOURCES  4

struct RzSource {
    u8 kind;                            /* RZ_SOURCE_* */
    u8 arg;                             /* the planet, the item id or the flag byte */
};

/* A text of the Missions page or a map label that names a reward: its planet and the rewards its mission's
 * conditions name (a label's: its marker's mission's, and the reward its icon shows). */
struct RzMissionText {
    u16 text;
    u8 planet;
    u8 count;
    struct RzSource sources[RZ_MISSION_SOURCES];
};

/* A map marker whose icon is a reward's own: an item's HUD icon, a special item's, an infobot's marker. */
struct RzMapIcon {
    u8 planet;
    u16 texture;
    u8 frame;
    u8 backdrop;                        /* drawn on a dark backdrop (the icon's flags & 0x40) */
    struct RzSource source;
};

extern const struct RzMissionText ee_rz_mission_texts[];
extern const s32 ee_rz_mission_text_count;
extern const struct RzMapIcon ee_rz_map_icons[];
extern const s32 ee_rz_map_icon_count;

/* --- between the files ----------------------------------------------------------------------------------------- */

/* randomizer_play.c */
s32 rz_play_active(void);                           /* the game in memory is a Randomizer save */
s32 rz_play_owned(s32 item_id);                     /* the item is really owned, whether or not the view is on */
void rz_play_collect(s32 check, s32 L);             /* CHECK collected now: its item, once, with its banner */
s32 ee_rz_save_c(s32 slot, s32 flags, s32 L);       /* memcard_Save, with the collected view off around it */
s32 rz_play_map_planet(s32 L);                      /* the planet the pause map and the Missions page show */
s32 rz_play_hidden_check(const struct RzSource *source, s32 planet);   /* SOURCE's check: PLANET's, uncollected */

/* randomizer_world.c */
void rz_world_forget(void);                         /* a level's visit starts: no stand-ins yet */
s32 rz_world_before(void *moby, void (*update)(void *), s32 L); /* 1: it ran MOBY's update, or none must run */
void rz_world_after(void *moby, s32 L);             /* after MOBY's update */
void rz_world_before_save(s32 L);                   /* memcard_Save is called: a live stand-in bolt's touch */
void rz_world_frame(s32 L);                         /* each gameplay frame */
void rz_world_any_frame(s32 L);                     /* each frame in any game mode: the map's release of copies */

/* randomizer_text.c */
void rz_show_item_banner(s32 check, s32 item, s32 L);   /* "You found the ...!" for ITEM received at CHECK */

/* randomizer_vendor.c */
void rz_vendor_note_purchase(s32 vendor_item_id, s32 reward_item_id);  /* a vendor check was bought (GiveItem) */
void rz_vendor_forget_purchase(void);
void rz_vendor_menu_closed(void);                   /* a gameplay frame ran: the vendor's menu is not up */
s32 rz_vendor_menu_open(void);
s32 rz_vendor_purchase_selected(s32 L);              /* the vendor's cursor is on a purchase entry (a check) */

#endif
