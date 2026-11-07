#!/usr/bin/env python3
"""
Patch the 19 levels' data on an image tools/install.py is making, each level read and written once: the seed code's
glyphs in its menu fonts (splice_boots.paint_fonts), the Charge Boots class (splice_boots.add_boots, with Going
Commando), Kalebo III's rebuilt sign (splice_sign.replace_sign) and the extra classes it lacks
(extra_classes.add_extra_classes), then its core compressed again (level_data.write_level).

The extra classes are copied from their own levels as this patch leaves them before their extra classes (with the
boots and the fonts): those six levels are read first, and held until their turn.

A level's new archive goes back in the sectors the disc gave it when it fits there, the rest of them zeroed;
otherwise it goes at the end of the image, after any blank tail the image ends with, followed by a blank tail of
its own (the stream reader reads past an archive's end), and the volume size grows to match. The level header's
data range (+0x08) is pointed at it.

  python tools/patch_levels.py IMAGE [--boots CORE --rac2 SCUS_972.68.iso] [--sign CORE] [--rac-wad EXE] [--work DIR]
"""
import argparse
import struct
import sys
from pathlib import Path

from extra_classes import EXTRA, add_extra_classes, class_source
from level_data import LEVELS, RAC_WAD, SECTOR, TAIL_SECTORS, Level, Wad, level_archives, read_level, \
    section_names, write_level
from splice_boots import RAC2_CLASS, add_boots, paint_fonts
from splice_sign import LEVEL as SIGN_LEVEL, replace_sign

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


class Image:
    """The image's levels, read from and written back to the file F."""

    def __init__(self, f):
        self.f = f
        f.seek(0, 2)
        size = f.tell()
        if size % SECTOR:
            raise SystemExit("the image is not a whole number of sectors")
        self.end = size // SECTOR
        f.seek((self.end - TAIL_SECTORS) * SECTOR)
        if not any(f.read(TAIL_SECTORS * SECTOR)):
            self.end -= TAIL_SECTORS        # an archive at the end goes over the blank tail the image ends with
        self.archives = level_archives(f)

    def read(self, number: int) -> bytes:
        _, lba, sectors = self.archives[number]
        self.f.seek(lba * SECTOR)
        return self.f.read(sectors * SECTOR)

    def write(self, number: int, archive: bytes) -> str:
        """ARCHIVE in the level's own sectors when it fits, else at the end; the level header pointed at it."""
        header_lba, lba, sectors = self.archives[number]
        if len(archive) <= sectors * SECTOR:
            self.f.seek(lba * SECTOR)
            self.f.write(archive + bytes(sectors * SECTOR - len(archive)))
            where = f"in place at sector {lba}"
        else:
            lba = self.end
            self.f.seek(lba * SECTOR)
            self.f.write(archive + bytes(TAIL_SECTORS * SECTOR))
            self.end = lba + len(archive) // SECTOR + TAIL_SECTORS
            self.f.seek(16 * SECTOR + 80)
            self.f.write(struct.pack("<I", self.end) + struct.pack(">I", self.end))
            where = f"moved to new sectors at {lba}"
        self.f.seek(header_lba * SECTOR + 8)
        self.f.write(struct.pack("<2I", lba, len(archive) // SECTOR))
        return where


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", type=Path)
    ap.add_argument("--boots", type=Path, help="the Charge Boots' class core (tools/gen_boots.py); without it, none")
    ap.add_argument("--rac2", type=Path, help="the R&C2 (USA) disc, for the boots' texture")
    ap.add_argument("--sign", type=Path, default=ROOT / "build/sign/gadgetron_sign.bin",
                    help="Kalebo III's sign class core (tools/gen_sign.py)")
    ap.add_argument("--rac-wad", type=Path, default=RAC_WAD, help="the installer core's WAD codec")
    ap.add_argument("--work", type=Path, default=ROOT / "build/patch_levels",
                    help="a folder for the codec's files (the installer gives each build its own)")
    args = ap.parse_args()
    if args.boots and not args.rac2:
        raise SystemExit("--boots needs --rac2, for the class's texture")
    names = section_names()
    wad = Wad(args.rac_wad, args.work)
    sign = args.sign.read_bytes()
    boots = None
    if args.boots:
        sys.path.insert(0, str(HERE.parents[2] / "common"))
        from rac2_anim import rac2_class_texture
        width, height, pixels, palette = rac2_class_texture(args.rac2, RAC2_CLASS, decode=wad.decode)
        if width & (width - 1) or height & (height - 1) or width < 8:
            raise SystemExit(f"R&C2 class {RAC2_CLASS}'s texture is {width}x{height}, not a power-of-two texture")
        boots = (args.boots.read_bytes(), pixels, palette, width, height)

    with args.image.open("r+b") as f:
        image = Image(f)

        def prepared(number: int) -> tuple[Level, str]:
            """Level NUMBER with everything but its extra classes, and what was done to it."""
            level = read_level(image.read(number), number, names, wad)
            paint_fonts(level)
            done = "fonts"
            if boots:
                info = add_boots(level, *boots)
                done += f", boots class at {info['insert']:#x} ({info['shifted']} offsets shifted)"
            if number == SIGN_LEVEL:
                info = replace_sign(level, sign)
                done += f", sign {info['old']:#x} -> {info['new']:#x} bytes"
            return level, done

        held = {}
        sources = {}
        for cid, (name, source) in EXTRA.items():
            if source not in held:
                held[source] = prepared(source)
            blob, texes = sources[cid] = class_source(held[source][0].core, held[source][0].index,
                                                      held[source][0].gs, cid, source)
            print(f"class {cid} ({name}) from level {source}: {len(blob):#x} bytes, {len(texes)} textures")
        for number in range(LEVELS):
            level, done = held.pop(number) if number in held else prepared(number)
            extra = add_extra_classes(level, sources)
            if extra:
                done += f", classes {extra['classes']}"
            archive = write_level(level, names, wad)
            where = image.write(number, archive)
            print(f"level {number:2d}: {done}; archive {len(archive):#x} bytes, {where}")
            if extra and extra["left_out"]:
                print(f"  no room in level {number}'s class table for {extra['left_out']}")
        f.seek(0, 2)
        print(f"{args.image.name}: 19 levels patched; image {f.tell() // SECTOR} sectors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
