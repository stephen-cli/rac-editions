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

#define LAYOUT_SETTING  (*(volatile u8 *)EE_SETTING_CONTROL_LAYOUT)

s32 GetControlLayout(void)
{
    if (LAYOUT_SETTING == EE_LAYOUT_FUTURE) {
        return EE_LAYOUT_FUTURE;
    }
    return EE_LAYOUT_CLASSIC;
}
