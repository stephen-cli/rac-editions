/* rz2/rz6: the title screen's Goodies row (src/goodies.c). Numeric register
 * names: the SN assembler takes no aliases. */
.set noreorder
.set noat
.text

/* Pause_Update's call of Pause_Open (func_0021A1A0 + 0x120, jal func_00219E90)
 * in the boot executable and in frontbin (tools/gen_frontend.py) goes to the
 * stub of its program, which passes the program's id. */
.globl ee_title_menu_open_19
.extern ee_title_menu_open_c
ee_title_menu_open_19:
    j     ee_title_menu_open_c
    addiu $4, $0, 19

.globl ee_title_menu_open_20
ee_title_menu_open_20:
    j     ee_title_menu_open_c
    addiu $4, $0, 20

/* The title screen's main loop's call of MemCard_UpdateState
 * (func_001EBB48 + 0x248 in the executable, jal func_00209E68), the same way. */
.globl ee_title_card_19
.extern ee_title_card_c
ee_title_card_19:
    j     ee_title_card_c
    addiu $4, $0, 19

.globl ee_title_card_20
ee_title_card_20:
    j     ee_title_card_c
    addiu $4, $0, 20
