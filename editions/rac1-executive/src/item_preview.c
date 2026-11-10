/* The Weapons and Gadgets pages' item preview: no flash of an unowned item.
 *
 * Retail's preview draw (draw_available_item_preview_mobys; NTSC decomp
 * draw_available_item_preview_mobys) decides whether to draw from the item
 * of the grid cell under the cursor, then draws whatever preview moby the
 * page holds. The moby is replaced by the preview binding
 * (update_item_preview_binding), which runs before the
 * cursor moves in a frame, so on the frame the cursor leaves an unowned
 * cell for an owned one the old moby is still there and gets drawn: the
 * binding builds a moby for unowned cells too and only the draw hides it.
 * That is the one-frame flash of the empty slot's item the owner saw going
 * from an empty Head or Foot Items cell (Charge Boots locked, a helmet not
 * found yet) to an owned one (2026-10-18). tools/overlay_patch.py's
 * item_preview feature makes the draw also ask this whether the moby's own
 * item (PreviewVars.item, set by the binding) is owned; the retail cursor
 * test stays, so leaving an owned item for an unowned one is hidden as
 * before. Returns the moby to draw, or 0 for nothing. */
#include "modseg.h"

typedef struct {                /* a preview moby's variables (Moby.pvars) */
    u8 pad0[0xC];
    s32 item;                   /* 0xC: the item it shows */
} PreviewVars;

Moby *ee_preview_shown(Moby *moby)
{
    if (moby == 0) {
        return 0;
    }
    return item_available[((PreviewVars *)moby->pvars)->item] ? moby : 0;
}
