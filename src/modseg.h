/* Executive Edition resident code: shared declarations.
 * Built by tools/modseg.py with the game's compiler (SN GCC 2.95.3, -O2 -G0).
 * Game symbols are addresses by name. Only the resident core (below
 * 0x15F000: core text, data, bss, lit) is at one address; the executable's
 * own game text and data are re-linked into every level program at
 * level-specific addresses, as is level code. So any function above the
 * core, exe-kind or level-only, is reached through LVL(), a 19-entry table
 * the build generates from OpenRAC's overlay catalogue, indexed by the level
 * whose program is running; level-local data through LVL_DATA().
 *
 * Which level is running is NOT the game's current-level variable
 * (0x15EE84): loading a save from the pause menu rewrites the save's
 * globals, that variable included, while the old level program still runs
 * its last frames, and a hook indexing the tables with the new level then
 * jumped into the old program at the new level's addresses (crash "jump to
 * unaligned address" on the second load, 2026-10-17). A hook learns its
 * level from its own call site instead: the trampolines in src/hooks.s pass
 * the return address, and ee_level_of_call() matches it against the hooked
 * function's 19 addresses; msg_string's patch passes the level id in $at. */
#ifndef MODSEG_H
#define MODSEG_H

typedef signed char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;
typedef float f32;

/* Save globals the executable keeps resident (ReRAC game_state.md, US + 0x100 for PAL).
 * The current level, 0x15EE84, is deliberately absent: see above. */
#define g_bolts      (*(volatile s32 *)0x0015EE98)
#define g_completes  (*(volatile s32 *)0x0015EF20)   /* times completed; > 0 in a challenge-mode run */

/* Level tables, indexed by the running level (from ee_level_of_call). */
#define LVL(level, name, type) ((type)(lvl_##name[level]))
#define DECLARE_LVL(name) extern const u32 lvl_##name[19]
/* Level-local data (tools/modseg.py LEVEL_DATA): its address in that level. */
#define LVL_DATA(level, name, type) ((type)(lvl_data_##name[level]))
#define DECLARE_LVL_DATA(name) extern const u32 lvl_data_##name[19]

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

/* The level whose program made the call that returns to RA, given the hooked
 * function's 19 addresses and the call site's offset in it (src/level.c). */
s32 ee_level_of_call(u32 ra, const u32 *funcs, u32 site);

/* x2: grants the Charge Boots item each frame (src/charge_boots.c). */
void ee_charge_boots_frame(s32 level);
/* x2: the level loader's HUD header copy, with the edition's icon appended (src/hud_icons.c). */
void ee_hud_header_copy(u8 *dst, const u8 *src, s32 size);
#define EE_SITE_HUD_HEADER_COPY 0xB8    /* func_002032D0 + 0xB8: jal FastMemCopy(header, archive, size) */
void ee_hud_link_bank(s32 bank, u32 addr);
#define EE_SITE_HUD_LINK_BANK   0x190   /* func_002032D0 + 0x190: jal LinkHudBank(1, bank) */

#endif
