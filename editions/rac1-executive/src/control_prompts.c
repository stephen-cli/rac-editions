/* The button prompts of the Future control layout (src/control_layout.c):
 * weapon prompts show R1 where retail shows Circle, crouch and Hydro-Pack
 * prompts R2 where retail shows R1, and the prompts naming two buttons for
 * one action name the one Future uses. Gadget, menu, vehicle and hoverboard
 * prompts are unchanged. Classic shows retail's text.
 *
 * The text. A prompt is a string of the loaded text table: D_0015F780 points
 * at its entries, {text, id, voice, 0} of 16 bytes each (the voice is the
 * HelpDesk line, + 30000, or -1), and D_001997D0 + 0x2C is their count. The
 * level's table comes with the level (LoadLevelGameplay); while the pause
 * menu is open, MenuTextBank_Load has the global table loaded in its own
 * buffer and points D_0015F780 at it, and MenuTextBank_Restore points it back
 * at the level's. msg_string (func_001FE540) looks strings up there, and so
 * do the help popups (Help_Open, Help_Update and Help_Draw, func_001FE588,
 * func_001FE6C0 and func_001FF1B0), which read the entries directly. The
 * font (FontPrint, func_001F6668) draws each byte below 0x20 that has a
 * glyph as a button: 0x10 Cross, 0x11 Circle, 0x12 Triangle, 0x13 Square,
 * 0x14 L1, 0x15 R1, 0x16 L2, 0x17 R2, 0x18 and 0x19 the sticks; bytes 8 to
 * 15 set a colour and no code takes an argument. German writes "R1-Taste"
 * ("R1 button") in words in five crouch and jets prompts.
 *
 * The game keeps no record of whether a prompt is about a weapon or a
 * gadget: a help request carries only the string id, and the item table
 * puts weapons and gadgets in the same (hand) slot. So the prompts are
 * listed below by id, read from what each says. Every change is one byte
 * for one byte and can be undone from the text alone: no weapon prompt
 * shows R1 and no crouch prompt R2 in any language, so Circle <-> R1 and
 * R1 <-> R2 (glyph or word) give retail back exactly. The three prompts
 * naming two buttons ("L2 or R2", "R1 or R2") would read "L2 or L2": they
 * are copied, mapped and merged into one glyph ("L2"), joining word and
 * all, which works in all five languages, and their entries point at the
 * copy while Future is on.
 *
 * Each gameplay frame the pad hook (src/strafe.c) brings the table in force
 * to the layout: the prompts' entries are found again when the table, its
 * size or an entry moves, and the text is set again when the layout changes
 * or a byte the last pass set no longer holds (a table loaded again). That
 * costs a few dozen loads a frame. The title screen shows only menu
 * prompts and is not changed. */
#include "modseg.h"

DECLARE_LVL_DATA(text_table);   /* D_0015F780: the loaded table's entries */
DECLARE_LVL_DATA(help_state);   /* D_001997D0: + 0x2C the number of entries */

typedef struct {
    u8 *text;
    s32 id;
    s32 voice;
    s32 unused;
} TextEntry;

#define HELP_STATE_COUNT 0x2C

#define GLYPH_CIRCLE 0x11
#define GLYPH_R1     0x15
#define GLYPH_L2     0x16
#define GLYPH_R2     0x17
#define IS_GLYPH(c)  ((c) >= 0x10 && (c) < 0x20)

enum PromptKind {
    WEAPON,         /* Circle -> R1 */
    CROUCH,         /* R1 -> R2, glyph or word: crouch, the crouch moves, the Hydro-Pack's jets */
    TWO_BUTTONS     /* a copy with the buttons below mapped, then merged */
};

typedef struct {
    u16 id;
    u8 kind;
    u8 copy;        /* TWO_BUTTONS: which copy buffer */
    u8 from[2];     /* TWO_BUTTONS: glyphs mapped, in order, to... */
    u8 to[2];
} Prompt;

static const Prompt prompts[] = {
    { 0x0000, WEAPON },     /* Bomb Glove (with L1 for first person) */
    { 0x0BC2, WEAPON },     /* Blaster */
    { 0x36B0, WEAPON },     /* Morph-o-Ray */
    { 0x4E24, WEAPON },     /* Pyrocitor */
    { 0x4E30, WEAPON },     /* Tesla Claw */
    { 0x4F73, WEAPON },     /* Pyrocitor, pause menu Help */
    { 0x4F7E, WEAPON },     /* Suck Cannon, pause menu Help */
    { 0x0005, CROUCH },     /* Comet-Strike */
    { 0x4F0F, CROUCH },     /* Comet-Strike, pause menu Help */
    { 0x0BB9, CROUCH },     /* Boost Jump */
    { 0x1390, CROUCH },     /* Boost Jump */
    { 0x4F19, CROUCH },     /* Boost Jump, pause menu Help */
    { 0x0BBB, CROUCH },     /* Stretch Jump */
    { 0x138E, CROUCH },     /* Stretch Jump */
    { 0x4F18, CROUCH },     /* Stretch Jump, pause menu Help */
    { 0x4F8D, CROUCH },     /* Stretch Jump, pause menu Help */
    { 0x2710, CROUCH },     /* tiptoeing */
    { 0x2AFB, CROUCH },     /* Power Slam */
    { 0x2B01, CROUCH },     /* Power Slam */
    { 0x4650, CROUCH },     /* Power Slam */
    { 0x4651, CROUCH },     /* Power Slam */
    { 0x4F8C, CROUCH },     /* Power Slam, pause menu Help */
    { 0x4F11, CROUCH },     /* crouch and turn, pause menu Help */
    { 0x4F13, CROUCH },     /* side flip, pause menu Help */
    { 0x4F14, CROUCH },     /* back flip, pause menu Help */
    { 0x4E2E, CROUCH },     /* Hydro-Pack jets */
    /* Thruster-Pack: double-tap R1 to hover, L2 or R2 to strafe -> R2, L2 */
    { 0x2EE3, TWO_BUTTONS, 0, { GLYPH_R2, GLYPH_R1 }, { GLYPH_L2, GLYPH_R2 } },
    /* Thruster-Pack: R2 or L2 with the stick to strafe while hovering -> L2 */
    { 0x2EE4, TWO_BUTTONS, 1, { GLYPH_R2, 0 }, { GLYPH_L2, 0 } },
    /* ledges: R1 or R2 to drop down -> R2 */
    { 0x4F15, TWO_BUTTONS, 2, { GLYPH_R1, 0 }, { GLYPH_R2, 0 } },
};
#define PROMPTS ((s32)(sizeof prompts / sizeof prompts[0]))
#define COPIES 3
#define COPY_SIZE 256       /* the longest of the three, in any language, is under 180 bytes */
#define MERGE_GAP 8         /* the joining word between two glyphs: " or ", " oder ", " ou " ... */

static u8 copies[COPIES][COPY_SIZE];

/* The table the entries below were found in. */
static TextEntry *found_table;
static s32 found_count;
static s32 found_index[PROMPTS];    /* -1: not in this table */
static s32 applied_layout = -1;     /* -1: the table has not been brought to a layout yet */
/* For each prompt, what the last pass left: a byte it set (or none), or for
 * TWO_BUTTONS the entry's text pointer. */
static volatile u8 *mark_byte[PROMPTS];
static u8 mark_value[PROMPTS];
static u8 *mark_text[PROMPTS];

/* Entries pointed at a copy, and the game's text they replaced. One table
 * holds three; the level's and the pause menu's both may be pointed. */
#define REDIRECTS 8
static struct {
    TextEntry *entry;
    u8 *original;
} redirects[REDIRECTS];
static s32 redirect_next;

static void find_prompts(TextEntry *table, s32 count)
{
    s32 i;
    s32 n;

    for (n = 0; n < PROMPTS; n++) {
        found_index[n] = -1;
    }
    for (i = 0; i < count; i++) {
        for (n = 0; n < PROMPTS; n++) {
            if (table[i].id == prompts[n].id && found_index[n] < 0) {
                found_index[n] = i;
                break;
            }
        }
    }
    found_table = table;
    found_count = count;
    applied_layout = -1;
}

static s32 found_still_hold(TextEntry *table, s32 count)
{
    s32 n;

    if (table != found_table || count != found_count) {
        return 0;
    }
    for (n = 0; n < PROMPTS; n++) {
        if (found_index[n] >= 0 && table[found_index[n]].id != prompts[n].id) {
            return 0;
        }
    }
    return 1;
}

static s32 marks_hold(TextEntry *table)
{
    s32 n;

    for (n = 0; n < PROMPTS; n++) {
        if (found_index[n] < 0) {
            continue;
        }
        if (prompts[n].kind == TWO_BUTTONS) {
            if (table[found_index[n]].text != mark_text[n]) {
                return 0;
            }
        } else if (mark_byte[n] != 0 && *mark_byte[n] != mark_value[n]) {
            return 0;
        }
    }
    return 1;
}

/* WEAPON and CROUCH: set every button of TEXT to LAYOUT's, and mark the first. */
static void set_buttons(s32 n, u8 *text, s32 future)
{
    u8 *c;
    u8 classic_glyph = prompts[n].kind == WEAPON ? GLYPH_CIRCLE : GLYPH_R1;
    u8 future_glyph = prompts[n].kind == WEAPON ? GLYPH_R1 : GLYPH_R2;

    mark_byte[n] = 0;
    for (c = text; *c != 0; c++) {
        if (*c == classic_glyph || *c == future_glyph) {
            *c = future ? future_glyph : classic_glyph;
        } else if (prompts[n].kind == CROUCH && c != text && c[-1] == 'R' && (*c == '1' || *c == '2')) {
            *c = future ? '2' : '1';    /* German "R1-Taste" */
        } else {
            continue;
        }
        if (mark_byte[n] == 0) {
            mark_byte[n] = c;
            mark_value[n] = *c;
        }
    }
}

/* TWO_BUTTONS: SOURCE with the prompt's glyphs mapped into its copy, and a
 * glyph that follows the same glyph across a short joining word merged
 * into it. 0 if SOURCE does not fit. */
static s32 build_copy(s32 n, const u8 *source)
{
    const Prompt *p = &prompts[n];
    u8 *out = copies[p->copy];
    s32 length = 0;
    s32 last_glyph = 0;         /* the last glyph written, while only letters have followed it */
    s32 after_last = 0;         /* where the copy stood just after it */
    u8 c;
    s32 i;

    for (; *source != 0; source++) {
        c = *source;
        for (i = 0; i < 2; i++) {
            if (p->from[i] != 0 && c == p->from[i]) {
                c = p->to[i];
                break;
            }
        }
        if (IS_GLYPH(c)) {
            if (c == last_glyph && length > after_last && length - after_last <= MERGE_GAP) {
                length = after_last;    /* "L2 or L2" -> "L2" */
                continue;
            }
            last_glyph = c;
            after_last = length + 1;
        } else if (c < 0x20) {
            last_glyph = 0;
        }
        if (length >= COPY_SIZE - 1) {
            return 0;
        }
        out[length++] = c;
    }
    out[length] = 0;
    return 1;
}

static void remember_original(TextEntry *entry, u8 *original)
{
    s32 r;

    for (r = 0; r < REDIRECTS; r++) {
        if (redirects[r].entry == entry) {
            redirects[r].original = original;
            return;
        }
    }
    redirects[redirect_next].entry = entry;
    redirects[redirect_next].original = original;
    redirect_next = (redirect_next + 1) % REDIRECTS;
}

static u8 *original_of(TextEntry *entry)
{
    s32 r;

    for (r = 0; r < REDIRECTS; r++) {
        if (redirects[r].entry == entry) {
            return redirects[r].original;
        }
    }
    return 0;
}

static void set_two_buttons(s32 n, TextEntry *entry, s32 future)
{
    u8 *copy = copies[prompts[n].copy];
    u8 *original;

    if (future && entry->text != copy) {
        if (build_copy(n, entry->text)) {
            remember_original(entry, entry->text);
            entry->text = copy;
        }
    } else if (!future && entry->text == copy) {
        original = original_of(entry);
        if (original != 0) {
            entry->text = original;
        }
    }
    mark_text[n] = entry->text;
}

void ee_layout_prompts(s32 level)
{
    TextEntry *table = *LVL_DATA(level, text_table, TextEntry *volatile *);
    s32 count = *(volatile s32 *)(LVL_DATA(level, help_state, u8 *) + HELP_STATE_COUNT);
    s32 layout = GetControlLayout();
    s32 n;

    if (table == 0 || count <= 0) {
        return;
    }
    if (!found_still_hold(table, count)) {
        find_prompts(table, count);
    }
    if (layout == applied_layout && marks_hold(table)) {
        return;
    }
    for (n = 0; n < PROMPTS; n++) {
        if (found_index[n] < 0) {
            continue;
        }
        if (prompts[n].kind == TWO_BUTTONS) {
            set_two_buttons(n, &table[found_index[n]], layout == EE_LAYOUT_FUTURE);
        } else {
            set_buttons(n, table[found_index[n]].text, layout == EE_LAYOUT_FUTURE);
        }
    }
    applied_layout = layout;
}
