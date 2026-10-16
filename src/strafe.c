/* x5: strafing, as Going Commando has it on L2 and R2 (the movement and
 * the camera; the strafe animations follow).
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
 * stick heading. The camera yaw is the camera record's + 0x158 (0x166ED8
 * in level 0): executable data re-linked per level, so it comes from the
 * level tables (tools/modseg.py LEVEL_DATA "camera_yaw", read from
 * stick_target's own lui/lwc1 in every level; a first build used level 0's
 * address everywhere and faced a fixed world direction on other planets).
 *
 * L2 and R2 only mirror L1 (look) and R1 (crouch) in R&C1, so the pad
 * parse hook reads them for the strafe and clears them from every field
 * gameplay reads (held, pressed, released, raw and their copies) and from
 * the 30-entry ring of pressed edges at + 0x1E0 that pad_pressed_within
 * (func_L00_00267BA8) scans: the pack jumps test crouch through that
 * history (pad_pressed_within(0xA, 2)), which is how R2 still gave long
 * jumps in the first build. This frees the triggers without touching the
 * hero's own button tests, with one exception: the thruster pack's hover
 * (state 0x81) is R&C1's own primitive strafe, and its movement block
 * (NTSC decomp 00233de0.c case 0x81) reads L2/R2 itself (held & 3: turn
 * toward the camera yaw instead of the stick). The owner wants it kept as
 * it is, so while the hero hovers the triggers are left in the pad and the
 * game sees them as in retail.
 *
 * The camera. R&C2's strafe camera (its hero camera pass at 0x425F70, when
 * strafing with a trigger held) is: no auto-turn (camera state + 0x10 = 0),
 * start a slide (+ 0x136 = 1), lateral spring 0.04 / 0.2, and the three
 * stiffness timers cleared. R&C1's per-frame follow camera overrides
 * (func_L00_002E74B0; NTSC follow_camera_apply_overrides) carry the very
 * same block, byte for byte in intent (FUN_L00_002e8568(0),
 * follow_camera_start_slide(), FUN_L00_002e8590(0.04f, 0.2f), h12/h14/h16
 * = 0, return), gated on `hero state == 0x81 && held & 3`: the hover's
 * strafe camera. tools/overlay_patch.py (feature strafe_camera) drops the
 * state test and makes the button test read ee_strafe_held, so the block
 * runs whenever a trigger is held: in the hover as before, and now while
 * strafing on foot. With the auto-turn off the camera holds its yaw and
 * only the right stick turns it; the approach pass (func_L00_002E72E8)
 * sets the auto-turn back on at the end of every frame, so releasing the
 * trigger restores the follow camera at once.
 *
 * Jumps and flips. R&C2's jump-press handler (0x2C2420; R&C1's is
 * func_L00_00229348, the same function: X pressed within a window, high
 * jump with crouch held, else a plain jump unless the caller only wants
 * the crouch jumps) adds one rule: strafing with the stick past 0.9, when
 * the stick's direction class is not "forward", it enters the HIGH_JUMP
 * state (0xB) instead of a plain jump. R&C1's state 0xB is already the
 * flip family: its entry classifies the stick against the facing
 * (func_L00_00222580: 2 forward, 3 back, 0/1 the sides; R&C2's 0x2BEA58)
 * and plays animation 0x1C + class (0x1C/0x1D the side flips, 0x1F the
 * back flip; both games), and the flip travels along the move heading at
 * the take-off speed. So the flips are R&C1's own crouch flips, reached
 * from the strafe. In the air, R&C2's jump block (0x2BD34C) turns toward
 * the heading, which is the camera yaw while strafing, and sets the
 * planar velocity along the stick heading (0x2BD4C4), and its air control
 * (0x2B7360, at 0x2B7920) turns toward the camera yaw every frame while
 * strafing; R&C1's jump block (NTSC 00233de0.c, the state 7 case) and air
 * control (FUN_L01_00234b40, the plain-jump branch) have the same calls,
 * hooked here the same way as the run. R&C2 also makes a jump from the
 * run a plain jump (7) rather than a run-jump (9) while strafing
 * (0x2C4A08); R&C1 picks the run-jump only when the heading is within 60
 * degrees of the facing and the speed is high, so it still happens when
 * strafing straight ahead at speed (not changed: the call is not found by
 * pattern in the level programs).
 *
 * Hero fields are core data (0x13F450): state + 0x2084, facing yaw +
 * 0x98, move heading + 0x180, stick magnitude + 0x229C. The per-level
 * call sites and the helpers come from tools/gen_hero.py. */
#include "modseg.h"

DECLARE_LVL(func_00217F68);            /* pad begin-frame; its parse call is hooked */
DECLARE_LVL(func_002181F0);            /* pad parse(pad, report, length): derives held/pressed/released */
DECLARE_LVL(func_L00_00267BA8);        /* pad_pressed_within(button, window, out): a pure query of the press history */
DECLARE_LVL(func_L00_00222580);        /* stick_direction_vs_facing(): 2 forward, 3 back, 0 and 1 the sides */
extern const u32 ee_run_turn_a[19], ee_run_turn_b[19], ee_run_pivot[19], ee_run_planar_a[19], ee_run_planar_b[19];
extern const u32 ee_turn_to[19], ee_pivot_turn[19], ee_set_planar_vel[19], ee_hero_set_state[19];
extern const u32 ee_jump_turn_site[19], ee_jump_planar_site[19], ee_air_turn_site[19], ee_jump_press_fn[19];
extern const u32 ee_jump_press_0[19], ee_jump_press_1[19], ee_jump_press_2[19], ee_jump_press_3[19];
DECLARE_LVL_DATA(camera_yaw);          /* camera record + 0x158, per level */

#define PAD0            0x0013CA40
#define HERO            0x0013F450
#define hero_state      (*(volatile s32 *)(HERO + 0x2084))
#define hero_move_yaw   (*(volatile f32 *)(HERO + 0x180))
#define hero_stick      (*(volatile f32 *)(HERO + 0x229C))   /* left stick magnitude, 0..1 */
#define CAMERA_YAW(L)   (*(volatile f32 *)LVL_DATA(L, camera_yaw, u32))
#define PAD_HISTORY     0x1E0         /* 30 words of pressed edges, a ring */
#define PAD_HISTORY_LEN 30
#define FRAME_SCALE_SQ  (*(volatile f32 *)0x0015EE64)
#define FRAME_TIME      (*(volatile f32 *)0x0015EE6C)
#define STATE_RUN       2
#define STATE_HOVER     0x81          /* thruster pack hover: reads L2/R2 itself */
#define STATE_JUMP      7
#define STATE_HIGH_JUMP 0xB           /* the flips: animation 0x1C + stick class */
#define FLIP_STICK      0.9f          /* R&C2: a strafe jump flips from this stick magnitude (0x2C24B8) */
#define CLASS_FORWARD   2
#define PAD_CROSS       0x40u
#define PAD_STRAFE      0x3u          /* L2 | R2 in the game's button layout */
#define EE_SITE_PAD_PARSE 0x1C4       /* func_00217F68 + 0x1C4: jal func_002181F0 */

typedef void (*PadParse)(void *pad, u8 *report, s32 length);
typedef void (*TurnTo)(s32 mode, f32 accel, f32 max_speed, f32 rate);
typedef void (*Pivot)(void);
typedef void (*SetPlanarVel)(f32 yaw);
typedef s32 (*JumpPress)(s32 window, s32 crouch_jumps_only);
typedef s32 (*PadPressedWithin)(u32 button, s32 window, s32 *out);
typedef s32 (*StickClass)(void);
typedef s32 (*SetState)(s32 state, s32 with_animation);

u32 ee_strafe_held;           /* L2 or R2 held this frame (pad 0); the camera patch reads it */
static s32 strafe_active;     /* this frame's RUN turn was the strafe one */
static f32 strafe_heading;    /* the stick heading the velocity follows */

/* After the game's pad parse (pad.c: func_002181F0(pad, report, length)):
 * remember L2/R2 and hide them from the game, except while hovering. */
void ee_pad_parse_c(void *pad, u8 *report, s32 length, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_00217F68, EE_SITE_PAD_PARSE);
    volatile u32 *p = (volatile u32 *)pad;
    static const u16 fields[] = { 0x1A0, 0x1A4, 0x1A8, 0x1B0, 0x1B4, 0x1B8, 0x1C0, 0x1C4, 0x1C8, 0x348 };
    s32 i;

    LVL(L, func_002181F0, PadParse)(pad, report, length);
    if ((u32)pad != PAD0) {
        return;
    }
    ee_strafe_held = p[0x1A0 / 4] & PAD_STRAFE;
    if (hero_state == STATE_HOVER) {
        return;
    }
    for (i = 0; i < 10; i++) {
        p[fields[i] / 4] &= ~PAD_STRAFE;
    }
    for (i = 0; i < PAD_HISTORY_LEN; i++) {
        p[PAD_HISTORY / 4 + i] &= ~PAD_STRAFE;
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

/* R&C2's strafe facing: heading = camera yaw, turn toward it; the stick
 * heading is kept for the planar velocity. */
static void strafe_turn_with(s32 L, s32 mode, f32 accel, f32 max_speed, f32 rate)
{
    strafe_heading = hero_move_yaw;
    hero_move_yaw = CAMERA_YAW(L);
    ((TurnTo)ee_turn_to[L])(mode, accel, max_speed, rate);
    strafe_active = 1;
}

/* The run's strafe turn, with R&C2's rates (0.037 / 0.25 / 0.20 per frame,
 * in R&C1's scaled units). */
static void strafe_turn(s32 L)
{
    strafe_turn_with(L, 0, FRAME_SCALE_SQ * 0.037f, FRAME_SCALE_SQ * 0.25f, FRAME_TIME * 12.2f);
}

void ee_strafe_turn_c(s32 mode, u32 ra, f32 accel, f32 max_speed, f32 rate)
{
    s32 L = level_of_two(ra, ee_run_turn_a, ee_run_turn_b);
    if (ee_strafe_held && hero_state == STATE_RUN) {
        strafe_turn(L);
    } else {
        strafe_active = 0;
        ((TurnTo)ee_turn_to[L])(mode, accel, max_speed, rate);
    }
}

void ee_strafe_pivot_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, ee_run_pivot, 0);
    if (ee_strafe_held && hero_state == STATE_RUN) {
        strafe_turn(L);
    } else {
        strafe_active = 0;
        ((Pivot)ee_pivot_turn[L])();
    }
}

static void strafe_planar(s32 L, f32 yaw)
{
    if (strafe_active) {
        hero_move_yaw = strafe_heading;
        ((SetPlanarVel)ee_set_planar_vel[L])(strafe_heading);
    } else {
        ((SetPlanarVel)ee_set_planar_vel[L])(yaw);
    }
}

void ee_strafe_planar_c(f32 yaw, u32 ra)
{
    strafe_planar(level_of_two(ra, ee_run_planar_a, ee_run_planar_b), yaw);
}

/* The jump block's turn_to and set_planar_vel (the first frames of a
 * jump): as the run, with the jump's own turn rates, in any state of the
 * jump family (the flips never reach these calls). */
void ee_jump_turn_c(s32 mode, u32 ra, f32 accel, f32 max_speed, f32 rate)
{
    s32 L = ee_level_of_call(ra, ee_jump_turn_site, 0);
    if (ee_strafe_held) {
        strafe_turn_with(L, mode, accel, max_speed, rate);
    } else {
        strafe_active = 0;
        ((TurnTo)ee_turn_to[L])(mode, accel, max_speed, rate);
    }
}

void ee_jump_planar_c(f32 yaw, u32 ra)
{
    strafe_planar(ee_level_of_call(ra, ee_jump_planar_site, 0), yaw);
}

/* The air control's turn_to for plain jumps: toward the camera yaw while
 * strafing (R&C2 0x2B7920), the heading put back for the air steering. */
void ee_air_turn_c(s32 mode, u32 ra, f32 accel, f32 max_speed, f32 rate)
{
    s32 L = ee_level_of_call(ra, ee_air_turn_site, 0);
    if (ee_strafe_held) {
        f32 keep = hero_move_yaw;
        hero_move_yaw = CAMERA_YAW(L);
        ((TurnTo)ee_turn_to[L])(mode, accel, max_speed, rate);
        hero_move_yaw = keep;
    } else {
        ((TurnTo)ee_turn_to[L])(mode, accel, max_speed, rate);
    }
}

/* The jump-press handler with R&C2's strafe rule (0x2C2498): after the
 * crouch high jump has had its chance, a strafe jump with the stick past
 * 0.9 in any class but forward is a flip (state 0xB); forward, or a
 * gentle stick, is the plain jump as before. The game's handler is called
 * with crouch_jumps_only so it never takes the plain jump first; the
 * press itself is re-read with pad_pressed_within, a pure query. */
s32 ee_jump_press_c(s32 window, s32 crouch_jumps_only, u32 ra)
{
    s32 L;
    for (L = 0; L < 19; L++) {
        if (ee_jump_press_0[L] + 8 == ra || ee_jump_press_1[L] + 8 == ra
            || ee_jump_press_2[L] + 8 == ra || ee_jump_press_3[L] + 8 == ra) {
            break;
        }
    }
    if (L == 19) {
        L = ee_level_of_call(ra, ee_jump_press_0, 0);
    }
    if (((JumpPress)ee_jump_press_fn[L])(window, 1)) {
        return 1;
    }
    if (!LVL(L, func_L00_00267BA8, PadPressedWithin)(PAD_CROSS, window, 0)) {
        return 0;
    }
    if (ee_strafe_held && hero_stick > FLIP_STICK) {
        if (LVL(L, func_L00_00222580, StickClass)() != CLASS_FORWARD) {
            ((SetState)ee_hero_set_state[L])(STATE_HIGH_JUMP, 1);
            return 1;
        }
    }
    if (crouch_jumps_only) {
        return 0;
    }
    ((SetState)ee_hero_set_state[L])(STATE_JUMP, 1);
    return 1;
}
