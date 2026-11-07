#!/usr/bin/env python3
"""
Convert Going Commando's Charge Boots model to an R&C1 moby class core,
which tools/splice_boots.py splices into every level of the image.

R&C2 names the Charge Boots in its weapon table (record 54 of g_weaponTable:
text 0xA9F "Charge Boots", foot slot, moby class 0xE70 = 3696 on both feet),
and every level of the player's R&C2 disc holds that class (read from level
1, common/rac2_anim.py). The installer core's rac-moby
(installer/core/moby.c, written from wrench's moby engine) rewrites the class
core in R&C1's format, taking the R&C1-only header bytes from R&C1's
Grindboots class (195), which the Charge Boots stand in for, read from the
player's PAL disc (Novalis, level 3), and re-skins the boot below the ankle
(REBIND). A worn
boot's five joints are Ratchet's knee, ankle, heel, ball and toe (his class's
joint lists 2 and 3). R&C2's Charge Boots hang the whole boot, sole
included, on joint 0, the knee, which follows the shin, where both games'
other boots (R&C1's Magneboots and Grindboots, R&C2's Magneboots) put the
shoe on joint 1, the ankle, and only the cuff on joint 0; R&C2 shows these
boots only for the length of a charge. The game must also pose the boots
from Ratchet's leg at all (src/charge_boots.c). The class gets id CLASS_ID
(R&C1 uses ids up to 1973 and indexes them in a 0x800-entry table).

Nothing here comes from or goes into git: the inputs are the player's discs
and the class core is written under build/ (ignored).

  python tools/gen_boots.py [--rac1 SCES_509.16.iso] [--rac2 SCUS_972.68.iso] [--rac-moby EXE]
                            [--rac-wad EXE] [-o build/charge_boots.bin]
"""
import argparse
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[2] / "common"))
import core_tools  # noqa: E402
from extra_classes import disc_class  # noqa: E402
from rac2_anim import rac2_class  # noqa: E402
from splice_boots import Wad  # noqa: E402

CLASS_ID = 2000
RAC2_CLASS = 3696
RAC1_GRINDBOOTS = 195
GRINDBOOTS_LEVEL = 3                # Novalis
# rac-moby --rebind: joint 0 -> joint 1 below the ankle (the boot's origin
# is the knee; joint 1, the ankle, sits at z -1335 in the boot's vertex units,
# the ground at -1905), blended between, about where the Magneboots blend.
REBIND = ("0", "1", "-950", "-1250")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    openrac = ROOT.parents[2] / "OpenRAC"
    ap.add_argument("--rac1", type=Path, default=openrac / "games/rac1/baserom/SCES_509.16.iso")
    ap.add_argument("--rac2", type=Path, default=openrac / "games/rac2/ntsc/baserom/SCUS_972.68.iso")
    ap.add_argument("--rac-moby", type=Path, default=core_tools.tool("rac-moby"),
                    help="the installer core's moby class tool")
    ap.add_argument("--rac-wad", type=Path, default=core_tools.tool("rac-wad"), help="the installer core's WAD codec")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/charge_boots.bin")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        wad = Wad(args.rac_wad, tmp)
        boots = tmp / "rac2_charge_boots.bin"
        boots.write_bytes(rac2_class(args.rac2, RAC2_CLASS, decode=wad.decode))
        grindboots = tmp / "rac1_grindboots.bin"
        grindboots.write_bytes(disc_class(args.rac1, GRINDBOOTS_LEVEL, RAC1_GRINDBOOTS, wad.decode)[0])
        args.out.parent.mkdir(parents=True, exist_ok=True)
        core_tools.run(args.rac_moby, "convert", boots, "gc", args.out, "rac", grindboots, "--rebind", *REBIND)
    print(f"wrote {args.out}: class {CLASS_ID} from R&C2 class {RAC2_CLASS}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
