/* x4: the Metal Detector pays 10x in challenge mode, and 20x in a Randomizer save.
 *
 * The detector's update function (class 585, func_L00_002D4398) drains a
 * hidden spot's bolt count in steps of 5, spawning one bolt per step with
 * func_L00_002A86D8(spot, pos, dir, 0x13, value, 0). The spawner knows the
 * denominations 1-5, 20 and 50 (each its own bolt model), so in a
 * challenge-mode run each 5-bolt hexnut becomes a 50-bolt circlenut: exactly
 * 10x, 174,600 bolts over the 358 spots of the 19 levels. The stashes pay
 * the same whatever the bolt multiplier (x1, src/bolt_multiplier.c) stands
 * at: that pays on enemy kills only, so a hit never makes digging worth
 * less. A Randomizer save (rz3) pays 20x, two circlenuts a step (349,200):
 * it is no challenge-mode run, but its shuffled items leave the RYNO and
 * the Gold Weapons to buy with bolts found along the way, the spots are its
 * grind, and with few planets open early it needs the higher rate. Kind 0x13
 * gives every bolt its own random +-30 degree throw, so two scatter; with no
 * moby free the spawner adds the value to the bolt count instead, so none is
 * lost. Spots stay drained for the run (per-level save block at 0x14C010)
 * and refill only with the next challenge-mode reset, so this cannot be
 * farmed. tools/overlay_patch.py redirects that one call here in all 19
 * levels (through src/hooks.s, which adds the return address that names the
 * running level). */
#include "modseg.h"

DECLARE_LVL(func_L00_002D4398);        /* the detector's update: the hooked call site is in it */
DECLARE_LVL(func_L00_002A86D8);        /* the bolt spawner */
typedef void (*SpawnBolt)(void *spot, void *pos, void *dir, s32 kind, s32 value, s32 flag);

#define STEP_BOLTS      5               /* retail: a hexnut per step */
#define CIRCLENUT       50

void ee_metal_detector_spawn_c(void *spot, void *pos, void *dir, s32 kind, s32 value, s32 flag, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_002D4398, EE_SITE_DETECTOR_SPAWN);
    SpawnBolt spawn = LVL(L, func_L00_002A86D8, SpawnBolt);
    if (value == STEP_BOLTS && ee_rz_active()) {
        spawn(spot, pos, dir, kind, CIRCLENUT, flag);
        value = CIRCLENUT;
    } else if (value == STEP_BOLTS && g_completes > 0) {
        value = CIRCLENUT;
    }
    spawn(spot, pos, dir, kind, value, flag);
}
