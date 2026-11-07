#!/usr/bin/env python3
"""
The Charge Boots class in a level's data (a stage of tools/patch_levels.py), and the seed code's glyphs in its
menu fonts.

Each level's own data gets the class and its one texture, in the one way the game accepted (2026-10-17/18,
Hoven, isolated step by step in PCSX2; every other way hung the load at stage 7, waiting for the sound bank):

  core_data   the class blob and the texture's pixels are inserted where the moby class blobs end, inside
              the part of the core the engine keeps (below +0x7C, wrench's scene_view_size, which follows
              the moby, tie and shrub class blobs; Ratchet's sequences and the gadget blobs come after it).
              Placed after the gadget blobs, past +0x7C, the class hung the load. Every core offset past
              the insertion moves up (core_pointers).
  core_index  the moby class table must stay where it is, right after the header (moved, even unchanged,
              it hangs the load): it grows in place by one entry and the tables after it move down 32
              bytes, the header's index pointers with them (INDEX_POINTERS). The moby texture table and
              the GS RAM upload list may move: they are copied to the end with one more entry each.
  gs_ram      the texture's palette (256 RGBA entries in the GS's CSM1 order, alpha 0..0x80) is appended
              at a 0x100 boundary; the game uploads the list in order, advancing by each entry's size,
              which lands it at its file offset (check_uploads).

The texture is type 1 (one mip level, no mip images), as every texture of an item Ratchet carries or wears is on
the retail disc. Its pixels and palette are read from the player's R&C2 disc as its level holds them
(common/rac2_anim.py, rac2_class_texture); the class core is tools/gen_boots.py's.

Every level also gets the seed code's glyphs in its three menu fonts (paint_fonts), with or without the boots.
"""
import struct

from level_data import INDEX_POINTERS, MAX_CLASSES, MAX_SLOT, Level, check_uploads, core_pointers, \
    moby_classes_end, shift_textures, table

CLASS_ID = 2000
RAC2_CLASS = 3696           # the Charge Boots in R&C2 (tools/gen_boots.py)
FONT_TEXTURES = (1, 2, 3)   # the level's effect textures that are the menu fonts (normal, small, large)


def paint_fonts(level: Level) -> None:
    """The seed code's glyphs (tools/gen_glyphs.py) painted into the level's three menu fonts: the pause menu's
    Save and Load pages show a Randomizer save's seed (src/goodies.c), and a level draws text from its own copies
    of the fonts, which have the same empty corner and palette as the title screen's."""
    import gen_glyphs
    from formats import Texture
    _, fx_table = struct.unpack_from("<II", level.index, 0x58)
    bank, = struct.unpack_from("<I", level.index, 0x68)
    core = bytearray(level.core)
    for i in FONT_TEXTURES:
        pal, pix, w, h = struct.unpack_from("<4i", level.index, fx_table + 16 * i)
        if (w, h) != (256, 128):
            raise SystemExit(f"level {level.number}: effect texture {i} is {w}x{h}, not a 256x128 font")
        sheet = level.core[bank + pix:bank + pix + w * h]
        colours = Texture(w, h, sheet, level.core[bank + pal:bank + pal + 1024]).colours
        core[bank + pix:bank + pix + w * h] = gen_glyphs.paint(sheet, w, colours)
    level.core = bytes(core)


def add_boots(level: Level, core_class: bytes, pixels: bytes, palette: bytes, width: int, height: int) -> dict:
    """The Charge Boots class (CORE_CLASS) and its texture (PIXELS, PALETTE, WIDTH x HEIGHT) added to LEVEL."""
    index, gs = level.index, level.gs
    n_cls, classes_at, classes = table(index, 0x18, 32)
    n_tex, _, textures = table(index, 0x38, 16)
    _, _, uploads = table(index, 0x00, 16)
    if any(struct.unpack_from("<i", e, 4)[0] == CLASS_ID for e in classes):
        raise SystemExit(f"level {level.number}: class {CLASS_ID} is already in it")
    if n_cls + 1 > MAX_CLASSES or n_tex > MAX_SLOT:
        raise SystemExit(f"level {level.number}: no room: {n_cls} classes, {n_tex} moby textures")
    check_uploads(index, gs)

    # core data: the class blob and the texture's pixels where the moby class blobs end, every core offset past
    # them moved up; texture offsets count from textures_base, which stays put
    insert_at = moby_classes_end(index)
    textures_base, = struct.unpack_from("<I", index, 0x60)
    tex_at = insert_at + ((len(core_class) + 0xFF) & ~0xFF)
    delta = tex_at - insert_at + ((len(pixels) + 0xFF) & ~0xFF)
    shifted = 0
    for where, offset in core_pointers(index):
        if offset >= insert_at:
            struct.pack_into("<i", index, where, offset + delta)
            shifted += 1
    shifted += shift_textures(index, insert_at, delta)    # none on the retail disc
    insert = bytearray(delta)
    insert[:len(core_class)] = core_class
    insert[tex_at - insert_at:tex_at - insert_at + len(pixels)] = pixels
    level.core = level.core[:insert_at] + bytes(insert) + level.core[insert_at:]

    # GS RAM: the palette
    pal_at = len(gs)
    gs += palette
    upload = struct.pack("<IIII", 0, 0, pal_at, pal_at)

    # core index: the class table grows in place by one entry, every table after it moving down 32 bytes with
    # its pointer (the blocks the pointers lead to use offsets relative to themselves); the texture table and the
    # upload list are copied to the end with one more entry each
    texture_entry = struct.pack("<iHHHHhh", tex_at - textures_base, width, height, 1, pal_at // 0x100, -1, -1)
    class_entry = struct.pack("<iiii", insert_at, CLASS_ID, 0, 0) + bytes([n_tex] + [0xFF] * 15)
    grow_at = classes_at + 32 * n_cls
    for at in INDEX_POINTERS:
        value, = struct.unpack_from("<I", index, at)
        if value >= grow_at:
            struct.pack_into("<I", index, at, value + 32)
    index[grow_at:grow_at] = class_entry
    struct.pack_into("<I", index, 0x18, n_cls + 1)
    for at, entries, extra in ((0x00, uploads, upload), (0x38, textures, texture_entry)):
        while len(index) % 0x10:
            index.append(0)
        offset = len(index)
        index += b"".join(entries) + extra
        struct.pack_into("<II", index, at, len(entries) + 1, offset)
    while len(index) % 0x10:
        index.append(0)
    return {"classes": n_cls + 1, "texture": n_tex, "core_grew": delta, "shifted": shifted, "insert": insert_at}
