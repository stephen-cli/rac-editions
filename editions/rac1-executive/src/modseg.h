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
#define EE_QS_PAGE_INDEX     0x00141A60   /* which of the three pages is active, 0-2 */
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

#endif
