/* The game's resident data the edition reads and writes, declared as the game's own code declares it: by
 * name, with the linker supplying each address. The names and the Hero and PadState layouts are OpenRAC's
 * (games/rac1/include: names.h, structs/hero.h, structs/pad.h). A read through a name is `lui` + a load with
 * the low half as its offset, the form retail's code has; the same read through an address literal costs an
 * `ori` more.
 *
 * Resident means below 0x15F000: the core's data, at one address whatever program runs. A level program's own
 * data moves with the level and goes through LVL_DATA() (src/modseg.h).
 *
 * The hero is declared as the game's own code declares it, so the compiler shares one address across a
 * function's reads and keeps a value it has just read. The rest is declared volatile: every use reads or writes
 * memory.
 *
 * Where OpenRAC has no name for a symbol, or one that says something other than what the data is, the symbol
 * keeps its address name and the edition's name for it is a macro beside it, as names.h does it. */
#ifndef GAME_H
#define GAME_H

#include "structs/hero.h"
#include "structs/pad.h"
#include "structs/moby.h"
#include "structs/records.h"

/* Ratchet (or Clank, or Giant Clank: g_hero.vehicle_mode) and the first controller. */
extern Hero g_hero;
extern volatile PadState g_controller_state;

/* A class's sequence table (MobyClass.seqs, a pointer per sequence a moby of the class can play; seq_count of
 * them are in use, and an empty slot holds 0). The header's first word, which MobyClass has no name for yet,
 * is the address of the class's next block of data once the class is loaded (Ratchet's class: right after
 * its 162 slots), so a slot at or past that address is not the table's. The edition's sequences are built as
 * words (tools/gen_anim*.py) and enter a table as SEQUENCE(words). */
#define CLASS_HAS_SLOT(pclass, slot)    ((u32)&(pclass)->seqs[slot] < *(const u32 *)(pclass))
#define SEQUENCE(words)                 ((AnimSeq *)(words))

/* The frame's timing: a frame's seconds and their square. pal_mode is nonzero at 50 Hz. */
extern volatile f32 g_frame_time;
extern volatile f32 g_frame_time_sq;
extern volatile f32 g_frame_scale_sq;
extern volatile s32 pal_mode;

/* The saved game's globals. */
extern volatile s32 g_current_level_index;      /* rewritten by a load while the old level still runs: src/modseg.h */
extern volatile s32 g_game_language;            /* 0 English, 2 French, 3 German, 4 Spanish, 5 Italian */
extern volatile s32 gBolts;
extern volatile s32 hero_max_health;            /* 4, 5 with the Premium Nanotech, 8 with the Ultra */
extern volatile s32 challenge_mode;             /* set after the credits; the game treats it as Challenge Mode */
extern volatile s32 D_0015EF20;                 /* times the game was completed; > 0 in a Challenge Mode run */
#define g_completes D_0015EF20
extern volatile s32 active_play_frame_count;    /* the bolt limiter's active frames since a respawn's drop */
extern volatile s32 counted_bolts_total;        /* the bolt limiter's bolts from respawns */
extern volatile u8 gCheats[];                   /* 12 bytes; retail uses 0-7 (5: mirrored animations) */
extern volatile u8 D_0015EEC0[];                /* a byte per cheat: unlocked, so listed on the Cheats page */
#define cheats_seen D_0015EEC0
extern volatile s32 invert52;                   /* the first of the three camera options, saved in a row */
extern volatile s32 playback_mode;              /* nonzero: stereo */
extern volatile s32 music_volume;
extern volatile s32 sound_volume;
extern volatile u8 gHelpDeskVoice;              /* 0 off */
extern volatile u8 gHelpDeskText;               /* 0 off */
extern volatile u8 subtitles_enabled;

/* What the player has, per item id (0-36). */
extern volatile u8 item_available[];            /* owned */
extern volatile u8 items_acquired[];            /* "has had": GiveItem sets it; pickups and some characters test it */
extern volatile s32 weapon_ammo_counts[];       /* ammo held */
extern volatile s32 D_00141760[];               /* the hero's equipped item per slot, saved */
#define equipped_items D_00141760
extern volatile u32 D_00141FA0[];               /* the quick-select ring's eight items, saved */
#define quick_select_items D_00141FA0

/* The galaxy and the levels' saved state. */
extern volatile u8 level_available[];           /* unlocked, per planet 0-19 */
extern volatile s32 gGalaxyList[];              /* the galactic map's planets, in unlock order */
extern volatile u8 gSpecialItems[];             /* the global flags: the special items' bytes */
extern volatile u8 gold_bolts_collected[];      /* a Gold Bolt spot taken, [level * 4 + spot] */
extern volatile u8 saved_mission_state[];       /* per level 16 bytes: a level trigger done (0xFF) */
extern volatile u8 level_moby_killed[];         /* per level 256 bytes: the save bits by moby uid */
extern u8 help_records[];                       /* the HelpDesk's saved records */

/* The memory card. OpenRAC has no layout for its state yet; the fields the edition reads are named where they
 * are used (src/goodies.c, src/randomizer_save.c). */
extern volatile u8 memory_card_state[];
extern const char D_0013D340[];                 /* "/BESCES-50916RATCHET/BESCES-50916RATCHET", the save's main file */
#define memcard_main_file D_0013D340
extern u8 memcard_game_data_buffer[];           /* memcard_PrepData's output: the game's records */
extern u8 memcard_level_data_buffer[];          /* and a level page's */
extern volatile s32 mode_freeze_flags;          /* bit 2 a new game being started, bit 4 a load */

/* Sound: the mixer's six group levels are 0x48 into the voice pool's record. */
extern u8 voice_pool[];

/* The display. */
/* The GS viewport record: width, height, half width, half height, then the screen's left, top, right and
 * bottom edges in 12.4 around 2048. */
extern volatile s32 g_screen_extent[];
extern volatile s16 D_001519D0[];               /* the game's screen width and height, from its resolution table */
#define display_resolution D_001519D0
extern volatile u32 draw_buffer_address;
extern u8 fs_aa_buffer[];                       /* the frame setup (FrameCfg) */
extern u32 fs_aa_resample_packet[];             /* the movie frame's GS packet */

#endif
