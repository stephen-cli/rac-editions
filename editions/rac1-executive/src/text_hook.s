/* Entry of every level's msg_string (func_001FE540), patched to jump here
 * with `addiu $1, $0, LEVEL` in the jump's delay slot (tools/overlay_patch.py
 * edition_text): $1 ($at, free at a function's entry) names the level whose
 * program is running; the game's current-level variable is not used (see
 * modseg.h). Edition ids tail-call the C lookup; anything else re-executes
 * the two prologue instructions the patch replaced (addiu $sp,-0x10;
 * sq $ra,0($sp)) and continues at that level's msg_string + 8, with $a0 ($4)
 * and $ra ($31) untouched, except that the return comes back here first:
 * msg_string returns to redact_return, which hands the string, the id and
 * the program to ee_rz_redact_c (src/randomizer_play.c: a Randomizer save's
 * givers name their item as "???") and returns its result to the caller.
 * The caller's $ra and the id wait in ee_redact_saved meanwhile (msg_string
 * is never re-entered). The title screen's two programs are patched the
 * same way with ids 19 and 20 (FE_BOOT, FE_BIN; tools/gen_frontend.py), the
 * table's last two entries. Numeric register names: the SN assembler takes
 * no aliases. */
.set noreorder
.set noat
.text
.globl ee_msg_string_hook
.extern ee_msg_string_edition
.extern ee_text_first
.extern ee_text_count
.extern lvl_func_001FE540
.extern ee_debug_msg_watch
.extern ee_rz_redact_c
.extern ee_redact_saved
ee_msg_string_hook:
    lui   $3, %hi(ee_debug_msg_watch)  /* the debug watch (src/text.c) */
    addiu $3, $3, %lo(ee_debug_msg_watch)
    lw    $2, 0($3)
    subu  $2, $4, $2
    lw    $5, 4($3)
    sltu  $2, $2, $5
    beqz  $2, watched
    nop
    sw    $31, 8($3)
    sw    $1, 12($3)
    sw    $4, 16($3)
watched:
    lui   $2, %hi(ee_text_first)
    lw    $2, %lo(ee_text_first)($2)
    lui   $3, %hi(ee_text_count)
    lw    $3, %lo(ee_text_count)($3)
    subu  $2, $4, $2               /* id - first */
    sltu  $2, $2, $3               /* (unsigned)(id - first) < count */
    bnez  $2, edition
    nop
    lui   $3, %hi(lvl_func_001FE540)
    sll   $2, $1, 2                /* the program's id from the delay slot */
    addu  $3, $3, $2
    lw    $3, %lo(lvl_func_001FE540)($3)
    lui   $2, %hi(ee_redact_saved)
    addiu $2, $2, %lo(ee_redact_saved)
    sw    $31, 0($2)               /* the caller's return address, the id and the program */
    sw    $4, 4($2)
    sw    $1, 8($2)
    lui   $31, %hi(redact_return)
    addiu $31, $31, %lo(redact_return)
    addiu $29, $29, -0x10          /* the replaced prologue; msg_string saves our $ra and returns to it */
    sq    $31, 0($29)
    addiu $3, $3, 8
    jr    $3
    nop
redact_return:
    daddu $4, $2, $0               /* the string msg_string returned */
    lui   $3, %hi(ee_redact_saved)
    addiu $3, $3, %lo(ee_redact_saved)
    lw    $5, 4($3)                /* ee_rz_redact_c(string, id, program) */
    lw    $6, 8($3)
    addiu $29, $29, -0x10
    jal   ee_rz_redact_c
    nop
    addiu $29, $29, 0x10
    lui   $3, %hi(ee_redact_saved)
    addiu $3, $3, %lo(ee_redact_saved)
    lw    $31, 0($3)
    jr    $31
    nop
edition:
    j     ee_msg_string_edition
    nop
