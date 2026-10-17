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

/* pad parse(pad, report, length): three integer arguments, $4-$6; the
 * return address goes in $7. (A first build passed it in $5 and so fed
 * the parser the return address as its report buffer: every input dead.) */
.globl ee_pad_parse
.extern ee_pad_parse_c
ee_pad_parse:
    j     ee_pad_parse_c
    addu  $7, $31, $0

/* turn_to(mode, accel, max_speed, rate): mode in $4, the floats in
 * $f12-$f14; the return address goes in $5 (second integer argument). */
.globl ee_strafe_turn
.extern ee_strafe_turn_c
ee_strafe_turn:
    j     ee_strafe_turn_c
    addu  $5, $31, $0

/* pivot turn(): no arguments, the return address goes in $4. */
.globl ee_strafe_pivot
.extern ee_strafe_pivot_c
ee_strafe_pivot:
    j     ee_strafe_pivot_c
    addu  $4, $31, $0

/* set_planar_vel(yaw): one float in $f12, the return address goes in $4. */
.globl ee_strafe_planar
.extern ee_strafe_planar_c
ee_strafe_planar:
    j     ee_strafe_planar_c
    addu  $4, $31, $0

/* The jump family's turn_to and set_planar_vel (same signatures as above). */
.globl ee_jump_turn
.extern ee_jump_turn_c
ee_jump_turn:
    j     ee_jump_turn_c
    addu  $5, $31, $0

.globl ee_jump_planar
.extern ee_jump_planar_c
ee_jump_planar:
    j     ee_jump_planar_c
    addu  $4, $31, $0

/* The air control's turn_to(mode, accel, max_speed, rate). */
.globl ee_air_turn
.extern ee_air_turn_c
ee_air_turn:
    j     ee_air_turn_c
    addu  $5, $31, $0

/* jump press handler(window, no_plain_jump): two integer arguments, the
 * return address goes in $6; the C hook's result goes back in $2. */
.globl ee_jump_press
.extern ee_jump_press_c
ee_jump_press:
    j     ee_jump_press_c
    addu  $6, $31, $0

/* lean helper() and run animation updater(): no arguments, the return
 * address goes in $4. */
.globl ee_twist
.extern ee_twist_c
ee_twist:
    j     ee_twist_c
    addu  $4, $31, $0

.globl ee_run_anim
.extern ee_run_anim_c
ee_run_anim:
    j     ee_run_anim_c
    addu  $4, $31, $0

/* approach_value(target, step, &var): two floats in $f12/$f13 and the
 * pointer in $4; the return address goes in $5. */
.globl ee_flip_approach
.extern ee_flip_approach_c
ee_flip_approach:
    j     ee_flip_approach_c
    addu  $5, $31, $0

/* hero_set_state(state, with_animation): two integer arguments, the
 * return address goes in $6; the result goes back in $2. */
.globl ee_stop
.extern ee_stop_c
ee_stop:
    j     ee_stop_c
    addu  $6, $31, $0

/* bolt spawner(spot, pos, dir, kind, value, flag): six arguments, $4-$9;
 * the return address goes in $10. */
.globl ee_metal_detector_spawn
.extern ee_metal_detector_spawn_c
ee_metal_detector_spawn:
    j     ee_metal_detector_spawn_c
    addu  $10, $31, $0
