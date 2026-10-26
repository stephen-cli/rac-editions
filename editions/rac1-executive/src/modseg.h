/* Executive Edition resident code: shared declarations.
 * Built by tools/modseg.py with the game's compiler (SN GCC 2.95.3, -O2 -G0).
 * Game symbols are addresses by name. Only the resident core (below
 * 0x15F000: core text, data, bss, lit) is at one address; the executable's
 * own game text and data are re-linked into every level program at
 * level-specific addresses, as is level code. So any function above the
 * core, exe-kind or level-only, is reached through LVL(), a table the build
 * generates (tools/modseg.py) with an address per program: the 19 levels'
 * from OpenRAC's overlay catalogue, then the title screen's two programs'
 * (FE_BOOT, FE_BIN; tools/gen_frontend.py), 0 where a program has no copy.
 * It is indexed by the program running; that program's data through
 * LVL_DATA().
 *
 * Which level is running is NOT the game's current-level variable
 * (0x15EE84): loading a save from the pause menu rewrites the save's
 * globals, that variable included, while the old level program still runs
 * its last frames, and a hook indexing the tables with the new level then
 * jumped into the old program at the new level's addresses (crash "jump to
 * unaligned address" on the second load, 2026-10-17). A hook learns its
 * level from its own call site instead: the trampolines in src/hooks.s pass
 * the return address, and ee_level_of_call() matches it against the hooked
 * function's addresses; msg_string's patch passes the program's id in $at. */
#ifndef MODSEG_H
#define MODSEG_H

typedef signed char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;
typedef unsigned long u64;      /* 64-bit with the game's compiler, as in OpenRAC's common.h */
typedef float f32;

/* Save globals the executable keeps resident (ReRAC game_state.md, US + 0x100 for PAL).
 * The current level, 0x15EE84, is deliberately absent: see above. */
#define g_bolts      (*(volatile s32 *)0x0015EE98)
#define g_completes  (*(volatile s32 *)0x0015EF20)   /* times completed; > 0 in a challenge-mode run */

/* The programs: the 19 levels, then the title screen's two, the boot
 * executable's own code after power-on and frontbin after quitting a level
 * (tools/gen_frontend.py). */
#define FE_BOOT         19
#define FE_BIN          20
#define PROGRAMS        21

/* Per-program tables, indexed by the running program (from ee_level_of_call). */
#define LVL(level, name, type) ((type)(lvl_##name[level]))
#define DECLARE_LVL(name) extern const u32 lvl_##name[PROGRAMS]
/* Program-local data (tools/modseg.py LEVEL_DATA): its address in that program. */
#define LVL_DATA(level, name, type) ((type)(lvl_data_##name[level]))
#define DECLARE_LVL_DATA(name) extern const u32 lvl_data_##name[PROGRAMS]

/* Hooked call sites, as offsets from the hooked function's start. The same
 * numbers drive tools/overlay_patch.py, which reads them from this header. */
#define EE_SITE_HUD_UPDATE      0x838   /* func_L00_00299250 + 0x838: jal HudUpdate */
#define EE_SITE_DETECTOR_SPAWN  0x638   /* func_L00_002D4398 + 0x638: jal the bolt spawner */

/* Where the quick select pause setting lives (q3): gCheats[11], the last
 * byte of the cheat flags D_0015EEB0 (12 bytes, saved whole as the save's
 * chunk 7). Retail addresses cheats 0-7 only, by constant or through the
 * cheat list ids {1, 3, 0, 7, 4, 6, 2}; bytes 8-11 are never read or
 * written other than by the whole-block copies, so the byte is 0 in every
 * retail save. (The first choice, gSpecialItems[120] at 0x13D508, turned
 * out to be a level pickup flag: set in old saves, counted by nothing we
 * control.) tools/gen_menu.py reads this name from here. */
#define EE_SETTING_QS_PAUSE     0x0015EEBB

/* x2: the Charge Boots cheat's toggle, gCheats[8], saved in the same block.
 * The game's move-combo cheat detector (func_L00_00207418, NTSC decomp
 * FUN_L00_00206e00) matches 12 entries of a per-level sequence table,
 * toggles gCheats[i] and marks the cheat seen at 0x15EEC0 + i (saved too),
 * which lists it on the Goodies Cheats page; retail fills entries 0-4, 6
 * and 7, and the edition adds entry 8 (src/charge_boots.c). */
#define EE_CHEAT_CHARGE_BOOTS   0x0015EEB8
#define EE_CHEAT_ENTRY          8

/* q4: the 16:9 (widescreen) setting (src/widescreen.c): gCheats[10], the
 * third of the cheat bytes retail never touches (see EE_SETTING_QS_PAUSE),
 * saved with the file, 0 (4:3) in every retail save. A row of the Camera
 * page (tools/gen_menu.py reads this name from here). */
#define EE_SETTING_WIDESCREEN   0x0015EEBA
#define EE_TEXT_WIDESCREEN      0x540D       /* "16:9 (widescreen)" */

/* x5: the strafing setting (src/strafe.c): gCheats[9], the last of the
 * cheat bytes retail never touches (see EE_SETTING_QS_PAUSE), saved with
 * the file, 0 (off: L2 and R2 as in retail) in every retail save and new
 * game. A row of the Controls page, above Quick Select pause
 * (tools/gen_menu.py reads both names from here). */
#define EE_SETTING_STRAFE       0x0015EEB9
#define EE_TEXT_STRAFE          0x540E       /* "Strafing" */

/* The control layout (src/control_layout.c): a byte of the Controls page, above
 * Strafing. 0 is Classic, 1 is Future; any other value reads as Classic. All
 * four never-read cheat bytes (gCheats[8-11]) are taken, so it lives in the
 * saved statistics block's spare bytes after the Movie volume (see q2): retail
 * never touches them, so a retail game keeps and re-saves it. It is 0 in a retail
 * save and after the new-game reset, and the challenge-mode carry-over keeps it
 * with the quick-select pages (ee_qs_carry). */
#define EE_SETTING_CONTROL_LAYOUT 0x00141A64
#define EE_TEXT_CONTROL_LAYOUT  0x5413       /* "Control layout" */
#define EE_TEXT_LAYOUT_CLASSIC  0x5414       /* "Classic" */
#define EE_TEXT_LAYOUT_FUTURE   0x5415       /* "Future" */
#define EE_LAYOUT_CLASSIC       0
#define EE_LAYOUT_FUTURE        1
/* The layout in force: EE_LAYOUT_CLASSIC or EE_LAYOUT_FUTURE. */
s32 GetControlLayout(void);
/* Nonzero in play: game mode 0 (not paused, in a cutscene, at a vendor or in
 * the credits) and not on the hoverboard (src/control_layout.c). */
s32 ee_in_play(s32 level);
/* Nonzero where Future's buttons apply: Future, in play, as Ratchet or Clank
 * (not Giant Clank). */
s32 ee_layout_in_play(s32 level);
/* Called by the pad hook (src/strafe.c) after the game's parse of pad 0: the
 * Future buttons (src/control_layout.c; pad_lock is the pad's + 0x1CC as it
 * was before the parse) and the prompts of the layout in force
 * (src/control_prompts.c). */
void ee_layout_pad(s32 level, volatile u8 *pad, s32 pad_lock);
void ee_layout_prompts(s32 level);

/* The game's language (0 English, 2 French, 3 German, 4 Spanish, 5 Italian;
 * 1 is a slot no disc uses). */
#define g_language   (*(volatile s32 *)0x0015EE88)
#define LANGUAGE_ENGLISH 0

/* An entry of the loaded text table (D_0015F780, the level's or, while the
 * pause menu is open, the global one): the string, its id, its HelpDesk voice
 * (a help-audio slot, played as voice + 30000; -1 for none) and a zero word. */
typedef struct {
    u8 *text;
    s32 id;
    s32 voice;
    s32 unused;
} TextEntry;

/* Called by the pad hook each frame, after the prompts: the Future layout's
 * HelpDesk lines (src/helpdesk_voices.c) and the strafing help (src/strafe_help.c). */
void ee_helpdesk_voices(s32 level);
void ee_strafe_help_frame(s32 level);
/* x5: the strafing help, shown once per save: bit 0 of a byte in the saved
 * statistics block's spare bytes (after the control layout), 0 in a retail
 * save and after the new-game reset. */
#define EE_STRAFE_HELP_SEEN     0x00141A65
#define EE_TEXT_STRAFE_HELP        0x5417
#define EE_TEXT_STRAFE_HELP_FUTURE 0x5418
/* The Charge Boots' description (text/strings.tsv), and its Future wording,
 * which src/text.c serves in its place while Future is on. */
#define EE_TEXT_CHARGE_BOOTS_INFO        0x5407
#define EE_TEXT_CHARGE_BOOTS_INFO_FUTURE 0x5416
/* DrawWorld (func_001F3D78, every level) calls, as offsets in it: HudDraw,
 * draw_help, the letterbox bars and the subtitles (the same in all 19). */
#define EE_SITE_WS_HUD          0x4C0
#define EE_SITE_WS_HELP         0x4C8
#define EE_SITE_WS_LETTERBOX    0x4D0
#define EE_SITE_WS_SUBTITLES    0x4FC
#define EE_SITE_WS_PILLARS      0x520   /* DrawWorld + 0x520: jal DoGifPaging, after the HUD block */
/* How far the quick-select ring's slot is moved while the HUD draws: 0 in
 * 4:3, half the way out to the 16:9 edge in 16:9 (src/widescreen.c). */
s32 ee_ws_ring_shift(void);
#define EE_SITE_WS_MOVIE       0xF8    /* func_0023E298 + 0xF8: jal setImageTag (the movie frame's tags) */

/* The Options list row the edition inserts (q3), shared with tools/gen_menu.py,
 * and the two core tables the game fills at boot from the disc's table of
 * contents: {first sector, sectors} per Options row (options_ss) and per Help
 * row (help_ss: Help Log, Controls, Moves, Weapons, Gadgets). */
#define EE_OPTIONS_ROW          3
#define EE_OPTIONS_SS           0x00138140
#define EE_HELP_SS              0x00138108
/* The Controls page's list panel made two rows high, given a pause panel (src/quick_select_pause.c). */
void ee_controls_panel(u32 moby);

/* q2: three quick-select pages (src/quick_select_pages.c). The active page is
 * the game's own eight saved words (D_00141FA0, save block 0xD); the other
 * two and the page number live in the last three records of the saved
 * statistics block (D_00141948, 0x120 bytes of {u16 count, u16 time, u32
 * level bits} event counters, save block 0x11): retail code reads and writes
 * the records up to 0x141A4C by constant offsets, never these 24 bytes, so a
 * retail game loads, keeps and re-saves them blindly. Both blocks are zeroed
 * by the new-game reset; the challenge-mode carry-over keeps the ring but
 * resets the statistics, so its reset call is hooked (EE_SITE_QS_CARRY). */
#define EE_QS_PAGE_NEXT      0x00141A50   /* 8 item ids: the page R1 brings up */
#define EE_QS_PAGE_PREV      0x00141A58   /* 8 item ids: the page L1 brings up */
#define EE_QS_PAGE_INDEX     0x00141A60   /* which of the three pages is active, 0-2; 0 in every save */
s32 ee_qs_turn_to_page(s32 page);
/* x2: 1 in a save written while the Charge Boots were worn (src/charge_boots.c,
 * ee_save_c): the save itself holds no boots in the boots slot, which a
 * retail game would build from its own record 36, the race Devastator. In
 * the page number's record, after its byte; 0 in RAM except while saving. */
#define EE_SAVE_BOOTS_WORN   0x00141A61
/* The Movie volume (src/movie_volume.c), u16, saved with the game like
 * retail's volumes: 0 in a retail save or after the new-game reset, else
 * the volume + 1. In the page number's record too; kept across the
 * challenge-mode carry-over with the pages. */
#define EE_SETTING_MOVIE_VOLUME 0x00141A62
#define EE_SITE_QS_CARRY     0x140        /* func_00227DB0 + 0x140: jal func_00209DC0 (the save-state reset) */
#define EE_SITE_QS_AUTOSLOT  0x1EC        /* func_L00_002618D8 + 0x1EC: the empty-slot test and store, 6 words */
#define EE_TEXT_QS_SWITCH    0x5409       /* "[square] Next ring" (the retail box is too narrow for "Switch rings") */
#define EE_TEXT_QS_PAGE      0x540A       /* "Page 1"; + 1 "Page 2"; + 2 "Page 3" */
/* The ring is open in LEVEL (HUD slot 3's update callback is the edition's). */
s32 ee_qs_ring_open(s32 level);

/* The program whose copy of a hooked function made the call that returns to
 * RA, given the function's per-program addresses and the call site's offset
 * in it (src/level.c): ee_program_of_call gives -1 for none,
 * ee_level_of_call falls back to the game's level. */
s32 ee_program_of_call(u32 ra, const u32 *funcs, u32 site);
s32 ee_level_of_call(u32 ra, const u32 *funcs, u32 site);

/* x2: the Charge Boots item each frame: owned once the cheat is unlocked (src/charge_boots.c). */
void ee_charge_boots_frame(s32 level);

/* x2: the Charge Boots cheat, each gameplay frame before the hero update
 * (src/charge_boots.c, called from src/charge_dash.c's frame hook). */
void ee_charge_cheat_frame(s32 level);

/* x2: item record 36, the Charge Boots' and the hoverboard races' Devastator:
 * retail's words while Ratchet is on the board (src/charge_boots.c). */
void ee_race_item_frame(s32 level);

/* Movie volume (src/movie_volume.c): a fourth row on the Sound page, in the
 * pause menu and the title screen's. Its calls of snd_SetMasterVolume
 * (func_0012E348), as offsets in their functions, go to the hooks named:
 * the movie group (5) at all three of its calls, and the per-frame sound
 * update's other groups that follow the sound effects volume, whose
 * cutscene branch (game mode 2) marks the frame as a cutscene's. */
#define EE_SITE_MOVIE_INIT      0x164   /* func_0022DBE8 (Sound_InitState): group 5 -> ee_movie_group_volume */
#define EE_SITE_MOVIE_END       0x3B0   /* func_0023B740 (the movie loop), as a movie ends: group 5 -> the same */
#define EE_SITE_VOL_CUT_SFX     0x2A0   /* func_0022DD68 (sound_update), cutscene branch: group 0, halved -> ee_volume_cutscene */
#define EE_SITE_VOL_CUT_SFX3    0x2B8   /* the same branch: group 3, halved -> ee_volume_cutscene */
#define EE_SITE_VOL_PLAY        0x304   /* the other branch: group 0 -> ee_volume_play */
#define EE_SITE_VOL_DIALOGUE    0x334   /* both: group 2, the dialogue streams -> ee_movie_group_volume */
#define EE_SITE_VOL_SFX4        0x358   /* both: group 4 -> ee_movie_group_volume */
#define EE_SITE_MOVIE_UPDATE    0x364   /* both: group 5 -> ee_movie_group_volume */
#define EE_TEXT_MOVIE_VOLUME    0x540F  /* "Movie volume" */
#define EE_ICON_MOVIE_BAR       0xE99F  /* the orange bar: icon 0xE99E's green bar frame (8) with an orange palette */
void ee_movie_group_volume(s32 group, s32 volume);
void ee_volume_cutscene(s32 group, s32 volume);
void ee_volume_play(s32 group, s32 volume);

/* x2: the level loader's HUD header copy, with the edition's icon appended (src/hud_icons.c). */
void ee_hud_header_copy(u8 *dst, const u8 *src, s32 size);
#define EE_SITE_HUD_HEADER_COPY 0xB8    /* func_002032D0 + 0xB8: jal FastMemCopy(header, archive, size) */
void ee_hud_link_bank_c(s32 bank, u32 addr, u32 ra);   /* entered through the trampoline ee_hud_link_bank */
#define EE_SITE_HUD_LINK_BANK   0x190   /* func_002032D0 + 0x190: jal LinkHudBank(1, bank) */

/* The vendor's ammo dialog opens at the most ammo the player can buy (src/vendor_ammo.c). */
void ee_vendor_ammo_open_c(void *moby, u32 ra);         /* entered through the trampoline ee_vendor_ammo_open */
#define EE_SITE_VENDOR_AMMO     0xF4    /* func_L00_0029CDE0 (VendorBuyDialogUpdate) + 0xF4: jal func_L00_00251E30 */

/* rz2: the title screen's Goodies pages (src/goodies.c) and their strings
 * (text/strings.tsv); the seed box's text is built as buttons are pressed. */
#define EE_TEXT_RANDOMIZER      0x5410  /* "Randomizer" */
#define EE_TEXT_REUSE_SEED      0x5411  /* "Reuse Seed" */
#define EE_TEXT_SEED_INPUT      0x5412  /* the Reuse Seed box's buttons, from ee_seed_text() */
#define EE_TEXT_RZ_OPTIONS      0x5421  /* "Randomizer Options" */
#define EE_TEXT_START_PLANET    0x5422  /* "Starting Planet" */
#define EE_TEXT_START_NOVALIS   0x5423  /* "Novalis": the Starting Planet row's values, in RzStart order */
#define EE_TEXT_START_VELDIN    0x5424  /* "Veldin" */
#define EE_TEXT_START_RANDOM    0x5425  /* "Random" */
const char *ee_seed_text(void);

/* rz1/rz2: the Randomizer (src/randomizer.h, src/randomizer.c for the logic;
 * src/randomizer_save.c for the save; src/goodies.c for the menus). */
#define EE_RANDOMIZER_CHARGE_BOOTS 1    /* the pool has the Charge Boots, sold as the bonus vendor entry (every build has the boots) */
#define EE_RZ_PAGE          0x0014B7C0  /* the Randomizer's record: level page 19 of save block 0xBBA, 2 KB no level owns */
#define EE_RZ_RECORD_SIZE   160
#define EE_TEXT_SEED        0x5419      /* "Seed" */
#define EE_TEXT_CONFIRM     0x541A      /* "[cross] Confirm", the Reuse Seed prompt */
#define EE_TEXT_BAD_SEED    0x541B      /* "Invalid seed code" */
#define EE_TEXT_RZ_BANNER   0x541C      /* the Randomizer's banner, built as an item is received (src/randomizer_text.c) */
#define EE_TEXT_RZ_FOUND    0x541D      /* "You found the @!": the banner's words, @ the item (RzHow) */
#define EE_TEXT_RZ_GOT      0x541E      /* "You got the @!" */
#define EE_TEXT_RZ_WON      0x541F      /* "You won a @!" */
#define EE_TEXT_MYSTERY     0x5420      /* "???": a vendor's purchase entry and a giver's item in its prompts */
const char *ee_rz_banner_text(void);
#define EE_ICON_RANDOMIZER  0xE9A0      /* the Randomizer save badge: the Challenge Mode R (icon 0xE99E frame 4) in green */
#define EE_ICON_MYSTERY     0xE9A1      /* the vendor's mystery tile: a blue tile with a question mark (tools/gen_hud.py) */
/* What the title screen knows of a slot's Randomizer record. */
struct RzSlot {
    u8 present;
    u8 version;                         /* the logic version it was made with */
    u8 weapon_mode;
    u8 with_boots;
    u32 seed;
    u8 start_mode;                      /* RzStart: the seed code carries whether it is Random */
};
u32 ee_save_checksum(const u8 *data, s32 length);          /* memcard_Checksum, func_0020BB10 */
s32 ee_rz_record_offset(s32 program);                       /* the record's offset in a slot's file; -1 if the tables changed */
s32 ee_rz_record_parse(const u8 *bytes, struct RzSlot *out);   /* EE_RZ_RECORD_SIZE bytes from there */
s32 ee_rz_active(void);                                     /* the game in memory is a Randomizer save */
void ee_rz_arm(s32 on);                                     /* the next new-game reset starts a Randomizer game */
void ee_rz_set_seed(u32 seed, s32 start_mode, s32 given);   /* with this seed (Reuse Seed), or a fresh one; starting as START_MODE */
void ee_rz_after_reset(s32 L);                              /* from ee_fe_reset_c, once the reset has run */
/* The record's play state (src/randomizer_save.c), read and changed in place by src/randomizer_play.c and its neighbours. */
s32 ee_rz_layout_item(s32 check);                           /* the RzItem the seed put at CHECK */
s32 ee_rz_check_collected(s32 check);
void ee_rz_set_collected(s32 check);
s32 ee_rz_gold_received(void);
s32 ee_rz_bonus_planet(void);                               /* the bonus vendor check's planet, 0 without the Charge Boots */
s32 ee_rz_bonus_seen(void);                                 /* its entry has been listed once: every vendor lists it now */
void ee_rz_set_bonus_seen(void);
void ee_rz_add_gold(void);
s32 ee_rz_received_special(s32 flag);                       /* FLAG: the byte of D_0013D490 the item sets in retail */
void ee_rz_set_received_special(s32 flag);/* rz3: each gameplay frame, from src/charge_dash.c's frame hook. */
void ee_rz_frame(s32 L);
void ee_rz_pause_open(s32 L);                               /* the pause menu opening in a level (src/goodies.c) */
void ee_rz_record_autosave(s32 L);                          /* the record's page to the card once it changed (from ee_rz_frame) */

#endif
