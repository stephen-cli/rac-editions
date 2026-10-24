/* Entry points for the Randomizer's hooks (src/randomizer_play.c), one per
 * level for each hooked call, so that the C hook knows which level program
 * made the call: tools/overlay_patch.py (feature randomizer) sends level L's
 * call sites to the stubs numbered L. Each passes L in the first free
 * argument register after the call's own arguments ($4-$7), and the moby
 * update loop's stub passes the update function the loop was about to call
 * ($3, the jalr's register) as well. Written by a generator, kept as source.
 * Numeric register names: the SN assembler takes no aliases. */
.set noreorder
.text
.extern ee_rz_give_item_c
.globl ee_rz_give_item_00
ee_rz_give_item_00:
    j     ee_rz_give_item_c
    addiu $6, $0, 0
.globl ee_rz_give_item_01
ee_rz_give_item_01:
    j     ee_rz_give_item_c
    addiu $6, $0, 1
.globl ee_rz_give_item_02
ee_rz_give_item_02:
    j     ee_rz_give_item_c
    addiu $6, $0, 2
.globl ee_rz_give_item_03
ee_rz_give_item_03:
    j     ee_rz_give_item_c
    addiu $6, $0, 3
.globl ee_rz_give_item_04
ee_rz_give_item_04:
    j     ee_rz_give_item_c
    addiu $6, $0, 4
.globl ee_rz_give_item_05
ee_rz_give_item_05:
    j     ee_rz_give_item_c
    addiu $6, $0, 5
.globl ee_rz_give_item_06
ee_rz_give_item_06:
    j     ee_rz_give_item_c
    addiu $6, $0, 6
.globl ee_rz_give_item_07
ee_rz_give_item_07:
    j     ee_rz_give_item_c
    addiu $6, $0, 7
.globl ee_rz_give_item_08
ee_rz_give_item_08:
    j     ee_rz_give_item_c
    addiu $6, $0, 8
.globl ee_rz_give_item_09
ee_rz_give_item_09:
    j     ee_rz_give_item_c
    addiu $6, $0, 9
.globl ee_rz_give_item_10
ee_rz_give_item_10:
    j     ee_rz_give_item_c
    addiu $6, $0, 10
.globl ee_rz_give_item_11
ee_rz_give_item_11:
    j     ee_rz_give_item_c
    addiu $6, $0, 11
.globl ee_rz_give_item_12
ee_rz_give_item_12:
    j     ee_rz_give_item_c
    addiu $6, $0, 12
.globl ee_rz_give_item_13
ee_rz_give_item_13:
    j     ee_rz_give_item_c
    addiu $6, $0, 13
.globl ee_rz_give_item_14
ee_rz_give_item_14:
    j     ee_rz_give_item_c
    addiu $6, $0, 14
.globl ee_rz_give_item_15
ee_rz_give_item_15:
    j     ee_rz_give_item_c
    addiu $6, $0, 15
.globl ee_rz_give_item_16
ee_rz_give_item_16:
    j     ee_rz_give_item_c
    addiu $6, $0, 16
.globl ee_rz_give_item_17
ee_rz_give_item_17:
    j     ee_rz_give_item_c
    addiu $6, $0, 17
.globl ee_rz_give_item_18
ee_rz_give_item_18:
    j     ee_rz_give_item_c
    addiu $6, $0, 18
.extern ee_rz_item_banner_c
.globl ee_rz_item_banner_00
ee_rz_item_banner_00:
    j     ee_rz_item_banner_c
    addiu $6, $0, 0
.globl ee_rz_item_banner_01
ee_rz_item_banner_01:
    j     ee_rz_item_banner_c
    addiu $6, $0, 1
.globl ee_rz_item_banner_02
ee_rz_item_banner_02:
    j     ee_rz_item_banner_c
    addiu $6, $0, 2
.globl ee_rz_item_banner_03
ee_rz_item_banner_03:
    j     ee_rz_item_banner_c
    addiu $6, $0, 3
.globl ee_rz_item_banner_04
ee_rz_item_banner_04:
    j     ee_rz_item_banner_c
    addiu $6, $0, 4
.globl ee_rz_item_banner_05
ee_rz_item_banner_05:
    j     ee_rz_item_banner_c
    addiu $6, $0, 5
.globl ee_rz_item_banner_06
ee_rz_item_banner_06:
    j     ee_rz_item_banner_c
    addiu $6, $0, 6
.globl ee_rz_item_banner_07
ee_rz_item_banner_07:
    j     ee_rz_item_banner_c
    addiu $6, $0, 7
.globl ee_rz_item_banner_08
ee_rz_item_banner_08:
    j     ee_rz_item_banner_c
    addiu $6, $0, 8
.globl ee_rz_item_banner_09
ee_rz_item_banner_09:
    j     ee_rz_item_banner_c
    addiu $6, $0, 9
.globl ee_rz_item_banner_10
ee_rz_item_banner_10:
    j     ee_rz_item_banner_c
    addiu $6, $0, 10
.globl ee_rz_item_banner_11
ee_rz_item_banner_11:
    j     ee_rz_item_banner_c
    addiu $6, $0, 11
.globl ee_rz_item_banner_12
ee_rz_item_banner_12:
    j     ee_rz_item_banner_c
    addiu $6, $0, 12
.globl ee_rz_item_banner_13
ee_rz_item_banner_13:
    j     ee_rz_item_banner_c
    addiu $6, $0, 13
.globl ee_rz_item_banner_14
ee_rz_item_banner_14:
    j     ee_rz_item_banner_c
    addiu $6, $0, 14
.globl ee_rz_item_banner_15
ee_rz_item_banner_15:
    j     ee_rz_item_banner_c
    addiu $6, $0, 15
.globl ee_rz_item_banner_16
ee_rz_item_banner_16:
    j     ee_rz_item_banner_c
    addiu $6, $0, 16
.globl ee_rz_item_banner_17
ee_rz_item_banner_17:
    j     ee_rz_item_banner_c
    addiu $6, $0, 17
.globl ee_rz_item_banner_18
ee_rz_item_banner_18:
    j     ee_rz_item_banner_c
    addiu $6, $0, 18
.extern ee_rz_unlock_planet_c
.globl ee_rz_unlock_planet_00
ee_rz_unlock_planet_00:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 0
.globl ee_rz_unlock_planet_01
ee_rz_unlock_planet_01:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 1
.globl ee_rz_unlock_planet_02
ee_rz_unlock_planet_02:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 2
.globl ee_rz_unlock_planet_03
ee_rz_unlock_planet_03:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 3
.globl ee_rz_unlock_planet_04
ee_rz_unlock_planet_04:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 4
.globl ee_rz_unlock_planet_05
ee_rz_unlock_planet_05:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 5
.globl ee_rz_unlock_planet_06
ee_rz_unlock_planet_06:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 6
.globl ee_rz_unlock_planet_07
ee_rz_unlock_planet_07:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 7
.globl ee_rz_unlock_planet_08
ee_rz_unlock_planet_08:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 8
.globl ee_rz_unlock_planet_09
ee_rz_unlock_planet_09:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 9
.globl ee_rz_unlock_planet_10
ee_rz_unlock_planet_10:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 10
.globl ee_rz_unlock_planet_11
ee_rz_unlock_planet_11:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 11
.globl ee_rz_unlock_planet_12
ee_rz_unlock_planet_12:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 12
.globl ee_rz_unlock_planet_13
ee_rz_unlock_planet_13:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 13
.globl ee_rz_unlock_planet_14
ee_rz_unlock_planet_14:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 14
.globl ee_rz_unlock_planet_15
ee_rz_unlock_planet_15:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 15
.globl ee_rz_unlock_planet_16
ee_rz_unlock_planet_16:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 16
.globl ee_rz_unlock_planet_17
ee_rz_unlock_planet_17:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 17
.globl ee_rz_unlock_planet_18
ee_rz_unlock_planet_18:
    j     ee_rz_unlock_planet_c
    addiu $5, $0, 18
.extern ee_rz_planet_banner_c
.globl ee_rz_planet_banner_00
ee_rz_planet_banner_00:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 0
.globl ee_rz_planet_banner_01
ee_rz_planet_banner_01:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 1
.globl ee_rz_planet_banner_02
ee_rz_planet_banner_02:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 2
.globl ee_rz_planet_banner_03
ee_rz_planet_banner_03:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 3
.globl ee_rz_planet_banner_04
ee_rz_planet_banner_04:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 4
.globl ee_rz_planet_banner_05
ee_rz_planet_banner_05:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 5
.globl ee_rz_planet_banner_06
ee_rz_planet_banner_06:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 6
.globl ee_rz_planet_banner_07
ee_rz_planet_banner_07:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 7
.globl ee_rz_planet_banner_08
ee_rz_planet_banner_08:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 8
.globl ee_rz_planet_banner_09
ee_rz_planet_banner_09:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 9
.globl ee_rz_planet_banner_10
ee_rz_planet_banner_10:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 10
.globl ee_rz_planet_banner_11
ee_rz_planet_banner_11:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 11
.globl ee_rz_planet_banner_12
ee_rz_planet_banner_12:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 12
.globl ee_rz_planet_banner_13
ee_rz_planet_banner_13:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 13
.globl ee_rz_planet_banner_14
ee_rz_planet_banner_14:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 14
.globl ee_rz_planet_banner_15
ee_rz_planet_banner_15:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 15
.globl ee_rz_planet_banner_16
ee_rz_planet_banner_16:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 16
.globl ee_rz_planet_banner_17
ee_rz_planet_banner_17:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 17
.globl ee_rz_planet_banner_18
ee_rz_planet_banner_18:
    j     ee_rz_planet_banner_c
    addiu $5, $0, 18
.extern ee_rz_save_c
.globl ee_rz_save_00
ee_rz_save_00:
    j     ee_rz_save_c
    addiu $6, $0, 0
.globl ee_rz_save_01
ee_rz_save_01:
    j     ee_rz_save_c
    addiu $6, $0, 1
.globl ee_rz_save_02
ee_rz_save_02:
    j     ee_rz_save_c
    addiu $6, $0, 2
.globl ee_rz_save_03
ee_rz_save_03:
    j     ee_rz_save_c
    addiu $6, $0, 3
.globl ee_rz_save_04
ee_rz_save_04:
    j     ee_rz_save_c
    addiu $6, $0, 4
.globl ee_rz_save_05
ee_rz_save_05:
    j     ee_rz_save_c
    addiu $6, $0, 5
.globl ee_rz_save_06
ee_rz_save_06:
    j     ee_rz_save_c
    addiu $6, $0, 6
.globl ee_rz_save_07
ee_rz_save_07:
    j     ee_rz_save_c
    addiu $6, $0, 7
.globl ee_rz_save_08
ee_rz_save_08:
    j     ee_rz_save_c
    addiu $6, $0, 8
.globl ee_rz_save_09
ee_rz_save_09:
    j     ee_rz_save_c
    addiu $6, $0, 9
.globl ee_rz_save_10
ee_rz_save_10:
    j     ee_rz_save_c
    addiu $6, $0, 10
.globl ee_rz_save_11
ee_rz_save_11:
    j     ee_rz_save_c
    addiu $6, $0, 11
.globl ee_rz_save_12
ee_rz_save_12:
    j     ee_rz_save_c
    addiu $6, $0, 12
.globl ee_rz_save_13
ee_rz_save_13:
    j     ee_rz_save_c
    addiu $6, $0, 13
.globl ee_rz_save_14
ee_rz_save_14:
    j     ee_rz_save_c
    addiu $6, $0, 14
.globl ee_rz_save_15
ee_rz_save_15:
    j     ee_rz_save_c
    addiu $6, $0, 15
.globl ee_rz_save_16
ee_rz_save_16:
    j     ee_rz_save_c
    addiu $6, $0, 16
.globl ee_rz_save_17
ee_rz_save_17:
    j     ee_rz_save_c
    addiu $6, $0, 17
.globl ee_rz_save_18
ee_rz_save_18:
    j     ee_rz_save_c
    addiu $6, $0, 18
.extern ee_rz_gold_count_c
.globl ee_rz_gold_count_00
ee_rz_gold_count_00:
    j     ee_rz_gold_count_c
    addiu $4, $0, 0
.globl ee_rz_gold_count_01
ee_rz_gold_count_01:
    j     ee_rz_gold_count_c
    addiu $4, $0, 1
.globl ee_rz_gold_count_02
ee_rz_gold_count_02:
    j     ee_rz_gold_count_c
    addiu $4, $0, 2
.globl ee_rz_gold_count_03
ee_rz_gold_count_03:
    j     ee_rz_gold_count_c
    addiu $4, $0, 3
.globl ee_rz_gold_count_04
ee_rz_gold_count_04:
    j     ee_rz_gold_count_c
    addiu $4, $0, 4
.globl ee_rz_gold_count_05
ee_rz_gold_count_05:
    j     ee_rz_gold_count_c
    addiu $4, $0, 5
.globl ee_rz_gold_count_06
ee_rz_gold_count_06:
    j     ee_rz_gold_count_c
    addiu $4, $0, 6
.globl ee_rz_gold_count_07
ee_rz_gold_count_07:
    j     ee_rz_gold_count_c
    addiu $4, $0, 7
.globl ee_rz_gold_count_08
ee_rz_gold_count_08:
    j     ee_rz_gold_count_c
    addiu $4, $0, 8
.globl ee_rz_gold_count_09
ee_rz_gold_count_09:
    j     ee_rz_gold_count_c
    addiu $4, $0, 9
.globl ee_rz_gold_count_10
ee_rz_gold_count_10:
    j     ee_rz_gold_count_c
    addiu $4, $0, 10
.globl ee_rz_gold_count_11
ee_rz_gold_count_11:
    j     ee_rz_gold_count_c
    addiu $4, $0, 11
.globl ee_rz_gold_count_12
ee_rz_gold_count_12:
    j     ee_rz_gold_count_c
    addiu $4, $0, 12
.globl ee_rz_gold_count_13
ee_rz_gold_count_13:
    j     ee_rz_gold_count_c
    addiu $4, $0, 13
.globl ee_rz_gold_count_14
ee_rz_gold_count_14:
    j     ee_rz_gold_count_c
    addiu $4, $0, 14
.globl ee_rz_gold_count_15
ee_rz_gold_count_15:
    j     ee_rz_gold_count_c
    addiu $4, $0, 15
.globl ee_rz_gold_count_16
ee_rz_gold_count_16:
    j     ee_rz_gold_count_c
    addiu $4, $0, 16
.globl ee_rz_gold_count_17
ee_rz_gold_count_17:
    j     ee_rz_gold_count_c
    addiu $4, $0, 17
.globl ee_rz_gold_count_18
ee_rz_gold_count_18:
    j     ee_rz_gold_count_c
    addiu $4, $0, 18
.extern ee_rz_vendor_list_c
.globl ee_rz_vendor_list_00
ee_rz_vendor_list_00:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 0
.globl ee_rz_vendor_list_01
ee_rz_vendor_list_01:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 1
.globl ee_rz_vendor_list_02
ee_rz_vendor_list_02:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 2
.globl ee_rz_vendor_list_03
ee_rz_vendor_list_03:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 3
.globl ee_rz_vendor_list_04
ee_rz_vendor_list_04:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 4
.globl ee_rz_vendor_list_05
ee_rz_vendor_list_05:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 5
.globl ee_rz_vendor_list_06
ee_rz_vendor_list_06:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 6
.globl ee_rz_vendor_list_07
ee_rz_vendor_list_07:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 7
.globl ee_rz_vendor_list_08
ee_rz_vendor_list_08:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 8
.globl ee_rz_vendor_list_09
ee_rz_vendor_list_09:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 9
.globl ee_rz_vendor_list_10
ee_rz_vendor_list_10:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 10
.globl ee_rz_vendor_list_11
ee_rz_vendor_list_11:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 11
.globl ee_rz_vendor_list_12
ee_rz_vendor_list_12:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 12
.globl ee_rz_vendor_list_13
ee_rz_vendor_list_13:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 13
.globl ee_rz_vendor_list_14
ee_rz_vendor_list_14:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 14
.globl ee_rz_vendor_list_15
ee_rz_vendor_list_15:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 15
.globl ee_rz_vendor_list_16
ee_rz_vendor_list_16:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 16
.globl ee_rz_vendor_list_17
ee_rz_vendor_list_17:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 17
.globl ee_rz_vendor_list_18
ee_rz_vendor_list_18:
    j     ee_rz_vendor_list_c
    addiu $5, $0, 18
.extern ee_rz_slots_end_c
.globl ee_rz_slots_end_00
ee_rz_slots_end_00:
    j     ee_rz_slots_end_c
    addiu $4, $0, 0
.globl ee_rz_slots_end_01
ee_rz_slots_end_01:
    j     ee_rz_slots_end_c
    addiu $4, $0, 1
.globl ee_rz_slots_end_02
ee_rz_slots_end_02:
    j     ee_rz_slots_end_c
    addiu $4, $0, 2
.globl ee_rz_slots_end_03
ee_rz_slots_end_03:
    j     ee_rz_slots_end_c
    addiu $4, $0, 3
.globl ee_rz_slots_end_04
ee_rz_slots_end_04:
    j     ee_rz_slots_end_c
    addiu $4, $0, 4
.globl ee_rz_slots_end_05
ee_rz_slots_end_05:
    j     ee_rz_slots_end_c
    addiu $4, $0, 5
.globl ee_rz_slots_end_06
ee_rz_slots_end_06:
    j     ee_rz_slots_end_c
    addiu $4, $0, 6
.globl ee_rz_slots_end_07
ee_rz_slots_end_07:
    j     ee_rz_slots_end_c
    addiu $4, $0, 7
.globl ee_rz_slots_end_08
ee_rz_slots_end_08:
    j     ee_rz_slots_end_c
    addiu $4, $0, 8
.globl ee_rz_slots_end_09
ee_rz_slots_end_09:
    j     ee_rz_slots_end_c
    addiu $4, $0, 9
.globl ee_rz_slots_end_10
ee_rz_slots_end_10:
    j     ee_rz_slots_end_c
    addiu $4, $0, 10
.globl ee_rz_slots_end_11
ee_rz_slots_end_11:
    j     ee_rz_slots_end_c
    addiu $4, $0, 11
.globl ee_rz_slots_end_12
ee_rz_slots_end_12:
    j     ee_rz_slots_end_c
    addiu $4, $0, 12
.globl ee_rz_slots_end_13
ee_rz_slots_end_13:
    j     ee_rz_slots_end_c
    addiu $4, $0, 13
.globl ee_rz_slots_end_14
ee_rz_slots_end_14:
    j     ee_rz_slots_end_c
    addiu $4, $0, 14
.globl ee_rz_slots_end_15
ee_rz_slots_end_15:
    j     ee_rz_slots_end_c
    addiu $4, $0, 15
.globl ee_rz_slots_end_16
ee_rz_slots_end_16:
    j     ee_rz_slots_end_c
    addiu $4, $0, 16
.globl ee_rz_slots_end_17
ee_rz_slots_end_17:
    j     ee_rz_slots_end_c
    addiu $4, $0, 17
.globl ee_rz_slots_end_18
ee_rz_slots_end_18:
    j     ee_rz_slots_end_c
    addiu $4, $0, 18
.extern ee_rz_vendor_class_c
.globl ee_rz_vendor_class_00
ee_rz_vendor_class_00:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 0
.globl ee_rz_vendor_class_01
ee_rz_vendor_class_01:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 1
.globl ee_rz_vendor_class_02
ee_rz_vendor_class_02:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 2
.globl ee_rz_vendor_class_03
ee_rz_vendor_class_03:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 3
.globl ee_rz_vendor_class_04
ee_rz_vendor_class_04:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 4
.globl ee_rz_vendor_class_05
ee_rz_vendor_class_05:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 5
.globl ee_rz_vendor_class_06
ee_rz_vendor_class_06:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 6
.globl ee_rz_vendor_class_07
ee_rz_vendor_class_07:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 7
.globl ee_rz_vendor_class_08
ee_rz_vendor_class_08:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 8
.globl ee_rz_vendor_class_09
ee_rz_vendor_class_09:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 9
.globl ee_rz_vendor_class_10
ee_rz_vendor_class_10:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 10
.globl ee_rz_vendor_class_11
ee_rz_vendor_class_11:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 11
.globl ee_rz_vendor_class_12
ee_rz_vendor_class_12:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 12
.globl ee_rz_vendor_class_13
ee_rz_vendor_class_13:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 13
.globl ee_rz_vendor_class_14
ee_rz_vendor_class_14:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 14
.globl ee_rz_vendor_class_15
ee_rz_vendor_class_15:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 15
.globl ee_rz_vendor_class_16
ee_rz_vendor_class_16:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 16
.globl ee_rz_vendor_class_17
ee_rz_vendor_class_17:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 17
.globl ee_rz_vendor_class_18
ee_rz_vendor_class_18:
    j     ee_rz_vendor_class_c
    addiu $6, $0, 18
.extern ee_rz_health_draw_c
.globl ee_rz_health_draw_00
ee_rz_health_draw_00:
    j     ee_rz_health_draw_c
    addiu $5, $0, 0
.globl ee_rz_health_draw_01
ee_rz_health_draw_01:
    j     ee_rz_health_draw_c
    addiu $5, $0, 1
.globl ee_rz_health_draw_02
ee_rz_health_draw_02:
    j     ee_rz_health_draw_c
    addiu $5, $0, 2
.globl ee_rz_health_draw_03
ee_rz_health_draw_03:
    j     ee_rz_health_draw_c
    addiu $5, $0, 3
.globl ee_rz_health_draw_04
ee_rz_health_draw_04:
    j     ee_rz_health_draw_c
    addiu $5, $0, 4
.globl ee_rz_health_draw_05
ee_rz_health_draw_05:
    j     ee_rz_health_draw_c
    addiu $5, $0, 5
.globl ee_rz_health_draw_06
ee_rz_health_draw_06:
    j     ee_rz_health_draw_c
    addiu $5, $0, 6
.globl ee_rz_health_draw_07
ee_rz_health_draw_07:
    j     ee_rz_health_draw_c
    addiu $5, $0, 7
.globl ee_rz_health_draw_08
ee_rz_health_draw_08:
    j     ee_rz_health_draw_c
    addiu $5, $0, 8
.globl ee_rz_health_draw_09
ee_rz_health_draw_09:
    j     ee_rz_health_draw_c
    addiu $5, $0, 9
.globl ee_rz_health_draw_10
ee_rz_health_draw_10:
    j     ee_rz_health_draw_c
    addiu $5, $0, 10
.globl ee_rz_health_draw_11
ee_rz_health_draw_11:
    j     ee_rz_health_draw_c
    addiu $5, $0, 11
.globl ee_rz_health_draw_12
ee_rz_health_draw_12:
    j     ee_rz_health_draw_c
    addiu $5, $0, 12
.globl ee_rz_health_draw_13
ee_rz_health_draw_13:
    j     ee_rz_health_draw_c
    addiu $5, $0, 13
.globl ee_rz_health_draw_14
ee_rz_health_draw_14:
    j     ee_rz_health_draw_c
    addiu $5, $0, 14
.globl ee_rz_health_draw_15
ee_rz_health_draw_15:
    j     ee_rz_health_draw_c
    addiu $5, $0, 15
.globl ee_rz_health_draw_16
ee_rz_health_draw_16:
    j     ee_rz_health_draw_c
    addiu $5, $0, 16
.globl ee_rz_health_draw_17
ee_rz_health_draw_17:
    j     ee_rz_health_draw_c
    addiu $5, $0, 17
.globl ee_rz_health_draw_18
ee_rz_health_draw_18:
    j     ee_rz_health_draw_c
    addiu $5, $0, 18
.extern ee_rz_level_init_c
.globl ee_rz_level_init_00
ee_rz_level_init_00:
    j     ee_rz_level_init_c
    addiu $4, $0, 0
.globl ee_rz_level_init_01
ee_rz_level_init_01:
    j     ee_rz_level_init_c
    addiu $4, $0, 1
.globl ee_rz_level_init_02
ee_rz_level_init_02:
    j     ee_rz_level_init_c
    addiu $4, $0, 2
.globl ee_rz_level_init_03
ee_rz_level_init_03:
    j     ee_rz_level_init_c
    addiu $4, $0, 3
.globl ee_rz_level_init_04
ee_rz_level_init_04:
    j     ee_rz_level_init_c
    addiu $4, $0, 4
.globl ee_rz_level_init_05
ee_rz_level_init_05:
    j     ee_rz_level_init_c
    addiu $4, $0, 5
.globl ee_rz_level_init_06
ee_rz_level_init_06:
    j     ee_rz_level_init_c
    addiu $4, $0, 6
.globl ee_rz_level_init_07
ee_rz_level_init_07:
    j     ee_rz_level_init_c
    addiu $4, $0, 7
.globl ee_rz_level_init_08
ee_rz_level_init_08:
    j     ee_rz_level_init_c
    addiu $4, $0, 8
.globl ee_rz_level_init_09
ee_rz_level_init_09:
    j     ee_rz_level_init_c
    addiu $4, $0, 9
.globl ee_rz_level_init_10
ee_rz_level_init_10:
    j     ee_rz_level_init_c
    addiu $4, $0, 10
.globl ee_rz_level_init_11
ee_rz_level_init_11:
    j     ee_rz_level_init_c
    addiu $4, $0, 11
.globl ee_rz_level_init_12
ee_rz_level_init_12:
    j     ee_rz_level_init_c
    addiu $4, $0, 12
.globl ee_rz_level_init_13
ee_rz_level_init_13:
    j     ee_rz_level_init_c
    addiu $4, $0, 13
.globl ee_rz_level_init_14
ee_rz_level_init_14:
    j     ee_rz_level_init_c
    addiu $4, $0, 14
.globl ee_rz_level_init_15
ee_rz_level_init_15:
    j     ee_rz_level_init_c
    addiu $4, $0, 15
.globl ee_rz_level_init_16
ee_rz_level_init_16:
    j     ee_rz_level_init_c
    addiu $4, $0, 16
.globl ee_rz_level_init_17
ee_rz_level_init_17:
    j     ee_rz_level_init_c
    addiu $4, $0, 17
.globl ee_rz_level_init_18
ee_rz_level_init_18:
    j     ee_rz_level_init_c
    addiu $4, $0, 18
.extern ee_rz_vendor_icon_c
.globl ee_rz_vendor_icon_00
ee_rz_vendor_icon_00:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 0
.globl ee_rz_vendor_icon_01
ee_rz_vendor_icon_01:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 1
.globl ee_rz_vendor_icon_02
ee_rz_vendor_icon_02:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 2
.globl ee_rz_vendor_icon_03
ee_rz_vendor_icon_03:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 3
.globl ee_rz_vendor_icon_04
ee_rz_vendor_icon_04:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 4
.globl ee_rz_vendor_icon_05
ee_rz_vendor_icon_05:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 5
.globl ee_rz_vendor_icon_06
ee_rz_vendor_icon_06:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 6
.globl ee_rz_vendor_icon_07
ee_rz_vendor_icon_07:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 7
.globl ee_rz_vendor_icon_08
ee_rz_vendor_icon_08:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 8
.globl ee_rz_vendor_icon_09
ee_rz_vendor_icon_09:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 9
.globl ee_rz_vendor_icon_10
ee_rz_vendor_icon_10:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 10
.globl ee_rz_vendor_icon_11
ee_rz_vendor_icon_11:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 11
.globl ee_rz_vendor_icon_12
ee_rz_vendor_icon_12:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 12
.globl ee_rz_vendor_icon_13
ee_rz_vendor_icon_13:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 13
.globl ee_rz_vendor_icon_14
ee_rz_vendor_icon_14:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 14
.globl ee_rz_vendor_icon_15
ee_rz_vendor_icon_15:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 15
.globl ee_rz_vendor_icon_16
ee_rz_vendor_icon_16:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 16
.globl ee_rz_vendor_icon_17
ee_rz_vendor_icon_17:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 17
.globl ee_rz_vendor_icon_18
ee_rz_vendor_icon_18:
    j     ee_rz_vendor_icon_c
    addiu $6, $0, 18
.extern ee_rz_vendor_text_c
.globl ee_rz_vendor_text_00
ee_rz_vendor_text_00:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 0
.globl ee_rz_vendor_text_01
ee_rz_vendor_text_01:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 1
.globl ee_rz_vendor_text_02
ee_rz_vendor_text_02:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 2
.globl ee_rz_vendor_text_03
ee_rz_vendor_text_03:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 3
.globl ee_rz_vendor_text_04
ee_rz_vendor_text_04:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 4
.globl ee_rz_vendor_text_05
ee_rz_vendor_text_05:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 5
.globl ee_rz_vendor_text_06
ee_rz_vendor_text_06:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 6
.globl ee_rz_vendor_text_07
ee_rz_vendor_text_07:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 7
.globl ee_rz_vendor_text_08
ee_rz_vendor_text_08:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 8
.globl ee_rz_vendor_text_09
ee_rz_vendor_text_09:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 9
.globl ee_rz_vendor_text_10
ee_rz_vendor_text_10:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 10
.globl ee_rz_vendor_text_11
ee_rz_vendor_text_11:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 11
.globl ee_rz_vendor_text_12
ee_rz_vendor_text_12:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 12
.globl ee_rz_vendor_text_13
ee_rz_vendor_text_13:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 13
.globl ee_rz_vendor_text_14
ee_rz_vendor_text_14:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 14
.globl ee_rz_vendor_text_15
ee_rz_vendor_text_15:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 15
.globl ee_rz_vendor_text_16
ee_rz_vendor_text_16:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 16
.globl ee_rz_vendor_text_17
ee_rz_vendor_text_17:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 17
.globl ee_rz_vendor_text_18
ee_rz_vendor_text_18:
    j     ee_rz_vendor_text_c
    addiu $5, $0, 18
.extern ee_rz_vendor_model_c
.globl ee_rz_vendor_model_00
ee_rz_vendor_model_00:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 0
.globl ee_rz_vendor_model_01
ee_rz_vendor_model_01:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 1
.globl ee_rz_vendor_model_02
ee_rz_vendor_model_02:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 2
.globl ee_rz_vendor_model_03
ee_rz_vendor_model_03:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 3
.globl ee_rz_vendor_model_04
ee_rz_vendor_model_04:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 4
.globl ee_rz_vendor_model_05
ee_rz_vendor_model_05:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 5
.globl ee_rz_vendor_model_06
ee_rz_vendor_model_06:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 6
.globl ee_rz_vendor_model_07
ee_rz_vendor_model_07:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 7
.globl ee_rz_vendor_model_08
ee_rz_vendor_model_08:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 8
.globl ee_rz_vendor_model_09
ee_rz_vendor_model_09:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 9
.globl ee_rz_vendor_model_10
ee_rz_vendor_model_10:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 10
.globl ee_rz_vendor_model_11
ee_rz_vendor_model_11:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 11
.globl ee_rz_vendor_model_12
ee_rz_vendor_model_12:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 12
.globl ee_rz_vendor_model_13
ee_rz_vendor_model_13:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 13
.globl ee_rz_vendor_model_14
ee_rz_vendor_model_14:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 14
.globl ee_rz_vendor_model_15
ee_rz_vendor_model_15:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 15
.globl ee_rz_vendor_model_16
ee_rz_vendor_model_16:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 16
.globl ee_rz_vendor_model_17
ee_rz_vendor_model_17:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 17
.globl ee_rz_vendor_model_18
ee_rz_vendor_model_18:
    j     ee_rz_vendor_model_c
    addiu $6, $0, 18
.extern ee_rz_vendor_place_c
.globl ee_rz_vendor_place_00
ee_rz_vendor_place_00:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 0
.globl ee_rz_vendor_place_01
ee_rz_vendor_place_01:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 1
.globl ee_rz_vendor_place_02
ee_rz_vendor_place_02:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 2
.globl ee_rz_vendor_place_03
ee_rz_vendor_place_03:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 3
.globl ee_rz_vendor_place_04
ee_rz_vendor_place_04:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 4
.globl ee_rz_vendor_place_05
ee_rz_vendor_place_05:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 5
.globl ee_rz_vendor_place_06
ee_rz_vendor_place_06:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 6
.globl ee_rz_vendor_place_07
ee_rz_vendor_place_07:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 7
.globl ee_rz_vendor_place_08
ee_rz_vendor_place_08:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 8
.globl ee_rz_vendor_place_09
ee_rz_vendor_place_09:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 9
.globl ee_rz_vendor_place_10
ee_rz_vendor_place_10:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 10
.globl ee_rz_vendor_place_11
ee_rz_vendor_place_11:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 11
.globl ee_rz_vendor_place_12
ee_rz_vendor_place_12:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 12
.globl ee_rz_vendor_place_13
ee_rz_vendor_place_13:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 13
.globl ee_rz_vendor_place_14
ee_rz_vendor_place_14:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 14
.globl ee_rz_vendor_place_15
ee_rz_vendor_place_15:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 15
.globl ee_rz_vendor_place_16
ee_rz_vendor_place_16:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 16
.globl ee_rz_vendor_place_17
ee_rz_vendor_place_17:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 17
.globl ee_rz_vendor_place_18
ee_rz_vendor_place_18:
    j     ee_rz_vendor_place_c
    addiu $5, $0, 18
.extern ee_rz_vendor_slogan_c
.globl ee_rz_vendor_slogan_00
ee_rz_vendor_slogan_00:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 0
.globl ee_rz_vendor_slogan_01
ee_rz_vendor_slogan_01:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 1
.globl ee_rz_vendor_slogan_02
ee_rz_vendor_slogan_02:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 2
.globl ee_rz_vendor_slogan_03
ee_rz_vendor_slogan_03:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 3
.globl ee_rz_vendor_slogan_04
ee_rz_vendor_slogan_04:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 4
.globl ee_rz_vendor_slogan_05
ee_rz_vendor_slogan_05:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 5
.globl ee_rz_vendor_slogan_06
ee_rz_vendor_slogan_06:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 6
.globl ee_rz_vendor_slogan_07
ee_rz_vendor_slogan_07:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 7
.globl ee_rz_vendor_slogan_08
ee_rz_vendor_slogan_08:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 8
.globl ee_rz_vendor_slogan_09
ee_rz_vendor_slogan_09:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 9
.globl ee_rz_vendor_slogan_10
ee_rz_vendor_slogan_10:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 10
.globl ee_rz_vendor_slogan_11
ee_rz_vendor_slogan_11:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 11
.globl ee_rz_vendor_slogan_12
ee_rz_vendor_slogan_12:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 12
.globl ee_rz_vendor_slogan_13
ee_rz_vendor_slogan_13:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 13
.globl ee_rz_vendor_slogan_14
ee_rz_vendor_slogan_14:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 14
.globl ee_rz_vendor_slogan_15
ee_rz_vendor_slogan_15:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 15
.globl ee_rz_vendor_slogan_16
ee_rz_vendor_slogan_16:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 16
.globl ee_rz_vendor_slogan_17
ee_rz_vendor_slogan_17:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 17
.globl ee_rz_vendor_slogan_18
ee_rz_vendor_slogan_18:
    j     ee_rz_vendor_slogan_c
    addiu $5, $0, 18
.extern ee_rz_vendor_holo_c
.globl ee_rz_vendor_holo_00
ee_rz_vendor_holo_00:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 0
.globl ee_rz_vendor_holo_01
ee_rz_vendor_holo_01:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 1
.globl ee_rz_vendor_holo_02
ee_rz_vendor_holo_02:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 2
.globl ee_rz_vendor_holo_03
ee_rz_vendor_holo_03:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 3
.globl ee_rz_vendor_holo_04
ee_rz_vendor_holo_04:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 4
.globl ee_rz_vendor_holo_05
ee_rz_vendor_holo_05:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 5
.globl ee_rz_vendor_holo_06
ee_rz_vendor_holo_06:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 6
.globl ee_rz_vendor_holo_07
ee_rz_vendor_holo_07:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 7
.globl ee_rz_vendor_holo_08
ee_rz_vendor_holo_08:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 8
.globl ee_rz_vendor_holo_09
ee_rz_vendor_holo_09:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 9
.globl ee_rz_vendor_holo_10
ee_rz_vendor_holo_10:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 10
.globl ee_rz_vendor_holo_11
ee_rz_vendor_holo_11:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 11
.globl ee_rz_vendor_holo_12
ee_rz_vendor_holo_12:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 12
.globl ee_rz_vendor_holo_13
ee_rz_vendor_holo_13:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 13
.globl ee_rz_vendor_holo_14
ee_rz_vendor_holo_14:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 14
.globl ee_rz_vendor_holo_15
ee_rz_vendor_holo_15:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 15
.globl ee_rz_vendor_holo_16
ee_rz_vendor_holo_16:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 16
.globl ee_rz_vendor_holo_17
ee_rz_vendor_holo_17:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 17
.globl ee_rz_vendor_holo_18
ee_rz_vendor_holo_18:
    j     ee_rz_vendor_holo_c
    addiu $5, $0, 18
.extern ee_rz_vendor_holo_add_c
.globl ee_rz_vendor_holo_add_00
ee_rz_vendor_holo_add_00:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 0
.globl ee_rz_vendor_holo_add_01
ee_rz_vendor_holo_add_01:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 1
.globl ee_rz_vendor_holo_add_02
ee_rz_vendor_holo_add_02:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 2
.globl ee_rz_vendor_holo_add_03
ee_rz_vendor_holo_add_03:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 3
.globl ee_rz_vendor_holo_add_04
ee_rz_vendor_holo_add_04:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 4
.globl ee_rz_vendor_holo_add_05
ee_rz_vendor_holo_add_05:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 5
.globl ee_rz_vendor_holo_add_06
ee_rz_vendor_holo_add_06:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 6
.globl ee_rz_vendor_holo_add_07
ee_rz_vendor_holo_add_07:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 7
.globl ee_rz_vendor_holo_add_08
ee_rz_vendor_holo_add_08:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 8
.globl ee_rz_vendor_holo_add_09
ee_rz_vendor_holo_add_09:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 9
.globl ee_rz_vendor_holo_add_10
ee_rz_vendor_holo_add_10:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 10
.globl ee_rz_vendor_holo_add_11
ee_rz_vendor_holo_add_11:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 11
.globl ee_rz_vendor_holo_add_12
ee_rz_vendor_holo_add_12:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 12
.globl ee_rz_vendor_holo_add_13
ee_rz_vendor_holo_add_13:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 13
.globl ee_rz_vendor_holo_add_14
ee_rz_vendor_holo_add_14:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 14
.globl ee_rz_vendor_holo_add_15
ee_rz_vendor_holo_add_15:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 15
.globl ee_rz_vendor_holo_add_16
ee_rz_vendor_holo_add_16:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 16
.globl ee_rz_vendor_holo_add_17
ee_rz_vendor_holo_add_17:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 17
.globl ee_rz_vendor_holo_add_18
ee_rz_vendor_holo_add_18:
    j     ee_rz_vendor_holo_add_c
    addiu $7, $0, 18
.extern ee_rz_moby_update_c
.globl ee_rz_moby_update_00
ee_rz_moby_update_00:
    addiu $6, $0, 0
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_01
ee_rz_moby_update_01:
    addiu $6, $0, 1
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_02
ee_rz_moby_update_02:
    addiu $6, $0, 2
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_03
ee_rz_moby_update_03:
    addiu $6, $0, 3
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_04
ee_rz_moby_update_04:
    addiu $6, $0, 4
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_05
ee_rz_moby_update_05:
    addiu $6, $0, 5
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_06
ee_rz_moby_update_06:
    addiu $6, $0, 6
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_07
ee_rz_moby_update_07:
    addiu $6, $0, 7
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_08
ee_rz_moby_update_08:
    addiu $6, $0, 8
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_09
ee_rz_moby_update_09:
    addiu $6, $0, 9
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_10
ee_rz_moby_update_10:
    addiu $6, $0, 10
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_11
ee_rz_moby_update_11:
    addiu $6, $0, 11
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_12
ee_rz_moby_update_12:
    addiu $6, $0, 12
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_13
ee_rz_moby_update_13:
    addiu $6, $0, 13
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_14
ee_rz_moby_update_14:
    addiu $6, $0, 14
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_15
ee_rz_moby_update_15:
    addiu $6, $0, 15
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_16
ee_rz_moby_update_16:
    addiu $6, $0, 16
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_17
ee_rz_moby_update_17:
    addiu $6, $0, 17
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
.globl ee_rz_moby_update_18
ee_rz_moby_update_18:
    addiu $6, $0, 18
    j     ee_rz_moby_update_c
    addu  $5, $3, $0
