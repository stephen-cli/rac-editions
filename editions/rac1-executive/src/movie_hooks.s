/* Entry points for the Sound page's Movie volume row (src/movie_volume.c). The
 * page's widget calls its input and draw through data pointers, so the return
 * address does not name the level: tools/overlay_patch.py (feature
 * movie_volume) points each level's widget at that level's stubs, which pass
 * the level in $5 after the widget in $4, like src/qs_hooks.s. */
.set noreorder
.text

.extern ee_sound_input_c
.extern ee_sound_draw_c

.globl ee_sound_input_00
ee_sound_input_00:
    j     ee_sound_input_c
    addiu $5, $0, 0
.globl ee_sound_input_01
ee_sound_input_01:
    j     ee_sound_input_c
    addiu $5, $0, 1
.globl ee_sound_input_02
ee_sound_input_02:
    j     ee_sound_input_c
    addiu $5, $0, 2
.globl ee_sound_input_03
ee_sound_input_03:
    j     ee_sound_input_c
    addiu $5, $0, 3
.globl ee_sound_input_04
ee_sound_input_04:
    j     ee_sound_input_c
    addiu $5, $0, 4
.globl ee_sound_input_05
ee_sound_input_05:
    j     ee_sound_input_c
    addiu $5, $0, 5
.globl ee_sound_input_06
ee_sound_input_06:
    j     ee_sound_input_c
    addiu $5, $0, 6
.globl ee_sound_input_07
ee_sound_input_07:
    j     ee_sound_input_c
    addiu $5, $0, 7
.globl ee_sound_input_08
ee_sound_input_08:
    j     ee_sound_input_c
    addiu $5, $0, 8
.globl ee_sound_input_09
ee_sound_input_09:
    j     ee_sound_input_c
    addiu $5, $0, 9
.globl ee_sound_input_10
ee_sound_input_10:
    j     ee_sound_input_c
    addiu $5, $0, 10
.globl ee_sound_input_11
ee_sound_input_11:
    j     ee_sound_input_c
    addiu $5, $0, 11
.globl ee_sound_input_12
ee_sound_input_12:
    j     ee_sound_input_c
    addiu $5, $0, 12
.globl ee_sound_input_13
ee_sound_input_13:
    j     ee_sound_input_c
    addiu $5, $0, 13
.globl ee_sound_input_14
ee_sound_input_14:
    j     ee_sound_input_c
    addiu $5, $0, 14
.globl ee_sound_input_15
ee_sound_input_15:
    j     ee_sound_input_c
    addiu $5, $0, 15
.globl ee_sound_input_16
ee_sound_input_16:
    j     ee_sound_input_c
    addiu $5, $0, 16
.globl ee_sound_input_17
ee_sound_input_17:
    j     ee_sound_input_c
    addiu $5, $0, 17
.globl ee_sound_input_18
ee_sound_input_18:
    j     ee_sound_input_c
    addiu $5, $0, 18

.globl ee_sound_draw_00
ee_sound_draw_00:
    j     ee_sound_draw_c
    addiu $5, $0, 0
.globl ee_sound_draw_01
ee_sound_draw_01:
    j     ee_sound_draw_c
    addiu $5, $0, 1
.globl ee_sound_draw_02
ee_sound_draw_02:
    j     ee_sound_draw_c
    addiu $5, $0, 2
.globl ee_sound_draw_03
ee_sound_draw_03:
    j     ee_sound_draw_c
    addiu $5, $0, 3
.globl ee_sound_draw_04
ee_sound_draw_04:
    j     ee_sound_draw_c
    addiu $5, $0, 4
.globl ee_sound_draw_05
ee_sound_draw_05:
    j     ee_sound_draw_c
    addiu $5, $0, 5
.globl ee_sound_draw_06
ee_sound_draw_06:
    j     ee_sound_draw_c
    addiu $5, $0, 6
.globl ee_sound_draw_07
ee_sound_draw_07:
    j     ee_sound_draw_c
    addiu $5, $0, 7
.globl ee_sound_draw_08
ee_sound_draw_08:
    j     ee_sound_draw_c
    addiu $5, $0, 8
.globl ee_sound_draw_09
ee_sound_draw_09:
    j     ee_sound_draw_c
    addiu $5, $0, 9
.globl ee_sound_draw_10
ee_sound_draw_10:
    j     ee_sound_draw_c
    addiu $5, $0, 10
.globl ee_sound_draw_11
ee_sound_draw_11:
    j     ee_sound_draw_c
    addiu $5, $0, 11
.globl ee_sound_draw_12
ee_sound_draw_12:
    j     ee_sound_draw_c
    addiu $5, $0, 12
.globl ee_sound_draw_13
ee_sound_draw_13:
    j     ee_sound_draw_c
    addiu $5, $0, 13
.globl ee_sound_draw_14
ee_sound_draw_14:
    j     ee_sound_draw_c
    addiu $5, $0, 14
.globl ee_sound_draw_15
ee_sound_draw_15:
    j     ee_sound_draw_c
    addiu $5, $0, 15
.globl ee_sound_draw_16
ee_sound_draw_16:
    j     ee_sound_draw_c
    addiu $5, $0, 16
.globl ee_sound_draw_17
ee_sound_draw_17:
    j     ee_sound_draw_c
    addiu $5, $0, 17
.globl ee_sound_draw_18
ee_sound_draw_18:
    j     ee_sound_draw_c
    addiu $5, $0, 18
