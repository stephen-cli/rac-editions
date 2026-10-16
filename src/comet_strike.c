/* x3b: keep moving after a Comet-Strike, as in Going Commando.
 *
 * R&C1's Comet-Strike (crouch + Square) is hero state 0x15. Its movement
 * block (in func_L00_002180F8, the per-state movement switch; NTSC decomp
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
 * call site itself identifies the level (ra = site + 8). */
#include "modseg.h"

extern const u32 ee_comet_site[19];       /* the jal speed_step in the state 0x15 movement block */
extern const u32 ee_speed_step[19];
extern const u32 ee_hero_set_state[19];

#define HERO                 0x0013F450
#define hero_state           (*(volatile s32 *)(HERO + 0x2084))
#define hero_lock_target     (*(volatile s32 *)(HERO + 0xA9C))
#define hero_anim_frame      (*(volatile f32 *)(HERO + 0xAA8))
#define hero_stick           (*(volatile f32 *)(HERO + 0x229C))   /* left stick magnitude, 0..1 */

#define STATE_RUN            2
#define STATE_COMET_STRIKE   0x15
#define RELEASE_FRAME        34.0f      /* the wrench leaves the hand at 33 (both games) */
#define RUN_STICK            0.9f       /* R&C2's threshold at 0x2C46A0 */

typedef void (*SpeedStep)(f32 accel, f32 decel);
typedef s32 (*SetState)(s32 state, s32 with_animation);

void ee_comet_speed_step_c(f32 accel, f32 decel, u32 ra)
{
    s32 L = ee_level_of_call(ra, ee_comet_site, 0);

    ((SpeedStep)ee_speed_step[L])(accel, decel);

    if (hero_state == STATE_COMET_STRIKE && hero_lock_target == 0
        && hero_anim_frame > RELEASE_FRAME && hero_stick > RUN_STICK) {
        ((SetState)ee_hero_set_state[L])(STATE_RUN, 1);
    }
}
