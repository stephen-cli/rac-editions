/* x5: strafing, as Going Commando has it on L2 and R2 (first increment:
 * the movement; the strafe animations and the camera follow later).
 *
 * R&C2 (SCUS_972.68, Oozla's program) reads L2 or R2 held (pad + 0x1A0 &
 * 3, at 0x2B40A0 in its hero input pass) and marks the hero strafing
 * (hero + 0x1454 = 1 when no target is locked). Its run movement then
 * calls its strafe facing routine (0x2B3F30) instead of turning toward
 * the stick: it saves the stick heading, sets the move heading to the
 * camera's yaw (camera + 0x158), turns the hero toward it
 * (turn(0, 0.037, 0.25, 0.20)) and later sets the planar velocity along
 * the saved stick heading (set_planar_vel(hero + 0x1458) at 0x2BC274), so
 * Ratchet faces where the camera looks and slides in the stick's
 * direction. Holding both triggers is the lock-on strafe; the edition
 * treats it as plain strafing.
 *
 * R&C1's RUN movement block (state 2; NTSC decomp 00233de0.c case 2) has
 * the same shape: stick_target() turns the stick into the move heading
 * (hero + 0x180) relative to the camera yaw, turn_to() (or the pivot turn
 * FUN_L00_00211f38 when the stick is hard over) turns the hero toward it,
 * and set_planar_vel() sets the velocity along the facing (99999) or, in
 * the drift case, along the heading. So the strafe is: hook the turns to
 * turn toward the camera yaw instead, and hook set_planar_vel to use the
 * stick heading. R&C1 reads the camera yaw from 0x166ED8 (lit data; the
 * same address in all 19 levels, lit starts at 0x15F000 everywhere).
 *
 * L2 and R2 only mirror L1 (look) and R1 (crouch) in R&C1, so the pad
 * parse hook reads them for the strafe and clears them from every field
 * gameplay reads (held, pressed, released, raw and their copies), which
 * frees the triggers without touching the hero's own button tests.
 * Hero fields are core data (0x13F450): state + 0x2084, facing yaw +
 * 0x98, move heading + 0x180. The per-level call sites and the helpers
 * come from tools/gen_hero.py. */
#include "modseg.h"

DECLARE_LVL(func_00217F68);            /* pad begin-frame; its parse call is hooked */
DECLARE_LVL(func_002181F0);            /* pad parse(pad): derives held/pressed/released */
extern const u32 ee_run_turn_a[19], ee_run_turn_b[19], ee_run_pivot[19], ee_run_planar_a[19], ee_run_planar_b[19];
extern const u32 ee_turn_to[19], ee_pivot_turn[19], ee_set_planar_vel[19];

#define PAD0            0x0013CA40
#define HERO            0x0013F450
#define hero_state      (*(volatile s32 *)(HERO + 0x2084))
#define hero_move_yaw   (*(volatile f32 *)(HERO + 0x180))
#define CAMERA_YAW      (*(volatile f32 *)0x00166ED8)
#define FRAME_SCALE_SQ  (*(volatile f32 *)0x0015EE64)
#define FRAME_TIME      (*(volatile f32 *)0x0015EE6C)
#define STATE_RUN       2
#define PAD_STRAFE      0x3u          /* L2 | R2 in the game's button layout */
#define EE_SITE_PAD_PARSE 0x1C4       /* func_00217F68 + 0x1C4: jal func_002181F0 */

typedef void (*PadParse)(void *pad);
typedef void (*TurnTo)(s32 mode, f32 accel, f32 max_speed, f32 rate);
typedef void (*Pivot)(void);
typedef void (*SetPlanarVel)(f32 yaw);

static u32 strafe_held;       /* L2 or R2 held this frame (pad 0) */
static s32 strafe_active;     /* this frame's RUN turn was the strafe one */
static f32 strafe_heading;    /* the stick heading the velocity follows */

/* After the game's pad parse: remember L2/R2 and hide them from the game. */
void ee_pad_parse_c(void *pad, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_00217F68, EE_SITE_PAD_PARSE);
    volatile u32 *p = (volatile u32 *)pad;
    static const u16 fields[] = { 0x1A0, 0x1A4, 0x1A8, 0x1B0, 0x1B4, 0x1B8, 0x1C0, 0x1C4, 0x1C8, 0x348 };
    s32 i;

    LVL(L, func_002181F0, PadParse)(pad);
    if ((u32)pad != PAD0) {
        return;
    }
    strafe_held = p[0x1A0 / 4] & PAD_STRAFE;
    for (i = 0; i < 10; i++) {
        p[fields[i] / 4] &= ~PAD_STRAFE;
    }
}

static s32 level_of_two(u32 ra, const u32 *a, const u32 *b)
{
    s32 L;
    for (L = 0; L < 19; L++) {
        if (a[L] + 8 == ra || b[L] + 8 == ra) {
            return L;
        }
    }
    return ee_level_of_call(ra, a, 0);
}

/* R&C2's strafe facing: heading = camera yaw, turn toward it (its rates
 * 0.037 / 0.25 / 0.20 per frame, in R&C1's scaled units). */
static void strafe_turn(s32 L)
{
    strafe_heading = hero_move_yaw;
    hero_move_yaw = CAMERA_YAW;
    ((TurnTo)ee_turn_to[L])(0, FRAME_SCALE_SQ * 0.037f, FRAME_SCALE_SQ * 0.25f, FRAME_TIME * 12.2f);
    strafe_active = 1;
}

void ee_strafe_turn_c(s32 mode, u32 ra, f32 accel, f32 max_speed, f32 rate)
{
    s32 L = level_of_two(ra, ee_run_turn_a, ee_run_turn_b);
    if (strafe_held && hero_state == STATE_RUN) {
        strafe_turn(L);
    } else {
        strafe_active = 0;
        ((TurnTo)ee_turn_to[L])(mode, accel, max_speed, rate);
    }
}

void ee_strafe_pivot_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, ee_run_pivot, 0);
    if (strafe_held && hero_state == STATE_RUN) {
        strafe_turn(L);
    } else {
        strafe_active = 0;
        ((Pivot)ee_pivot_turn[L])();
    }
}

void ee_strafe_planar_c(f32 yaw, u32 ra)
{
    s32 L = level_of_two(ra, ee_run_planar_a, ee_run_planar_b);
    if (strafe_active) {
        hero_move_yaw = strafe_heading;
        ((SetPlanarVel)ee_set_planar_vel[L])(strafe_heading);
    } else {
        ((SetPlanarVel)ee_set_planar_vel[L])(yaw);
    }
}
