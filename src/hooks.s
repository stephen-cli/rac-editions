/* Entry points tools/overlay_patch.py redirects level call sites to (jal).
 * Each passes the return address, which identifies the running level, as
 * one more argument to its C hook (src/level.c ee_level_of_call) and jumps
 * there; $31 is untouched, so the C hook returns straight to the level.
 * Argument registers on the EE: $4-$11. Numeric names: the SN assembler
 * takes no aliases. */
.set noreorder
.text

/* HudUpdate(int): one argument, so the return address goes in $5. */
.globl ee_hud_update_hook
.extern ee_hud_update_hook_c
ee_hud_update_hook:
    j     ee_hud_update_hook_c
    addu  $5, $31, $0

/* speed_step(accel, decel): two float arguments in $f12/$f13 and no
 * integer ones, so the return address goes in $4 (the compiler assigns
 * integer and float argument registers independently; checked). */
.globl ee_comet_speed_step
.extern ee_comet_speed_step_c
ee_comet_speed_step:
    j     ee_comet_speed_step_c
    addu  $4, $31, $0

/* bolt spawner(spot, pos, dir, kind, value, flag): six arguments, $4-$9;
 * the return address goes in $10. */
.globl ee_metal_detector_spawn
.extern ee_metal_detector_spawn_c
ee_metal_detector_spawn:
    j     ee_metal_detector_spawn_c
    addu  $10, $31, $0
