/* x3b: keep moving after a Comet-Strike, as in Going Commando.
 *
 * R&C1's Comet-Strike (crouch + Square) is hero state 0x15. Its movement
 * block (in hero_update_movement, the per-state movement switch; NTSC decomp
 * 00233de0.c case 0x15) sets the target speed to 0 every frame and eases
 * the ground speed down with speed_step(37, 28), and its state update
 * (00242930.c case 21) leaves the state only when the animation passes the
 * attack record's end frame, when it goes to IDLE; the wrench itself is
 * released at animation frame 33 (FUN_L00_00211870(33.0f)). So Ratchet
 * stands through the whole throw-and-catch.
 *
 * R&C2 (SCUS_972.68, Oozla's program: states 0x15 crouch throw and 0x83
 * Circle throw share the movement block at 0x2BAE08 and the entry at
 * 0x2C0A48) has the very same movement block, down to speed_step(37, 28),
 * and the same release frame; what differs is the state update: from
 * animation frame 34, with the stick pushed past 0.9, it leaves the throw
 * for RUN (set_state(2) at 0x2C46B4) and at the record's end frame it
 * picks RUN over IDLE when the stick is past 0.8. That is the "he can move
 * while throwing": the throw animation is cancelled into a run the moment
 * the wrench has left the hand, and the wrench returns to wherever he is.
 *
 * This hook sits on the throw block's speed_step call, which runs every
 * frame of state 0x15 (the hero_set_state'd states keep their own blocks,
 * so no other state reaches it). After the game's speed_step it applies
 * R&C2's rule: anim frame > 34, stick past 0.9, no lock-on target
 * (hero + 0xA9C, R&C2's + 0xC2C) -> hero_set_state(RUN, 1), whose entry
 * picks the run animation and blend itself. The hero is core data
 * (0x13F450). The movement switch and hero_set_state are built differently
 * in groups of levels, so the catalogue cannot name them for all 19:
 * tools/gen_hero.py finds the hooked call, speed_step and hero_set_state
 * in each level's own program and emits the three tables used here; the
 * call site itself identifies the level (ra = site + 8).
 *
 * The jump cancel. R&C1's state update already has one: states 0x13-0x15
 * share a block (NTSC 00242930.c case 19-21) that, past the attack
 * record's f14 frame with no lock-on target, takes Cross pressed within
 * (int(frame) - fC) * 2 frames (pad_pressed_within) as a jump, or with R1
 * held as the crouch jumps (FUN_L00_00229660). But the Comet-Strike's
 * record (attack record 3: fC 83, f14 87, f18 91 in every level) opens it
 * at frame 87 of 91, so in practice he cannot jump out of the throw. R&C2
 * has the same block (0x2C45B0) and the same record with two changes:
 * f14 is 35, just after the release, and for the crouch throw the window
 * is a fixed 8 frames (movz at 0x2C4604), since (frame - 83) * 2 is
 * negative that early. The edition makes both: tools/overlay_patch.py
 * writes 35 into record 3's f14 (level data), and the block's
 * pad_pressed_within call comes here, where state 0x15 gets R&C2's
 * window (8 frames at 60 Hz, scaled like the game's other jump windows);
 * states 0x13 and 0x14 keep the game's. Everything after the press test
 * (lock-on, the R1 crouch jumps, hero_set_state(JUMP)) is the game's own
 * code. Site from tools/gen_hero.py.
 *
 * No wrench attack while the wrench is away. Retail never needs the rule:
 * Ratchet stands in state 0x15 until he has caught the wrench, so no code of
 * its tests for a wrench in flight before a swing (state 0x13), a
 * Hyper-Strike (0x14) or another throw. Moving out of the throw opens all
 * three, and a second throw relaunches the wrench from where it is
 * (launch_hero_wrench aims and launches the wrench moby wherever it flies), so
 * throw after throw walks it across the level. The game does keep the
 * flight's state: launch_hero_wrench sets g_hero.items[0].thrown and the
 * wrench moby's state to 10 (flying out); wrench_update turns it to 11 (coming
 * back) and, at the catch, clears both. All three attacks start from Square
 * (the wrench's button mask, 0x80), so while the wrench is in flight the pad
 * hook (src/strafe.c) takes Square out of what the game sees: held, pressed
 * and the newest word of the press history that pad_pressed_within searches.
 * The frame it is caught, Square is the game's again. */
#include "modseg.h"
#include "structs/moby.h"

extern const u32 ee_comet_site[19];       /* the jal speed_step in the state 0x15 movement block */
extern const u32 ee_speed_step[19];
extern const u32 ee_hero_set_state[19];
extern const u32 ee_comet_jump_site[19];  /* the jal pad_pressed_within in the state update's attack block */
DECLARE_LVL(pad_pressed_within);   /* pad_pressed_within(button, window, out) */
DECLARE_LVL(scale_ticks);          /* scale_game_frames(n): the level's copy */

/* R&C2's rule reads this word as the lock-on target; OpenRAC's Hero has it among the animation fields. */
#define hero_lock_target     g_hero.anim_changing

#define WRENCH_SLOT          0          /* the hero's item slot the wrench is in */
#define WRENCH_FLYING_OUT    10         /* the wrench moby's states: thrown, and on its way back */
#define WRENCH_COMING_BACK   11
#define STATE_RUN            2
#define STATE_COMET_STRIKE   0x15
#define RELEASE_FRAME        34.0f      /* the wrench leaves the hand at 33 (both games) */
#define RUN_STICK            0.9f       /* R&C2's threshold at 0x2C46A0 */
#define JUMP_WINDOW          8          /* R&C2's Cross window for the crouch throw, frames at 60 Hz */

typedef void (*SpeedStep)(f32 accel, f32 decel);
typedef s32 (*SetState)(s32 state, s32 with_animation);
typedef s32 (*PadPressedWithin)(u32 button, s32 window, s32 *out);
typedef s32 (*ScaleFrames)(s32 frames);

void ee_comet_speed_step_c(f32 accel, f32 decel, u32 ra)
{
    s32 L = ee_level_of_call(ra, ee_comet_site, 0);

    ((SpeedStep)ee_speed_step[L])(accel, decel);

    if (g_hero.state.current == STATE_COMET_STRIKE && hero_lock_target == 0
        && g_hero.anim_frame > RELEASE_FRAME && g_hero.stick_magnitude > RUN_STICK) {
        ((SetState)ee_hero_set_state[L])(STATE_RUN, 1);
    }
}

static s32 wrench_in_flight(void)
{
    struct Moby *wrench = g_hero.items[WRENCH_SLOT].moby;

    if (!g_hero.items[WRENCH_SLOT].thrown || wrench == 0) {
        return 0;
    }
    return wrench->state == WRENCH_FLYING_OUT || wrench->state == WRENCH_COMING_BACK;
}

void ee_comet_pad(s32 level, volatile PadState *pad)
{
    s32 newest;

    if (!ee_in_play(level) || !wrench_in_flight()) {
        return;
    }
    /* The parse has already stored this frame's pressed word in the history. */
    newest = (pad->hist_idx + ARRAY_COUNT(pad->hist_btn) - 1) % ARRAY_COUNT(pad->hist_btn);
    pad->held &= ~PAD_SQUARE;
    pad->pressed &= ~PAD_SQUARE;
    pad->hist_btn[newest] &= ~PAD_SQUARE;
}

s32 ee_comet_jump_window_c(u32 button, s32 window, s32 *out, u32 ra)
{
    s32 L = ee_level_of_call(ra, ee_comet_jump_site, 0);

    if (g_hero.state.current == STATE_COMET_STRIKE) {
        window = LVL(L, scale_ticks, ScaleFrames)(JUMP_WINDOW);
    }
    return LVL(L, pad_pressed_within, PadPressedWithin)(button, window, out);
}
