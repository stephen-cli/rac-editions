/* The edition's strings.
 *
 * msg_string(id) (func_001FE540) returns the text for a string id: it asks
 * func_001FE4D0 for the id's index in the loaded text block (a linear scan
 * over the block's entries, 16 bytes each, pointer first) and returns that
 * entry's pointer, or the "Paradox: This message does not exist" string.
 * In a level the block is the level program's own per-language text, which
 * has no room for new entries, so tools/overlay_patch.py makes every
 * level's copy of msg_string jump here instead. Ids in the edition's range
 * come from the tables tools/gen_text.py generates from text/strings.tsv;
 * everything else goes through the game's own lookup. The entries base
 * D_0015F780 is gp-relative data, at one address in every level. */
#include "modseg.h"

DECLARE_LVL(func_001FE4D0);              /* id -> entry index, or -1 */

extern const int ee_text_first;
extern const int ee_text_count;
extern const char *const ee_text[][8];

#define g_language       (*(volatile s32 *)0x0015EE88)   /* 0 English, 2 French, 3 German, 4 Spanish, 5 Italian */
#define g_text_entries   (*(volatile u32 *)0x0015F780)

typedef s32 (*TextIndexFn)(s32);

static const char missing[] = "Paradox: This message does not exist";

const char *ee_msg_string(s32 id)
{
    s32 index;

    if (id >= ee_text_first && id < ee_text_first + ee_text_count) {
        s32 lang = g_language;
        const char *s = (lang >= 0 && lang < 8) ? ee_text[id - ee_text_first][lang] : 0;
        return s ? s : ee_text[id - ee_text_first][0];
    }
    index = LVL(func_001FE4D0, TextIndexFn)(id);
    if (index >= 0) {
        return *(const char **)(g_text_entries + index * 16);
    }
    return missing;
}
