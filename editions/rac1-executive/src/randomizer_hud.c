/* rz3: the health display with 7 orbs (src/randomizer_play.h for the other
 * files).
 *
 * In a Randomizer save health starts at 4 orbs and the Nanotech upgrades
 * come in any order: one more with the Premium, three more with the Ultra.
 * HudHealthDraw (func_L00_0023A1A0) lays the orbs out by their count: 8 (two
 * rows of four) and 5 (three over two) have tables, anything else is the row
 * of four, so the 7 of the Ultra without the Premium drew seven orbs over
 * four places. The edition's drawer takes HudHealthDraw's place (its address
 * in Hud_RegisterHealthBank's lui/addiu, tools/gen_randomizer.py) and draws
 * 7 as four over three, the three centred under the four as retail centres
 * the two under the three, with the backing bars of a row of four and of
 * three; any other count is retail's. The drawing is retail's, with its
 * constants: the element's fade over 8, the bars 32 tall 16 above the orbs,
 * the rows 32 apart. */
#include "randomizer_play.h"

DECLARE_LVL(func_L00_0023A1A0);         /* HudHealthDraw(element) */
DECLARE_LVL(func_00200468);             /* HudSprite(frame, x, y, width, height, alpha) */
DECLARE_LVL(func_00200650);             /* DrawHudSpriteTurned(frame, x, y, width, height, alpha) */
DECLARE_LVL(func_L00_0023BAB8);         /* HudFrame(element, frame, x, y, scale, alpha) */
typedef s32 (*HealthFn)(void *);
typedef void (*SpriteFn)(s32, s32, s32, s32, s32, s32);
typedef void (*FrameFn)(void *, s32, s32, s32, s32, s32);

/* The HUD element HudHealthDraw is given. */
#define ELEMENT_X       0x50            /* the orbs' centre */
#define ELEMENT_RESULT  0x58            /* what the drawer returns */
#define ELEMENT_FADE    0x70            /* two bytes, 0-8: shown, and strength */
#define ELEMENT_HEALTH  0x78            /* the orbs full */
#define ELEMENT_ANIM    0x80            /* the orbs' animation: phase, glow */

#define ORB_ICON        0x7536
#define ORB_PHASES      0x1E            /* the orb icon's frames; frame ORB_PHASES is an orb at rest */
#define ORB_GLOW_X      0xE             /* the glow sprite's place and size over an orb */
#define ORB_GLOW_Y      -0x11
#define ORB_GLOW_SIZE   0x22
#define BAR_ICON        0x7580
#define BAR_HEIGHT      32
#define BAR_ABOVE       -16             /* the bars sit 16 above the orbs */
#define BAR_HALF_FOUR   48.0f           /* a row of four orbs' bar, half its width */
#define BAR_HALF_THREE  32.0f
#define ROW_STEP        32
#define HUD_Y_BASE      14
#define FADE_MAX        8.0f
#define VEHICLE         2
#define HERO_STATE_HIDDEN 0x32          /* a state that hides the HUD's health */

/* x, y of each orb: four, then three under them. */
static const s16 SEVEN_ORBS[7][2] = {
    { -80, 0 }, { -48, 0 }, { -16, 0 }, { 16, 0 },
    { -64, ROW_STEP }, { -32, ROW_STEP }, { 0, ROW_STEP },
};

static s32 health_count(void)
{
    return HERO_MODE == 1 ? CLANK_HEALTH_MAX : MAX_HEALTH;
}

static f32 clamp_unit(f32 value)
{
    if (value > 1.0f) {
        return 1.0f;
    }
    return value < 0.0f ? 0.0f : value;
}

/* A backing bar centred on CENTRE_X, HALF_WIDTH wide each side, with rounded ends. */
static void draw_bar(s32 centre_x, s32 y, s32 half_width, s32 alpha, s32 end_frame, s32 middle_frame, s32 L)
{
    LVL(L, func_00200650, SpriteFn)(end_frame, centre_x + half_width, y, BAR_HEIGHT, BAR_HEIGHT, alpha);
    LVL(L, func_00200468, SpriteFn)(middle_frame, centre_x - half_width, y, half_width << 1, BAR_HEIGHT, alpha);
    LVL(L, func_00200468, SpriteFn)(end_frame, centre_x - half_width - BAR_HEIGHT, y, BAR_HEIGHT, BAR_HEIGHT,
                                    alpha);
}

s32 ee_rz_health_draw_c(void *element, s32 L)
{
    u8 *hud = (u8 *)element;
    const u8 *fade = hud + ELEMENT_FADE;
    const s16 *anim = *(const s16 *const *)(hud + ELEMENT_ANIM);
    s32 centre_x, top_y, full, phase, alpha, bar_alpha, end_frame, middle_frame, i;
    f32 shown, strength;
    if (health_count() != 7 || HERO_MODE == VEHICLE || HERO_STATE == HERO_STATE_HIDDEN || fade[0] == 0) {
        return LVL(L, func_L00_0023A1A0, HealthFn)(element);
    }
    centre_x = *(s32 *)(hud + ELEMENT_X);
    top_y = HUD_Y_BASE + (PAL_MODE ? 0xA : 0x12);
    full = *(s32 *)(hud + ELEMENT_HEALTH);
    shown = clamp_unit((f32)fade[0] / FADE_MAX);
    strength = clamp_unit((f32)fade[1] / FADE_MAX);
    alpha = (s32)(strength * 128.0f);
    bar_alpha = (s32)(20.0f * shown);
    phase = full == 0 ? ORB_PHASES : anim[0] >> 1;

    end_frame = LVL(L, func_00200198, IconFn)(BAR_ICON, 1);
    middle_frame = LVL(L, func_00200198, IconFn)(BAR_ICON, 0);
    draw_bar(centre_x, top_y + BAR_ABOVE, (s32)(shown * BAR_HALF_FOUR), bar_alpha, end_frame, middle_frame, L);
    draw_bar(centre_x, top_y + BAR_ABOVE + ROW_STEP, (s32)(shown * BAR_HALF_THREE), bar_alpha, end_frame,
             middle_frame, L);

    for (i = 0; i < 7; i++) {
        s32 x = centre_x + SEVEN_ORBS[i][0];
        s32 y = top_y + SEVEN_ORBS[i][1];
        LVL(L, func_L00_0023BAB8, FrameFn)(hud, LVL(L, func_00200198, IconFn)(ORB_ICON, phase), x, y, 1, alpha);
        if (phase != ORB_PHASES) {
            s32 glow = (s32)((f32)(anim[1] >> 4) * strength);
            s32 rest_frame = LVL(L, func_00200198, IconFn)(ORB_ICON, ORB_PHASES);
            s32 glow_frame = LVL(L, func_00200198, IconFn)(ORB_ICON, ORB_PHASES + 1);
            LVL(L, func_L00_0023BAB8, FrameFn)(hud, rest_frame, x, y, 1, alpha);
            LVL(L, func_00200468, SpriteFn)(glow_frame, x + ORB_GLOW_X, y + ORB_GLOW_Y, ORB_GLOW_SIZE, ORB_GLOW_SIZE,
                                            glow);
            phase = full == i + 1 ? ORB_PHASES : (phase + 6) % ORB_PHASES;
        }
    }
    return *(s32 *)(hud + ELEMENT_RESULT);
}
