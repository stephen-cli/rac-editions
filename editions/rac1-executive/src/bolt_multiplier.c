/* x1: the Challenge Mode bolt multiplier ("skill" economy).
 *
 * Going Commando's Challenge Mode pays a multiplier on enemy kills that climbs
 * while the player is not hit and drops back to x1 on any hit (its payout,
 * Oozla 0x30F920; its climb, 0x30F7A8; its reset, the hero-hit routine at
 * 0x2B3858), and it pays less for an enemy each time that enemy dies again
 * (a 3-bit count per placed enemy, 0x2D9528: 100, 50, 40, 30, 25, 20, 15, then
 * 10 %). The edition gives R&C1 the same loop, tuned for R&C1's enemies, which
 * die faster and hit less often: a step from xm costs 2m^2 + 2 kills, so x2
 * after 4 kills, x3 after 14, x4 after 34 and x5 after 68, as Going Commando's
 * steps grow towards its x20.
 *
 * How R&C1 pays a kill. SetDeathBits spreads the
 * moby's placed bolt value +-25 % and calls the bolt drop,
 * spawn_randomized_bolt_drop(moby, low, high, flags, path), which picks the amount in
 * [low, high], doubles it in Challenge Mode (retail), runs the anti-farm
 * limiter and throws the bolts as 50/20/5/1-bolt pieces through SpawnBolt.
 * R&C1's base already falls for a repeat: the level loader halves a placed
 * enemy's value once it has died before, and a respawn after its mission
 * pays its lower post-mission value.
 *
 * The limiter. It only judges drops under 500 bolts from mobys whose mission
 * is already complete (D_L00_0015FD48[mission] == 0xFF, a respawn), and only
 * once the save has some farm time: m = such bolts per minute of active play
 * (the saved counters D_0015EF28, frames, and D_0015EF2C, bolts; CollectBolt
 * adds a piece to the bolts only when SpawnBolt was told it came from such a
 * drop). Under 150 a minute it pays x2 to x5, 150-300 x1, 301-351 a fading
 * fraction (feature limiter_fade), from 352 a tenth. Retail halves m in
 * Challenge Mode to cancel its doubling.
 *
 * What feature bolt_multiplier changes (tools/overlay_patch.py):
 *   - the drop no longer doubles in Challenge Mode and the limiter no longer
 *     halves m: Challenge Mode pays what a first playthrough pays, and the
 *     multiplier is the way to earn more ("classic", the build without this
 *     feature, keeps retail's doubling);
 *   - SetDeathBits's call of the drop comes here (ee_bolt_drop, src/hooks.s).
 *
 * A kill. In Challenge Mode, a creature's kill (class type 5) by Ratchet or
 * Clank on foot: the base drops through retail as before, so the limiter
 * judges and counts it; then the bonus, base x (multiplier - 1) x the share
 * for the enemy's death count (Going Commando's table), drops as its own
 * pieces with flag 0x20, which keeps it out of the limiter. The falloff scales
 * the bonus, not the base, because R&C1's base already falls on a repeat. For
 * a respawn the bonus takes the limiter's penalty, never its slow-play bonus
 * (which would stack on the multiplier), and counts towards the limiter's
 * rate, as Going Commando's multiplied drops do. Then the enemy's death count
 * goes up and the kill counts towards the next step. Ship combat, turrets and
 * scripted moments (hero state 0x32: the ship, Batalia's cannon and Gaspar's
 * mountable turret set it, and retail hides the bolt counter in it) and Giant
 * Clank and the other vehicles (hero vehicle mode 2 and 3) neither pay, climb
 * nor reset the multiplier. Crates, the Metal Detector's stashes and the
 * drop's other callers pay as before.
 *
 * The death counts. A nibble per enemy (tools/gen_enemy_ids.py lists each
 * level's by uid; a kill finds its slot by binary search) in the save's
 * twentieth page (EE_DEATHS_PAGE, src/modseg.h): a full save writes it, and
 * after the game's next autosave the edition writes it too. A new game and
 * the Challenge Mode reset restore the page to zeros, so the counts restart
 * with each run.
 *
 * The reset. Any hit resets it to x1: the hero's hit points (hero + 0x22A8)
 * going down between two frames, while the hero stays in the same vehicle
 * mode (entering and leaving Clank swaps the hit points) and in neither frame
 * in an excluded section, and not across a level change (loading a save
 * passes through the title and sets the hit points anew).
 *
 * The multiplier and the kills since its last step are saved, as Going
 * Commando saves its multiplier: they live in the save's twentieth page
 * (EE_MULTIPLIER_PAGE), which goes to the card the way the death counts do,
 * so loading a save brings back the multiplier it was saved with. A new game
 * and the Challenge Mode reset start at x1, with the rest of the page.
 *
 * The HUD. While the multiplier is above x1 the bolt counter shows it in the
 * corner below its bolt icon, as Going Commando does, in orange and fading in
 * and out with the counter; a step up shows the counter. The counter's slot
 * draws HudBoltsDraw (draw_bolt_counter_hud_panel); each frame the HUD update swaps that
 * slot's draw for ee_bolts_draw, which draws retail's counter and then the
 * multiplier. */
#include "modseg.h"

#define MAX_MULTIPLIER  5

#define STATE_EXTERNAL    0x32          /* the hero driven by something else: the ship, a turret, a script */
#define VEHICLE_CLANK     1

#define MOBY_CLASS      0x24            /* the moby's class header */
#define MOBY_MISSION    0xB0            /* its mission, 0xFF for none */
#define MOBY_UID        0xB2            /* s16: its placement's uid, < 0 for none */
#define CLASS_TYPE      0x46            /* s16: 5 a creature */
#define TYPE_CREATURE   5
#define MISSION_NONE    0xFF
#define MISSION_DONE    0xFF            /* D_L00_0015FD48[mission]: the level's mission states */
#define DROP_NO_LIMITER 0x20            /* the drop's flag: skip the limiter and its counters */
#define LIMITER_MAX     500             /* the limiter judges drops under this */
#define DEATHS_MAX      7

/* Going Commando's share of a kill's bolts by the enemy's earlier deaths (Oozla 0x1A89C8), in percent. */
static const u8 falloff[DEATHS_MAX + 1] = { 100, 50, 40, 30, 25, 20, 15, 10 };

DECLARE_LVL(SetDeathBits);                  /* SetDeathBits: the hooked call site is in it */
DECLARE_LVL(spawn_randomized_bolt_drop);    /* the bolt drop(moby, low, high, flags, path) */
DECLARE_LVL(random_integer_below);          /* random_integer_below(n) */
DECLARE_LVL(scale_ticks);                   /* scale_ticks(ticks at 60 a second): the frames a second at 60 */
DECLARE_LVL(queue_animation_update);        /* queue_animation_update: a HUD slot's element, as CollectBolt queues it */
DECLARE_LVL(initialize_bolt_counter_hud);   /* HudBoltsInit */
DECLARE_LVL(update_bolt_counter_hud_animation_and_visibility);   /* HudBoltsUpdate */
DECLARE_LVL(draw_bolt_counter_hud_panel);   /* HudBoltsDraw(slot) */
DECLARE_LVL(font_print_right_small);        /* font_print_right_small(x, y, colour, text, limit) */
DECLARE_LVL_DATA(mission_state);            /* D_L00_0015FD48: a byte per mission, 0xFF once done */
DECLARE_LVL_DATA(hud_slots);
typedef void (*DropFn)(void *moby, s32 low, s32 high, s32 flags, s32 path);
typedef s32 (*RandomFn)(s32 n);
typedef s32 (*TicksFn)(s32 ticks);
typedef s32 (*QueueFn)(s32 slot, s32 icon, u32 init, u32 update, u32 draw, void *value, s32 most);
typedef s32 (*SlotDrawFn)(void *slot);
typedef void (*PrintFn)(s32 x, s32 y, u32 colour, const char *text, s32 limit);

extern const u16 ee_enemy_first[];      /* build/modseg/gen_enemy_ids.c */
extern const u16 ee_enemy_ids[];

/* The saved multiplier (src/modseg.h): a byte each, zero in a save that never had one. */
#define SAVED_MULTIPLIER (((volatile u8 *)EE_MULTIPLIER_PAGE)[0])  /* 0 or 1 for x1 */
#define SAVED_KILLS      (((volatile u8 *)EE_MULTIPLIER_PAGE)[1])  /* kills since the last step */

static s32 last_level = -1;
static s32 last_hit_points = -1;
static s32 last_vehicle_mode = -1;
static s32 last_excluded;
static s32 hud_level;                   /* the program whose HUD the swapped draw belongs to */

static s32 in_challenge_mode(void)
{
    return g_completes > 0 || challenge_mode != 0;
}

static s32 current_multiplier(void)
{
    s32 multiplier = SAVED_MULTIPLIER;
    if (multiplier < 1) {
        return 1;
    }
    return multiplier > MAX_MULTIPLIER ? MAX_MULTIPLIER : multiplier;
}

static void set_multiplier(s32 multiplier, s32 kills)
{
    if (SAVED_MULTIPLIER == multiplier && SAVED_KILLS == kills) {
        return;
    }
    SAVED_MULTIPLIER = (u8)multiplier;
    SAVED_KILLS = (u8)kills;
    ee_page19_changed_with_next_save();
}

/* The sections where the multiplier stands still: hard to play without damage, or not Ratchet's own fight. */
static s32 excluded_section(void)
{
    return g_hero.state.current == STATE_EXTERNAL || g_hero.vehicle_mode > VEHICLE_CLANK;
}

static s32 is_creature(const u8 *moby)
{
    const u8 *klass = *(u8 *const *)(moby + MOBY_CLASS);
    return klass != 0 && *(const s16 *)(klass + CLASS_TYPE) == TYPE_CREATURE;
}

/* The kills the limiter judges: a moby of a mission already completed, which the level respawns. */
static s32 is_respawn(s32 L, const u8 *moby)
{
    s32 mission = moby[MOBY_MISSION];
    if (mission == MISSION_NONE) {
        return 0;
    }
    return LVL_DATA(L, mission_state, const u8 *)[mission] == MISSION_DONE;
}

/* --- the death counts ------------------------------------------------------------------------------------------ */

/* The moby's slot among all levels' counted enemies, or -1 if it is not one. */
static s32 death_slot(s32 L, const u8 *moby)
{
    s32 uid = *(const s16 *)(moby + MOBY_UID);
    s32 low, high;
    if (uid < 0 || L < 0 || L >= FE_BOOT) {
        return -1;
    }
    low = ee_enemy_first[L];
    high = ee_enemy_first[L + 1] - 1;
    while (low <= high) {
        s32 mid = (low + high) / 2;
        s32 id = ee_enemy_ids[mid];
        if (id == uid) {
            return mid;
        }
        if (id < uid) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return -1;
}

static s32 deaths_of(s32 slot)
{
    u8 byte = ((volatile u8 *)EE_DEATHS_PAGE)[slot >> 1];
    return (slot & 1) ? byte >> 4 : byte & 0xF;
}

static void add_death(s32 slot)
{
    volatile u8 *byte = (volatile u8 *)EE_DEATHS_PAGE + (slot >> 1);
    s32 shift = (slot & 1) * 4;
    s32 deaths = (*byte >> shift) & 0xF;
    if (deaths >= DEATHS_MAX) {
        return;
    }
    *byte = (u8)((*byte & ~(0xF << shift)) | ((deaths + 1) << shift));
    ee_page19_changed_with_next_save();
}

/* --- the limiter's penalty for a respawn's bonus ----------------------------------------------------------------- */

/* The limiter's factor in tenths of a percent for a drop of AMOUNT from a respawn: its penalty tiers only. */
static s32 limiter_penalty(s32 L, s32 amount)
{
    s32 rate;
    if (amount >= LIMITER_MAX || active_play_frame_count == 0) {
        return 1000;
    }
    rate = LVL(L, scale_ticks, TicksFn)(60) * 60 * counted_bolts_total / active_play_frame_count;
    if (rate <= 300) {
        return 1000;
    }
    if (rate < 352) {
        return (360 - rate) * 1000 / 80;
    }
    return 100;
}

/* --- the climb and the kill -------------------------------------------------------------------------------------- */

static void show_bolt_counter(s32 L);

static void count_kill(s32 L)
{
    s32 multiplier = current_multiplier();
    s32 kills = SAVED_KILLS + 1;
    if (multiplier >= MAX_MULTIPLIER) {
        return;
    }
    if (kills >= 2 * multiplier * multiplier + 2) {
        set_multiplier(multiplier + 1, 0);
        show_bolt_counter(L);
    } else {
        set_multiplier(multiplier, kills);
    }
}

/* SetDeathBits's call of the bolt drop (src/hooks.s passes its return address). */
void ee_bolt_drop_c(void *moby, s32 low, s32 high, s32 flags, s32 path, u32 ra)
{
    s32 L = ee_level_of_call(ra, LVL_TABLE(SetDeathBits), EE_SITE_DEATH_DROP);
    DropFn drop = LVL(L, spawn_randomized_bolt_drop, DropFn);
    s32 amount, bonus, slot, deaths, respawn;

    if (!in_challenge_mode() || flags == 0 || (low <= 0 && high <= 0) || !is_creature(moby)) {
        drop(moby, low, high, flags, path);
        return;
    }
    slot = death_slot(L, moby);
    deaths = slot >= 0 ? deaths_of(slot) : 0;
    if (slot >= 0) {
        add_death(slot);
    }
    if (excluded_section()) {
        drop(moby, low, high, flags, path);
        return;
    }
    /* the drop's own pick: low at least 1, then a value in [low, high] */
    if (low <= 0) {
        low = 1;
    }
    amount = low + LVL(L, random_integer_below, RandomFn)(high - low + 1);
    drop(moby, amount, amount, flags, path);
    bonus = amount * (current_multiplier() - 1) * falloff[deaths] / 100;
    respawn = is_respawn(L, moby);
    if (respawn) {
        bonus = bonus * limiter_penalty(L, amount) / 1000;
    }
    if (bonus > 0) {
        drop(moby, bonus, bonus, flags | DROP_NO_LIMITER, path);
        if (respawn) {
            counted_bolts_total += bonus;
        }
    }
    count_kill(L);
}

/* --- the reset on a hit, and the HUD ---------------------------------------------------------------------------- */

#define HUD_SLOTS       13
#define HUD_SLOT_BYTES  0x90
#define HUD_SLOT_DRAW   0x18
#define BOLTS_SLOT      2               /* CollectBolt's queue_animation_update(2, 0x754E, ...) */
#define BOLTS_ICON      0x754E
#define BOLTS_MOST      9999999

static s32 bolts_draw(void *slot);

/* The bolt counter up, as CollectBolt shows it after a pickup. */
static void show_bolt_counter(s32 L)
{
    LVL(L, queue_animation_update, QueueFn)(BOLTS_SLOT, BOLTS_ICON, LVL(L, initialize_bolt_counter_hud, u32),
                                            LVL(L, update_bolt_counter_hud_animation_and_visibility, u32),
                                            LVL(L, draw_bolt_counter_hud_panel, u32), (void *)&gBolts, BOLTS_MOST);
}

/* COUNT bolts added, up to the counter's most, with the counter shown as a pickup shows it. */
void ee_add_bolts(s32 count, s32 L)
{
    gBolts = gBolts + count > BOLTS_MOST ? BOLTS_MOST : gBolts + count;
    show_bolt_counter(L);
}

static void swap_bolt_counter_draw(s32 L)
{
    u32 base = LVL_DATA(L, hud_slots, u32);
    u32 retail = LVL(L, draw_bolt_counter_hud_panel, u32);
    s32 i;
    hud_level = L;
    for (i = 0; i < HUD_SLOTS; i++) {
        volatile u32 *draw = (volatile u32 *)(base + i * HUD_SLOT_BYTES + HUD_SLOT_DRAW);
        if (*draw == retail) {
            *draw = (u32)bolts_draw;
        }
    }
}

/* Once a frame, from the HUD update hook (src/quick_select_pause.c). */
void ee_bolt_multiplier_frame(s32 L)
{
    s32 hit_points = g_hero.health.hp;
    s32 vehicle_mode = g_hero.vehicle_mode;
    s32 excluded = excluded_section();

    if (L == last_level && vehicle_mode == last_vehicle_mode && !excluded && !last_excluded
        && hit_points < last_hit_points && in_challenge_mode()) {
        set_multiplier(1, 0);
    }
    last_level = L;
    last_hit_points = hit_points;
    last_vehicle_mode = vehicle_mode;
    last_excluded = excluded;
    if (L < FE_BOOT) {
        swap_bolt_counter_draw(L);
    }
}

/* The bolt counter's layout, the level's own words beside its $gp (HudBoltsDraw reads them the same way). */
#define GP_TEXT_FRAMES  (-0x73F8)       /* D_L00_0015F908: frames to fade the text in */
#define SLOT_X          0x50            /* the counter's right edge */
#define SLOT_SHOWN      0x70            /* bytes: the panel's and the text's frames */

/* Where "x5" sits: its right edge under the bolt icon's right edge, its top over the icon's lower third. */
#define MULT_RIGHT      2
#define MULT_TOP        18
#define MULT_COLOUR     0x0020A0FF      /* orange (0xBBGGRR; the alpha goes on top) */

static s32 gp_word(s32 offset)
{
    register u32 gp __asm__("$28");
    return *(volatile s32 *)(gp + offset);
}

static s32 bolts_draw(void *slot)
{
    s32 L = hud_level;
    u8 *s = (u8 *)slot;
    s32 width = LVL(L, draw_bolt_counter_hud_panel, SlotDrawFn)(slot);
    s32 multiplier = current_multiplier();
    s32 text_frames, alpha, x, y;
    char text[4];

    /* retail hides the counter while the hero is driven by something else (HudBoltsDraw's own test) */
    if (multiplier <= 1 || !in_challenge_mode() || g_hero.state.current == STATE_EXTERNAL || s[SLOT_SHOWN] == 0) {
        return width;
    }
    text_frames = gp_word(GP_TEXT_FRAMES);
    alpha = text_frames > 0 ? 0x80 * s[SLOT_SHOWN + 1] / text_frames : 0x80;
    if (alpha > 0x80) {
        alpha = 0x80;
    }
    if (alpha <= 0) {
        return width;
    }
    x = *(s32 *)(s + SLOT_X) + MULT_RIGHT;
    y = (pal_mode ? 10 : 18) + MULT_TOP;
    text[0] = 'x';
    text[1] = (char)('0' + multiplier);
    text[2] = 0;
    LVL(L, font_print_right_small, PrintFn)(x + 1, y + 1, (u32)alpha << 24, text, -1);
    LVL(L, font_print_right_small, PrintFn)(x, y, ((u32)alpha << 24) | MULT_COLOUR, text, -1);
    return width;
}
