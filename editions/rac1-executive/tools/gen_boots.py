#!/usr/bin/env python3
"""
Convert Going Commando's Charge Boots model to an R&C1 moby class core,
which tools/splice_boots.py splices into every level of the image.

R&C2 names the Charge Boots in its weapon table (record 54 of g_weaponTable:
text 0xA9F "Charge Boots", foot slot, moby class 0xE70 = 3696 on both feet),
and wrench's unpack of the owner's R&C2 disc holds that class under
moby_classes/unsorted/3696 (core.bin, 0.png). wrench's mobyconvert (the
edition's wrench fork) rewrites the class core in R&C1's format, taking the
R&C1-only header bytes from R&C1's Grindboots class (195), which the Charge
Boots stand in for, and re-skins the boot below the ankle (REBIND). A worn
boot's five joints are Ratchet's knee, ankle, heel, ball and toe (his class's
joint lists 2 and 3). R&C2's Charge Boots hang the whole boot, sole
included, on joint 0, the knee, which follows the shin, where both games'
other boots (R&C1's Magneboots and Grindboots, R&C2's Magneboots) put the
shoe on joint 1, the ankle, and only the cuff on joint 0; R&C2 shows these
boots only for the length of a charge. The game must also pose the boots
from Ratchet's leg at all (src/charge_boots.c). The class gets id CLASS_ID
(R&C1 uses ids up to 1973 and indexes them in a 0x800-entry table).

Nothing here comes from or goes into git: the inputs are wrench unpacks of
the player's discs and the class core is written under build/ (ignored).

  python tools/gen_boots.py [--openrac DIR] [--wrench DIR] [-o build/charge_boots.bin]
"""
import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CLASS_ID = 2000
RAC2_CLASS = 3696
RAC1_GRINDBOOTS = "moby_classes/gadget/195_grindboot"
# mobyconvert --rebind: joint 0 -> joint 1 below the ankle (the boot's origin
# is the knee; joint 1, the ankle, sits at z -1335 in the boot's vertex units,
# the ground at -1905), blended between, about where the Magneboots blend.
REBIND = ("0", "1", "-950", "-1250")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("--wrench", type=Path, default=ROOT.parents[2] / "wrench")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/charge_boots.bin")
    args = ap.parse_args()

    rac1 = args.openrac / "assets/wrench/rac1-pal"
    rac2 = args.openrac / "assets/wrench/rac2-ntsc"
    src = rac2 / f"moby_classes/unsorted/{RAC2_CLASS}"
    for need in (src / "core.bin", rac1 / RAC1_GRINDBOOTS / "core.bin"):
        if not need.is_file():
            raise SystemExit(f"missing {need} (wrench unpack of the R&C2 NTSC and R&C1 PAL discs)")
    convert = args.wrench / "bin/mobyconvert.exe"
    if not convert.is_file():
        raise SystemExit(f"missing {convert} (the edition's wrench fork, branch executive-edition)")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(convert), str(src / "core.bin"), "gc", str(args.out), "rac",
                             str(rac1 / RAC1_GRINDBOOTS / "core.bin"), "--rebind", *REBIND],
                            capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit("mobyconvert failed")
    print(f"wrote {args.out}: class {CLASS_ID} from R&C2 class {RAC2_CLASS}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
