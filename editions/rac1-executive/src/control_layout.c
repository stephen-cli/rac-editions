/* The control layout: the Controls page's "Control layout" row, Classic or
 * Future, above Strafing. Classic is retail's buttons. Future moves Ratchet's
 * buttons toward the later games':
 *
 *   crouch, and everything built on it (the crouch jumps, Comet-Strike, the
 *     Hydro-Pack's jets, the Thruster-Pack's and the Charge Boots' double
 *     tap)                                       R1 -> R2
 *   fire the weapon or use the gadget in hand    Circle -> Circle and R1
 *   strafe (src/strafe.c)                        L2 or R2 -> L2
 *
 * and the prompts follow (src/control_prompts.c).
 *
 * Setting. EE_SETTING_CONTROL_LAYOUT (src/modseg.h) is a byte in the saved
 * statistics block's spare bytes (save block 0x11, after the Movie volume),
 * which retail keeps and re-saves without reading. The Controls page's toggle
 * widget (func_L00_0027FA68 in level 0) flips it between 0 and 1 like the
 * other rows; tools/gen_menu.py writes the row as {label, &byte, Future,
 * Classic, 0}, the toggle showing the first name for a nonzero byte. A retail
 * save, a card that wiped the block and the new-game reset all leave 0, and
 * so does any value that is neither 0 nor 1: all read as Classic.
 *
 * The buttons. Every button test of Ratchet's goes through pad 0 (D_0013CA40,
 * OpenRAC include/pad.h) after the game's pad parse (func_002181F0), which
 * src/strafe.c's hook follows: held (+ 0x1A0), pressed (+ 0x1A4), released
 * (+ 0x1A8) and the ring of the last 30 pressed words (+ 0x1E0) that
 * PadPressedWithin (func_L00_00267BA8) searches. In the game's bit layout L2
 * is 0x1, R2 0x2, R1 0x8 and Circle 0x20, and:
 *   - crouch, the crouch jumps, Comet-Strike, the ledge drop and the
 *     Hydro-Pack's jets all test R1 or R2 (0xA): retail already crouches on
 *     R2 too;
 *   - the Thruster-Pack's hover (entered and left by a double tap at the end
 *     of Hero_TryLaunchGadget, func_L00_00228770, and in the hero update's
 *     state 0x81 case) and the edition's Charge Boots dash test R1 (0x8)
 *     alone;
 *   - every weapon and gadget tests the hero's item button mask (hero +
 *     0x10A0, set to 0x20 by func_L00_0020F118; 0x80 for the wrench), and a
 *     few test 0x20 directly (the throwing gloves, the Visibomb's detonation,
 *     the Metal Detector, the swap back from the wrench).
 * So Future gives the game Circle for physical Circle or R1, and the game's R1
 * bit for physical R2: crouch and the R1-only double taps follow R2 with no
 * change to the game's code, and R1 fires whatever is in hand.
 *
 * Pressing R1 and Circle together must count as one press: the edges are
 * worked out again from the merged buttons, the way the parse works them
 * out (pressed = held & ~prev_held, released = prev_held & ~held, both 0
 * while + 0x18C counts down after a reconnection), with the previous frame's
 * physical buttons (prev_held + 0x1AC, which Pad_UpdatePort latches from
 * + 0x348 before the parse) merged the same way. The raw words (+ 0x1B0..
 * + 0x1BC), the unmasked ones (+ 0x1C0..+ 0x1C8, which menus and the
 * quick-select ring read) and + 0x348 keep the physical buttons: the ship
 * flights read the raw words (missiles on R1 or Circle, as retail), and L1
 * and R1 still turn the quick-select ring's pages.
 *
 * Where Future applies: gameplay (game mode 0) as Ratchet or Clank. Giant
 * Clank keeps retail's buttons, as does the hoverboard (control mode 0x16:
 * its tricks are L1, L2, R1 and R2, held & 0xF in func_L05_00254030), and
 * so do the pause menu, cutscenes (whose skip holds all four shoulder
 * buttons), vendors and every other game mode. The remap also waits while
 * the quick-select ring is open, or while the game has locked the pad for a
 * frame (+ 0x1CC: 1 strips Circle, Triangle and the left stick, 2 keeps only
 * Start and Select; the ring and Clank's Gadgebot menu set it). */
#include "modseg.h"

DECLARE_LVL_DATA(game_mode);

#define LAYOUT_SETTING  (*(volatile u8 *)EE_SETTING_CONTROL_LAYOUT)

#define HERO                0x0013F450
#define hero_control_mode   (*(volatile s32 *)(HERO + 0x208C))
#define hero_vehicle        (*(volatile u8 *)(HERO + 0x20A4))   /* 0 Ratchet, 1 Clank, 2 Giant Clank */
#define CONTROL_HOVERBOARD  0x16
#define VEHICLE_GIANT_CLANK 2
#define GAME_MODE_PLAY      0

#define PAD_LOCK_FRAMES 0x18C   /* s16: nonzero, the parse reports no edges */
#define PAD_HIST_INDEX  0x18E   /* s16: the history slot the next parse writes */
#define PAD_HELD        0x1A0
#define PAD_PRESSED     0x1A4
#define PAD_RELEASED    0x1A8
#define PAD_PREV_HELD   0x1AC
#define PAD_HISTORY     0x1E0
#define PAD_HISTORY_LEN 30

#define BUTTON_R2       0x02u
#define BUTTON_R1       0x08u
#define BUTTON_CIRCLE   0x20u
#define REMAPPED        (BUTTON_R2 | BUTTON_R1 | BUTTON_CIRCLE)

s32 GetControlLayout(void)
{
    if (LAYOUT_SETTING == EE_LAYOUT_FUTURE) {
        return EE_LAYOUT_FUTURE;
    }
    return EE_LAYOUT_CLASSIC;
}

s32 ee_in_play(s32 level)
{
    if (*LVL_DATA(level, game_mode, volatile s32 *) != GAME_MODE_PLAY) {
        return 0;
    }
    if (hero_control_mode == CONTROL_HOVERBOARD) {
        return 0;
    }
    return 1;
}

s32 ee_layout_in_play(s32 level)
{
    if (GetControlLayout() != EE_LAYOUT_FUTURE) {
        return 0;
    }
    if (!ee_in_play(level) || hero_vehicle == VEHICLE_GIANT_CLANK) {
        return 0;
    }
    return 1;
}

/* The buttons the game sees in Future for the physical PHYSICAL. */
static u32 future_buttons(u32 physical)
{
    u32 game = physical & ~REMAPPED;

    if (physical & (BUTTON_CIRCLE | BUTTON_R1)) {
        game |= BUTTON_CIRCLE;
    }
    if (physical & BUTTON_R2) {
        game |= BUTTON_R1;
    }
    return game;
}

void ee_layout_pad(s32 level, volatile u8 *pad, s32 pad_lock)
{
    volatile u32 *held = (volatile u32 *)(pad + PAD_HELD);
    volatile u32 *pressed = (volatile u32 *)(pad + PAD_PRESSED);
    volatile u32 *released = (volatile u32 *)(pad + PAD_RELEASED);
    volatile u32 *history = (volatile u32 *)(pad + PAD_HISTORY);
    s32 newest = (*(volatile s16 *)(pad + PAD_HIST_INDEX) + PAD_HISTORY_LEN - 1) % PAD_HISTORY_LEN;
    u32 now;
    u32 before;

    if (!ee_layout_in_play(level) || pad_lock != 0 || ee_qs_ring_open(level)) {
        return;
    }
    now = future_buttons(*held);
    before = future_buttons(*(volatile u32 *)(pad + PAD_PREV_HELD));
    *held = now;
    if (*(volatile s16 *)(pad + PAD_LOCK_FRAMES) == 0) {
        *pressed = (*pressed & ~REMAPPED) | (now & ~before & REMAPPED);
        *released = (*released & ~REMAPPED) | (before & ~now & REMAPPED);
    }
    /* The parse has already stored this frame's pressed word in the history. */
    history[newest] = (history[newest] & ~REMAPPED) | (*pressed & REMAPPED);
}
