/* x5: the strafing help. The first time a save turns Strafing on (Options >
 * Controls) and goes back to the game, the HelpDesk explains it, voice and
 * all: "Try using the L2 or R2 button with the left stick to strafe" (L2
 * alone in the Future layout, which strafes on L2 only). Once shown, a bit in
 * the save (EE_STRAFE_HELP_SEEN) keeps it from showing again.
 *
 * When. The pad hook runs this every frame, in every game mode. Entering the
 * pause menu (game mode 3) notes the setting; going straight from the pause
 * menu back to play (mode 0) with the setting now on and the help not yet
 * seen makes it pending. It is requested in play as soon as the HelpDesk is
 * free, and dropped if Strafing is turned off before then. A save loaded
 * from the pause menu with Strafing on, whose help was never shown, counts
 * as turning it on.
 *
 * How the game shows a help. Help_Request (func_L00_00203F20 in level 0) takes
 * a string id and a slot of the saved help records (D_00141A68, 148 records
 * {u16 count, u16 best time, u32 level bits}, save block 0x10). When the
 * HelpDesk is idle (help state D_001997D0: + 0x00 state 0, + 0x24 no pending
 * id) and the dialogue player is free, it stores the id at + 0x24 and the
 * slot at + 0x28. Help_Update (func_001FE6C0) then finds the id's entry in
 * the loaded text table (Help_FindIndex, func_001FE4D0, a scan of D_0015F780's
 * + 0x2C entries), opens the popup with the entry's text, queues its voice,
 * and on closing counts the showing in the slot's record. Help_Update holds
 * nothing back until the player has first moved (+ 0x30, set 120 frames after
 * the first d-pad or stick press of the level); a popup open when the game
 * pauses carries on after it.
 *
 * The edition's string is not in any level's table, and a level's table has
 * no room to grow. So for the length of the popup the table is a copy with
 * one more entry at the end, the edition's string (src/text.c's, in the
 * game's language and the layout's wording) with the layout's strafing line
 * (ee_helpdesk_strafe, English only; tools/gen_helpdesk.py builds both lines
 * from the game's own): D_0015F780 and the count point at the copy and are
 * pointed back when the popup is over. The pause menu keeps them across a
 * pause (MenuTextBank_Load, func_0021E340, saves both and
 * MenuTextBank_Restore, func_0021E2D0, puts them back), and loading a level
 * replaces them, so a copy that is no longer in place is let go.
 *
 * The record. No retail code requests or reads slot 104 (no English line
 * either), but retail could still name it from level data, so its eight
 * bytes are taken before the request and written back when the popup is
 * over, and the save keeps retail's record. A level load in between restarts
 * the help (+ 0x30 back to 0), and then the record is left as loaded. */
#include "modseg.h"

DECLARE_LVL(func_L00_00203F20);   /* Help_Request */
DECLARE_LVL_DATA(text_table);     /* D_0015F780: the loaded table's entries */
DECLARE_LVL_DATA(help_state);     /* D_001997D0 */
DECLARE_LVL_DATA(game_mode);

typedef s32 (*HelpRequestFn)(s32 string_id, s32 record);

const char *ee_msg_string_edition(s32 id);          /* src/text.c */
extern const s16 ee_helpdesk_strafe[2];             /* Classic's and Future's line (build/modseg/gen_helpdesk.c) */

#define STRAFE_SETTING  (*(volatile u8 *)EE_SETTING_STRAFE)
#define SEEN_BYTE       (*(volatile u8 *)EE_STRAFE_HELP_SEEN)
#define SEEN_BIT        0x01

#define HELP_STATE      0x00
#define HELP_PENDING    0x24
#define HELP_COUNT      0x2C
#define HELP_STARTED    0x30

#define HELP_RECORDS    0x00141A68
#define HELP_RECORD     104
#define RECORD_SIZE     8

#define GAME_MODE_PLAY  0
#define GAME_MODE_PAUSE 3

#define COPY_ENTRIES    2048    /* the level tables hold about 1,500 */

static TextEntry copy[COPY_ENTRIES];

static s32 last_mode = -1;
static s32 strafe_at_pause;     /* the setting when the pause menu opened */
static s32 pending;             /* to be requested as soon as the HelpDesk is free */

/* The popup in progress: the table it replaced and the record it borrows. */
static s32 showing;
static TextEntry *level_table;
static s32 level_count;
static u32 record_before[RECORD_SIZE / 4];

static volatile s32 *help_word(s32 level, s32 offset)
{
    return (volatile s32 *)(LVL_DATA(level, help_state, u8 *) + offset);
}

static volatile u32 *help_record(void)
{
    return (volatile u32 *)(HELP_RECORDS + HELP_RECORD * RECORD_SIZE);
}

static void point_table(s32 level, TextEntry *table, s32 count)
{
    *LVL_DATA(level, text_table, TextEntry *volatile *) = table;
    *help_word(level, HELP_COUNT) = count;
}

/* Puts the copy in place and asks for the help: 0 if the HelpDesk is busy. */
static s32 request(s32 level)
{
    TextEntry *table = *LVL_DATA(level, text_table, TextEntry *volatile *);
    s32 count = *help_word(level, HELP_COUNT);
    volatile u32 *record = help_record();
    TextEntry *entry;
    s32 i;

    if (table == 0 || count <= 0 || count >= COPY_ENTRIES) {
        pending = 0;    /* no table to copy, or too large: no help at all rather than a broken one */
        return 0;
    }
    if (*help_word(level, HELP_STATE) != 0 || *help_word(level, HELP_PENDING) != -1
        || *help_word(level, HELP_STARTED) == 0) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        copy[i] = table[i];
    }
    entry = &copy[count];
    entry->text = (u8 *)ee_msg_string_edition(EE_TEXT_STRAFE_HELP);
    entry->id = EE_TEXT_STRAFE_HELP;
    if (g_language != LANGUAGE_ENGLISH) {
        entry->voice = -1;
    } else if (GetControlLayout() == EE_LAYOUT_FUTURE) {
        entry->voice = ee_helpdesk_strafe[1];
    } else {
        entry->voice = ee_helpdesk_strafe[0];
    }
    entry->unused = 0;

    record_before[0] = record[0];
    record_before[1] = record[1];
    point_table(level, copy, count + 1);
    if (!LVL(level, func_L00_00203F20, HelpRequestFn)(EE_TEXT_STRAFE_HELP, HELP_RECORD)) {
        point_table(level, table, count);
        return 0;
    }
    level_table = table;
    level_count = count;
    showing = 1;
    return 1;
}

/* Once the popup is over, the record and the table go back as they were. */
static void finish(s32 level)
{
    volatile u32 *record = help_record();
    TextEntry *loaded = *LVL_DATA(level, text_table, TextEntry *volatile *);
    s32 mode = *LVL_DATA(level, game_mode, volatile s32 *);

    if (*help_word(level, HELP_STARTED) == 0) {
        showing = 0;    /* a level load: the record and the table are the loaded ones */
        return;
    }
    if (*help_word(level, HELP_STATE) != 0 || *help_word(level, HELP_PENDING) != -1) {
        return;
    }
    if (showing == 1) {
        record[0] = record_before[0];
        record[1] = record_before[1];
        showing = 2;    /* the record is back; the table may still be away in the pause menu */
    }
    if (loaded == copy) {
        point_table(level, level_table, level_count);
        showing = 0;
    } else if (mode == GAME_MODE_PLAY) {
        showing = 0;    /* the copy is no longer the loaded table */
    }
}

void ee_strafe_help_frame(s32 level)
{
    s32 mode;

    if (level < 0 || level >= 19) {
        return;
    }
    mode = *LVL_DATA(level, game_mode, volatile s32 *);
    if (mode == GAME_MODE_PAUSE && last_mode != GAME_MODE_PAUSE) {
        strafe_at_pause = STRAFE_SETTING;
    }
    if (mode == GAME_MODE_PLAY && last_mode == GAME_MODE_PAUSE
        && STRAFE_SETTING && !strafe_at_pause && !(SEEN_BYTE & SEEN_BIT)) {
        pending = 1;
    }
    last_mode = mode;
    if (!STRAFE_SETTING) {
        pending = 0;
    }
    if (showing) {
        finish(level);
    }
    if (pending && !showing && mode == GAME_MODE_PLAY && request(level)) {
        pending = 0;
        SEEN_BYTE |= SEEN_BIT;
    }
}
