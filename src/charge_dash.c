/* x2: the Charge Boots dash, Going Commando's, on R&C1's double tap of R1.
 *
 * R&C2 (SCUS_972.68, Oozla's program) has the Charge Boots as hero state
 * 0x7B. Its input handler ends with the very block R&C1's ends with for the
 * Thruster-Pack (R&C1 NTSC decomp hero_set_state.c, end of
 * FUN_L01_00240ed8): when nothing changed state this frame, a press of R1
 * (held and pressed, pad + 0x1A0 / + 0x1A4 bit 8) within 30 frames of the
 * last one, in control mode 0, 1 or 12 or (2 or 4 on the ground), enters
 * the state. R&C1's block tests slot_item(3) == 3 (the Thruster-Pack on the
 * back) and enters the hover, 0x81; R&C2's tests the Charge Boots owned and
 * worn (0x1A7B2E, 0x1A739C == 0x36) and enters 0x7B (0x2C2300). R&C2 kept
 * the Thruster-Pack's whole kit for the boots: the hover spring at ground
 * height + 0.17, the looping sound in hero sound slot 4, the entry fields.
 *
 * In the edition both are worn at once, the boots in the foot slot
 * (D_00141760[1] == 36, src/charge_boots.c) and the pack on the back, and
 * the owner wants the boots to win: the input handler's slot_item(3) call
 * goes to ee_charge_trigger_hook, which, with the boots on, runs R&C2's
 * block itself and answers "no Thruster-Pack" (0), so the game's block is
 * skipped; without them it is the game's call.
 *
 * R&C1 has no state 0x7B (none of its switches has the case), so the dash
 * is driven from here. Its pieces, R&C2's (60 fps per-frame constants,
 * converted to R&C1's frame_time units as R&C1 writes its own):
 *
 *  entry (set_state case, 0x2BF200)  control mode 0x1C (R&C2's own mode for
 *        the dash: no other action reads it), fidget timer 0x68, unk2284 0,
 *        velocity = this frame's displacement, animation 0x8B blended
 *        immediately (-2). R&C2 also forces its Charge Boots item into the
 *        foot slot for the dash (it wears them only while charging); the
 *        edition's player wears them already.
 *  update (0x2C4BD8)  ends when R1 is let go after 61 frames, or on the
 *        surface flag hero + 0x348 (R&C1 + 0x308): STOP without its entry
 *        animation, the skid 6 from frame 4 over an 11-frame blend, and the
 *        STOP jump/crouch tests held off for 30 frames (R&C2 hero + 0x1E0,
 *        R&C1 + 0x1C4, which R&C1's Thruster-Pack exit sets too), since R1
 *        is still held. Against a wall (the collision's wall flag, and this
 *        frame's movement under 3/4 of the velocity) the wall bonk, R2's
 *        0x77 = R&C1's 0x7A (same animation 0x29, same 9 u/s rebound).
 *        Every frame two sprites at each ankle, blue 20 80 FF 40 and cyan
 *        00 FF FF 20: the boots' jets. R&C2 asks for the positions of
 *        Ratchet's joint lists 22 and 23, his class's ankle chains (both
 *        games' Ratchet carry the same skeleton and lists), and draws the
 *        sprites with a particle type R&C1 lacks (0x4A), riding on the
 *        hero; here they are R&C1's glow particles (class 112, in every
 *        level), moving on with most of Ratchet's motion so that each
 *        ankle trails a short streak.
 *  movement (0x2BC338)  the loop sound (R&C2's sound 0xD; R&C1's Ratchet
 *        has the Thruster-Pack's jet loop, 0x12, in the same slot); the
 *        stick target; target speed 0.1917 per frame (11.5 u/s), and for
 *        the first 60 frames 0.5 (30 u/s) with a tenth of the steering;
 *        steering = smoothed stick x (approach 0.17 per frame) * 0.02036 *
 *        stick magnitude, applied when the stick is past 0.2; speed_step
 *        1/36 up, 1/240 down; set_planar_vel along the facing; the hover
 *        spring (pos.z toward ground + 0.17: 0.04, 0.3, 0.125); then
 *        everything in a 0.57 sphere three frames ahead of the body centre
 *        takes a hit of 1 (flags 0x10000) pushed along 7 frames of travel;
 *        and the lean: whole-body roll and upper-body twist from the turn
 *        (clamp(turn * 27, 1.7)) in the hero's ease table.
 *
 * Sites: tools/gen_charge.py. The sequence: R&C2's 0x8B expanded by
 * tools/rac2_anim.py (build/modseg/gen_anim_charge.c), installed in a free
 * slot of Ratchet's class like the strafe's backpedal (src/strafe.c). */
#include "modseg.h"

DECLARE_LVL(func_L00_002ADBB0);   /* spawn_glow_particle(owner, pos, vel, lifetime, r, g, b, a, scale) */
DECLARE_LVL(func_L00_001F2BE8);   /* collect_mobies_in_sphere(radius, pos, mask, self, hit) */
DECLARE_LVL(func_L00_0025A8C0);   /* make_hit_descriptor(hit, attacker, flags, damage, push) */
DECLARE_LVL(func_0020DAF8);       /* build_moby_bone_transform(moby, joint list, matrix): the level's copy */
DECLARE_LVL(func_L00_002175D0);   /* hero_loop_sound(slot, moby, sound): plays once into a free hero sound slot */
DECLARE_LVL(func_L00_0025C918);   /* spring(&value, &velocity, target, k, damping, snap) */
DECLARE_LVL(func_L00_00211F80);   /* stick_target(scale, mode) */
DECLARE_LVL(func_L00_00212740);   /* speed_step(accel, decel): ground speed toward target speed */
DECLARE_LVL(func_L00_00212790);   /* set_planar_vel(yaw): velocity from ground speed (99999: the facing) */
DECLARE_LVL(func_L00_00232C10);   /* set_hero_animation(id, start_frame, blend_frames) */
DECLARE_LVL_DATA(ease_table);     /* the hero's per-joint ease table */
extern const u32 ee_charge_trigger[19], ee_charge_slot_item[19], ee_charge_scale[19];
extern const u32 ee_charge_set_state[19], ee_charge_move_site[19], ee_charge_move_fn[19];
extern const u32 ee_charge_seq[];   /* R&C2's sequence 0x8B in R&C1's layout (build/modseg/gen_anim_charge.c) */

#define HERO            0x0013F450
#define F32(o)          (*(volatile f32 *)(HERO + (o)))
#define S32(o)          (*(volatile s32 *)(HERO + (o)))
#define S16(o)          (*(volatile s16 *)(HERO + (o)))
#define U8(o)           (*(volatile u8 *)(HERO + (o)))
#define HERO_POS        0x80
#define HERO_YAW        0x98
#define HERO_CENTRE     0xD0          /* body centre: pos + 0.7 up */
#define HERO_VEL        0xE0
#define HERO_DISP       0x100         /* this frame's displacement */
#define HERO_TARGET     0x190         /* target_speed */
#define HERO_TIMER      0x198         /* state_timer */
#define HERO_HOLD_OFF   0x1C4         /* STOP's jump/crouch tests wait for 0 */
#define HERO_WALL_HIT   0x257
#define HERO_GROUND_Z   0x2D8
#define HERO_GROUNDED   0x300         /* grounded_frames */
#define HERO_SPRING     0x304         /* the hover spring's velocity (the Thruster-Pack's too) */
#define HERO_SURFACE    0x308         /* s16; R&C2 + 0x348 ends the dash */
#define HERO_ANIM_SPEED 0xA90
#define HERO_FIDGET     0xFF8         /* rand_timer.range */
#define HERO_STICK_X    0x1D20
#define HERO_MOBY       0x2080
#define HERO_STATE      0x2084
#define HERO_MODE       0x208C        /* control mode */
#define HERO_2284       0x2284
#define HERO_STICK      0x229C        /* stick magnitude, 0..1 */
#define HERO_22D8       0x22D8
#define PAD_HELD        (*(volatile u32 *)(0x0013CA40 + 0x1A0))
#define PAD_PRESSED     (*(volatile u32 *)(0x0013CA40 + 0x1A4))
#define PAD_R1          0x8u
#define OWNED_ITEM_1C   (*(volatile u8 *)(0x0013D5C8 + 0x1C))
#define WORN_BOOTS      (*(volatile s32 *)0x00141764)   /* D_00141760[1] */
#define ITEM_CHARGE_BOOTS 36
#define FRAME_TIME      (*(volatile f32 *)0x0015EE6C)
#define FRAME_TIME_SQ   (*(volatile f32 *)0x0015EE70)
#define FRAME_SCALE_SQ  (*(volatile f32 *)0x0015EE64)

#define STATE_LOOK      1
#define STATE_STOP      3
#define STATE_1D        0x1D          /* R&C2's block skips 1 and 0x1D (both look modes) */
#define STATE_1E        0x1E
#define STATE_WALL_BONK 0x7A
#define STATE_CHARGE    0x7B
#define STATE_THRUSTER  0x81
#define MODE_CHARGE     0x1C
#define ANIM_SKID       6
#define CHARGE_SLOT     141           /* a free slot of Ratchet's class (162 kept, 134 used, 140 the backpedal) */
#define SOUND_JET       0x12          /* the Thruster-Pack's loop, hero sound slot 4 */
#define ANKLE_RIGHT     22            /* Ratchet's joint lists: the right ankle chain */
#define ANKLE_LEFT      23
#define PI              3.14159265f

typedef s32 (*SlotItem)(s32 slot);
typedef s32 (*ScaleFrames)(s32 frames);
typedef s32 (*SetState)(s32 state, s32 with_animation);
typedef void (*Void)(void);
typedef void (*SetAnim)(s32 id, s32 start_frame, f32 blend);
typedef void (*StickTarget)(f32 scale, s32 mode);
typedef void (*SpeedStep)(f32 accel, f32 decel);
typedef void (*SetPlanarVel)(f32 yaw);
typedef void (*LoopSound)(s32 slot, void *moby, s32 sound);
typedef void (*Spring)(volatile f32 *value, volatile f32 *velocity, f32 target, f32 k, f32 damping, f32 snap);
typedef void (*HitDescriptor)(void *hit, void *attacker, s32 flags, f32 damage, void *push);
typedef s32 (*CollectInSphere)(f32 radius, void *pos, s32 mask, void *self, void *hit);
typedef void (*BoneTransform)(void *moby, s32 joint, f32 *matrix);
typedef void *(*SpawnGlow)(void *owner, void *pos, void *vel, s32 lifetime, s32 r, s32 g, s32 b, s32 a, f32 scale);

#define scale_frames(L, n)  (((ScaleFrames)ee_charge_scale[L])(n))
#define set_state(L, s, a)  (((SetState)ee_charge_set_state[L])((s), (a)))

static s32 tap_window;        /* frames left to make the second tap */
static f32 steer;             /* the smoothed stick x (R&C2 0x2AB900) */
/* Buffers the game reads and writes as quadwords. */
static f32 probe[4] __attribute__((aligned(16)));
static f32 push[4] __attribute__((aligned(16)));
static f32 drift[4] __attribute__((aligned(16)));
static f32 bone[16] __attribute__((aligned(16)));
static u32 hit[16] __attribute__((aligned(16)));

static f32 wrap_angle(f32 a)
{
    while (a > PI) {
        a -= 2.0f * PI;
    }
    while (a < -PI) {
        a += 2.0f * PI;
    }
    return a;
}

static f32 length_sq(u32 o)
{
    return F32(o) * F32(o) + F32(o + 4) * F32(o + 4) + F32(o + 8) * F32(o + 8);
}

/* The dash's sequence in a free slot of the hero's class, once per class
 * (each level loads its own). 0 when there is no room. */
static s32 install_sequence(void)
{
    u8 *cls = *(u8 **)(*(u8 **)(HERO + HERO_MOBY) + 0x24);
    volatile u32 *seqs = (volatile u32 *)(cls + 0x48);
    if (seqs[CHARGE_SLOT] == (u32)ee_charge_seq) {
        return 1;
    }
    if (0x48 + 4 * CHARGE_SLOT >= *(u32 *)cls || seqs[CHARGE_SLOT] != 0) {
        return 0;
    }
    seqs[CHARGE_SLOT] = (u32)ee_charge_seq;
    return 1;
}

/* R&C2's state entry (0x2BF200) after hero_set_state(0x7B, 1). */
static void charge_enter(s32 L)
{
    s32 i;
    S32(HERO_MODE) = MODE_CHARGE;
    S32(HERO_FIDGET) = 0x68;
    S32(HERO_2284) = 0;
    for (i = 0; i < 16; i += 4) {
        F32(HERO_VEL + i) = F32(HERO_DISP + i);
    }
    F32(HERO_SPRING) = 0.0f;
    steer = 0.0f;
    tap_window = 0;
    F32(HERO_ANIM_SPEED) = 1.0f;
    if (install_sequence()) {
        LVL(L, func_L00_00232C10, SetAnim)(CHARGE_SLOT, 0, -2.0f);
    }
}

/* R&C2's block at the end of the input handler (0x2C2300), in place of
 * R&C1's Thruster-Pack block while the Charge Boots are worn. */
s32 ee_charge_trigger_c(s32 slot, u32 ra)
{
    s32 L = ee_level_of_call(ra, ee_charge_trigger, 0);
    s32 state = S32(HERO_STATE);
    s32 mode;
    if (WORN_BOOTS != ITEM_CHARGE_BOOTS) {
        return ((SlotItem)ee_charge_slot_item[L])(slot);
    }
    if (state == STATE_THRUSTER || state == STATE_CHARGE || state == STATE_LOOK || state == STATE_1D || state == STATE_1E
        || (S16(HERO_SURFACE) != 0 && OWNED_ITEM_1C != 0) || S16(HERO_22D8) != 0) {
        return 0;
    }
    if (tap_window > 0) {
        tap_window--;
    }
    mode = S32(HERO_MODE);
    if (tap_window != 0
        && ((u32)mode < 2 || mode == 0xC || ((mode == 2 || mode == 4) && S32(HERO_GROUNDED) != 0))
        && (PAD_HELD & PAD_R1) && (PAD_PRESSED & PAD_R1)) {
        if (set_state(L, STATE_CHARGE, 1)) {
            charge_enter(L);
            return 0;
        }
    }
    if (PAD_PRESSED & PAD_R1) {
        tap_window = scale_frames(L, 30);
    }
    return 0;
}

/* R&C2's state update (0x2C4BD8): 1 when the dash ended this frame. */
static s32 charge_ends(s32 L)
{
    if (S16(HERO_SURFACE) != 0 || (!(PAD_HELD & PAD_R1) && S32(HERO_TIMER) >= scale_frames(L, 61))) {
        S32(HERO_HOLD_OFF) = scale_frames(L, 30);
        if (set_state(L, STATE_STOP, 0)) {
            LVL(L, func_L00_00232C10, SetAnim)(ANIM_SKID, 4, (f32)scale_frames(L, 11));
        }
        return 1;
    }
    if (U8(HERO_WALL_HIT) && length_sq(HERO_DISP) < length_sq(HERO_VEL) * (0.75f * 0.75f)) {
        set_state(L, STATE_WALL_BONK, 1);
        return 1;
    }
    return 0;
}

/* Two sprites at an ankle, R&C2's colours. R&C2's ride on the hero; these
 * move on with 90% of his motion, so each frame's pair falls a little
 * behind the last: a short streak off the boot. */
static void streak(s32 L, void *moby, s32 joint_list)
{
    s32 i;
    LVL(L, func_0020DAF8, BoneTransform)(moby, joint_list, bone);
    probe[0] = bone[12];
    probe[1] = bone[13];
    probe[2] = bone[14];
    probe[3] = 1.0f;
    for (i = 0; i < 3; i++) {
        drift[i] = F32(HERO_DISP + 4 * i) * 0.9f;
    }
    drift[3] = 0.0f;
    LVL(L, func_L00_002ADBB0, SpawnGlow)(moby, probe, drift, scale_frames(L, 8), 0x20, 0x80, 0xFF, 0x50, 0.3f);
    LVL(L, func_L00_002ADBB0, SpawnGlow)(moby, probe, drift, scale_frames(L, 6), 0x00, 0xFF, 0xFF, 0x40, 0.22f);
}

/* R&C2's movement block (0x2BC338). */
static void charge_move(s32 L)
{
    f32 ft = FRAME_TIME;
    f32 fs = ft * 60.0f;              /* 60 Hz frames per frame */
    f32 rate = 0.17f * fs;
    f32 target = -F32(HERO_STICK_X);
    f32 turn, lean;
    void *moby = *(void **)(HERO + HERO_MOBY);
    volatile f32 *ease = (volatile f32 *)LVL_DATA(L, ease_table, u32);
    s32 i;

    LVL(L, func_L00_002175D0, LoopSound)(4, moby, SOUND_JET);
    LVL(L, func_L00_00211F80, StickTarget)(1.0f, 0);
    F32(HERO_TARGET) = ft * 11.5f;
    if (steer < target) {
        steer = steer + rate > target ? target : steer + rate;
    } else {
        steer = steer - rate < target ? target : steer - rate;
    }
    if (steer > 1.0f) {
        steer = 1.0f;
    } else if (steer < -1.0f) {
        steer = -1.0f;
    }
    turn = steer * 0.0203622f * F32(HERO_STICK);      /* per 60 Hz frame */
    if (S32(HERO_TIMER) < scale_frames(L, 60)) {
        turn *= 0.1f;
        F32(HERO_TARGET) = ft * 30.0f;
    }
    if (F32(HERO_STICK) > 0.2f) {
        F32(HERO_YAW) = wrap_angle(F32(HERO_YAW) + turn * fs);
    }
    LVL(L, func_L00_00212740, SpeedStep)(FRAME_TIME_SQ * 100.0f, FRAME_TIME_SQ * 15.0f);
    LVL(L, func_L00_00212790, SetPlanarVel)(99999.0f);
    LVL(L, func_L00_0025C918, Spring)((volatile f32 *)(HERO + HERO_POS + 8), (volatile f32 *)(HERO + HERO_SPRING),
                                      F32(HERO_GROUND_Z) + 0.17f, FRAME_SCALE_SQ * 0.04f, FRAME_SCALE_SQ * 0.3f, ft * 7.5f);

    /* What the boots run into: a 0.57 sphere three (60 Hz) frames ahead. */
    for (i = 0; i < 3; i++) {
        probe[i] = F32(HERO_CENTRE + 4 * i) + F32(HERO_DISP + 4 * i) * (3.0f / fs);
        push[i] = F32(HERO_DISP + 4 * i) * (7.0f / fs);
    }
    probe[3] = push[3] = 0.0f;
    for (i = 0; i < 16; i++) {
        hit[i] = 0;
    }
    LVL(L, func_L00_0025A8C0, HitDescriptor)(hit, moby, 0x10000, 1.0f, push);
    LVL(L, func_L00_001F2BE8, CollectInSphere)(0.57f, probe, 0x10, moby, hit);

    /* The lean: R&C2's rates (0x2AC468/498/4B0) and targets (+ 0x60, + 0x1C8, + 0x278). */
    lean = turn * 27.0f;
    if (lean > 1.7f) {
        lean = 1.7f;
    } else if (lean < -1.7f) {
        lean = -1.7f;
    }
    ease[0xA4 / 4] = 0.017f;
    ease[0xA8 / 4] = 0.3f;
    ease[0x204 / 4] = 0.025f;
    ease[0x208 / 4] = 0.3f;
    ease[0x2B4 / 4] = 0.03f;
    ease[0x2B8 / 4] = 0.27f;
    ease[0x60 / 4] = -lean;
    ease[0x1C8 / 4] = lean * 0.5f;
    ease[0x278 / 4] = lean * 0.5f;

    streak(L, moby, ANKLE_RIGHT);
    streak(L, moby, ANKLE_LEFT);
}

/* The per-state movement function's call: the dash moves the hero itself
 * (the game's function has no case for 0x7B and runs after it for its
 * bookkeeping), and hands over when the dash ends. */
void ee_charge_move_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, ee_charge_move_site, 0);
    if (S32(HERO_STATE) == STATE_CHARGE && !charge_ends(L)) {
        charge_move(L);
    }
    ((Void)ee_charge_move_fn[L])();
}
