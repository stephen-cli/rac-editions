/* rz: the pause map's reward markers. A marker whose icon names its reward
 * (Al's Heli-Pack, the Nanotech vendor's upgrades, the mechanic's infobot;
 * tools/gen_randomizer.py lists them, ee_rz_map_icons) shows the map's own
 * quest marker, the "?" of the planet's other missions, while that reward is
 * an uncollected check of the planet the map shows. DrawMapView
 * (draw_map_overlay), which draws the pause map and nothing else, asks
 * GetIconFrame for each marker's frame, once for its bounds
 * and once for its sprite; the map's own frames and the hero's arrow go
 * through the same calls and pass. A marker showing another planet's
 * reward (Pokitaru's Raritanium, the Persuader's trade) is a prerequisite
 * and keeps its icon.
 *
 * The item icons sit on a dark backdrop (the icon's flags & 0x40), which
 * the quest marker has not: the first call, the bounds pass, comes before
 * the drawing pass reads the flag, so it takes the flag off the map's
 * icons of that texture and frame while the reward is hidden, and puts it
 * back once the check is collected. */
#include "randomizer_play.h"

#define LEVELS              19
#define MAP_MARKERS         0xE99A      /* the map's own markers' texture, */
#define QUEST_MARKER        2           /* and the quest marker's frame */
#define MAP_ICONS(L)        (*(u8 *volatile *)((u8 *)LVL_DATA(L, map_state, u8 *) + 0x20))
#define ICON_SIZE           0x28
#define ICON_TEXTURE        0x06
#define ICON_FRAME          0x08
#define ICON_FLAGS          0x04
#define ICON_LIST_END       0x4
#define ICON_BACKDROP       0x40

DECLARE_LVL_DATA(map_state);   /* D_001A01F0 in the executable: + 0x20 the shown map's icons */

/* For the PINE test scripts: frames swapped for the quest marker; the last icon swapped. */
volatile u32 ee_debug_rz_map[2];

/* The backdrop of the shown map's icons of TEXTURE and FRAME, on or off. */
static void set_backdrop(s32 texture, s32 frame, s32 on, s32 L)
{
    u8 *icon = MAP_ICONS(L);
    if (icon == 0) {
        return;
    }
    for (; !(*(volatile u16 *)(icon + ICON_FLAGS) & ICON_LIST_END); icon += ICON_SIZE) {
        volatile u16 *flags = (volatile u16 *)(icon + ICON_FLAGS);
        if (*(volatile u16 *)(icon + ICON_TEXTURE) == texture && *(volatile s16 *)(icon + ICON_FRAME) == frame) {
            *flags = on ? (u16)(*flags | ICON_BACKDROP) : (u16)(*flags & ~ICON_BACKDROP);
        }
    }
}

/* GetIconFrame(icon, frame) in DrawMapView, in level L's program. */
s32 ee_rz_map_icon_c(s32 icon, s32 frame, s32 L)
{
    s32 i, planet;
    if (rz_play_active() && L >= 0 && L < LEVELS) {
        planet = rz_play_map_planet(L);
        for (i = 0; i < ee_rz_map_icon_count; i++) {
            const struct RzMapIcon *marker = &ee_rz_map_icons[i];
            s32 hidden;
            if (marker->planet != planet || marker->texture != icon || marker->frame != frame) {
                continue;
            }
            hidden = rz_play_hidden_check(&marker->source, planet) >= 0;
            if (marker->backdrop) {
                set_backdrop(icon, frame, !hidden, L);
            }
            if (hidden) {
                ee_debug_rz_map[0]++;
                ee_debug_rz_map[1] = (u32)icon;
                return LVL(L, GetIconFrame, IconFn)(MAP_MARKERS, QUEST_MARKER);
            }
            break;
        }
    }
    return LVL(L, GetIconFrame, IconFn)(icon, frame);
}
