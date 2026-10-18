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
 *
 * The flips, R&C2's way (owner's second test: the strafe flips felt stiff
 * and sometimes came out as the twisting flip). Both games keep the same
 * four flip sequences (0x1C-0x1F, 33 frames each; R&C2's are the same
 * data packed) and the same flip air control, except for one path R&C2
 * adds for a strafe flip (0x2B760C): the horizontal speed (hero + 0x458)
 * starts at frame_time * 5 instead of 0 and follows the stick, approached
 * at frame_time^2 * 37 toward max(stick * frame_time * 5, frame_time * 2),
 * the ground speed is kept at the speed actually travelled (R&C2's + 0x174
 * = R&C1's motion + 0x164, the field both games' flip entries take the
 * take-off speed from), and the "drift back toward the take-off yaw" term
 * is dropped (take-off speed + 0x454 = 0). A first port copied motion +
 * 0x160 instead, which keeps easing toward the run target through the
 * flip (0.15 to 0.22 per frame by the landing, measured, against the
 * flip's 0.10): a press in the last frames of the flip, which both games
 * turn into a plain jump on landing (Cross within 8 frames, NTSC
 * 00242930.c's jump-family landing; R&C2 0x2C6A18, measured in Oozla: a
 * plain jump that can double jump), left at twice R&C2's speed (owner's
 * report: a boosted side jump instead of the next flip). R&C1's flip only
 * gains speed after the apex, so it stands
 * still in the air. R&C2 also classifies the flip from the stick relative
 * to the camera (its raw stick classes), where R&C1's entry uses the
 * facing, which lags the camera while strafing: the edition reclassifies
 * against the camera yaw after the entry and sets the flip animation
 * itself (0x1C left, 0x1D right, 0x1F back; the mirror option swaps the
 * sides as the game does).
 *
 * The run twist. R&C2 has no strafe run cycles: its run block calls a
 * helper (0x2B8078) that, while strafing with the stick past 0.15 and not
 * backpedalling, writes the angle between the stick heading and the
 * facing into the hero's per-joint ease table (hips + angle at + 0x68,
 * torso - angle at + 0x118, spring rates 0.037/0.27 and 0.025/0.27 at
 * + 0xA4/+ 0xA8 and + 0x154/+ 0x158) instead of the run lean. R&C1's lean
 * helper (func_L00_00215A90) writes the same table (NTSC D_L00_0017A680,
 * records of 0xB0 bytes: lean at + 0x60 and + 0x118, rates at record +
 * 0x44/+ 0x48) from the turn rate, so the twist is the same table written
 * the R&C2 way; the helper's six calls in the movement switch are hooked.
 *
 * The backpedal. R&C2 plays a 30-frame backward step cycle (its slot 0x14)
 * when the stick class is "back" past 0.15, at speed max(ground speed *
 * 25, 0.7) capped at 2.2, and goes back to the walk/run cycle with a
 * 7-frame blend. R&C1 has no such cycle (its slot 0x14 is the 14-frame
 * stop). R&C2's is carried over: tools/rac2_anim.py reads it from the
 * owner's R&C2 disc (the level core, as R&C1 keeps Ratchet's sequences)
 * and expands its packed frames into R&C1's frame layout exactly as the
 * R&C2 engine does at animation time (checked against R&C1's own run,
 * walk and jump cycles, which both discs carry: identical to the packed
 * format's 2-bit quantization), emitting build/modseg/gen_anim.c: the
 * frames and a sequence header with their addresses (ee_backpedal_seq).
 * A loaded sequence is a 0x1C-byte header and a table of frame pointers
 * (update_moby_animation_state reads seqs[seq]->frames[frame]), so the
 * hook installs that header in a free slot (140) of the hero's class and
 * the run animation updater's hook plays it. The class's table has room
 * for 162 slots only (its mesh packet table starts at + 0x2D0, header
 * word 0); a first build used slot 200, wrote into the packet table and
 * crashed the renderer.
 *
 * Hero fields are core data (0x13F450): state + 0x2084, facing yaw +
 * 0x98, move heading + 0x180, stick magnitude + 0x229C. The per-level
 * call sites and the helpers come from tools/gen_hero.py. */
#include "modseg.h"

DECLARE_LVL(func_00217F68);            /* pad begin-frame; its parse call is hooked */
DECLARE_LVL(func_002181F0);            /* pad parse(pad, report, length): derives held/pressed/released */
DECLARE_LVL(func_L00_00267BA8);        /* pad_pressed_within(button, window, out): a pure query of the press history */
DECLARE_LVL(func_L00_00222580);        /* stick_direction_vs_facing(): 2 forward, 3 back, 0 and 1 the sides */
DECLARE_LVL(func_L00_00232C10);        /* set_hero_animation(id, start_frame, blend_frames) */
DECLARE_LVL(func_001FA790);            /* fast_subtract_rotations(a, b): the level's copy */
DECLARE_LVL(func_001F9850);            /* scale_game_frames(n): the level's copy */
DECLARE_LVL_DATA(ease_table);          /* the hero's per-joint ease table, per level */
extern const u32 ee_twist_0[19], ee_twist_1[19], ee_twist_2[19], ee_twist_3[19], ee_twist_4[19], ee_twist_5[19], ee_twist_fn[19];
extern const u32 ee_runanim_site[19], ee_run_anim_fn[19], ee_flip_appr_0[19], ee_flip_appr_1[19], ee_approach_fn[19];
extern const u32 ee_stop_site[19], ee_stop_site_low[19];
#define hero_target_speed (*(volatile f32 *)(HERO + 0x190))
#define hero_speed_164  (*(volatile f32 *)(HERO + 0x164))
#define hero_vel_x      (*(volatile f32 *)(HERO + 0xE0))
#define hero_vel_y      (*(volatile f32 *)(HERO + 0xE4))
#define hero_slide_x    (*(volatile f32 *)(HERO + 0x150))   /* motion.unk150: the momentum STOP and IDLE add to the velocity each frame, decaying */
#define hero_slide_y    (*(volatile f32 *)(HERO + 0x154))
#define hero_slide_z    (*(volatile f32 *)(HERO + 0x158))
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
#define hero_stick_x    (*(volatile f32 *)(HERO + 0x1D20))   /* left stick vector, every frame (stick_target's input) */
#define hero_stick_y    (*(volatile f32 *)(HERO + 0x1D24))
#define hero_facing     (*(volatile f32 *)(HERO + 0x98))
#define hero_speed_xy   (*(volatile f32 *)(HERO + 0x160))
#define hero_ground_speed (*(volatile f32 *)(HERO + 0x194))
#define hero_state_step (*(volatile s32 *)(HERO + 0x2088))
#define hero_moby       (*(volatile u8 **)(HERO + 0x2080))
#define hero_anim_speed (*(volatile f32 *)(HERO + 0xA90))
#define hero_flip_class (*(volatile s32 *)(HERO + 0x450))
#define hero_flip_speed0 (*(volatile f32 *)(HERO + 0x454))   /* take-off speed: the drift term */
#define hero_flip_speed (*(volatile f32 *)(HERO + 0x458))    /* horizontal speed along the take-off heading */
#define FRAME_TIME_SQ   (*(volatile f32 *)0x0015EE70)
#define OPT_MIRROR_ANIM (*(volatile u8 *)0x0015EEB5)
#define EASE(L)         ((volatile f32 *)LVL_DATA(L, ease_table, u32))
#define BACKPEDAL_SLOT  140           /* a free slot: the class keeps 162 (its packet table follows at + 0x2D0), 134 used */
#define CAMERA_YAW(L)   (*(volatile f32 *)LVL_DATA(L, camera_yaw, u32))
#define PAD_HISTORY     0x1E0         /* 30 words of pressed edges, a ring */
#define PAD_HISTORY_LEN 30
#define FRAME_SCALE_SQ  (*(volatile f32 *)0x0015EE64)
#define FRAME_TIME      (*(volatile f32 *)0x0015EE6C)
#define STATE_RUN       2
#define STATE_HOVER     0x81          /* thruster pack hover: reads L2/R2 itself */
#define STATE_JUMP      7
#define STATE_STOP      3
#define ANIM_STOP_SHORT 0x14          /* the stop from frame 7: the game's own low-speed stop */
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
typedef f32 (*Rot2)(f32 a, f32 b);
typedef s32 (*ScaleFrames)(s32 frames);
typedef void (*SetAnim)(s32 id, s32 start_frame, f32 blend);
typedef void (*Void)(void);
typedef f32 (*Approach)(f32 target, f32 step, f32 *var);
/* Executable helpers above 0x15F000 are not resident: only the core below
 * it keeps its addresses, and every level carries its own copy of these
 * at its own address (the catalogue's places). A build that called
 * them at the boot executable's addresses jumped into level data and died
 * ("jump to unmapped recLUT page") the first time a strafe ran. */
#define sub_rot(L, a, b)      (LVL(L, func_001FA790, Rot2)(a, b))        /* fast_subtract_rotations */
#define scale_frames(L, n)    (LVL(L, func_001F9850, ScaleFrames)(n))    /* frames at 60 Hz -> this build's frames (NTSC FUN_001f96f8) */

u32 ee_strafe_held;           /* L2 or R2 held this frame (pad 0); the camera patch reads it */
/* Debugging from outside (PCSX2's PINE): a fake pad 0 report while [0] is
 * set ([1] buttons, game layout, active high; [2] left stick x | y << 8;
 * [3] right stick), and a trace of the hook running (its id, 0 between). */
volatile u32 ee_debug_pad[4];
volatile u32 ee_debug_trace;
volatile u32 ee_debug_last;
#define TRACE(id)  (ee_debug_trace = ee_debug_last = (id))
#define TRACE_END() (ee_debug_trace = 0)
static s32 strafe_active;     /* this frame's RUN turn was the strafe one */
static f32 strafe_heading;    /* the stick heading the velocity follows */
static s32 strafe_flip;       /* the current flip (state 0xB) is a strafe flip with the stick pushed */
static s32 strafe_grace;      /* frames since a trigger was released, counting down from STRAFE_GRACE */
#define STRAFE_GRACE 6        /* letting go of the trigger and the stick together still ends as a strafe */
extern const u32 ee_backpedal_seq[];   /* R&C2's backward step in R&C1's layout (build/modseg/gen_anim.c) */

/* After the game's pad parse (pad.c: func_002181F0(pad, report, length)):
 * remember L2/R2 and hide them from the game, except while hovering. */
void ee_pad_parse_c(void *pad, u8 *report, s32 length, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_00217F68, EE_SITE_PAD_PARSE);
    volatile u32 *p = (volatile u32 *)pad;
    static const u16 fields[] = { 0x1A0, 0x1A4, 0x1A8, 0x1B0, 0x1B4, 0x1B8, 0x1C0, 0x1C4, 0x1C8, 0x348 };
    s32 i;

    TRACE(11);
    if ((u32)pad == PAD0 && ee_debug_pad[0] && length >= 6) {
        u32 b = ~ee_debug_pad[1];
        report[0] = (u8)(b >> 8);
        report[1] = (u8)b;
        report[2] = (u8)ee_debug_pad[3];
        report[3] = (u8)(ee_debug_pad[3] >> 8);
        report[4] = (u8)ee_debug_pad[2];
        report[5] = (u8)(ee_debug_pad[2] >> 8);
    }
    /* The patched call site is in each level's copy of the pad begin-frame
     * (the catalogue's places for these two exe functions are the levels'
     * copies); a debug image that also patches the executable's own copy
     * (ra 0x218134, before any level is loaded) must call the executable's
     * parser. */
    if (ra == 0x00218134) {
        ((PadParse)0x002181F0)(pad, report, length);
    } else {
        LVL(L, func_002181F0, PadParse)(pad, report, length);
    }
    if ((u32)pad != PAD0) {
        TRACE_END();
        return;
    }
    ee_strafe_held = p[0x1A0 / 4] & PAD_STRAFE;
    if (ee_strafe_held) {
        strafe_grace = STRAFE_GRACE;
    } else if (strafe_grace) {
        strafe_grace--;
    }
    if (hero_state != STATE_HIGH_JUMP) {
        strafe_flip = 0;
    }
    if (hero_state == STATE_HOVER) {
        TRACE_END();
        return;
    }
    for (i = 0; i < 10; i++) {
        p[fields[i] / 4] &= ~PAD_STRAFE;
    }
    for (i = 0; i < PAD_HISTORY_LEN; i++) {
        p[PAD_HISTORY / 4 + i] &= ~PAD_STRAFE;
    }
    TRACE_END();
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
    s32 trace_ = (TRACE(3), 0);
    s32 L = level_of_two(ra, ee_run_turn_a, ee_run_turn_b);
    if (ee_strafe_held && hero_state == STATE_RUN) {
        strafe_turn(L);
    } else {
        strafe_active = 0;
        ((TurnTo)ee_turn_to[L])(mode, accel, max_speed, rate);
    }
    TRACE_END();
}

void ee_strafe_pivot_c(u32 ra)
{
    s32 trace_ = (TRACE(4), 0);
    s32 L = ee_level_of_call(ra, ee_run_pivot, 0);
    if (ee_strafe_held && hero_state == STATE_RUN) {
        strafe_turn(L);
    } else {
        strafe_active = 0;
        ((Pivot)ee_pivot_turn[L])();
    }
    TRACE_END();
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
    s32 trace_ = (TRACE(5), 0);
    strafe_planar(level_of_two(ra, ee_run_planar_a, ee_run_planar_b), yaw);
    TRACE_END();
}

/* The jump block's turn_to and set_planar_vel (the first frames of a
 * jump): as the run, with the jump's own turn rates, in any state of the
 * jump family (the flips never reach these calls). */
void ee_jump_turn_c(s32 mode, u32 ra, f32 accel, f32 max_speed, f32 rate)
{
    s32 trace_ = (TRACE(9), 0);
    s32 L = ee_level_of_call(ra, ee_jump_turn_site, 0);
    if (ee_strafe_held) {
        strafe_turn_with(L, mode, accel, max_speed, rate);
    } else {
        strafe_active = 0;
        ((TurnTo)ee_turn_to[L])(mode, accel, max_speed, rate);
    }
    TRACE_END();
}

void ee_jump_planar_c(f32 yaw, u32 ra)
{
    s32 trace_ = (TRACE(10), 0);
    strafe_planar(ee_level_of_call(ra, ee_jump_planar_site, 0), yaw);
    TRACE_END();
}

/* The air control's turn_to for plain jumps: toward the camera yaw while
 * strafing (R&C2 0x2B7920), the heading put back for the air steering. */
void ee_air_turn_c(s32 mode, u32 ra, f32 accel, f32 max_speed, f32 rate)
{
    s32 trace_ = (TRACE(8), 0);
    s32 L = ee_level_of_call(ra, ee_air_turn_site, 0);
    if (ee_strafe_held) {
        f32 keep = hero_move_yaw;
        hero_move_yaw = CAMERA_YAW(L);
        ((TurnTo)ee_turn_to[L])(mode, accel, max_speed, rate);
        hero_move_yaw = keep;
    } else {
        ((TurnTo)ee_turn_to[L])(mode, accel, max_speed, rate);
    }
    TRACE_END();
}

/* The stick heading this frame: the saved one while the run's strafe turn
 * holds the camera yaw in the move heading, else the move heading. */
static f32 stick_heading(void)
{
    return (strafe_active && hero_state == STATE_RUN) ? strafe_heading : hero_move_yaw;
}

/* R&C2's strafe stick classes: 2 forward, 3 back, 0 left, 1 right, 45
 * degrees either side (R&C1's stick_direction_vs_facing has the same
 * numbering against the facing). While strafing, R&C2's stick routine
 * (Oozla 0x2B4534-0x2B45D4) classifies fast_arc_tan(x, y) of the raw stick
 * vector (hero + 0x1FB0; R&C1's + 0x1D20, refreshed every frame in every
 * state) against pi/2 (back), 0 (right) and pi (left) and stores the class
 * at hero + 0x1457, which its flip classifier returns while strafing
 * (0x2BEA78); the same pass sets its backpedal flag (+ 0x1456) for class 3
 * past 0.15. The same quadrants here, by comparing the components. A
 * first build classified the move heading instead, which R&C1's flips
 * freeze at their take-off heading, so it held only in RUN. */
static s32 strafe_class(void)
{
    f32 fwd = -hero_stick_y;
    f32 left = -hero_stick_x;
    f32 af = fwd < 0.0f ? -fwd : fwd;
    f32 al = left < 0.0f ? -left : left;
    if (al > af) {
        return left > 0.0f ? 0 : 1;
    }
    return fwd < 0.0f ? 3 : CLASS_FORWARD;
}

static s32 backpedalling(void)
{
    return ee_strafe_held && hero_stick > 0.15f && strafe_class() == 3;
}

/* R&C2's strafe flip rule (0x2C2498): strafing with the stick past 0.9 in
 * any class but forward. The class, or -1 for no flip. */
static s32 strafe_flip_class(void)
{
    s32 cls;
    if (!ee_strafe_held || hero_stick <= FLIP_STICK) {
        return -1;
    }
    cls = strafe_class();
    return cls == CLASS_FORWARD ? -1 : cls;
}

/* Enter the flip (state 0xB) as R&C2's strafe flip: classified against
 * the camera, moving along the stick from the start. */
static s32 enter_strafe_flip(s32 L, s32 cls)
{
    s32 id = 0x1C + cls;
    s32 r = ((SetState)ee_hero_set_state[L])(STATE_HIGH_JUMP, 1);
    hero_flip_class = cls;
    if (OPT_MIRROR_ANIM && (id == 0x1C || id == 0x1D)) {
        id ^= 1;
    }
    LVL(L, func_L00_00232C10, SetAnim)(id, 0, (f32)scale_frames(L, 5));
    strafe_flip = 1;
    hero_flip_speed = FRAME_TIME * 5.0f;
    hero_flip_speed0 = 0.0f;
    return r;
}

/* The jump-press handler with R&C2's strafe rule (0x2C2498): after the
 * crouch high jump has had its chance, a strafe jump with the stick past
 * 0.9 in any class but forward is a flip (state 0xB); forward, or a
 * gentle stick, is the plain jump as before. The game's handler is called
 * with crouch_jumps_only so it never takes the plain jump first; the
 * press itself is re-read with pad_pressed_within, a pure query. */
s32 ee_jump_press_c(s32 window, s32 crouch_jumps_only, u32 ra)
{
    s32 trace_ = (TRACE(6), 0);
    s32 L, cls;
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
        TRACE_END();
        return 1;
    }
    if (!LVL(L, func_L00_00267BA8, PadPressedWithin)(PAD_CROSS, window, 0)) {
        TRACE_END();
        return 0;
    }
    cls = strafe_flip_class();
    if (cls >= 0) {
        enter_strafe_flip(L, cls);
        TRACE_END();
        return 1;
    }
    if (crouch_jumps_only) {
        TRACE_END();
        return 0;
    }
    ((SetState)ee_hero_set_state[L])(STATE_JUMP, 1);
    TRACE_END();
    return 1;
}

/* The flip's speed approach (two calls in the air control, states 0xB and
 * 0xC): R&C2's strafe path (0x2B760C) while a strafe flip is in the air,
 * else the game's. */
void ee_flip_approach_c(f32 target, f32 step, f32 *var, u32 ra)
{
    s32 trace_ = (TRACE(7), 0);
    s32 L = level_of_two(ra, ee_flip_appr_0, ee_flip_appr_1);
    if (strafe_flip && hero_state == STATE_HIGH_JUMP) {
        f32 t = hero_stick * FRAME_TIME * 5.0f;
        if (t < FRAME_TIME * 2.0f) {
            t = FRAME_TIME * 2.0f;
        }
        ((Approach)ee_approach_fn[L])(t, FRAME_TIME_SQ * 37.0f, var);
        hero_ground_speed = hero_speed_164;
    } else {
        ((Approach)ee_approach_fn[L])(target, step, var);
    }
    TRACE_END();
}

/* The lean helper's six calls: R&C2's strafe twist (0x2B80B4) instead of
 * the lean while strafing on the stick, not flipping, not backpedalling. */
void ee_twist_c(u32 ra)
{
    s32 trace_ = (TRACE(1), 0);
    static const u32 *const sites[6] = { ee_twist_0, ee_twist_1, ee_twist_2, ee_twist_3, ee_twist_4, ee_twist_5 };
    s32 L, k;
    for (L = 0; L < 19; L++) {
        for (k = 0; k < 6; k++) {
            if (sites[k][L] + 8 == ra) {
                goto found;
            }
        }
    }
    L = ee_level_of_call(ra, ee_twist_0, 0);
found:
    if (ee_strafe_held && hero_stick > 0.15f && hero_state != STATE_HIGH_JUMP && !backpedalling()) {
        volatile f32 *t = EASE(L);
        f32 a = sub_rot(L, stick_heading(), hero_facing);
        t[0x158 / 4] = 0.27f;
        t[0xA4 / 4] = 0.037f;
        t[0x154 / 4] = 0.025f;
        t[0xA8 / 4] = 0.27f;
        t[0x68 / 4] = a;
        t[0x118 / 4] = -a;
        TRACE_END();
        return;
    }
    ((Void)ee_twist_fn[L])();
    TRACE_END();
}

/* The backward step, R&C2's cycle in R&C1's layout, in slot 140 of the
 * hero's class: installed once per class (a new level loads a new class). */
static void install_backpedal(void)
{
    u8 *cls = *(u8 **)(hero_moby + 0x24);
    volatile u32 *seqs = (volatile u32 *)(cls + 0x48);
    if (seqs[BACKPEDAL_SLOT] == (u32)ee_backpedal_seq) {
        return;
    }
    if (0x48 + 4 * BACKPEDAL_SLOT >= *(u32 *)cls || seqs[BACKPEDAL_SLOT] != 0) {
        return;     /* no room before the packet table, or the slot is in use: keep the run cycle */
    }
    seqs[BACKPEDAL_SLOT] = (u32)ee_backpedal_seq;
}

/* The run animation updater's call (state RUN): R&C2's backpedal
 * (0x2C2560) while strafing back, else the game's walk/run cycles. */
void ee_run_anim_c(u32 ra)
{
    s32 trace_ = (TRACE(2), 0);
    s32 L = ee_level_of_call(ra, ee_runanim_site, 0);
    u8 *moby = hero_moby;
    if (backpedalling()) {
        f32 speed;
        install_backpedal();
        if (*(volatile u32 *)(*(u8 **)(moby + 0x24) + 0x48 + 4 * BACKPEDAL_SLOT) != (u32)ee_backpedal_seq) {
            ((Void)ee_run_anim_fn[L])();      /* not installed: the game's cycles */
            return;
        }
        if (moby[0x53] != BACKPEDAL_SLOT) {
            LVL(L, func_L00_00232C10, SetAnim)(BACKPEDAL_SLOT, 0, 11.0f);
        }
        speed = hero_ground_speed * 25.0f;
        if (speed < 0.7f) {
            speed = 0.7f;
        }
        if (speed > 2.2f) {
            speed = 2.2f;
        }
        hero_anim_speed = speed;
        TRACE_END();
        return;
    }
    if (moby[0x53] == BACKPEDAL_SLOT) {
        LVL(L, func_L00_00232C10, SetAnim)(hero_state_step + 3, 0, 7.0f);
    }
    ((Void)ee_run_anim_fn[L])();
    TRACE_END();
}

/* The run update's two stops (owner's request: no foot slide while
 * strafing). Letting the stick go at speed enters STOP: with its skid
 * animation at high speed (the entry picks 6 or 5 from frame 4), or without
 * it and animation 0x14 from frame 7 otherwise; either way STOP decelerates
 * the hero until the ground speed reaches 0 and only then hands to IDLE
 * (NTSC 00242930.c case 3); the slide itself is the momentum vector at
 * hero + 0x150, which set_state copies from the last displacement on entry
 * and which the shared idle/stop movement block adds to the velocity
 * every frame while shrinking it by the state's deceleration
 * (FUN_L00_00216710). While a trigger is held the hook enters STOP without
 * the entry animation, zeroes that momentum, the speeds and the planar
 * velocity so the next STOP update goes to IDLE at once, and shows the
 * short stop pose over the game's 9-frame blend: Ratchet stops where he
 * is, as in Going Commando's strafe. (A first build zeroed the speeds
 * only; the momentum vector kept sliding him.) Letting go of the trigger
 * and the stick in the same motion counts too: the trigger is seen
 * released a frame or two before the game decides the stop, so the hook
 * keeps a six-frame grace after the release (measured: with the trigger
 * held the stop was already dead; released together it skidded). Outside the strafe both stops are the game's.
 * The high-speed call is a shared tail other paths branch into with other
 * states; those pass through. Among them is the jump family's landing jump
 * (NTSC 00242930.c: Cross within 8 frames of touching down gives JUMP or
 * RUN_JUMP; level 0 0x22FA70), which stays the game's plain jump while
 * strafing too, as in R&C2 (0x2C6A04, measured: a plain jump that can
 * double jump). A build that made it a strafe flip (7a8f766) was reverted
 * at the owner's word: Going Commando does not. */
s32 ee_stop_c(s32 state, s32 with_animation, u32 ra)
{
    s32 trace_ = (TRACE(12), 0);
    s32 L = level_of_two(ra, ee_stop_site, ee_stop_site_low);
    s32 r;
    if (state == STATE_STOP && (ee_strafe_held || strafe_grace)) {
        r = ((SetState)ee_hero_set_state[L])(STATE_STOP, 0);
        if (r) {
            hero_ground_speed = 0.0f;
            hero_target_speed = 0.0f;
            hero_speed_xy = 0.0f;
            hero_speed_164 = 0.0f;
            hero_vel_x = 0.0f;
            hero_vel_y = 0.0f;
            hero_slide_x = 0.0f;
            hero_slide_y = 0.0f;
            hero_slide_z = 0.0f;
            LVL(L, func_L00_00232C10, SetAnim)(ANIM_STOP_SHORT, 7, (f32)scale_frames(L, 9));
        }
        TRACE_END();
        return r;
    }
    r = ((SetState)ee_hero_set_state[L])(state, with_animation);
    TRACE_END();
    return r;
}
