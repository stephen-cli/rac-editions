/* x4: the Metal Detector pays 10x in challenge mode.
 *
 * The detector's update function (class 585, func_L00_002D4398) drains a
 * hidden spot's bolt count in steps of 5, spawning one bolt per step with
 * func_L00_002A86D8(spot, pos, dir, 0x13, value, 0). The spawner knows the
 * denominations 1-5, 20 and 50 (each its own bolt model), so in a
 * challenge-mode run the 5-bolt hexnut becomes a 50-bolt circlenut: same
 * number of spawns, exactly 10x. tools/overlay_patch.py redirects that one
 * call here in all 19 levels (through src/hooks.s, which adds the return
 * address that names the running level). */
#include "modseg.h"

DECLARE_LVL(func_L00_002D4398);        /* the detector's update: the hooked call site is in it */
DECLARE_LVL(func_L00_002A86D8);        /* the bolt spawner */
typedef void (*SpawnBolt)(void *spot, void *pos, void *dir, s32 kind, s32 value, s32 flag);

void ee_metal_detector_spawn_c(void *spot, void *pos, void *dir, s32 kind, s32 value, s32 flag, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_002D4398, EE_SITE_DETECTOR_SPAWN);
    if (g_completes > 0 && value == 5) {
        value = 50;
    }
    LVL(L, func_L00_002A86D8, SpawnBolt)(spot, pos, dir, kind, value, flag);
}
