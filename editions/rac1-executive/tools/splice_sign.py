#!/usr/bin/env python3
"""
Put the rebuilt Gadgetron sign (tools/gen_sign.py) into Kalebo III's data
archive of an image, without re-laying out the disc: the quick route's way of
changing a level's classes (tools/splice_boots.py, whose rules this follows).

Class 1891's blob in the decoded core data is replaced in place. The new blob
is larger, so its space grows by whole 0x100-byte steps (every later block
keeps its alignment) and every core offset past it moves up by as much
(splice_boots.core_pointers), and every texture whose pixels lie past it,
the Charge Boots' among them (splice_boots.shift_textures); the class stays inside the part of the core
the engine keeps resident (below +0x7C), where the level had it. The core
is compressed again with wrench's compressor and checked by decoding it back
with OpenRAC's decoder; the core index takes the new sizes (+0x88, +0x8C);
everything else in the archive is copied byte for byte. The archive is
never larger than the retail one, so while the level's archive sits in the
retail one's sectors (splice_boots.py writes in place) it is written there,
the sectors it leaves free are zeroed and the image keeps its size; otherwise
it goes at the end of the image and the volume size grows. The level header's
data range is pointed at it. Run it after splice_boots.py, which rewrites
every level's archive from the one the header names.

  python tools/splice_sign.py IMAGE [--openrac DIR] [--wrench wrench/bin/wrenchbuild.exe]
                                    [--class-core build/sign_bank/moby_classes/1891_gadgetron_sign/core.bin]
"""
import argparse
import struct
import sys
from pathlib import Path

from splice_boots import LEVEL_TABLE, SECTOR, TAIL_SECTORS, TOC_LBA, core_pointers, recompress, shift_textures, table

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
LEVEL = 16            # Kalebo III, the only level with the sign
CLASS_ID = 1891
STEP = 0x100


def replace_class(archive: bytes, names, blob: bytes, wad, wrench: Path, work: Path) -> tuple[bytes, dict]:
    sections = [struct.unpack_from("<ii", archive, 8 * i) for i in range(len(names))]
    data = {n: archive[o:o + s] for n, (o, s) in zip(names, sections)}
    first = min(o for o, s in sections if s)
    head = archive[:first]

    index = bytearray(data["core_index"])
    core = wad(data["core_data"])
    core_size, = struct.unpack_from("<I", index, 0x8C)
    if len(core) != core_size:
        raise SystemExit("the core data does not decode to the size its index gives")
    _, _, classes = table(index, 0x18, 32)
    starts = [struct.unpack_from("<i", e)[0] for e in classes if struct.unpack_from("<i", e, 4)[0] == CLASS_ID]
    if len(starts) != 1 or starts[0] <= 0:
        raise SystemExit(f"level {LEVEL} has no class {CLASS_ID} blob")
    start = starts[0]
    pointers = core_pointers(index)
    end = min(o for _, o in pointers if o > start)
    resident_end, = struct.unpack_from("<I", index, 0x7C)
    if end > resident_end:
        raise SystemExit(f"class {CLASS_ID} at {start:#x}..{end:#x} is not in the resident part (ends {resident_end:#x})")
    old = end - start
    region = old + max(0, -(-(len(blob) - old) // STEP)) * STEP
    delta = region - old
    shifted = 0
    for where, offset in pointers:
        if offset >= end:
            struct.pack_into("<i", index, where, offset + delta)
            shifted += 1
    shifted += shift_textures(index, end, delta)    # the Charge Boots texture (splice_boots.py) lies past the sign
    new_core = core[:start] + blob + bytes(region - len(blob)) + core[end:]
    new_stream = recompress(new_core, wrench, work)
    if wad(new_stream) != new_core:
        raise SystemExit("the recompressed core data does not decode to the new core")
    struct.pack_into("<I", index, 0x88, len(new_stream))
    struct.pack_into("<I", index, 0x8C, len(new_core))
    data.update(core_index=bytes(index), core_data=new_stream)

    # The archive laid out again as splice_boots.splice_level does: sections in order, each at a
    # 0x100 boundary, the table at the start rewritten, padded to whole sectors.
    out = bytearray(head)
    words = []
    for n, (o, s) in zip(names, sections):
        if not s:
            words.append((0, 0))
            continue
        while len(out) % 0x100:
            out.append(0)
        words.append((len(out), len(data[n])))
        out += data[n]
    for i, (o, s) in enumerate(words):
        struct.pack_into("<ii", out, 8 * i, o, s)
    while len(out) % SECTOR:
        out.append(0)
    return bytes(out), {"at": start, "old": old, "new": len(blob), "grew": delta, "shifted": shifted}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", type=Path)
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("--class-core", type=Path, default=ROOT / "build/sign_bank/moby_classes/1891_gadgetron_sign/core.bin")
    ap.add_argument("--retail", type=Path, default=None, help="the retail image, for its archive size (default OpenRAC's PAL baserom)")
    ap.add_argument("--wrench", type=Path, default=ROOT.parents[2] / "wrench/bin/wrenchbuild.exe")
    args = ap.parse_args()
    sys.path.insert(0, str(args.openrac / "editor"))
    from formats import wad
    from level import SECTION_NAMES

    if not args.class_core.is_file():
        raise SystemExit(f"missing {args.class_core} (tools/gen_sign.py)")
    blob = args.class_core.read_bytes()
    retail = args.retail or args.openrac / "games/rac1/baserom/SCES_509.16.iso"
    work = ROOT / "build/splice"
    work.mkdir(parents=True, exist_ok=True)

    def level_range(f):
        f.seek(TOC_LBA * SECTOR)
        toc = f.read(6 * SECTOR)
        header_lba, _ = struct.unpack_from("<2I", toc, LEVEL_TABLE + 8 * LEVEL)
        f.seek(header_lba * SECTOR + 8)
        return (header_lba, *struct.unpack("<2I", f.read(8)))

    with retail.open("rb") as f:
        _, retail_lba, retail_sectors = level_range(f)
    with args.image.open("r+b") as f:
        f.seek(0, 2)
        image_end = f.tell()
        if image_end % SECTOR:
            raise SystemExit("the image is not a whole number of sectors")
        header_lba, data_lba, data_sectors = level_range(f)
        f.seek(data_lba * SECTOR)
        archive = f.read(data_sectors * SECTOR)
        new, info = replace_class(archive, SECTION_NAMES, blob, wad, args.wrench, work)
        if len(new) > retail_sectors * SECTOR:
            raise SystemExit(f"the new archive ({len(new):#x} bytes) is larger than the retail one ({retail_sectors * SECTOR:#x})")
        lba = image_end // SECTOR
        if data_lba == retail_lba:  # in the retail archive's sectors, which the new one fits: overwrite it
            f.seek(data_lba * SECTOR)
            f.write(new + bytes(retail_sectors * SECTOR - len(new)))
            f.seek(header_lba * SECTOR + 8)
            f.write(struct.pack("<2I", data_lba, len(new) // SECTOR))
            print(f"level {LEVEL}: class {CLASS_ID} at core {info['at']:#x}, {info['old']:#x} -> {info['new']:#x} bytes "
                  f"(core +{info['grew']:#x}, {info['shifted']} offsets shifted), archive {len(new):#x} bytes "
                  f"(retail {retail_sectors * SECTOR:#x}) in place at sector {data_lba}; image still {lba} sectors")
            return 0
        if data_lba + data_sectors + TAIL_SECTORS == lba and len(new) <= data_sectors * SECTOR:
            lba = data_lba  # the level's archive is the image's last one and the new one fits: overwrite it
        f.seek(lba * SECTOR)
        f.write(new)
        f.seek(header_lba * SECTOR + 8)
        f.write(struct.pack("<2I", lba, len(new) // SECTOR))
        end = max(lba + len(new) // SECTOR, image_end // SECTOR - TAIL_SECTORS)
        f.seek(end * SECTOR)
        f.write(bytes(TAIL_SECTORS * SECTOR))
        total = end + TAIL_SECTORS
        f.truncate(total * SECTOR)
        f.seek(16 * SECTOR + 80)
        f.write(struct.pack("<I", total) + struct.pack(">I", total))
    print(f"level {LEVEL}: class {CLASS_ID} at core {info['at']:#x}, {info['old']:#x} -> {info['new']:#x} bytes "
          f"(core +{info['grew']:#x}, {info['shifted']} offsets shifted), archive {len(new):#x} bytes "
          f"(retail {retail_sectors * SECTOR:#x}) at sector {lba}; image now {total} sectors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
