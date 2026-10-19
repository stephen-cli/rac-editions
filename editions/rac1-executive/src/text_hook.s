/* Entry of every level's msg_string (func_001FE540), patched to jump here
 * with `addiu $1, $0, LEVEL` in the jump's delay slot (tools/overlay_patch.py
 * edition_text): $1 ($at, free at a function's entry) names the level whose
 * program is running; the game's current-level variable is not used (see
 * modseg.h). Edition ids tail-call the C lookup; anything else re-executes
 * the two prologue instructions the patch replaced (addiu $sp,-0x10;
 * sq $ra,0($sp)) and continues at that level's msg_string + 8, with $a0 ($4)
 * and $ra ($31) untouched. Numeric register names: the SN assembler takes
 * no aliases. */
.set noreorder
.set noat
.text
.globl ee_msg_string_hook
.extern ee_msg_string_edition
.extern ee_text_first
.extern ee_text_count
.extern lvl_func_001FE540
ee_msg_string_hook:
    lui   $2, %hi(ee_text_first)
    lw    $2, %lo(ee_text_first)($2)
    lui   $3, %hi(ee_text_count)
    lw    $3, %lo(ee_text_count)($3)
    subu  $2, $4, $2               /* id - first */
    sltu  $2, $2, $3               /* (unsigned)(id - first) < count */
    bnez  $2, edition
    nop
    lui   $3, %hi(lvl_func_001FE540)
    sll   $2, $1, 2                /* the level id from the delay slot */
    addu  $3, $3, $2
    lw    $3, %lo(lvl_func_001FE540)($3)
    addiu $29, $29, -0x10          /* the replaced prologue */
    sq    $31, 0($29)
    addiu $3, $3, 8
    jr    $3
    nop
edition:
    j     ee_msg_string_edition
    nop
