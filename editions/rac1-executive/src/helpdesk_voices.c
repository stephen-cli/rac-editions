/* The HelpDesk lines of the Future control layout (src/control_layout.c): the
 * English lines that name a button Future moves have new versions, cut from
 * the game's own lines, and in Future the game plays the new version wherever
 * it would play the old one. Classic plays retail's. Other languages keep
 * retail's lines in both layouts (only English has new versions).
 *
 * How the game plays a line. A HelpDesk line belongs to a string of the loaded
 * text table (D_0015F780, entries {text, id, voice, 0}): when a help popup
 * opens, Help_Update (func_001FE6C0) takes the entry's voice and queues
 * voice + 30000 on the dialogue player (D_001517D0 + 0x1C), and
 * music_start_track_by_id (func_002167C0) passes ids from 30000 to
 * Music_PlayId30000 (func_00216528), which streams the VAG whose first sector
 * is the word at 0x11C278 + id * 4 + language * 600: the disc's help-audio
 * table (table of contents + 0x1AB8, copied to 0x137C80 at boot), 150 slots
 * per language, in language order. While the line plays, Help_Update keeps
 * the popup open as long as the playing id is the entry's voice + 30000.
 *
 * So the new lines need no new code in the player: tools/gen_helpdesk.py
 * builds them from the player's disc (audio/helpdesk.json lists them) and
 * tools/splice_helpdesk.py puts them in slots 150 and up of English's table,
 * which the game reads as language 1's first slots, a language no disc uses
 * (retail leaves the whole block empty). In Future each frame points the
 * voices of the strings that use a replaced line at its new version
 * (ee_helpdesk_future, generated from the same list). Changing the entry, not
 * the queued id, keeps the popup's own comparison true. The pause menu's
 * (global) table carries voices too and is changed the same way while it is
 * loaded.
 *
 * Each frame the entries holding one of these voices are found again when
 * the table, its size or one of them moves (a table loaded again comes back
 * with retail's voices), and set when the layout or the language changes. */
#include "modseg.h"

DECLARE_LVL_DATA(text_table);   /* D_0015F780: the loaded table's entries */
DECLARE_LVL_DATA(help_state);   /* D_001997D0: + 0x2C the number of entries */

/* {retail slot, the new version's slot} per replaced line (build/modseg/gen_helpdesk.c). */
extern const s16 ee_helpdesk_future[][2];
extern const s32 ee_helpdesk_future_count;
#define CLASSIC(n) (ee_helpdesk_future[n][0])
#define FUTURE(n)  (ee_helpdesk_future[n][1])

#define HELP_STATE_COUNT 0x2C
#define FOUND_MAX 64    /* a table has one or two strings per line */

static TextEntry *found_table;
static s32 found_count;
static s32 found[FOUND_MAX];        /* indices of the entries holding one of the voices */
static s32 found_pair[FOUND_MAX];
static s32 found_n;
static s32 applied_future = -1;     /* -1: this table has not been set yet */

/* The pair whose retail or new voice is VOICE, or -1. */
static s32 pair_of(s32 voice)
{
    s32 n;

    for (n = 0; n < ee_helpdesk_future_count; n++) {
        if (voice == CLASSIC(n) || voice == FUTURE(n)) {
            return n;
        }
    }
    return -1;
}

static void find_voices(TextEntry *table, s32 count)
{
    s32 i;
    s32 n;

    found_n = 0;
    for (i = 0; i < count && found_n < FOUND_MAX; i++) {
        n = pair_of(table[i].voice);
        if (n >= 0) {
            found[found_n] = i;
            found_pair[found_n] = n;
            found_n++;
        }
    }
    found_table = table;
    found_count = count;
    applied_future = -1;
}

/* The found entries still hold the voices last set, as FUTURE says. */
static s32 found_hold(TextEntry *table, s32 count, s32 future)
{
    s32 k;
    s32 n;

    if (table != found_table || count != found_count) {
        return 0;
    }
    for (k = 0; k < found_n; k++) {
        n = found_pair[k];
        if (table[found[k]].voice != (future ? FUTURE(n) : CLASSIC(n))) {
            return 0;
        }
    }
    return 1;
}

void ee_helpdesk_voices(s32 level)
{
    TextEntry *table = *LVL_DATA(level, text_table, TextEntry *volatile *);
    s32 count = *(volatile s32 *)(LVL_DATA(level, help_state, u8 *) + HELP_STATE_COUNT);
    s32 future = GetControlLayout() == EE_LAYOUT_FUTURE && g_language == LANGUAGE_ENGLISH;
    s32 k;
    s32 n;

    if (table == 0 || count <= 0) {
        return;
    }
    if (future == applied_future && found_hold(table, count, future)) {
        return;
    }
    find_voices(table, count);
    for (k = 0; k < found_n; k++) {
        n = found_pair[k];
        table[found[k]].voice = future ? FUTURE(n) : CLASSIC(n);
    }
    applied_future = future;
}
