/* x5: the gun arm over the backpedal, as Going Commando holds it.
 *
 * Both games put a layer over Ratchet's arms while a trigger weapon is
 * held (weapon type 1, the right arm; type 2, both): R&C1's per-frame
 * manager (NTSC FUN_L00_0020dd20, level 0 func_L00_0020E3B8; R&C2 Oozla
 * 0x2AF820) keeps one per arm (hero + 0xD00 / + 0xD04; R&C2 hero +
 * 0xE90) on the arm's joint set, gives it the arm's own animation class
 * (moby classes 1 and 2, 20 joints) and every frame plays in it the
 * sequence Ratchet's body plays, frame for frame, through a mapper that is
 * the identity (func_L00_0020DC50; R&C2 0x2AEA88). The arm classes hold
 * the arm half of the body's first 23 sequences, the gun held forward, so
 * the manager enables the layer only for an arm sequence below 0x17;
 * above it the arm follows the body. Gloves (type 0) get no layer.
 *
 * Measured in R&C2 (the owner's Oozla state, a gun and a glove weapon,
 * the backpedal frozen at frame 8 in both): the same body sequence (0x14)
 * and the same unpacked body frames, but with the gun the layer holds the
 * forearm up and the gun forward, with the glove the arm swings. R&C2's
 * arm classes match R&C1's sequence for sequence (compared frame by
 * frame) except two: 0x14, its 30-frame backpedal arm where R&C1 has the
 * 14-frame stop (R&C2's body 0x14 is the backpedal), and a 24th sequence.
 *
 * The edition's backpedal is R&C2's body sequence in Ratchet's class slot
 * 140 (src/strafe.c), above 0x17, so the layer was off and every weapon
 * swung like a glove. Here R&C2's two backpedal arms (tools/rac2_anim.py
 * emit-arm, R&C1's layout already) go into the arm classes' free 24th slot
 * (0x17; the sequence table has room before its data), the mapper sends
 * body slot 140 to it, and tools/overlay_patch.py widens the manager's two
 * tests to < 0x18. Body sequence 0x17 must keep its layer off: the
 * manager falls back, for a disabled channel, to Ratchet's own class at
 * the mapped index, so it maps to an alias slot of Ratchet's class (142)
 * holding the very pointer of sequence 0x17. Both installs happen on
 * demand, right before the layer reads them (the pause menu borrows and
 * clears Ratchet's slots 134-161 while open; the arm classes are not
 * touched). The manager's change test reads the mapped layer id against
 * the identity of the body's id; that identity call maps the same way. */
#include "modseg.h"

extern const u32 ee_gunarm_seq_1[], ee_gunarm_seq_2[];   /* R&C2's arm classes 1 and 2, sequence 0x14 (build/modseg/gen_anim_arm.c) */

#define HERO            0x0013F450
#define hero_moby       (*(u8 *volatile *)(HERO + 0x2080))
#define arm_layer(i)    (*(u8 *volatile *)(HERO + 0xD00 + 4 * (i)))   /* the manager's layers: the right arm, the left */
#define LAYER_CLASS     0x18          /* a layer's arm class */
#define CLASS_SEQS      0x48
#define BACKPEDAL_SLOT  140           /* the body backpedal in Ratchet's class (src/strafe.c) */
#define ARM_SLOT        0x17          /* the arm classes' 24th sequence: R&C2 has one there, R&C1's table has room */
#define ALIAS_SLOT      142           /* Ratchet's class: stands in for body sequence 0x17 (the class keeps 162 slots) */

/* One sequence table entry: kept, or set if free; 0 if another sequence holds it. */
static s32 claim(u8 *cls, s32 slot, u32 seq)
{
    volatile u32 *seqs = (volatile u32 *)(cls + CLASS_SEQS);
    if (seqs[slot] == seq) {
        return 1;
    }
    if (seqs[slot] != 0) {
        return 0;
    }
    seqs[slot] = seq;
    return 1;
}

/* R&C2's backpedal arms in the arm classes of the layers the manager keeps. */
static s32 arms_ready(void)
{
    s32 i, any = 0;
    for (i = 0; i < 2; i++) {
        u8 *layer = arm_layer(i);
        if (layer) {
            u8 *cls = *(u8 **)(layer + LAYER_CLASS);
            if (!cls || !claim(cls, ARM_SLOT, (u32)(i ? ee_gunarm_seq_2 : ee_gunarm_seq_1))) {
                return 0;
            }
            any = 1;
        }
    }
    return any;
}

static s32 alias_ready(void)
{
    u8 *cls = *(u8 **)(hero_moby + 0x24);
    if (CLASS_SEQS + 4 * ALIAS_SLOT >= *(u32 *)cls) {
        return 0;   /* the class's table ends before it (header word 0: its packet table) */
    }
    return claim(cls, ALIAS_SLOT, ((volatile u32 *)(cls + CLASS_SEQS))[ARM_SLOT]);
}

static s32 arm_sequence(s32 seq)
{
    if (seq == BACKPEDAL_SLOT && arms_ready()) {
        return ARM_SLOT;
    }
    if (seq == ARM_SLOT) {
        if (alias_ready()) {
            return ALIAS_SLOT;
        }
        arms_ready();   /* no alias: at worst the backpedal arm plays, never an empty slot */
    }
    return seq;
}

/* The manager's mapper (func_L00_0020DC50: 0xFF means moby + 0xA5). */
s32 ee_arm_map(u8 *moby, s32 seq)
{
    if (seq == 0xFF) {
        seq = moby[0xA5];
    }
    return arm_sequence(seq);
}

/* The identity the manager compares the body's sequence through. */
s32 ee_arm_ident(s32 seq)
{
    return arm_sequence(seq);
}
