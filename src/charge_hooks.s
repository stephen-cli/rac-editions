/* Entry points for the Charge Boots dash (src/charge_dash.c), the jal
 * targets tools/overlay_patch.py (feature charge_dash) writes at the sites
 * tools/gen_charge.py finds. Like src/hooks.s: each passes the return
 * address, which identifies the running level, as one more argument and
 * jumps to its C hook, which returns straight to the level. */
.set noreorder
.text

/* slot_item(slot) in the boots predicate (func_L00_0020DBD8 + 8): one
 * integer argument, the return address goes in $5 (src/charge_boots.c). */
.globl ee_boots_pose_item_hook
.extern ee_boots_pose_item_c
ee_boots_pose_item_hook:
    j     ee_boots_pose_item_c
    addu  $5, $31, $0
