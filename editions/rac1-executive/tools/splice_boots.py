#!/usr/bin/env python3
"""
Add the Charge Boots class to every level of an image without re-laying out
the disc: each level's own data archive gets the class and its one texture,
and is written back over the old one, in the sectors it had.

A level's data archive (level header +0x08, read whole by func_00204C60 into
a buffer sized from it) holds eleven sections listed at its start as
{offset, size} (OpenRAC editor/level.py SECTION_NAMES). Three change, each in
the one way the game accepted (2026-10-17/18, Hoven, isolated step by step in
PCSX2; every other way hung the load at stage 7, waiting for the sound bank):

  core_data   WAD-compressed. Decoded; the class blob and the texture's pixels
              are inserted where the moby class blobs end, inside the part of
              the core the engine keeps (below +0x7C, wrench's scene_view_size,
              which follows the moby, tie and shrub class blobs; Ratchet's
              sequences and the gadget blobs come after it). Placed after the
              gadget blobs, past +0x7C, the class hung the load. Every core
              offset past the insertion moves up (core_pointers). Compressed
              again with wrench's compressor, about 3.5% smaller than retail's
              (Hoven: 12.75 MB against 13.21 MB), so every new archive is
              smaller than the retail one; decoded back with OpenRAC's decoder,
              which enforces the game's rules, and compared.
  core_index  raw. The moby class table must stay where it is, right after
              the header (moved, even unchanged, it hangs the load): it grows
              in place by one entry and the tables after it move down 32
              bytes, the header's index pointers with them (INDEX_POINTERS).
              The moby texture table and the GS RAM upload list may move: they
              are copied to the end with one more entry each. +0x88 and +0x8C
              take the new compressed and decompressed sizes.
  gs_ram      raw. The texture's palette (256 RGBA entries in the GS's CSM1
              order, alpha 0..0x80) is appended at a 0x100 boundary; the game
              uploads the list in order, advancing by each entry's size, which
              lands it at its file offset (checked on retail and here).

The texture is type 1 (one mip level, no mip images), as every texture of an
item Ratchet carries or wears is on the retail disc. Everything else in the
archive, the level code with the edition's patches included, is copied
byte for byte. The new archive is never larger than the old one (the script
refuses it otherwise), so it is written at the old one's first sector, the
sectors it leaves free are zeroed, and the level header's sector count takes
the new size; the image keeps its size. (Until 2026-10-20 each archive was
appended at the end of the image and the old one left unused: 320 MB more.)

Inputs: the class core (tools/gen_boots.py writes it, converted from R&C2 by
the wrench fork's mobyconvert) and the class's texture, an indexed PNG from
wrench's unpack of the R&C2 disc.

  python tools/splice_boots.py IMAGE [--openrac DIR] [--wrench wrench/bin/wrenchbuild.exe]
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
EDITOR = HERE.parents[2] / "common/openrac_editor"   # OpenRAC's disc readers, carried (common/deps.py)
ROOT = HERE.parent
SECTOR = 2048
TOC_LBA, LEVEL_TABLE = 1500, 0x28C8
TAIL_SECTORS = 32
CLASS_ID = 2000
MAX_CLASSES = 224          # moby_class_resources[224]
MAX_SLOT = 0xFE            # texture slots are bytes; 0xFF ends a class's list


def csm1(i: int) -> int:
    return i ^ 0x18 if ((i >> 3) & 1) != ((i >> 4) & 1) else i


def indexed_png(path: Path):
    """(width, height, pixel indices, 256 RGBA tuples) of an 8-bit indexed PNG."""
    d = path.read_bytes()
    pos, idat, plte, trns = 8, b"", b"", b""
    while pos < len(d):
        n, kind = struct.unpack_from(">I4s", d, pos)
        body = d[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">2I5B", body)
        elif kind == b"PLTE":
            plte = body
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    if depth != 8 or ctype != 3 or interlace:
        raise SystemExit(f"{path}: expected an 8-bit indexed, non-interlaced PNG")
    raw = zlib.decompress(idat)
    rows, prev = [], bytearray(w)
    for y in range(h):
        f = raw[y * (w + 1)]
        line = bytearray(raw[y * (w + 1) + 1:(y + 1) * (w + 1)])
        for i in range(w):
            a = line[i - 1] if i else 0
            b = prev[i]
            c = prev[i - 1] if i else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append(bytes(line))
        prev = line
    colours = []
    for i in range(256):
        rgb = tuple(plte[3 * i:3 * i + 3]) if 3 * i + 3 <= len(plte) else (0, 0, 0)
        alpha = trns[i] if i < len(trns) else 255
        colours.append(rgb + (alpha,))
    return w, h, b"".join(rows), colours


def gs_palette(colours) -> bytes:
    """256 RGBA32 entries as the GS reads them: CSM1 order, alpha 0..0x80 (wrench's divide_alphas)."""
    out = bytearray()
    for i in range(256):
        r, g, b, a = colours[csm1(i)]
        out += bytes((r, g, b, 0x80 if a == 0xFF else a // 2))
    return bytes(out)


def recompress(data: bytes, wrench: Path, work: Path) -> bytes:
    """DATA as a WAD stream, by wrench's compressor (wrenchbuild compress)."""
    import subprocess
    raw, packed = work / "core.bin", work / "core.wad"
    raw.write_bytes(data)
    result = subprocess.run([str(wrench), "compress", str(raw), "-o", str(packed)], capture_output=True, text=True)
    if result.returncode != 0 or not packed.is_file():
        raise SystemExit(f"wrenchbuild compress failed: {result.stdout}{result.stderr}")
    return packed.read_bytes()


def table(index: bytes, at: int, size: int):
    count, offset = struct.unpack_from("<II", index, at)
    return count, offset, [index[offset + size * i:offset + size * (i + 1)] for i in range(count)]


# Header words that are offsets into the core index itself: the ten {count, offset} tables'
# offsets, then the particle definitions, sound remap, ratchet sequence and gadget tables.
INDEX_POINTERS = (0x04, 0x1C, 0x24, 0x2C, 0x34, 0x3C, 0x44, 0x4C, 0x54, 0x5C, 0x6C, 0x70, 0x78, 0x84)


def core_pointers(index: bytes) -> list[tuple[int, int]]:
    """Every core data offset the core index holds, as (byte position in the index, offset):
    the block pointers (+0x08 terrain, +0x0C occlusion, +0x10 sky, +0x14 collision, +0x60
    textures, +0x64 particle bank, +0x68 effect bank, +0x74, +0x7C the end of the resident part
    (wrench's scene_view_size), +0xA0..+0xA8), each moby, tie and shrub class blob, each
    gadget blob (+0x80 count, +0x84 table) and each of the 256 ratchet sequences (+0x78).
    The list is wrench's level core header (LevelCoreHeader) and OpenRAC's core block bounds."""
    out = []
    for at in (0x08, 0x0C, 0x10, 0x14, 0x60, 0x64, 0x68, 0x74, 0x7C, 0xA0, 0xA4, 0xA8):
        v, = struct.unpack_from("<i", index, at)
        if v > 0:
            out.append((at, v))
    for at, size in ((0x18, 32), (0x20, 32), (0x28, 48)):
        count, offset = struct.unpack_from("<II", index, at)
        for i in range(count):
            v, = struct.unpack_from("<i", index, offset + size * i)
            if v > 0:
                out.append((offset + size * i, v))
    count, offset = struct.unpack_from("<II", index, 0x80)
    for i in range(count):
        v, = struct.unpack_from("<i", index, offset + 16 * i)
        if v > 0:
            out.append((offset + 16 * i, v))
    seqs, = struct.unpack_from("<I", index, 0x78)
    if seqs:
        for i in range(256):
            v, = struct.unpack_from("<i", index, seqs + 4 * i)
            if v > 0:
                out.append((seqs + 4 * i, v))
    return out


# The texture tables {count, offset} of 16-byte entries (tfrag, moby, tie, shrub, part, fx),
# each entry's first word the pixels' offset from textures_base (+0x60).
TEXTURE_TABLES = (0x30, 0x38, 0x40, 0x48, 0x50, 0x58)


def shift_textures(index: bytearray, at: int, delta: int) -> int:
    """Move up by DELTA every texture whose pixels start at core offset AT or later (offsets
    count from textures_base, not the core's start, so core_pointers leaves them out). The
    Charge Boots texture lies past the moby class blobs; a class that grows before it (the
    Kalebo III sign, tools/splice_sign.py) must move it too. Returns how many moved."""
    base, = struct.unpack_from("<I", index, 0x60)
    moved = 0
    for table_at in TEXTURE_TABLES:
        count, offset = struct.unpack_from("<II", index, table_at)
        for i in range(count):
            v, = struct.unpack_from("<i", index, offset + 16 * i)
            if base + v >= at:
                struct.pack_into("<i", index, offset + 16 * i, v + delta)
                moved += 1
    return moved


def splice_level(archive: bytes, names, core_class: bytes, pixels: bytes, palette: bytes,
                 width: int, height: int, wad, wrench: Path, work: Path, stage: int = 4) -> tuple[bytes, dict]:
    """STAGE (for isolating a load failure): 1 insert the bytes into the core only, 2 also add
    the palette and the texture entry, 4 also add the class."""
    sections = [struct.unpack_from("<ii", archive, 8 * i) for i in range(len(names))]
    data = {n: archive[o:o + s] for n, (o, s) in zip(names, sections)}
    first = min(o for o, s in sections if s)
    head = archive[:first]

    index = bytearray(data["core_index"])
    gs = bytearray(data["gs_ram"])
    stream = data["core_data"]
    core_size, = struct.unpack_from("<I", index, 0x8C)

    n_cls, _, classes = table(index, 0x18, 32)
    n_tex, _, textures = table(index, 0x38, 16)
    n_gs, _, uploads = table(index, 0x00, 16)
    if any(struct.unpack_from("<i", e, 4)[0] == CLASS_ID for e in classes):
        raise SystemExit(f"class {CLASS_ID} is already in this level")
    if n_cls + 1 > MAX_CLASSES or n_tex > MAX_SLOT:
        raise SystemExit(f"no room: {n_cls} classes, {n_tex} moby textures")
    # The upload list must already be in order (the game ignores the stored addresses).
    cursor = 0
    for e in uploads:
        psm, packed, address, offset = struct.unpack("<IIII", e)
        if address != cursor:
            raise SystemExit("the GS RAM upload list is not sequential")
        w, h = packed & 0xFFFF, packed >> 16
        cursor += max(w * h, 0x100) if psm == 0x13 else 0x200 if psm == 2 else 0x400
    if cursor != len(gs):
        raise SystemExit(f"the GS RAM image is {len(gs):#x} bytes but its upload list covers {cursor:#x}")

    # core data: the class blob and the texture's pixels inserted where the moby class blobs end,
    # inside the part of the core the engine keeps resident (+0x7C, below which wrench writes the
    # moby, tie and shrub class blobs; Ratchet's sequences and the gadget blobs follow it, in
    # memory the engine reuses: a class placed there hung the load), and every core offset past
    # the insertion moved up. Texture offsets count from textures_base, which stays put.
    retail_core = wad(stream)
    if len(retail_core) != core_size:
        raise SystemExit("the core data does not decode to the size its index gives")
    textures_base, = struct.unpack_from("<I", index, 0x60)
    resident_end, = struct.unpack_from("<I", index, 0x7C)
    moby_starts = [struct.unpack_from("<i", e)[0] for e in classes if struct.unpack_from("<i", e)[0] > 0]
    pointers = core_pointers(index)                       # (where in the index, core offset)
    after = [o for _, o in pointers if o > max(moby_starts)]
    insert_at = min(after)
    if not textures_base < insert_at <= resident_end:
        raise SystemExit(f"unexpected core layout: textures at {textures_base:#x}, moby classes end at {insert_at:#x}, "
                         f"resident part ends at {resident_end:#x}")
    cls_at = insert_at
    tex_at = insert_at + ((len(core_class) + 0xFF) & ~0xFF)
    delta = tex_at - insert_at + ((len(pixels) + 0xFF) & ~0xFF)
    shifted = 0
    for where, offset in pointers:
        if offset >= insert_at:
            struct.pack_into("<i", index, where, offset + delta)
            shifted += 1
    shifted += shift_textures(index, insert_at, delta)    # none on the retail disc
    insert = bytearray(delta)
    insert[:len(core_class)] = core_class
    insert[tex_at - insert_at:tex_at - insert_at + len(pixels)] = pixels
    new_core = retail_core[:insert_at] + bytes(insert) + retail_core[insert_at:]
    new_core_size = len(new_core)
    new_stream = recompress(new_core, wrench, work)

    # GS RAM: the palette
    pal_at = len(gs)
    gs += palette
    upload = struct.pack("<IIII", 0, 0, pal_at, pal_at)

    # core index. The moby class table stays where it is, right after the header (moved
    # elsewhere, even unchanged, it hangs the load): it grows in place by one entry and every
    # table after it moves down 32 bytes, their pointers with them (each index pointer of the
    # header: the tables' offsets, +0x6C particle definitions, +0x70 sound remap, +0x78 ratchet
    # sequences, +0x84 gadgets; the blocks they point to use offsets relative to themselves).
    # The texture table and the GS RAM upload list, which may move, are copied to the end with
    # one more entry each.
    texture_entry = struct.pack("<iHHHHhh", tex_at - textures_base, width, height, 1, pal_at // 0x100, -1, -1)
    class_entry = struct.pack("<iiii", cls_at, CLASS_ID, 0, 0) + bytes([n_tex] + [0xFF] * 15)
    _, classes_at, _ = table(index, 0x18, 32)
    grow_at = classes_at + 32 * n_cls
    if stage >= 4:
        for at in INDEX_POINTERS:
            value, = struct.unpack_from("<I", index, at)
            if value >= grow_at:
                struct.pack_into("<I", index, at, value + 32)
        index[grow_at:grow_at] = class_entry
        struct.pack_into("<I", index, 0x18, n_cls + 1)
    changes = [(0x00, uploads, upload), (0x38, textures, texture_entry)] if stage >= 2 else []
    if stage < 2:
        del gs[pal_at:]
    for at, entries, extra in changes:
        while len(index) % 0x10:
            index.append(0)
        offset = len(index)
        index += b"".join(entries) + extra
        struct.pack_into("<II", index, at, len(entries) + 1, offset)
    struct.pack_into("<I", index, 0x88, len(new_stream))
    struct.pack_into("<I", index, 0x8C, new_core_size)
    while len(index) % 0x10:
        index.append(0)

    # check: the new stream decodes, by the game's rules, to the new core
    if wad(new_stream) != new_core:
        raise SystemExit("the recompressed core data does not decode to the new core")

    data.update(core_index=bytes(index), gs_ram=bytes(gs), core_data=new_stream)
    out = bytearray(head)
    table_words = []
    for n, (o, s) in zip(names, sections):
        if not s:
            table_words.append((0, 0))
            continue
        while len(out) % 0x100:
            out.append(0)
        table_words.append((len(out), len(data[n])))
        out += data[n]
    for i, (o, s) in enumerate(table_words):
        struct.pack_into("<ii", out, 8 * i, o, s)
    while len(out) % SECTOR:
        out.append(0)
    if len(out) > len(archive):
        raise SystemExit(f"the new archive ({len(out):#x} bytes) is larger than the retail one ({len(archive):#x})")
    return bytes(out), {"classes": n_cls + 1, "texture": n_tex, "core_grew": delta, "shifted": shifted,
                        "insert": insert_at, "stream_grew": len(new_stream) - len(stream),
                        "archive": len(out) - len(archive)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", type=Path)
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("--class-core", type=Path,
                    default=ROOT / f"build/boots_bank/moby_classes/{CLASS_ID}_charge_boots/core.bin")
    ap.add_argument("--texture", type=Path, default=None,
                    help="indexed PNG (default: R&C2 class 3696's, wrench unpack)")
    ap.add_argument("--wrench", type=Path, default=ROOT.parents[2] / "wrench/bin/wrenchbuild.exe")
    args = ap.parse_args()
    sys.path.insert(0, str(EDITOR))
    from formats import wad
    from level import SECTION_NAMES

    texture = args.texture or args.openrac / "assets/wrench/rac2-ntsc/moby_classes/unsorted/3696/0.png"
    if not args.class_core.is_file():
        raise SystemExit(f"missing {args.class_core} (tools/gen_boots.py)")
    core_class = args.class_core.read_bytes()
    width, height, pixels, colours = indexed_png(texture)
    if width & (width - 1) or height & (height - 1) or width < 8:
        raise SystemExit(f"{texture}: {width}x{height} is not a power-of-two texture")
    palette = gs_palette(colours)
    work = ROOT / "build/splice"
    work.mkdir(parents=True, exist_ok=True)

    with args.image.open("r+b") as f:
        f.seek(0, 2)
        image_end = f.tell()
        if image_end % SECTOR:
            raise SystemExit("the image is not a whole number of sectors")
        f.seek(TOC_LBA * SECTOR)
        toc = f.read(6 * SECTOR)
        for level in range(19):
            header_lba, _ = struct.unpack_from("<2I", toc, LEVEL_TABLE + 8 * level)
            f.seek(header_lba * SECTOR + 8)
            data_lba, data_sectors = struct.unpack("<2I", f.read(8))
            f.seek(data_lba * SECTOR)
            archive = f.read(data_sectors * SECTOR)
            new, info = splice_level(archive, SECTION_NAMES, core_class, pixels, palette, width, height, wad,
                                     args.wrench, work)
            # In place: splice_level refuses an archive larger than the old one.
            f.seek(data_lba * SECTOR)
            f.write(new + bytes(len(archive) - len(new)))
            f.seek(header_lba * SECTOR + 8)
            f.write(struct.pack("<2I", data_lba, len(new) // SECTOR))
            print(f"level {level:2d}: {info['classes']} classes, "
                  f"class at {info['insert']:#x} ({info['shifted']} offsets shifted), "
                  f"texture slot {info['texture']}, core +{info['core_grew']:#x}, "
                  f"stream {info['stream_grew']:+#x}, archive {info['archive']:+#x} against retail, "
                  f"at sector {data_lba}")
    print(f"{args.image.name}: 19 levels spliced in place, image still {image_end // SECTOR} sectors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
