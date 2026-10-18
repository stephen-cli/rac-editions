/* Entry points for the quick-select pages (src/quick_select_pages.c), the
 * targets tools/overlay_patch.py (feature quick_select_pages) writes at the
 * sites tools/gen_qs.py finds. Like src/hooks.s: each passes one more
 * argument and jumps to its C hook, which returns straight to the game. */
.set noreorder
.text

/* The item-acquisition function's empty-slot store: the slot index found
 * is in $4, the item in $16; the item goes in $5. The words replaced were
 * the index test and the store, so nothing after the call reads $2-$5. */
.globl ee_qs_autoslot
.extern ee_qs_autoslot_c
ee_qs_autoslot:
    j     ee_qs_autoslot_c
    addu  $5, $16, $0

/* The save-state reset call in the challenge-mode carry-over: no arguments,
 * the return address (which names the level) goes in $4. */
.globl ee_qs_carry
.extern ee_qs_carry_hook_c
ee_qs_carry:
    j     ee_qs_carry_hook_c
    addu  $4, $31, $0

/* The pause menu's ring widget callbacks are reached through data pointers,
 * so the return address does not name the level: one stub per level passes
 * its own in $5, after the widget state in $4. */
.extern ee_qs_pause_update_c
.extern ee_qs_pause_draw_c

.globl ee_qs_pause_update_00
ee_qs_pause_update_00:
    j     ee_qs_pause_update_c
    addiu $5, $0, 0
.globl ee_qs_pause_update_01
ee_qs_pause_update_01:
    j     ee_qs_pause_update_c
    addiu $5, $0, 1
.globl ee_qs_pause_update_02
ee_qs_pause_update_02:
    j     ee_qs_pause_update_c
    addiu $5, $0, 2
.globl ee_qs_pause_update_03
ee_qs_pause_update_03:
    j     ee_qs_pause_update_c
    addiu $5, $0, 3
.globl ee_qs_pause_update_04
ee_qs_pause_update_04:
    j     ee_qs_pause_update_c
    addiu $5, $0, 4
.globl ee_qs_pause_update_05
ee_qs_pause_update_05:
    j     ee_qs_pause_update_c
    addiu $5, $0, 5
.globl ee_qs_pause_update_06
ee_qs_pause_update_06:
    j     ee_qs_pause_update_c
    addiu $5, $0, 6
.globl ee_qs_pause_update_07
ee_qs_pause_update_07:
    j     ee_qs_pause_update_c
    addiu $5, $0, 7
.globl ee_qs_pause_update_08
ee_qs_pause_update_08:
    j     ee_qs_pause_update_c
    addiu $5, $0, 8
.globl ee_qs_pause_update_09
ee_qs_pause_update_09:
    j     ee_qs_pause_update_c
    addiu $5, $0, 9
.globl ee_qs_pause_update_10
ee_qs_pause_update_10:
    j     ee_qs_pause_update_c
    addiu $5, $0, 10
.globl ee_qs_pause_update_11
ee_qs_pause_update_11:
    j     ee_qs_pause_update_c
    addiu $5, $0, 11
.globl ee_qs_pause_update_12
ee_qs_pause_update_12:
    j     ee_qs_pause_update_c
    addiu $5, $0, 12
.globl ee_qs_pause_update_13
ee_qs_pause_update_13:
    j     ee_qs_pause_update_c
    addiu $5, $0, 13
.globl ee_qs_pause_update_14
ee_qs_pause_update_14:
    j     ee_qs_pause_update_c
    addiu $5, $0, 14
.globl ee_qs_pause_update_15
ee_qs_pause_update_15:
    j     ee_qs_pause_update_c
    addiu $5, $0, 15
.globl ee_qs_pause_update_16
ee_qs_pause_update_16:
    j     ee_qs_pause_update_c
    addiu $5, $0, 16
.globl ee_qs_pause_update_17
ee_qs_pause_update_17:
    j     ee_qs_pause_update_c
    addiu $5, $0, 17
.globl ee_qs_pause_update_18
ee_qs_pause_update_18:
    j     ee_qs_pause_update_c
    addiu $5, $0, 18

.globl ee_qs_pause_draw_00
ee_qs_pause_draw_00:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 0
.globl ee_qs_pause_draw_01
ee_qs_pause_draw_01:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 1
.globl ee_qs_pause_draw_02
ee_qs_pause_draw_02:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 2
.globl ee_qs_pause_draw_03
ee_qs_pause_draw_03:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 3
.globl ee_qs_pause_draw_04
ee_qs_pause_draw_04:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 4
.globl ee_qs_pause_draw_05
ee_qs_pause_draw_05:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 5
.globl ee_qs_pause_draw_06
ee_qs_pause_draw_06:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 6
.globl ee_qs_pause_draw_07
ee_qs_pause_draw_07:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 7
.globl ee_qs_pause_draw_08
ee_qs_pause_draw_08:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 8
.globl ee_qs_pause_draw_09
ee_qs_pause_draw_09:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 9
.globl ee_qs_pause_draw_10
ee_qs_pause_draw_10:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 10
.globl ee_qs_pause_draw_11
ee_qs_pause_draw_11:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 11
.globl ee_qs_pause_draw_12
ee_qs_pause_draw_12:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 12
.globl ee_qs_pause_draw_13
ee_qs_pause_draw_13:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 13
.globl ee_qs_pause_draw_14
ee_qs_pause_draw_14:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 14
.globl ee_qs_pause_draw_15
ee_qs_pause_draw_15:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 15
.globl ee_qs_pause_draw_16
ee_qs_pause_draw_16:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 16
.globl ee_qs_pause_draw_17
ee_qs_pause_draw_17:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 17
.globl ee_qs_pause_draw_18
ee_qs_pause_draw_18:
    j     ee_qs_pause_draw_c
    addiu $5, $0, 18
