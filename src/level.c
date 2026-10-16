/* Which level program is running: from a hook's return address.
 *
 * A hook is entered by `jal` from the same call site in every level program,
 * at the site's level-specific address, so the return address names the
 * level: it is funcs[L] + site + 8 for exactly one L. The game's own
 * current-level variable is not used (see modseg.h): it changes before the
 * old program stops running when a save is loaded from the pause menu. */
#include "modseg.h"

#define g_level_variable (*(volatile s32 *)0x0015EE84)

s32 ee_level_of_call(u32 ra, const u32 *funcs, u32 site)
{
    s32 L;
    for (L = 0; L < 19; L++) {
        if (funcs[L] + site + 8 == ra) {
            return L;
        }
    }
    /* Not one of the patched sites: never expected. Fall back to the game's
     * variable, clamped, rather than index past the table. */
    L = g_level_variable;
    return (L >= 0 && L < 19) ? L : 0;
}
