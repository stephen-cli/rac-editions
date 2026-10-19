/* x4: the Metal Detector pays 20x in challenge mode.
 *
 * The detector's update function (class 585, func_L00_002D4398) drains a
 * hidden spot's bolt count in steps of 5, spawning one bolt per step with
 * func_L00_002A86D8(spot, pos, dir, 0x13, value, 0). The spawner knows the
 * denominations 1-5, 20 and 50 (each its own bolt model), so in a
 * challenge-mode run each 5-bolt hexnut becomes two 50-bolt circlenuts:
 * exactly 20x, 349,200 bolts over the 358 spots of the 19 levels. Kind 0x13
 * gives every bolt its own random +-30 degree throw, so the two scatter; with
 * no moby free the spawner adds the value to the bolt count instead, so none
 * is lost. Spots stay drained for the run (per-level save block at 0x14C010)
 * and refill only with the next challenge-mode reset, so this cannot be
 * farmed. tools/overlay_patch.py redirects that one call here in all 19
 * levels (through src/hooks.s, which adds the return address that names the
 * running level). */
#include "modseg.h"

DECLARE_LVL(func_L00_002D4398);        /* the detector's update: the hooked call site is in it */
DECLARE_LVL(func_L00_002A86D8);        /* the bolt spawner */
typedef void (*SpawnBolt)(void *spot, void *pos, void *dir, s32 kind, s32 value, s32 flag);

void ee_metal_detector_spawn_c(void *spot, void *pos, void *dir, s32 kind, s32 value, s32 flag, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_002D4398, EE_SITE_DETECTOR_SPAWN);
    SpawnBolt spawn = LVL(L, func_L00_002A86D8, SpawnBolt);
    if (g_completes > 0 && value == 5) {
        spawn(spot, pos, dir, kind, 50, flag);
        value = 50;
    }
    spawn(spot, pos, dir, kind, value, flag);
}
