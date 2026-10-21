/* Which program is running: from a hook's return address.
 *
 * A hook is entered by `jal` from the same call site in every program (the
 * 19 levels, the title screen's two), at the site's program-specific
 * address, so the return address names the program: it is funcs[P] + site
 * + 8 for exactly one P (a program without the function has 0). The game's own
 * current-level variable is not used (see modseg.h): it changes before the
 * old program stops running when a save is loaded from the pause menu. */
#include "modseg.h"

#define g_level_variable (*(volatile s32 *)0x0015EE84)

s32 ee_program_of_call(u32 ra, const u32 *funcs, u32 site)
{
    s32 P;
    for (P = 0; P < PROGRAMS; P++) {
        if (funcs[P] != 0 && funcs[P] + site + 8 == ra) {
            return P;
        }
    }
    return -1;
}

s32 ee_level_of_call(u32 ra, const u32 *funcs, u32 site)
{
    s32 L = ee_program_of_call(ra, funcs, site);
    if (L >= 0) {
        return L;
    }
    /* Not one of the patched sites: never expected. Fall back to the game's
     * variable, clamped, rather than index past the table. */
    L = g_level_variable;
    return (L >= 0 && L < 19) ? L : 0;
}
