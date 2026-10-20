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

/* The mode-0 frame's pause entry, func_00219C70(arg) at func_L00_00299250
 * + 0x36C and + 0x434 (record 36 back to the Charge Boots first): the
 * return address goes in $5. */
.globl ee_pause_entry_hook
.extern ee_pause_entry_c
ee_pause_entry_hook:
    j     ee_pause_entry_c
    addu  $5, $31, $0

/* memcard_MakeWholeSave(out) at its callers (src/charge_boots.c, ee_save_c:
 * the save written without the Charge Boots): the return address goes in
 * $5 and $6 = -1 (find the level from it); Veldin 2's ending save, a
 * level-only caller (func_L18_00282F48 + 0x1C), passes its level instead. */
.globl ee_save_hook
.extern ee_save_c
ee_save_hook:
    addiu $6, $0, -1
    j     ee_save_c
    addu  $5, $31, $0

.globl ee_save_hook_l18
ee_save_hook_l18:
    addiu $6, $0, 18
    j     ee_save_c
    addu  $5, $31, $0

/* MobyFreeSlotBookkeeping() at the start of the mode-0 frame (the
 * edition's sequences kept, the Charge Boots cheat): no arguments, the
 * return address goes in $4. */
.globl ee_frame_start_hook
.extern ee_frame_start_c
ee_frame_start_hook:
    j     ee_frame_start_c
    addu  $4, $31, $0

/* slot_item(slot) in the boots predicate (func_L00_0020DBD8 + 8): one
 * integer argument, the return address goes in $5 (src/charge_boots.c). */
.globl ee_boots_pose_item_hook
.extern ee_boots_pose_item_c
ee_boots_pose_item_hook:
    j     ee_boots_pose_item_c
    addu  $5, $31, $0

/* The Goodies Cheats page's list widget "enter" callback (the row list's
 * builder, func_0021B138), one stub per level: the callback is reached
 * through a data pointer, so the return address does not name the level;
 * the level goes in $8, past the callback's four arguments
 * (src/charge_boots.c). */
.extern ee_cheats_enter_c
.globl ee_cheats_enter_00
ee_cheats_enter_00:
    j     ee_cheats_enter_c
    addiu $8, $0, 0
.globl ee_cheats_enter_01
ee_cheats_enter_01:
    j     ee_cheats_enter_c
    addiu $8, $0, 1
.globl ee_cheats_enter_02
ee_cheats_enter_02:
    j     ee_cheats_enter_c
    addiu $8, $0, 2
.globl ee_cheats_enter_03
ee_cheats_enter_03:
    j     ee_cheats_enter_c
    addiu $8, $0, 3
.globl ee_cheats_enter_04
ee_cheats_enter_04:
    j     ee_cheats_enter_c
    addiu $8, $0, 4
.globl ee_cheats_enter_05
ee_cheats_enter_05:
    j     ee_cheats_enter_c
    addiu $8, $0, 5
.globl ee_cheats_enter_06
ee_cheats_enter_06:
    j     ee_cheats_enter_c
    addiu $8, $0, 6
.globl ee_cheats_enter_07
ee_cheats_enter_07:
    j     ee_cheats_enter_c
    addiu $8, $0, 7
.globl ee_cheats_enter_08
ee_cheats_enter_08:
    j     ee_cheats_enter_c
    addiu $8, $0, 8
.globl ee_cheats_enter_09
ee_cheats_enter_09:
    j     ee_cheats_enter_c
    addiu $8, $0, 9
.globl ee_cheats_enter_10
ee_cheats_enter_10:
    j     ee_cheats_enter_c
    addiu $8, $0, 10
.globl ee_cheats_enter_11
ee_cheats_enter_11:
    j     ee_cheats_enter_c
    addiu $8, $0, 11
.globl ee_cheats_enter_12
ee_cheats_enter_12:
    j     ee_cheats_enter_c
    addiu $8, $0, 12
.globl ee_cheats_enter_13
ee_cheats_enter_13:
    j     ee_cheats_enter_c
    addiu $8, $0, 13
.globl ee_cheats_enter_14
ee_cheats_enter_14:
    j     ee_cheats_enter_c
    addiu $8, $0, 14
.globl ee_cheats_enter_15
ee_cheats_enter_15:
    j     ee_cheats_enter_c
    addiu $8, $0, 15
.globl ee_cheats_enter_16
ee_cheats_enter_16:
    j     ee_cheats_enter_c
    addiu $8, $0, 16
.globl ee_cheats_enter_17
ee_cheats_enter_17:
    j     ee_cheats_enter_c
    addiu $8, $0, 17
.globl ee_cheats_enter_18
ee_cheats_enter_18:
    j     ee_cheats_enter_c
    addiu $8, $0, 18

/* The same page's "update" callback (update_menu_option_list, func_00221E60,
 * called every frame the page is open; Cross toggles a row there), one stub
 * per level: one argument, so the level goes in $5 (src/charge_boots.c). */
.extern ee_cheats_update_c
.globl ee_cheats_update_00
ee_cheats_update_00:
    j     ee_cheats_update_c
    addiu $5, $0, 0
.globl ee_cheats_update_01
ee_cheats_update_01:
    j     ee_cheats_update_c
    addiu $5, $0, 1
.globl ee_cheats_update_02
ee_cheats_update_02:
    j     ee_cheats_update_c
    addiu $5, $0, 2
.globl ee_cheats_update_03
ee_cheats_update_03:
    j     ee_cheats_update_c
    addiu $5, $0, 3
.globl ee_cheats_update_04
ee_cheats_update_04:
    j     ee_cheats_update_c
    addiu $5, $0, 4
.globl ee_cheats_update_05
ee_cheats_update_05:
    j     ee_cheats_update_c
    addiu $5, $0, 5
.globl ee_cheats_update_06
ee_cheats_update_06:
    j     ee_cheats_update_c
    addiu $5, $0, 6
.globl ee_cheats_update_07
ee_cheats_update_07:
    j     ee_cheats_update_c
    addiu $5, $0, 7
.globl ee_cheats_update_08
ee_cheats_update_08:
    j     ee_cheats_update_c
    addiu $5, $0, 8
.globl ee_cheats_update_09
ee_cheats_update_09:
    j     ee_cheats_update_c
    addiu $5, $0, 9
.globl ee_cheats_update_10
ee_cheats_update_10:
    j     ee_cheats_update_c
    addiu $5, $0, 10
.globl ee_cheats_update_11
ee_cheats_update_11:
    j     ee_cheats_update_c
    addiu $5, $0, 11
.globl ee_cheats_update_12
ee_cheats_update_12:
    j     ee_cheats_update_c
    addiu $5, $0, 12
.globl ee_cheats_update_13
ee_cheats_update_13:
    j     ee_cheats_update_c
    addiu $5, $0, 13
.globl ee_cheats_update_14
ee_cheats_update_14:
    j     ee_cheats_update_c
    addiu $5, $0, 14
.globl ee_cheats_update_15
ee_cheats_update_15:
    j     ee_cheats_update_c
    addiu $5, $0, 15
.globl ee_cheats_update_16
ee_cheats_update_16:
    j     ee_cheats_update_c
    addiu $5, $0, 16
.globl ee_cheats_update_17
ee_cheats_update_17:
    j     ee_cheats_update_c
    addiu $5, $0, 17
.globl ee_cheats_update_18
ee_cheats_update_18:
    j     ee_cheats_update_c
    addiu $5, $0, 18
