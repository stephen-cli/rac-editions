/* Entry points for the Charge Boots dash (src/charge_dash.c), the jal
 * targets tools/overlay_patch.py (feature charge_dash) writes at the sites
 * tools/gen_charge.py finds. Like src/hooks.s: each passes the return
 * address, which identifies the running level, as one more argument and
 * jumps to its C hook, which returns straight to the level. */
.set noreorder
.text

/* slot_item(slot) at the input handler's Thruster-Pack test: one integer
 * argument, so the return address goes in $5; the result goes back in $2. */
.globl ee_charge_trigger_hook
.extern ee_charge_trigger_c
ee_charge_trigger_hook:
    j     ee_charge_trigger_c
    addu  $5, $31, $0

/* The per-state movement function: no arguments, the return address goes
 * in $4. */
.globl ee_charge_move_hook
.extern ee_charge_move_c
ee_charge_move_hook:
    j     ee_charge_move_c
    addu  $4, $31, $0

/* collect_mobies_in_sphere(pos, mask, self, hit, radius) in the follow
 * camera overrides: four integer arguments and a float, so the return
 * address goes in $8, the fifth integer argument; the result goes back in
 * $2. */
.globl ee_charge_camera_hook
.extern ee_charge_camera_c
ee_charge_camera_hook:
    j     ee_charge_camera_c
    addu  $8, $31, $0

/* MobyFreeSlotBookkeeping() at the start of the mode-0 frame (the
 * edition's sequences kept, the Charge Boots cheat): no arguments, the
 * return address goes in $4. */
.globl ee_frame_start_hook
.extern ee_frame_start_c
ee_frame_start_hook:
    j     ee_frame_start_c
    addu  $4, $31, $0

/* ShowBanner(text, time) in the cheat detector: two integer arguments, the
 * return address goes in $6 (src/charge_boots.c). */
.globl ee_charge_banner_hook
.extern ee_charge_banner_c
ee_charge_banner_hook:
    j     ee_charge_banner_c
    addu  $6, $31, $0

/* slot_item(slot) in the boots predicate (func_L00_0020DBD8 + 8): one
 * integer argument, the return address goes in $5 (src/charge_boots.c). */
.globl ee_boots_pose_item_hook
.extern ee_boots_pose_item_c
ee_boots_pose_item_hook:
    j     ee_boots_pose_item_c
    addu  $5, $31, $0
