/* Executive Edition resident code: shared declarations.
 * Built by tools/modseg.py with the game's compiler (SN GCC 2.95.3, -O2 -G0).
 * Game symbols are addresses by name: func_XXXXXXXX / D_XXXXXXXX are the
 * executable's and are the same in every level; a level-only function is
 * reached through LVL(), a 19-entry table the build generates from
 * OpenRAC's overlay catalogue, indexed by the current level. */
#ifndef MODSEG_H
#define MODSEG_H

typedef signed char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;
typedef float f32;

/* Save globals the executable keeps resident (ReRAC game_state.md, US + 0x100 for PAL). */
#define g_level      (*(volatile s32 *)0x0015EE84)   /* current level, 0..18 */
#define g_bolts      (*(volatile s32 *)0x0015EE98)
#define g_completes  (*(volatile s32 *)0x0015EF20)   /* times completed; > 0 in a challenge-mode run */

#define LVL(name, type) ((type)(lvl_##name[g_level]))
#define DECLARE_LVL(name) extern const u32 lvl_##name[19]
/* Level-local data (tools/modseg.py LEVEL_DATA): its address in the current level. */
#define LVL_DATA(name, type) ((type)(lvl_data_##name[g_level]))
#define DECLARE_LVL_DATA(name) extern const u32 lvl_data_##name[19]

#endif
