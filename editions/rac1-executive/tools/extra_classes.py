#!/usr/bin/env python3
"""
Extra moby classes for levels whose data lacks them.

A level's data holds its moby classes in the core, which the engine keeps
whole in RAM, each listed in the core index's class table with its blob's
offset. An extra class goes in the same way, as tools/splice_boots.py puts the
Charge Boots class into every level: the blob and its textures' pixels where
the moby class blobs end, and a class table entry with its offset, so that the
level's loader registers it like its own. Today: the infobot (class 750) on the
five levels without it (Aridia, Gaspar, Pokitaru, Kalebo III, Veldin), taken
from Novalis's data, for the Randomizer's stand-ins of Gold Bolt spots and
world pickups that hold an infobot (src/randomizer_play.c).

It costs those levels its 0x15140-byte blob and 0x5000 bytes of pixels in the
core, which moves the level's heap and its last block, the effect pool, that
much nearer the stack: with it they keep 0x40000 or more under the stack (Blarg,
which keeps its own infobot, is the tight level at 82 KB). A first version
carried the blob compressed and loaded it at run time into a third part of the
gadget buffer, whose room came out of the HUD arena: the menus' icons no longer
fit there and decompressed over address 0.

  python tools/extra_classes.py splice IMAGE [--openrac DIR] [--wrench EXE]

splice edits the levels of IMAGE in place (an image tools/pack.sh has written,
after tools/splice_boots.py): the blob and the pixels go into the core, the
palettes into GS RAM, and the core index gets the texture entries, the class
entry, and a sound span for every class (the boots' class had none). A level
whose archive outgrows its sectors moves to new sectors at the end of the image.
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
EDITOR = HERE.parents[2] / "common/openrac_editor"   # OpenRAC's disc readers, carried (common/deps.py)
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from splice_boots import (SECTOR, TOC_LBA, LEVEL_TABLE, MAX_CLASSES, MAX_SLOT, INDEX_POINTERS,   # noqa: E402
                          table, core_pointers, shift_textures, recompress)

EXTRA = {750: "infobot"}            # class id -> name; what a level gets when its data lacks the class
SOURCE_LEVEL = 1                    # Novalis: its data has every extra class
LEVELS = 19
SPAN_FIXED = 2                      # the sound block's spans before the classes': the slot table, the remap
TAIL_SECTORS = 32                   # after a relocated archive: the stream reader reads past the end


def level_archives(f):
    """level -> (header LBA, data LBA, data sectors) from the table of contents."""
    f.seek(TOC_LBA * SECTOR)
    toc = f.read(6 * SECTOR)
    out = {}
    for level in range(LEVELS):
        header_lba, _ = struct.unpack_from("<2I", toc, LEVEL_TABLE + 8 * level)
        f.seek(header_lba * SECTOR + 8)
        data_lba, data_sectors = struct.unpack("<2I", f.read(8))
        out[level] = (header_lba, data_lba, data_sectors)
    return out


def parts(archive: bytes, names):
    """(the archive's head, {section: bytes}) and the sections' (offset, size)."""
    sections = [struct.unpack_from("<ii", archive, 8 * i) for i in range(len(names))]
    data = {n: archive[o:o + s] for n, (o, s) in zip(names, sections)}
    first = min(o for o, s in sections if s)
    return archive[:first], data, sections


def assemble(head: bytes, data: dict, names, sections) -> bytes:
    """The archive back from its sections, each at a 0x100 boundary, whole sectors."""
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
    return bytes(out)


def missing(index: bytes) -> list[int]:
    """The extra classes this level's data lacks."""
    _, _, classes = table(index, 0x18, 32)
    ids = [struct.unpack_from("<i", e, 4)[0] for e in classes]
    return [cid for cid in EXTRA if cid not in ids]


def texture_size(w: int, h: int, mips: int) -> int:
    return sum((w >> i) * (h >> i) for i in range(max(mips, 1)))


def class_source(core: bytes, index: bytes, gs: bytes, cid: int):
    """CID's blob and its textures, from a level that has it: [(entry, pixels, palette, upload)]."""
    _, _, classes = table(index, 0x18, 32)
    _, _, textures = table(index, 0x38, 16)
    _, _, uploads = table(index, 0x00, 16)
    textures_base, = struct.unpack_from("<I", index, 0x60)
    resident_end, = struct.unpack_from("<I", index, 0x7C)
    starts = sorted(struct.unpack_from("<i", e)[0] for e in classes if struct.unpack_from("<i", e)[0] > 0)
    entry = next((e for e in classes if struct.unpack_from("<i", e, 4)[0] == cid), None)
    if entry is None or struct.unpack_from("<i", entry)[0] <= 0:
        raise SystemExit(f"class {cid} has no data in level {SOURCE_LEVEL}")
    start, = struct.unpack_from("<i", entry)
    after = [s for s in starts if s > start]
    blob = core[start:min(after) if after else resident_end]
    out = []
    for slot in entry[16:32]:
        if slot == 0xFF:
            break
        tex = textures[slot]
        offset, w, h, mips, pal, _, _ = struct.unpack("<iHHHHhh", tex)
        pixels = core[textures_base + offset:textures_base + offset + texture_size(w, h, mips)]
        palette = gs[pal * 0x100:pal * 0x100 + 0x400]
        upload = next((u for u in uploads if struct.unpack_from("<I", u, 8)[0] == pal * 0x100), None)
        if upload is None or len(palette) != 0x400:
            raise SystemExit(f"class {cid}: texture slot {slot}'s palette at {pal * 0x100:#x} "
                             "is not in GS RAM's upload list")
        out.append((tex, pixels, palette, upload))
    return blob, out


def sound_spans(index: bytes):
    """The sound block's spans ({s16 off, s16 count} each, offsets from the block) and the block's start:
    the array runs up to the first span's data."""
    block_at, = struct.unpack_from("<I", index, 0x70)
    spans, data_at = [], None
    while True:
        off, count = struct.unpack_from("<hh", index, block_at + 4 * len(spans))
        spans.append((off, count))
        if count > 0 and (data_at is None or off < data_at):
            data_at = off
        if data_at is not None and 4 * len(spans) >= data_at:
            break
        if len(spans) > MAX_CLASSES + SPAN_FIXED:
            raise SystemExit("the sound block's spans do not end")
    return block_at, spans


def rebuilt_sound_block(index: bytes, classes: int) -> bytes:
    """The sound block with a span for each of CLASSES classes after the two fixed ones (the slot table,
    whose entries are 32 bytes, and the remap, 4 bytes an entry like the classes'): the spans the block
    has, then empty ones. The level loader walks exactly SPAN_FIXED + classes spans."""
    block_at, spans = sound_spans(index)
    wanted = SPAN_FIXED + classes
    while len(spans) > wanted and spans[-1] == (0, 0):     # the array's padding, read as spans
        spans.pop()
    if len(spans) > wanted:
        raise SystemExit(f"the sound block has {len(spans)} spans for {classes} classes")
    data = bytearray()
    sizes = []
    for i, (off, count) in enumerate(spans):
        size = count * (32 if i == 0 else 4)
        sizes.append((len(data), count))
        data += index[block_at + off:block_at + off + size]
        while len(data) % 16:
            data.append(0)
    array_size = (4 * wanted + 15) & ~15
    out = bytearray()
    for at, count in sizes:
        out += struct.pack("<hh", array_size + at, count)
    for _ in range(wanted - len(spans)):
        out += struct.pack("<hh", array_size + len(data), 0)
    while len(out) < array_size:
        out.append(0)
    out += data
    if len(out) >= 0x8000:
        raise SystemExit("the sound block is too big for its 16-bit offsets")
    return bytes(out)


def splice_level(archive: bytes, names, sources: dict, wad, wrench: Path, work: Path):
    """ARCHIVE with every extra class it lacks, or None when it lacks none. SOURCES: class id ->
    (blob, textures) from class_source. Returns (archive, report)."""
    head, data, sections = parts(archive, names)
    index = bytearray(data["core_index"])
    gs = bytearray(data["gs_ram"])
    wanted = missing(index)
    if not wanted:
        return None, None
    n_cls, classes_at, classes = table(index, 0x18, 32)
    n_tex, _, textures = table(index, 0x38, 16)
    _, _, uploads = table(index, 0x00, 16)
    if n_cls + len(wanted) > MAX_CLASSES:
        raise SystemExit(f"no room: {n_cls} classes")
    cursor = 0
    for e in uploads:
        psm, packed, address, _ = struct.unpack("<IIII", e)
        if address != cursor:
            raise SystemExit("the GS RAM upload list is not sequential")
        w, h = packed & 0xFFFF, packed >> 16
        cursor += max(w * h, 0x100) if psm == 0x13 else 0x200 if psm == 2 else 0x400
    if cursor != len(gs):
        raise SystemExit(f"the GS RAM image is {len(gs):#x} bytes but its upload list covers {cursor:#x}")

    core_size, = struct.unpack_from("<I", index, 0x8C)
    stream_size = len(data["core_data"])
    core = wad(data["core_data"])
    if len(core) != core_size:
        raise SystemExit("the core data does not decode to the size its index gives")
    textures_base, = struct.unpack_from("<I", index, 0x60)
    resident_end, = struct.unpack_from("<I", index, 0x7C)
    moby_starts = [struct.unpack_from("<i", e)[0] for e in classes if struct.unpack_from("<i", e)[0] > 0]
    pointers = core_pointers(index)
    insert_at = min(o for _, o in pointers if o > max(moby_starts))
    if not textures_base < insert_at <= resident_end:
        raise SystemExit(f"unexpected core layout: textures at {textures_base:#x}, moby classes end at {insert_at:#x}, "
                         f"resident part ends at {resident_end:#x}")

    # The insert: each class's blob, then its textures' pixels, 0x100 apart.
    insert = bytearray()
    new_classes, new_textures, new_uploads = [], [], []
    for cid in wanted:
        blob, texes = sources[cid]
        blob_at = insert_at + len(insert)
        insert += blob
        while len(insert) % 0x100:
            insert.append(0)
        slots = []
        for tex, pixels, palette, upload in texes:
            if n_tex + len(new_textures) > MAX_SLOT:
                raise SystemExit(f"no room: {n_tex} moby textures")
            pix_at = insert_at + len(insert)
            insert += pixels
            while len(insert) % 0x100:
                insert.append(0)
            pal_at = len(gs) + 0x400 * len(new_uploads)
            _, w, h, mips, _, a, b = struct.unpack("<iHHHHhh", tex)
            new_textures.append(struct.pack("<iHHHHhh", pix_at - textures_base, w, h, mips, pal_at // 0x100, a, b))
            psm, packed_wh, _, _ = struct.unpack("<IIII", upload)
            new_uploads.append((struct.pack("<IIII", psm, packed_wh, pal_at, pal_at), palette))
            slots.append(n_tex + len(new_textures) - 1)
        slot_list = bytes(slots + [0xFF] * (16 - len(slots)))
        new_classes.append((blob_at, struct.pack("<iiii", blob_at, cid, 0, 0) + slot_list))
    delta = len(insert)
    shifted = 0
    for where, offset in pointers:
        if offset >= insert_at:
            struct.pack_into("<i", index, where, offset + delta)
            shifted += 1
    shifted += shift_textures(index, insert_at, delta)
    # The tables copied to the end below, read again now that their core offsets are shifted (the Charge Boots
    # texture and the gadget blobs lie past the insertion; a first version copied the entries read before the
    # shift, and the hero's held weapon decompressed garbage into the trap at its level's load).
    _, _, textures = table(index, 0x38, 16)
    _, _, uploads = table(index, 0x00, 16)
    new_core = core[:insert_at] + bytes(insert) + core[insert_at:]
    new_stream = recompress(new_core, wrench, work)
    if wad(new_stream) != new_core:
        raise SystemExit("the recompressed core data does not decode to the new core")
    for upload, palette in new_uploads:
        gs += palette

    # The index: the class table grows in place (moved, it hangs the load; tools/splice_boots.py); the texture
    # and upload tables are copied to the end with their new entries, and the sound block is rebuilt there with
    # a span per class.
    grow_at = classes_at + 32 * n_cls
    grow = len(new_classes) * 32
    for at in INDEX_POINTERS:
        value, = struct.unpack_from("<I", index, at)
        if value >= grow_at:
            struct.pack_into("<I", index, at, value + grow)
    index[grow_at:grow_at] = b"".join(entry for _, entry in new_classes)
    struct.pack_into("<I", index, 0x18, n_cls + len(new_classes))
    for at, entries, extra in ((0x00, uploads, [u for u, _ in new_uploads]), (0x38, textures, new_textures)):
        while len(index) % 0x10:
            index.append(0)
        offset = len(index)
        index += b"".join(entries) + b"".join(extra)
        struct.pack_into("<II", index, at, len(entries) + len(extra), offset)
    block = rebuilt_sound_block(index, n_cls + len(new_classes))
    while len(index) % 0x10:
        index.append(0)
    struct.pack_into("<I", index, 0x70, len(index))
    index += block
    struct.pack_into("<I", index, 0x88, len(new_stream))
    struct.pack_into("<I", index, 0x8C, len(new_core))
    while len(index) % 0x10:
        index.append(0)

    data.update(core_index=bytes(index), gs_ram=bytes(gs), core_data=new_stream)
    out = assemble(head, data, names, sections)
    return out, {"classes": wanted, "core_grew": delta, "shifted": shifted, "insert": insert_at,
                 "stream_grew": len(new_stream) - stream_size, "archive": len(out) - len(archive)}


def splice(args) -> int:
    sys.path.insert(0, str(EDITOR))
    from formats import wad
    from level import SECTION_NAMES
    work = ROOT / "build/splice_classes"
    work.mkdir(parents=True, exist_ok=True)
    with args.image.open("r+b") as f:
        f.seek(0, 2)
        if f.tell() % SECTOR:
            raise SystemExit("the image is not a whole number of sectors")
        archives = level_archives(f)
        _, lba, sectors = archives[SOURCE_LEVEL]
        f.seek(lba * SECTOR)
        _, data, _ = parts(f.read(sectors * SECTOR), SECTION_NAMES)
        core = wad(data["core_data"])
        sources = {cid: class_source(core, data["core_index"], data["gs_ram"], cid) for cid in EXTRA}
        for cid, (blob, texes) in sources.items():
            print(f"class {cid} ({EXTRA[cid]}) from level {SOURCE_LEVEL}: {len(blob):#x} bytes, "
                  f"{len(texes)} textures ({sum(len(p) for _, p, _, _ in texes):#x} bytes)")
        moved = 0
        for level in range(LEVELS):
            header_lba, data_lba, data_sectors = archives[level]
            f.seek(data_lba * SECTOR)
            archive = f.read(data_sectors * SECTOR)
            new, info = splice_level(archive, SECTION_NAMES, sources, wad, args.wrench, work)
            if new is None:
                continue
            if len(new) <= len(archive):
                f.seek(data_lba * SECTOR)
                f.write(new + bytes(len(archive) - len(new)))
                where = f"in place at sector {data_lba}"
            else:
                f.seek(0, 2)
                data_lba = f.tell() // SECTOR
                f.write(new + bytes(TAIL_SECTORS * SECTOR))
                moved += 1
                where = f"moved to new sectors at {data_lba}"
            f.seek(header_lba * SECTOR + 8)
            f.write(struct.pack("<2I", data_lba, len(new) // SECTOR))
            print(f"level {level:2d}: classes {info['classes']} at {info['insert']:#x} "
                  f"({info['shifted']} offsets shifted), core +{info['core_grew']:#x}, "
                  f"archive {info['archive']:+#x} against the one before, {where}")
        f.seek(0, 2)
        print(f"{args.image.name}: {moved} level archives moved to the end; image {f.tell() // SECTOR} sectors")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    s = sub.add_parser("splice")
    s.add_argument("image", type=Path)
    s.add_argument("--wrench", type=Path, default=ROOT.parents[2] / "wrench/bin/wrenchbuild.exe")
    s.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    s.set_defaults(run=splice)
    args = ap.parse_args()
    return args.run(args)


if __name__ == "__main__":
    sys.exit(main())
