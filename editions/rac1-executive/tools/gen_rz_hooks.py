#!/usr/bin/env python3
"""
The Randomizer's per-level entry points (src/rz_hooks.s).

tools/overlay_patch.py (feature randomizer) points every hooked call in a
level's code at a stub of that level, found by tools/gen_randomizer.py. A
stub passes the level to the C hook in the first argument register the
call itself leaves free, so the hook knows which program's copy of a game
function to call back (LVL in src/modseg.h). The output is kept as source;
run this after adding a hook kind, and add the kind to gen_randomizer.py's
patches() as well.

  python tools/gen_rz_hooks.py [-o src/rz_hooks.s]
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LEVELS = 19

HEADER = """/* Entry points for the Randomizer's hooks (src/randomizer_*.c), one per
 * level for each hooked call, so that the C hook knows which level program
 * made the call: tools/overlay_patch.py (feature randomizer) sends level L's
 * call sites to the stubs numbered L. Each passes L in the first free
 * argument register after the call's own arguments ($4-$7), and the moby
 * update loop's stub passes the update function the loop was about to call
 * ($3, the jalr's register) as well. Written by tools/gen_rz_hooks.py, kept
 * as source. Numeric register names: the SN assembler takes no aliases. */
.set noreorder
.text
"""

# The stub's name, its C hook, and its instructions; the last one sits in the jump's delay slot. {c} is the hook,
# {n} the level. The register is the first argument the hooked call leaves free: GiveItem(id, flag) takes $4 and
# $5, so its level goes in $6.
KINDS = [
    ("ee_rz_give_item", "ee_rz_give_item_c", ["j     {c}", "addiu $6, $0, {n}"]),
    ("ee_rz_item_banner", "ee_rz_item_banner_c", ["j     {c}", "addiu $6, $0, {n}"]),
    ("ee_rz_unlock_planet", "ee_rz_unlock_planet_c", ["j     {c}", "addiu $5, $0, {n}"]),
    ("ee_rz_planet_banner", "ee_rz_planet_banner_c", ["j     {c}", "addiu $5, $0, {n}"]),
    ("ee_rz_save", "ee_rz_save_c", ["j     {c}", "addiu $6, $0, {n}"]),
    ("ee_rz_gold_count", "ee_rz_gold_count_c", ["j     {c}", "addiu $4, $0, {n}"]),
    ("ee_rz_vendor_list", "ee_rz_vendor_list_c", ["j     {c}", "addiu $5, $0, {n}"]),
    ("ee_rz_slots_end", "ee_rz_slots_end_c", ["j     {c}", "addiu $4, $0, {n}"]),
    ("ee_rz_vendor_class", "ee_rz_vendor_class_c", ["j     {c}", "addiu $6, $0, {n}"]),
    ("ee_rz_health_draw", "ee_rz_health_draw_c", ["j     {c}", "addiu $5, $0, {n}"]),
    ("ee_rz_level_init", "ee_rz_level_init_c", ["j     {c}", "addiu $4, $0, {n}"]),
    ("ee_rz_vendor_icon", "ee_rz_vendor_icon_c", ["j     {c}", "addiu $6, $0, {n}"]),
    ("ee_rz_vendor_text", "ee_rz_vendor_text_c", ["j     {c}", "addiu $5, $0, {n}"]),
    ("ee_rz_vendor_model", "ee_rz_vendor_model_c", ["j     {c}", "addiu $6, $0, {n}"]),
    ("ee_rz_vendor_place", "ee_rz_vendor_place_c", ["j     {c}", "addiu $5, $0, {n}"]),
    ("ee_rz_vendor_slogan", "ee_rz_vendor_slogan_c", ["j     {c}", "addiu $5, $0, {n}"]),
    ("ee_rz_vendor_holo", "ee_rz_vendor_holo_c", ["j     {c}", "addiu $5, $0, {n}"]),
    ("ee_rz_vendor_holo_add", "ee_rz_vendor_holo_add_c", ["j     {c}", "addiu $7, $0, {n}"]),
    # the moby update loop's jalr $3: the moby is in $4; the update function becomes the hook's second argument
    ("ee_rz_moby_update", "ee_rz_moby_update_c", ["addiu $6, $0, {n}", "j     {c}", "addu  $5, $3, $0"]),
]


def stubs() -> str:
    out = [HEADER]
    for name, hook, instructions in KINDS:
        out.append(f".extern {hook}\n")
        for level in range(LEVELS):
            out.append(f".globl {name}_{level:02d}\n{name}_{level:02d}:\n")
            for instruction in instructions:
                out.append("    " + instruction.format(c=hook, n=level) + "\n")
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0].strip())
    ap.add_argument("-o", "--output", type=Path, default=ROOT / "src/rz_hooks.s")
    args = ap.parse_args()
    args.output.write_text(stubs(), newline="\r\n")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
