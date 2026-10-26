/* The Randomizer in play, shared by its four source files:
 *
 *   src/randomizer_play.c    the checks: the hooks that turn a retail grant into the seed's item, the collected
 *                            view the givers see, the autosaves, the early-O2 fixes
 *   src/randomizer_text.c    the banners that name what was received, and the givers' prompts with the item's
 *                            name hidden
 *   src/randomizer_vendor.c  the Gadgetron vendor's list, and its mystery entries (the tile, "???", the chicken)
 *   src/randomizer_hud.c     the health display's 7 orbs
 *
 * The record behind it all (the layout, the checks collected) is src/randomizer_save.c; the logic that made the
 * layout is src/randomizer.c. */
#ifndef RANDOMIZER_PLAY_H
#define RANDOMIZER_PLAY_H

#include "randomizer.h"

/* --- the game's memory --------------------------------------------------------------------------------------- */

#define INVENTORY       ((volatile u8 *)0x0013D5C8)      /* owned, per item id 0-36 */
#define HAD             ((volatile u8 *)0x0013D5F0)      /* "has had", per item id: GiveItem sets it; pickups and
                                                          * some characters test it rather than INVENTORY */
#define PLANETS         ((volatile u8 *)0x0013DE48)      /* unlocked, per planet 0-19 */
#define FLAGS           ((volatile u8 *)0x0013D490)      /* the global flags: the special items' bytes */
#define GOLD_FLAGS      ((volatile u8 *)0x0014BFC0)      /* a Gold Bolt spot taken, [level * 4 + spot] */
#define PERSUADER_OWNED (*(volatile u8 *)0x0013D5EB)
#define CURRENT_LEVEL   (*(volatile s32 *)0x0015EE84)
#define MAX_HEALTH      (*(volatile s32 *)0x0015EEA0)    /* gMaxNanotech: 4, 5 with the Premium, 8 with the Ultra */
#define PAL_MODE        (*(volatile s32 *)0x0015EE80)
#define CHEAT_SEEN      (*(volatile u8 *)(0x0015EEC0 + EE_CHEAT_ENTRY))

#define HERO_BASE       0x0013F450
#define HERO_STATE      (*(volatile s32 *)(HERO_BASE + 0x2084))
#define HERO_MODE       (*(volatile u8 *)(HERO_BASE + 0x20A4))   /* 1 Clank, 2 a vehicle */
#define HERO_HEALTH     (*(volatile s32 *)(HERO_BASE + 0x22A8))
#define CLANK_HEALTH_MAX (*(volatile s32 *)(HERO_BASE + 0x22AC))

#define ITEMS           37              /* the game's item ids, 0-36 (36: the edition's Charge Boots) */
#define CHARGE_BOOTS_ID 36              /* src/goodies.c, tools/gen_items.py */
#define TEXT_CHARGE_BOOTS 0x5406        /* "Charge Boots" (text/strings.tsv) */

/* The special items' bytes in FLAGS. */
#define FLAG_BYTES      8
#define FLAG_ZOOMERATOR 0
#define FLAG_RARITANIUM 1
#define FLAG_CODEBOT    2
#define FLAG_PREMIUM    4
#define FLAG_ULTRA      5

/* --- a moby (include/moby.h) --------------------------------------------------------------------------------- */

#define MOBY_POS        0x10            /* x, y, z */
#define MOBY_POS_Z      0x18
#define MOBY_SCALE      0x2C
#define MOBY_ROT        0x40            /* x, y, z */
#define MOBY_STATE(m)   (*(volatile s8 *)((u8 *)(m) + 0x20))    /* negative: dead */
#define MOBY_PVARS(m)   (*(volatile u32 *)((u8 *)(m) + 0x78))
#define MOBY_CLASS(m)   (*(volatile u16 *)((u8 *)(m) + 0xA6))   /* oclass */
#define MOBY_UID(m)     (*(volatile u16 *)((u8 *)(m) + 0xB2))   /* the level instance's uid, its save id */
#define MOBY_TABLE      (*(volatile u32 *)0x00160058)          /* the level's mobys, MOBY_SIZE apart */
#define MOBY_SIZE       0x100

/* --- the game's functions, as the hooks call them (each program has its own copy: LVL) ------------------------ */

DECLARE_LVL(func_00200198);             /* GetIconFrame(icon, frame) */
DECLARE_LVL(func_001FE540);             /* msg_string(id) */
DECLARE_LVL(func_L00_00264DB8);         /* ShowBanner(text, ticks) */
typedef s32 (*IconFn)(s32, s32);
typedef const char *(*MsgFn)(s32);
typedef void (*BannerFn)(s32, s32);
typedef void (*VoidFn)(void);

const char *ee_msg_string_edition(s32 id);          /* src/text.c */

/* --- between the files ----------------------------------------------------------------------------------------- */

/* randomizer_play.c */
s32 rz_play_active(void);                           /* the game in memory is a Randomizer save */
s32 rz_play_owned(s32 item_id);                     /* the item is really owned, whether or not the view is on */

/* randomizer_text.c */
void rz_show_item_banner(s32 check, s32 item, s32 L);   /* "You found the ...!" for ITEM received at CHECK */

/* randomizer_vendor.c */
void rz_vendor_note_purchase(s32 vendor_item_id, s32 reward_item_id);  /* a vendor check was bought (GiveItem) */
void rz_vendor_forget_purchase(void);
void rz_vendor_menu_closed(void);                   /* a gameplay frame ran: the vendor's menu is not up */
s32 rz_vendor_menu_open(void);

#endif
