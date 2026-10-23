/* The edition's strings.
 *
 * msg_string(id) (func_001FE540) returns the text for a string id: it asks
 * func_001FE4D0 for the id's index in the loaded text block (a linear scan
 * over the block's entries, 16 bytes each, pointer first) and returns that
 * entry's pointer, or the "Paradox: This message does not exist" string.
 * A level does not keep the global text table loaded, and its block cannot
 * grow, so tools/overlay_patch.py makes every level's copy of msg_string
 * start with a jump to ee_msg_string_hook (src/text_hook.s): ids in the
 * edition's range are answered here from the tables tools/gen_text.py
 * generates from text/strings.tsv; any other id runs msg_string's two
 * replaced prologue instructions and continues inside the game's own
 * function. (A first version re-implemented msg_string in C and read the
 * entries base at the executable's address of that variable; the level
 * copies reach it gp-relative with a per-level offset, so every game string
 * came back null.) */
#include "modseg.h"

extern const int ee_text_first;
extern const int ee_text_count;
extern const char *const ee_text[][8];

#define g_language   (*(volatile s32 *)0x0015EE88)   /* 0 English, 2 French, 3 German, 4 Spanish, 5 Italian */

/* Only ids in the edition's range reach this (src/text_hook.s checks the
 * range first and sends every other id on into the game's own msg_string,
 * whose data this code never touches). */
const char *ee_msg_string_edition(s32 id)
{
    s32 lang = g_language;
    const char *s;
    if (id == EE_TEXT_CHARGE_BOOTS_INFO && GetControlLayout() == EE_LAYOUT_FUTURE) {
        id = EE_TEXT_CHARGE_BOOTS_INFO_FUTURE;      /* the dash's double tap is on R2 */
    }
    s = (lang >= 0 && lang < 8) ? ee_text[id - ee_text_first][lang] : 0;
    if (id == EE_TEXT_SEED_INPUT) {
        return ee_seed_text();      /* built as buttons are pressed (src/goodies.c) */
    }
    return s ? s : ee_text[id - ee_text_first][0];
}

/* Debug (docs/DEBUGGING.md): who asks for a string. While [1] is nonzero,
 * every msg_string call for an id in [0], [0] + [1] stores the caller's
 * return address in [2], the program's id in [3] and the string id in [4]
 * (src/text_hook.s). Written over PINE; zero in a normal run. */
volatile u32 ee_debug_msg_watch[5];
