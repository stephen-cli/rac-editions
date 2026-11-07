#!/usr/bin/env python3
"""
Extra moby classes for levels whose data lacks them (a stage of tools/patch_levels.py).

A level's data holds its moby classes in the core, which the engine keeps
whole in RAM, each listed in the core index's class table with its blob's
offset. An extra class goes in the same way as the Charge Boots class
(tools/splice_boots.py): the blob and its textures' pixels where the moby
class blobs end, and a class table entry with its offset, so that the
level's loader registers it like its own. For the Randomizer's stand-ins of
items in the world (src/randomizer_world.c): the infobot (class 750) on the
five levels without it (Aridia, Gaspar, Pokitaru, Kalebo III, Veldin), from
Novalis's data, and the items that exist on one planet only, each from its
own planet: the Hoverboard (84, Aridia), the Zoomerator (1002, Rilgar), the
Nanotech can (645, Orxon), the Codebot (1428, Quartu), the Raritanium (37,
Hoven). The classes are copied from their levels as the patch leaves them
(after the Charge Boots and the fonts), and each level gets the palettes in
GS RAM and, in the core index, the texture entries, the class entry, and a
sound span for every class (the boots' class had none).

Each moves the level's heap and its last block, the effect pool, nearer the
stack by its blob and its pixels: the infobot 0x1A200 bytes, the five items
some 139 KB together. A level too near the stack goes without the items
(SKIP), and their stand-ins there are the chicken. A level's class table has
room for 222 classes (level_data.MAX_CLASSES): a level near that gets the
extra classes in EXTRA's order while they fit (Umbris all but the Codebot,
Batalia the can and the Hoverboard, Gemlik the can). A first version
carried the blob compressed and loaded it at run time into a third part of the
gadget buffer, whose room came out of the HUD arena: the menus' icons no longer
fit there and decompressed over address 0.
"""
import struct
from pathlib import Path

from level_data import (INDEX_POINTERS, MAX_CLASSES, MAX_SLOT, SECTOR, Level, check_uploads, core_pointers,
                        level_archives, moby_classes_end, parts, section_names, shift_textures, table)

# class id -> (name, the level whose data it is copied from); what a level gets when its data lacks the class, in
# this order while its class table has room: the infobot first (it hosts checks), then the can (two items' model),
# the Codebot last (the largest)
EXTRA = {
    750: ("infobot", 1),
    645: ("Nanotech can", 10),      # the Orxon Nanotech vendor's
    84: ("Hoverboard", 2),          # Skid's, from Aridia's cutscene
    1002: ("Zoomerator", 5),        # the Rilgar race's prize
    37: ("Raritanium", 12),         # Hoven's pickup
    1428: ("Codebot", 15),          # the pickup's, as on Drek's Fleet
}
# Levels too near the stack for the one-planet items (their copies would move the effect pool up by some 139 KB):
# there those items' stand-ins stay the chicken (src/randomizer_world.c). Measured free RAM above the pool in
# PCSX2, 2026-10-30 (docs/FEATURES.md).
ONE_PLANET_ITEMS = (84, 1002, 645, 1428, 37)
SKIP = {
    5: ONE_PLANET_ITEMS,                # Rilgar: 77 KB
    6: ONE_PLANET_ITEMS,                # Blarg: 82 KB
    15: ONE_PLANET_ITEMS,               # Quartu: 135 KB
}
SPAN_FIXED = 2                      # the sound block's spans before the classes': the slot table, the remap


def missing(index: bytes, level: int) -> tuple[list[int], list[int]]:
    """The extra classes LEVEL's data lacks: those it gets, and those its class table has no room for."""
    _, _, classes = table(index, 0x18, 32)
    ids = [struct.unpack_from("<i", e, 4)[0] for e in classes]
    lacks = [cid for cid in EXTRA if cid not in ids and cid not in SKIP.get(level, ())]
    room = MAX_CLASSES - len(ids)
    if 750 in lacks[room:]:
        raise SystemExit(f"level {level}: no room for the infobot class")
    return lacks[:room], lacks[room:]


def texture_size(w: int, h: int, mips: int) -> int:
    return sum((w >> i) * (h >> i) for i in range(max(mips, 1)))


def disc_class(iso: Path, level: int, cid: int, decode) -> tuple[bytes, list]:
    """CID's blob and its textures (class_source) as level LEVEL holds them on the PAL disc ISO; DECODE decodes
    the level's core data (Wad.decode)."""
    with iso.open("rb") as f:
        _, lba, sectors = level_archives(f)[level]
        f.seek(lba * SECTOR)
        _, data, _ = parts(f.read(sectors * SECTOR), section_names())
    return class_source(decode(data["core_data"]), data["core_index"], data["gs_ram"], cid, level)


def class_source(core: bytes, index: bytes, gs: bytes, cid: int, level: int):
    """CID's blob and its textures, from a level that has it: [(entry, pixels, palette, upload)]."""
    _, _, classes = table(index, 0x18, 32)
    _, _, textures = table(index, 0x38, 16)
    _, _, uploads = table(index, 0x00, 16)
    textures_base, = struct.unpack_from("<I", index, 0x60)
    resident_end, = struct.unpack_from("<I", index, 0x7C)
    starts = sorted(struct.unpack_from("<i", e)[0] for e in classes if struct.unpack_from("<i", e)[0] > 0)
    entry = next((e for e in classes if struct.unpack_from("<i", e, 4)[0] == cid), None)
    if entry is None or struct.unpack_from("<i", entry)[0] <= 0:
        raise SystemExit(f"class {cid} has no data in level {level}")
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


def add_extra_classes(level: Level, sources: dict) -> dict | None:
    """The extra classes LEVEL lacks added to it, or None when it lacks none. SOURCES: class id -> (blob, textures)
    from class_source."""
    index, gs, core = level.index, level.gs, level.core
    wanted, left_out = missing(index, level.number)
    if not wanted:
        return None
    n_cls, classes_at, _ = table(index, 0x18, 32)
    n_tex, _, _ = table(index, 0x38, 16)
    if n_cls + len(wanted) > MAX_CLASSES:
        raise SystemExit(f"level {level.number}: no room: {n_cls} classes")
    check_uploads(index, gs)
    textures_base, = struct.unpack_from("<I", index, 0x60)
    insert_at = moby_classes_end(index)

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
    for where, offset in core_pointers(index):
        if offset >= insert_at:
            struct.pack_into("<i", index, where, offset + delta)
            shifted += 1
    shifted += shift_textures(index, insert_at, delta)
    # The tables copied to the end below, read again now that their core offsets are shifted (the Charge Boots
    # texture and the gadget blobs lie past the insertion; a first version copied the entries read before the
    # shift, and the hero's held weapon decompressed garbage into the trap at its level's load).
    _, _, textures = table(index, 0x38, 16)
    _, _, uploads = table(index, 0x00, 16)
    level.core = core[:insert_at] + bytes(insert) + core[insert_at:]
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
    while len(index) % 0x10:
        index.append(0)
    return {"classes": wanted, "left_out": left_out, "core_grew": delta, "shifted": shifted, "insert": insert_at}
