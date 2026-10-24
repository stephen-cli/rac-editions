/* rz3/rz4: what the Randomizer says. The banner that names what a check gave,
 * and the givers' prompts with the name of the item they no longer give
 * hidden (src/randomizer_play.h for the other files).
 *
 * The banners. The game's banner texts belong to the level they are shown on
 * (an item's "You got the ..." is in its own level's text block only, a
 * planet's "Infobot for Planet ... acquired" likewise), and a text missing
 * from the level's block shows as "Paradox: This message does not exist".
 * So the Randomizer's banner is its own string, EE_TEXT_RZ_BANNER (served
 * by src/text.c), built here in the game's language from the edition's
 * templates ("You found the @!", "You got the @!", "You won a @!",
 * text/strings.tsv) and the game's own words, which tools/gen_randomizer.py
 * takes from the disc's text table with their colour codes: the item names
 * in the game's orange, each planet's infobot banner, "Gold Bolt".
 *
 * The prompts. A character offering a check names the item retail has
 * there ("Buy the Swingshot for 1,000 bolts"), which is not what the check
 * gives. Every string msg_string hands out passes through ee_rz_redact_c
 * (src/text_hook.s), which replaces the name of each uncollected check's
 * item on the current planet with "???", and while the vendor's menu is up
 * the names of every uncollected vendor check's item too, as a vendor sells
 * the stock of every planet visited. The pause menu is left alone (it names
 * the items owned), as are the edition's own strings (its banners name the
 * reward). A HelpDesk line on that planet naming the item reads "???" too. */
#include "randomizer_play.h"

#define LANGUAGES       8               /* the game's language ids, g_language */
#define TEMPLATE_MARK   '@'             /* where the item's name goes in EE_TEXT_RZ_FOUND, _GOT and _WON */
#define BANNER_MAX      160
#define NAME_MAX        80
#define BANNER_TICKS    300             /* as long as GiveItem's own banner: scale_ticks(0x12C) */
#define REDACTED        "???"
#define REDACT_BUFFERS  4               /* strings handed out at once: a prompt's callers draw several together */
#define REDACT_MAX      256
#define MODE_PAUSE      3
#define LEVELS          19

DECLARE_LVL_DATA(game_mode);            /* D_0015F6E8: 0 play, 2 cinematic, 3 pause */

/* The game's words (tools/gen_randomizer.py, build/modseg/gen_rz.c), by language. */
extern const char *const ee_rz_item_names[ITEMS][LANGUAGES];        /* each item's name, in orange */
extern const char *const ee_rz_infobot_banners[18][LANGUAGES];      /* "Infobot for Planet Aridia acquired" */
extern const char *const ee_rz_special_names[5][LANGUAGES];         /* in SPECIAL_ITEMS' order */
extern const char *const ee_rz_word_gold_bolt[LANGUAGES];           /* "Gold Bolt" */
extern const char *const ee_rz_word_infobot[LANGUAGES];             /* "Infobot" */
extern const int ee_text_first, ee_text_count;                      /* the edition's string ids (gen_text.c) */

/* The items the game keeps as flags rather than in the inventory, in ee_rz_special_names' order. */
static const s32 SPECIAL_ITEMS[5] = {
    RZ_ZOOMERATOR, RZ_RARITANIUM, RZ_CODEBOT, RZ_PREMIUM_NANOTECH, RZ_ULTRA_NANOTECH,
};

/* src/text_hook.s keeps msg_string's caller here while ee_rz_redact_c runs: its return address, the id, the
 * program. */
volatile u32 ee_redact_saved[3];

static char banner_text[BANNER_MAX];
static char redacted[REDACT_BUFFERS][REDACT_MAX];
static s32 next_redacted;

/* --- strings ------------------------------------------------------------------------------------------------- */

/* WORDS in the game's language, English if it has none. */
static const char *word(const char *const words[LANGUAGES])
{
    s32 language = g_language;
    const char *text = language >= 0 && language < LANGUAGES ? words[language] : 0;
    if (text != 0) {
        return text;
    }
    return words[0] != 0 ? words[0] : "";
}

/* TEXT appended to OUT (CAPACITY bytes) at AT; returns the new end. */
static s32 append(char *out, s32 capacity, s32 at, const char *text)
{
    while (*text && at < capacity - 1) {
        out[at++] = *text++;
    }
    out[at] = 0;
    return at;
}

static s32 append_number(char *out, s32 capacity, s32 at, s32 number)
{
    char digits[12];
    s32 count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number > 0);
    while (count > 0 && at < capacity - 1) {
        out[at++] = digits[--count];
    }
    out[at] = 0;
    return at;
}

static s32 text_length(const char *text)
{
    s32 length = 0;
    while (text[length]) {
        length++;
    }
    return length;
}

/* The game's name for ITEM in the player's language; 0 for one it has no name for (an infobot, a Gold Bolt). */
static const char *display_name(s32 item)
{
    s32 i;
    if (item == RZ_CHARGE_BOOTS) {
        return ee_msg_string_edition(TEXT_CHARGE_BOOTS);
    }
    for (i = 0; i < 5; i++) {
        if (SPECIAL_ITEMS[i] == item) {
            return word(ee_rz_special_names[i]);
        }
    }
    if (rz_item_game_id(item) >= 0) {
        return word(ee_rz_item_names[rz_item_game_id(item)]);
    }
    return 0;
}

/* --- the banner ---------------------------------------------------------------------------------------------- */

const char *ee_rz_banner_text(void)
{
    return banner_text;
}

/* TEMPLATE into banner_text, with NAME at its mark. */
static void fill_template(const char *template, const char *name)
{
    s32 at = 0;
    while (*template && at < BANNER_MAX - 1) {
        if (*template == TEMPLATE_MARK) {
            at = append(banner_text, BANNER_MAX, at, name);
        } else {
            banner_text[at++] = *template;
        }
        template++;
    }
    banner_text[at] = 0;
}

/* How CHECK's reward is come by: found in the world, got from a character or a vendor, won in a race. */
static s32 how_of(s32 check)
{
    const struct RzCheck *row = rz_check(check);
    return row != 0 ? row->how : RZ_HOW_GOT;    /* the bonus vendor entry has no table row */
}

/* The banner for ITEM received at CHECK: the check's words around the item's name ("You found the Trespasser!"),
 * a Gold Bolt with the count ("Gold Bolt (12/40)"); an infobot shows retail's own banner for its planet. */
void rz_show_item_banner(s32 check, s32 item, s32 L)
{
    char name[NAME_MAX];
    s32 how = how_of(check);
    s32 template = how == RZ_HOW_WON ? EE_TEXT_RZ_WON : how == RZ_HOW_GOT ? EE_TEXT_RZ_GOT : EE_TEXT_RZ_FOUND;
    if (item >= RZ_INFOBOT_NOVALIS && item <= RZ_INFOBOT_VELDIN) {
        append(banner_text, BANNER_MAX, 0, word(ee_rz_infobot_banners[item - RZ_INFOBOT_NOVALIS]));
        LVL(L, func_L00_00264DB8, BannerFn)(EE_TEXT_RZ_BANNER, BANNER_TICKS);
        return;
    }
    name[0] = 0;
    if (item == RZ_GOLD_BOLT) {
        s32 at = append(name, NAME_MAX, 0, word(ee_rz_word_gold_bolt));
        at = append(name, NAME_MAX, at, " (");
        at = append_number(name, NAME_MAX, at, ee_rz_gold_received());
        at = append(name, NAME_MAX, at, "/");
        at = append_number(name, NAME_MAX, at, RZ_GOLD_BOLTS);
        append(name, NAME_MAX, at, ")");
    } else if (display_name(item) != 0) {
        append(name, NAME_MAX, 0, display_name(item));
    }
    fill_template(ee_msg_string_edition(template), name);
    LVL(L, func_L00_00264DB8, BannerFn)(EE_TEXT_RZ_BANNER, BANNER_TICKS);
}

/* --- the prompts --------------------------------------------------------------------------------------------- */

/* Whether TEXT begins with NAME (LENGTH bytes). */
static s32 starts_with(const char *text, const char *name, s32 length)
{
    s32 i;
    for (i = 0; i < length; i++) {
        if (text[i] != name[i]) {
            return 0;
        }
    }
    return 1;
}

/* TEXT with every NAME replaced by REDACTED, in the next buffer; 0 when NAME is not in TEXT. */
static const char *redact_name(const char *text, const char *name)
{
    s32 length = text_length(name);
    s32 i, at = 0;
    char *out;
    if (length == 0) {
        return 0;
    }
    for (i = 0; text[i] && !starts_with(text + i, name, length); i++) {
    }
    if (!text[i]) {
        return 0;
    }
    out = redacted[next_redacted];
    next_redacted = (next_redacted + 1) % REDACT_BUFFERS;
    for (i = 0; text[i] && at < REDACT_MAX - 1;) {
        if (starts_with(text + i, name, length)) {
            at = append(out, REDACT_MAX, at, REDACTED);
            i += length;
        } else {
            out[at++] = text[i++];
        }
    }
    out[at] = 0;
    return out;
}

/* The name CHECK's giver uses for its item, in the game's language; 0 for a Gold Bolt spot. */
static const char *check_name(s32 check)
{
    const struct RzCheck *row = rz_check(check);
    if (row == 0) {
        return display_name(RZ_CHARGE_BOOTS);       /* the bonus vendor entry */
    }
    if (row->kind == RZ_KIND_INFOBOT) {
        return word(ee_rz_word_infobot);
    }
    return display_name(row->vanilla);
}

static s32 edition_string(s32 id)
{
    return id >= ee_text_first && id < ee_text_first + ee_text_count;
}

/* msg_string's result for ID in program L (src/text_hook.s): TEXT, or a copy of it with names hidden. */
const char *ee_rz_redact_c(const char *text, s32 id, s32 L)
{
    s32 check, checks;
    if (text == 0 || !rz_play_active() || L < 0 || L >= LEVELS || edition_string(id)
        || *(volatile s32 *)LVL_DATA(L, game_mode, u8 *) == MODE_PAUSE) {
        return text;
    }
    checks = ee_rz_bonus_planet() != 0 ? RZ_CHECKS_MAX : RZ_CHECKS_BASE;
    for (check = 0; check < checks; check++) {
        const struct RzCheck *row = rz_check(check);
        s32 sold_by_vendor = row == 0 || row->kind == RZ_KIND_VENDOR;
        s32 relevant = sold_by_vendor ? rz_vendor_menu_open() : row->planet == L;
        const char *name, *redacted_text;
        if (ee_rz_check_collected(check) || !relevant) {
            continue;
        }
        name = check_name(check);
        if (name != 0) {
            redacted_text = redact_name(text, name);
            if (redacted_text != 0) {
                text = redacted_text;
            }
        }
    }
    return text;
}
