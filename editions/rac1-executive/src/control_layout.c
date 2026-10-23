/* The control layout: the Controls page's "Control layout" row, Classic or
 * Future, above Strafing. Other code asks GetControlLayout() which one is in
 * force; this file only stores and reports the choice.
 *
 * Setting. EE_SETTING_CONTROL_LAYOUT (src/modseg.h) is a byte in the saved
 * statistics block's spare bytes (save block 0x11, after the Movie volume),
 * which retail keeps and re-saves without reading. The Controls page's toggle
 * widget (func_L00_0027FA68 in level 0) flips it between 0 and 1 like the
 * other rows; tools/gen_menu.py writes the row as {label, &byte, Future,
 * Classic, 0}, the toggle showing the first name for a nonzero byte. A retail
 * save, a card that wiped the block and the new-game reset all leave 0, and
 * so does any value that is neither 0 nor 1: all read as Classic. */
#include "modseg.h"

DECLARE_LVL_DATA(game_mode);

#define LAYOUT_SETTING  (*(volatile u8 *)EE_SETTING_CONTROL_LAYOUT)

#define HERO                0x0013F450
#define hero_control_mode   (*(volatile s32 *)(HERO + 0x208C))
#define CONTROL_HOVERBOARD  0x16
#define GAME_MODE_PLAY      0

s32 GetControlLayout(void)
{
    if (LAYOUT_SETTING == EE_LAYOUT_FUTURE) {
        return EE_LAYOUT_FUTURE;
    }
    return EE_LAYOUT_CLASSIC;
}

/* In play: game mode 0 (the level's D_0015F6A8: not paused, in a cutscene,
 * at a vendor or in the credits) and not on the hoverboard (control mode
 * 0x16). The strafe (src/strafe.c) takes the triggers only here. */
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
