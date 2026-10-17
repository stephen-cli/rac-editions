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
 * The dash is driven from here, in R&C1's hero state 0x48. R&C2's number,
 * 0x7B, is one of R&C1's own in 11 level builds (0, 2, 4, 5, 7, 9, 10, 13,
 * 15, 17, 18: set_state and the movement switch have its case, a stun in
 * control mode 0x19 with animation 0x71, and 13 levels enter it), which
 * broke the dash there (first seen on Veldin 1). 0x48 is the state no
 * level's 130- and 131-entry state switches handle (in set_state, the
 * movement function and the update, all 19 levels), none enters and no
 * code compares the hero state with (levels and boot executable). Its
 * pieces, R&C2's (60 fps per-frame constants, converted to R&C1's
 * frame_time units as R&C1 writes its own):
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
 *        games' Ratchet carry the same skeleton and lists), and spawns
 *        particle type 0x4A (spawner 0x331FC8, update 0x332130): an
 *        additive sprite riding on the hero, blown backwards (his local
 *        -x) at 1/12 unit a frame, damped 2% a frame, growing and fading to
 *        nothing over 25 and 15 frames, drawn with particle texture 12, a
 *        faint cloud. R&C1's particle engine is the same one (ReRAC's
 *        particles.md), texture 12 included (the first frame of its cloud
 *        types 4, 11, 13, 18, 23 and 28, pixel for pixel R&C2's). Its type
 *        27 (spawner func_L00_0026EBC0, in every level) is that sprite
 *        without the riding, the drift, the growth, the colour change and
 *        the spin: additive, moving by a fixed step a frame, fading
 *        linearly. Here it takes texture 12, and every frame (jets_age)
 *        does the rest of R&C2's update on it.
 *  movement (0x2BC338)  the loop sound (R&C2's sound 0xD; R&C1's Ratchet
 *        has the Thruster-Pack's jet loop, 0x12, in the same slot); the
 *        stick target; target speed 0.1917 per frame (11.5 u/s), and for
 *        the first 60 frames 0.5 (30 u/s) with a tenth of the steering;
 *        steering = smoothed stick x (approach 0.17 per frame) * 0.02036 *
 *        stick magnitude, applied when the stick is past 0.2; speed_step
 *        1/36 up, 1/240 down; set_planar_vel along the facing; the hover
 *        spring (pos.z toward ground + 0.17: 0.04, 0.3, 0.125); then the
 *        crates in a 0.57 sphere three frames ahead of the body centre
 *        take a hit of 1 (flags 0x10000) pushed along 7 frames of travel
 *        (charge_contact: enemies bonk instead, the owner's rule); and the
 *        lean: whole-body roll and upper-body twist from the turn
 *        (clamp(turn * 27, 1.7)) in the hero's ease table.
 *  camera (the hero camera pass, 0x4256E0, at 0x425ED0)  every frame of
 *        the dash: lateral spring 0.04 / 0.2 (0x427D80), turn toward
 *        Ratchet's facing, his moby's matrix row 0, with strength pi/4 and
 *        no limit (0x428110), distance 3 at rate 0.003 (0x427C18(0, ..)),
 *        and the flag at camera + 0x246 (0x427E10). Measured in R&C2, the
 *        camera is back behind Ratchet 0.6 s after a U-turn mid-dash. R&C1's
 *        follow camera has the same helpers, the same code with the camera
 *        fields 0x20 to 0x30 lower (func_L00_002E9A40, func_L00_002E9DC8,
 *        func_L00_002E9900, func_L00_002E9AD0), and its per-frame overrides
 *        (func_L00_002E74B0) end with the Thruster-Pack block R&C2's sits
 *        next to; the dash hooks the call just before it.
 *
 * Sites: tools/gen_charge.py. The sequence: R&C2's 0x8B expanded by
 * tools/rac2_anim.py (build/modseg/gen_anim_charge.c), installed in a free
 * slot of Ratchet's class like the strafe's backpedal (src/strafe.c). */
#include "modseg.h"

DECLARE_LVL(func_L00_0026EBC0);   /* spawn particle type 27(pos, vel, colour, lifetime, size): an additive sprite that drifts and fades */
DECLARE_LVL(func_L00_001F2BE8);   /* collect_mobies_in_sphere(radius, pos, mask, self, hit) */
DECLARE_LVL(func_L00_0025A8C0);   /* make_hit_descriptor(hit, attacker, flags, damage, push) */
DECLARE_LVL(func_0020DAF8);       /* build_moby_bone_transform(moby, joint list, matrix): the level's copy */
DECLARE_LVL(func_L00_002175D0);   /* hero_loop_sound(slot, moby, sound): plays once into a free hero sound slot */
DECLARE_LVL(func_L00_0025C918);   /* spring(&value, &velocity, target, k, damping, snap) */
DECLARE_LVL(func_L00_00211F80);   /* stick_target(scale, mode) */
DECLARE_LVL(func_L00_00212740);   /* speed_step(accel, decel): ground speed toward target speed */
DECLARE_LVL(func_L00_00212790);   /* set_planar_vel(yaw): velocity from ground speed (99999: the facing) */
DECLARE_LVL(func_L00_00232C10);   /* set_hero_animation(id, start_frame, blend_frames) */
DECLARE_LVL(func_L00_002E9A40);   /* camera_lateral_spring(k, damping); 0 keeps one */
DECLARE_LVL(func_L00_002E9DC8);   /* camera_turn_toward(direction, strength, limit) */
DECLARE_LVL(func_L00_002E9900);   /* camera_set_distance(both, distance, rate) */
DECLARE_LVL(func_L00_002E9AD0);   /* camera + 0x226 = 1 (R&C2's + 0x246) */
DECLARE_LVL_DATA(ease_table);     /* the hero's per-joint ease table */
extern const u32 ee_charge_trigger[19], ee_charge_slot_item[19], ee_charge_scale[19];
extern const u32 ee_charge_set_state[19], ee_charge_move_site[19], ee_charge_move_fn[19];
extern const u32 ee_charge_part_frames[19];   /* the particle engine's per-type frame lists */
extern const u32 ee_charge_camera_site[19], ee_charge_collect[19], ee_charge_collect_list[19];
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
#define STATE_CHARGE    0x48          /* R&C2's 0x7B is a state of R&C1's own in 11 levels; see above */
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
typedef void (*CamSpring)(f32 k, f32 damping);
typedef void (*CamTurn)(void *direction, f32 strength, f32 limit);
typedef void (*CamDistance)(s32 both, f32 distance, f32 rate);
typedef void (*SetAnim)(s32 id, s32 start_frame, f32 blend);
typedef void (*StickTarget)(f32 scale, s32 mode);
typedef void (*SpeedStep)(f32 accel, f32 decel);
typedef void (*SetPlanarVel)(f32 yaw);
typedef void (*LoopSound)(s32 slot, void *moby, s32 sound);
typedef void (*Spring)(volatile f32 *value, volatile f32 *velocity, f32 target, f32 k, f32 damping, f32 snap);
typedef void (*HitDescriptor)(void *hit, void *attacker, s32 flags, f32 damage, void *push);
typedef s32 (*CollectInSphere)(f32 radius, void *pos, s32 mask, void *self, void *hit);
typedef void (*BoneTransform)(void *moby, s32 joint, f32 *matrix);
typedef void *(*SpawnFade)(void *pos, void *vel, u32 colour, s32 lifetime, f32 size);

/* R&C2's two jet records (0x1A7CA0, 0x1A7CB0), its layout: the colour at
 * birth and at death (R, G, B, A from the low byte, as R&C1's particles
 * take it), the size's growth over the life and its size at birth (x 1000,
 * in 1/210 000 unit, R&C1's and R&C2's sprite size), the lifetime in 60 Hz
 * frames. */
struct jet {
    u32 birth, death;
    s16 grow, base;
    s32 frames;
};
struct jet ee_charge_jets[2] = {
    { 0x40FF8020, 0x00CF6000, 100, 70, 25 },
    { 0x20FFFF00, 0x00008000, 90, 45, 15 },
};
f32 ee_charge_jet_size = 1000.0f;  /* R&C2's scale of grow and base */
f32 ee_charge_jet_push = 5.0f;     /* R&C2's velocity, local -x: 1/12 unit a frame, in units a second */
f32 ee_charge_jet_damp = 0.02f;    /* of the velocity, a 60 Hz frame */
#define PART_CLOUD      4             /* a particle type drawn with texture 12, R&C2's jets' */

/* Type 27 fades the alpha (R&C2's does the same) but keeps the step,
 * colour and size it was born with and does not spin; the rest of R&C2's
 * update (0x332130) is done here on the sprites this file spawned, while
 * each one is still the record it was: type 27, alive, the lifetime and
 * colour it was given. R&C2 keeps each sprite in its owner's frame (the
 * moby's matrix rows at + 0xC0 and position at + 0x10, R&C1's mobys
 * the same; row 0 is the facing): an offset moved by the velocity, so the
 * jets blow out behind the heels and turn with Ratchet. */
#define JET_TRACKED 128
static u8 *jet_part[JET_TRACKED];
static u8 jet_record[JET_TRACKED];
static u32 jet_rgb[JET_TRACKED];
static f32 jet_local[JET_TRACKED][3]; /* the offset in Ratchet's frame */
static f32 jet_push[JET_TRACKED];     /* its velocity backwards, units a frame */
static s32 jet_next;
static s32 jet_live;
static f32 jet_spin;                  /* R&C2's turn of 1/256 a 60 Hz frame, in PAL frames */

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

/* R&C2's state entry (0x2BF200), after hero_set_state(STATE_CHARGE, 1). */
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

static u32 lerp_rgb(u32 from, u32 to, s32 n, s32 d)
{
    u32 out = 0;
    s32 shift;
    for (shift = 0; shift < 24; shift += 8) {
        s32 a = (from >> shift) & 0xFF, b = (to >> shift) & 0xFF;
        out |= (u32)(a + (b - a) * n / d) << shift;
    }
    return out;
}

/* R&C2's two jet sprites at an ankle (its spawner, 0x331FC8): born where
 * the ankle is, kept as an offset in Ratchet's frame. */
static void jets(s32 L, u8 *moby, s32 joint_list)
{
    u8 *frames = *(u8 **)(ee_charge_part_frames[L] + 4 * PART_CLOUD);
    f32 *row = (f32 *)(moby + 0xC0);
    f32 *at = (f32 *)(moby + 0x10);
    f32 d[3];
    s32 i, r;
    LVL(L, func_0020DAF8, BoneTransform)(moby, joint_list, bone);
    probe[0] = bone[12];
    probe[1] = bone[13];
    probe[2] = bone[14];
    probe[3] = 1.0f;
    for (i = 0; i < 4; i++) {
        drift[i] = 0.0f;
    }
    for (i = 0; i < 3; i++) {
        d[i] = probe[i] - at[i];
    }
    for (i = 0; i < 2; i++) {
        const struct jet *j = &ee_charge_jets[i];
        u8 *part = LVL(L, func_L00_0026EBC0, SpawnFade)(probe, drift, j->birth, scale_frames(L, j->frames),
                                                        j->base * ee_charge_jet_size);
        if (part != 0) {
            s32 k = jet_next;
            if (frames != 0) {
                part[2] = frames[0];
            }
            jet_next = (jet_next + 1) % JET_TRACKED;
            jet_live += jet_part[k] == 0;
            jet_part[k] = part;
            jet_record[k] = i;
            jet_rgb[k] = j->birth & 0xFFFFFF;
            for (r = 0; r < 3; r++) {
                jet_local[k][r] = d[0] * row[4 * r] + d[1] * row[4 * r + 1] + d[2] * row[4 * r + 2];
            }
            jet_push[k] = FRAME_TIME * ee_charge_jet_push;
        }
    }
}

/* R&C2's update (0x332130), every frame before the particles' own: the
 * size and colour at the sprite's age, the velocity damped and added to
 * the offset, the position from Ratchet's frame, a turn. R&C2 places the
 * sprite after the hero has moved; here the hero has not moved yet, so the
 * record's own step is his last one and type 27's update adds it. */
static void jets_age(void)
{
    u8 *moby = *(u8 **)(HERO + HERO_MOBY);
    f32 *row = (f32 *)(moby + 0xC0);
    f32 *at = (f32 *)(moby + 0x10);
    f32 keep = 1.0f - ee_charge_jet_damp * FRAME_TIME * 60.0f;
    s32 k, i, turn;
    if (jet_live == 0) {
        return;
    }
    jet_spin += FRAME_TIME * 60.0f;
    turn = (s32)jet_spin;
    jet_spin -= turn;
    for (k = 0; k < JET_TRACKED && jet_live != 0; k++) {
        u8 *part = jet_part[k];
        f32 *l = jet_local[k];
        const struct jet *j;
        s32 left, life, age;
        if (part == 0) {
            continue;
        }
        j = &ee_charge_jets[jet_record[k]];
        left = *(s32 *)(part + 0x30);
        life = *(s32 *)(part + 0x34);
        if (part[0] != 27 || (part[1] & 0x80) || left <= 0 || left > life
            || (*(u32 *)(part + 4) & 0xFFFFFF) != jet_rgb[k]) {
            jet_part[k] = 0;
            jet_live--;
            continue;
        }
        age = life - left;
        *(f32 *)(part + 0xC) = (j->base + (f32)j->grow * age / life) * ee_charge_jet_size;
        jet_push[k] *= keep;
        l[0] -= jet_push[k];
        for (i = 0; i < 3; i++) {
            ((f32 *)(part + 0x10))[i] = at[i] + l[0] * row[i] + l[1] * row[4 + i] + l[2] * row[8 + i];
            ((f32 *)(part + 0x20))[i] = F32(HERO_DISP + 4 * i);
        }
        part[8] += turn;
        jet_rgb[k] = lerp_rgb(j->birth, j->death, age, life);
        *(u32 *)(part + 4) = (*(u32 *)(part + 4) & 0xFF000000) | jet_rgb[k];
    }
}

/* What the boots run into, in the sphere at probe. R&C2 (0x2BC53C) collects
 * the mobies there without a hit and hits only those 0x315368 accepts, its
 * classes 500 to 540, the crates, with flags 0x10000, damage 1 and push:
 * the charge breaks crates and is no weapon. R&C1's crates are class type
 * 9 (class + 0x46; bolt, nanotech, metal, explosive and ammo crates), its
 * creatures, the enemies the game's targeting knows, type 5. The owner's
 * rule: an enemy ends the dash in a bonk, unhurt. So: collect without a
 * hit (the result list, gen_charge's collect_list); an enemy bonks; else
 * the crates get R&C2's hit, every other moby in the sphere losing its
 * hittable flag (+ 0x34 bit 0x4000, which the hit pass tests) for the
 * call. 1 when the dash ended. */
#define CLASS_ENEMY     5
#define CLASS_CRATE     9
#define MOBY_HITTABLE   0x4000
#define CONTACTS        64

static s32 class_type(u8 *moby)
{
    u8 *cls = *(u8 **)(moby + 0x24);
    return cls != 0 ? *(s16 *)(cls + 0x46) : -1;
}

static s32 charge_contact(s32 L, void *moby)
{
    u8 *found[CONTACTS];
    u8 **list = (u8 **)ee_charge_collect_list[L];
    s32 n, i, crates = 0, hidden = 0;
    n = LVL(L, func_L00_001F2BE8, CollectInSphere)(0.57f, probe, 0x10, moby, 0);
    if (n > CONTACTS) {
        n = CONTACTS;
    }
    for (i = 0; i < n; i++) {
        found[i] = list[i];
        if (class_type(found[i]) == CLASS_ENEMY) {
            set_state(L, STATE_WALL_BONK, 1);
            return 1;
        }
        crates += class_type(found[i]) == CLASS_CRATE;
    }
    if (crates == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        u16 *flags = (u16 *)(found[i] + 0x34);
        if (class_type(found[i]) != CLASS_CRATE && (*flags & MOBY_HITTABLE)) {
            *flags &= ~MOBY_HITTABLE;
            found[hidden++] = found[i];
        }
    }
    for (i = 0; i < 16; i++) {
        hit[i] = 0;
    }
    LVL(L, func_L00_0025A8C0, HitDescriptor)(hit, moby, 0x10000, 1.0f, push);
    LVL(L, func_L00_001F2BE8, CollectInSphere)(0.57f, probe, 0x10, moby, hit);
    for (i = 0; i < hidden; i++) {
        *(u16 *)(found[i] + 0x34) |= MOBY_HITTABLE;
    }
    return 0;
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
    if (charge_contact(L, moby)) {
        return;
    }

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

    jets(L, moby, ANKLE_RIGHT);
    jets(L, moby, ANKLE_LEFT);
}

/* The per-state movement function's call: the dash moves the hero itself
 * (the game's function has no case for 0x48 and runs after it for its
 * bookkeeping), and hands over when the dash ends. */
void ee_charge_move_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, ee_charge_move_site, 0);
    if (S32(HERO_STATE) == STATE_CHARGE && !charge_ends(L)) {
        charge_move(L);
    }
    jets_age();
    ((Void)ee_charge_move_fn[L])();
}

/* The follow camera overrides' collect_mobies_in_sphere call, then R&C2's
 * charge camera block (0x425ED0) while Ratchet dashes. */
s32 ee_charge_camera_c(f32 radius, void *pos, s32 mask, void *self, void *hit, u32 ra)
{
    s32 L = ee_level_of_call(ra, ee_charge_camera_site, 0);
    s32 found = ((CollectInSphere)ee_charge_collect[L])(radius, pos, mask, self, hit);
    if (S32(HERO_STATE) == STATE_CHARGE) {
        u8 *moby = *(u8 **)(HERO + HERO_MOBY);
        LVL(L, func_L00_002E9A40, CamSpring)(0.04f, 0.2f);
        LVL(L, func_L00_002E9DC8, CamTurn)(moby + 0xC0, PI / 4.0f, 0.0f);
        LVL(L, func_L00_002E9900, CamDistance)(0, 3.0f, 0.003f);
        LVL(L, func_L00_002E9AD0, Void)();
    }
    return found;
}
