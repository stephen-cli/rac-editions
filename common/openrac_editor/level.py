"""Load one level from the disc: its data sections, core assets and textures.

Ratchet & Clank's levels come through load_level (disc.py's table of contents); Going Commando's,
Up Your Arsenal's and Deadlocked's through load_wad_level (wads.py). Both give the same Level: the
core index and core data the four games share (games.py), the gameplay file, and the textures.
"""

from bisect import bisect_right
from dataclasses import dataclass, field
import struct

from disc import LEVEL_HEADER_SIZE, SECTOR, Disc
from formats import FormatError, Texture, overlay_sections, span, unpack, wad
import games

# The level data header: eleven (offset, size) pairs, names from Wrench's
# RacLevelDataHeader. The first feeds ParseBin through D_0015EF4C.
SECTION_NAMES = ("overlay", "sound_bank", "core_index", "gs_ram", "hud_header",
                 "hud_bank_0", "hud_bank_1", "hud_bank_2", "hud_bank_3",
                 "hud_bank_4", "core_data")
# Core index texture tables: (count, offset) pairs of 16-byte entries.
TEXTURE_TABLES = {"terrain": 0x30, "moby": 0x38, "tie": 0x40, "shrub": 0x48}
# Class tables: (count, offset) pairs; each entry starts with a core offset.
CLASS_TABLES = {"moby": (0x18, 32), "tie": (0x20, 32), "shrub": (0x28, 48)}


def decoded(raw: bytes) -> bytes:
    return wad(raw) if raw[:3] == b"WAD" else raw


@dataclass
class Level:
    id: int
    header: bytes
    stored: dict[str, bytes]      # Sections and ranges as they are on the disc.
    index: bytes                  # Core index, decoded.
    core: bytes                   # Core data, decoded.
    gameplay: bytes               # The gameplay copy the disc's version loads, decoded.
    overlay: dict
    textures: dict[tuple[str, int], Texture] = field(default_factory=dict)
    boundaries: list[int] = field(default_factory=list)
    version: str = "pal"          # The disc's version (disc.Version.key).
    game: str = "rac1"            # The game (games.FORMATS): which of the readers' per-game values apply.
    # The file the ties, shrubs and directional lights are read from when it is not the gameplay
    # file: Deadlocked keeps them in a file of their own (its art instances).
    art: bytes | None = None
    # What a later game's level could not decode and left out (games.Format.lenient), in words.
    problems: list[str] = field(default_factory=list)
    # A later game's streamed chunks past the core's (wads.chunk_ranges): (chunk, tfrag block,
    # collision block), each decoded, or None where the chunk has none.
    chunks: list[tuple[int, bytes | None, bytes | None]] = field(default_factory=list)

    @property
    def instances(self) -> bytes:
        """The file the tie and shrub instances are in (art, or else the gameplay file)."""
        return self.gameplay if self.art is None else self.art

    def table(self, at: int, size: int) -> list[bytes]:
        count, offset = unpack("<II", self.index, at)
        data = span(self.index, offset, count * size)
        return [data[i * size:(i + 1) * size] for i in range(count)]

    def block(self, start: int) -> bytes:
        """A core block, ending at the next known block (Wrench's bounds)."""
        i = bisect_right(self.boundaries, start)
        if i == len(self.boundaries):
            raise FormatError(f"cannot bound core block at {start:#x}")
        return span(self.core, start, self.boundaries[i] - start)

    def texture(self, group: str, index: int) -> Texture:
        if (group, index) not in self.textures:
            raise FormatError(f"missing {group} texture {index}")
        return self.textures[(group, index)]


def load_level(disc: Disc, info: dict) -> Level:
    ranges = info["ranges"]
    data = disc.sectors(ranges["data"]["lba"], ranges["data"]["sectors"])
    stored, sections = {}, {}
    for i, name in enumerate(SECTION_NAMES):
        offset, size = unpack("<ii", data, i * 8)
        if size:
            stored[name] = span(data, offset, size)
            sections[name] = decoded(stored[name])
    for name in ("gameplay_ntsc", "gameplay_pal", "occlusion"):
        stored[name] = disc.sectors(ranges[name]["lba"], ranges[name]["sectors"])
    for name in ("overlay", "core_index", "core_data", "gs_ram"):
        if name not in sections:
            raise FormatError(f"level {info['id']} has no {name} section")
    index, core = sections["core_index"], sections["core_data"]
    if len(core) != unpack("<I", index, 0x8c)[0]:
        raise FormatError("core data size differs from its index")
    level = Level(info["id"], disc.read(info["header_lba"] * SECTOR, LEVEL_HEADER_SIZE), stored,
                  index, core, decoded(stored[disc.version.gameplay]), overlay_sections(sections["overlay"]))
    level.version = disc.version.key
    level.boundaries = core_boundaries(level)
    level.textures = textures(level, sections["gs_ram"])
    return level


def core_boundaries(level: Level) -> list[int]:
    """Where each core block starts (Wrench's enumerate_level_core_block_boundaries): a block ends
    where the next one starts."""
    fmt = games.of(level)
    words = [unpack("<I", level.index, at)[0] for at in (0x08, 0x0c, 0x10, 0x14, 0x60)]
    bounds = {len(level.core), *words}
    for at, size in CLASS_TABLES.values():
        bounds.update(unpack("<I", entry)[0] for entry in level.table(at, size))
    sequences, = unpack("<I", level.index, 0x78)
    if sequences and fmt.ratchet_sequences:  # 256 ratchet sequence offsets, stored in the index.
        bounds.update(o for o in unpack("<256i", level.index, sequences) if o > 0)
    if fmt.gadgets:
        bounds.update(unpack("<I", entry)[0] for entry in level.table(0x80, 16))  # Gadgets.
    if fmt.moby_sound_remap:
        bounds.add(unpack("<I", level.index, 0xb4)[0])
    return sorted(b for b in bounds if 0 < b <= len(level.core))


def textures(level: Level, gs: bytes) -> dict[tuple[str, int], Texture]:
    """Base mip levels of every texture, checked against the GS RAM table.

    An entry is (pixel offset from the texture base, width, height, type,
    palette in 256-byte units, mips, pad). Palettes must be RGBA32 CLUTs
    (PSM 0) that the level's GS RAM table uploads.
    """
    fmt = games.of(level)
    base, = unpack("<I", level.index, 0x60)
    gs_table, stashed, stash = level.table(0x00, 16), set(), 0
    if fmt.stash:
        # From Going Commando on, the GS RAM table goes on past its count with one entry per
        # permanently stashed moby texture block (index +0x84 of them): the textures of the moby
        # classes listed at +0xac (s16, ending at a negative one) keep their pixels in the GS RAM
        # lump, from the first such entry's address (Wrench's unpack_level_core).
        extra, = unpack("<i", level.index, 0x84)
        count, offset = unpack("<II", level.index, 0x00)
        gs_table = [span(level.index, offset + i * 16, 16) for i in range(count + max(extra, 0))]
        if extra > 0:
            stash, = unpack("<i", gs_table[count], 8)
            classes, at = set(), unpack("<I", level.index, 0xac)[0]
            while (class_id := unpack("<h", level.index, at)[0]) >= 0:
                classes.add(class_id)
                at += 2
            for entry in level.table(0x18, 32):
                if unpack("<i", entry, 4)[0] in classes:
                    stashed.update(slot for slot in entry[16:32] if slot != 0xff)
    clut32 = {unpack("<I", e, 8)[0] for e in gs_table if unpack("<I", e)[0] == 0}
    result = {}
    for group, at in TEXTURE_TABLES.items():
        for i, entry in enumerate(level.table(at, 16)):
            pixels, width, height, _kind, palette, _mips, _pad = unpack("<I6H", entry)
            if not width and not height:
                continue
            if palette * 256 not in clut32:
                raise FormatError(f"{group} texture {i} uses an unknown palette")
            if group == "moby" and i in stashed:
                data = span(gs, stash + pixels, width * height)
            else:
                data = span(level.core, base + pixels, width * height)
            if fmt.swizzled_textures:
                data = unswizzle(data, width)
            result[(group, i)] = Texture(width, height, data, span(gs, palette * 256, 1024))
    # The chrome (128 x 128) and glass (64 x 64) maps the metal packets are drawn with: byte offsets
    # into the GS RAM lump of their PSMT8 pixels and CLUT, at core index 0x90 / 0x94 and 0x98 / 0x9C
    # (ReRAC moby_skinning_lighting.md 9, "Texture"). Blurry environment pictures, alpha 13..0x80.
    for key, at, size in ((("metal", -2), 0x90, 128), (("metal", -3), 0x98, 64)):
        pixels, palette = unpack("<II", level.index, at)
        if pixels and pixels + size * size <= len(gs) and palette + 1024 <= len(gs):
            result[key] = Texture(size, size, span(gs, pixels, size * size), span(gs, palette, 1024))
    return result


def unswizzle(pixels: bytes, width: int, inverse: bool = False) -> bytes:
    """Deadlocked's 8-bit textures in row order. They are stored as the GS lays a PSMT8 texture
    out in its memory: each 32-byte run of a pair of rows holds blocks of four columns by four
    rows, every other run turned by one byte. The mapping is Wrench's map_pixel_index_rac4
    (src/core/texture.cpp, GPL-3.0-or-later), written out again: stored byte i is pixel (x, y).
    INVERSE: pixel i is stored byte (x, y), the way Wrench reads Deadlocked's sky textures."""
    out = bytearray(len(pixels))
    for i, value in enumerate(pixels):
        pair = i // (width * 2)
        row = pair * 2 if pair % 2 == 0 else (pair - 1) * 2 + 1
        column_block = (i % (width * 2)) // 32
        lane, quad, odd = i % 4, (i // 4) % 4, i % 2
        turn = (i // 16) % 2
        if (pair // 2) % 2:
            turn = 1 - turn
        lane = (lane + turn) % 4 if odd == 0 else (lane - turn + 4) % 4
        x = quad + (lane + column_block * 4) * 4
        y = row + odd * 2
        at = min(x % width + y * width, len(pixels) - 1)
        if inverse:
            out[i] = pixels[at]
        else:
            out[at] = value
    return bytes(out)


def load_wad_level(source, entry: dict) -> Level:
    """A Going Commando, Up Your Arsenal or Deadlocked level (one of a wads.WadDisc's levels()):
    its level data (the code overlay, core index, GS RAM lump and core data), its gameplay file
    and, in Deadlocked, its art instances, decoded into the Level the readers take."""
    import wads  # here: wads imports disc, as this module does, and nothing else needs it

    version = source.version
    part = entry["level"]
    ranges = wads.level_ranges(part["header"], version)
    disc = source.disc
    data = disc.sectors(part["lba"] + ranges["data"]["sector"], ranges["data"]["sectors"])
    stored, sections = {}, {}
    for i, name in enumerate(wads.DATA_SECTIONS[len(part["header"])]):
        offset, size = unpack("<ii", data, i * 8)
        if size > 0:
            stored[name] = span(data, offset, size)
            sections[name] = decoded(stored[name])
    for name in ("core_index", "core_data", "gs_ram"):
        if name not in sections:
            raise FormatError(f"{version.game} level {entry['id']} has no {name} section")
    if "gameplay_core" in sections:  # Deadlocked: the level data holds both instance files
        gameplay, art = sections["gameplay_core"], sections.get("art_instances")
    else:
        g = ranges["gameplay"]
        stored["gameplay"] = disc.sectors(part["lba"] + g["sector"], g["sectors"])
        gameplay, art = decoded(stored["gameplay"]), None
    try:
        overlay = overlay_sections(sections["overlay"]) if "overlay" in sections else None
    except FormatError:
        overlay = None
    index, core = sections["core_index"], sections["core_data"]
    if len(core) != unpack("<I", index, 0x8c)[0]:
        raise FormatError("core data size differs from its index")
    level = Level(entry["id"], part["header"], stored, index, core, gameplay,
                  overlay or {"entry_point": 0, "sections": []})
    level.game, level.version, level.art = version.game, version.key, art
    for chunk, sector, count in wads.chunk_ranges(part["header"]):
        if chunk == 0:
            continue  # chunk 0 is the core's own terrain and collision (Wrench reads it from the core)
        data = disc.sectors(part["lba"] + sector, count)
        tfrags, collision = unpack("<ii", data)
        level.chunks.append((chunk, decoded(data[tfrags:]) if tfrags > 0 else None,
                             decoded(data[collision:]) if collision > 0 else None))
    level.boundaries = core_boundaries(level)
    level.textures = textures(level, sections["gs_ram"])
    return level


# The title world: the space flight behind the title and the main menu (the boot program's
# transition_load_wad). Its lump (global table 0x14e8) is a level core whose header lists the same
# tables in another order; as ReRAC's TitleWorld (crates/rc-formats/src/frontend.rs; ISC License,
# Copyright (c) 2026 ReRAC contributors) a level core index is rebuilt over it, so the level readers
# read it unchanged. Exported as level 99.
TITLE_ID = 99
TITLE_INDEX_TABLES = 0x100


def load_title(disc: Disc, survey: dict) -> Level:
    ref = next(r for r in survey["global_references"] if r["group"] == "unknown_wad")
    lump = decoded(disc.sectors(ref["lba"], (ref["bytes"] + SECTOR - 1) // SECTOR))

    def h(i: int) -> int:
        return unpack("<i", lump, i * 4)[0]

    base, p = h(1), TITLE_INDEX_TABLES
    if not 0 < base <= len(lump):
        raise FormatError(f"title world: data base {base:#x}")
    header = bytearray(TITLE_INDEX_TABLES)

    def put(at: int, value: int) -> None:
        struct.pack_into("<i", header, at, value)

    def table(at: int, count: int, offset: int) -> None:
        put(at, h(count))
        put(at + 4, h(offset) + p)

    table(0x00, 2, 3)          # the GS upload
    put(0x08, h(4))            # tfrags
    put(0x10, h(5))            # sky
    table(0x18, 6, 7)          # moby classes
    table(0x20, 8, 9)          # tie classes
    table(0x28, 0xa, 0xb)      # shrub classes
    table(0x30, 0xc, 0xd)      # tfrag textures
    table(0x38, 0xe, 0xf)      # moby textures
    table(0x40, 0x10, 0x11)    # tie textures
    table(0x48, 0x12, 0x13)    # shrub textures
    table(0x50, 0x14, 0x15)    # particle textures
    table(0x58, 0x16, 0x17)    # effect textures
    put(0x60, h(0x18))         # texture data
    put(0x64, h(0x19))         # particle bank
    put(0x68, h(0x1a))         # effect bank
    put(0x6c, h(0x1b) + p)     # particle definitions
    put(0x90, h(0x1c))         # chrome map texture
    put(0x94, h(0x1d))         # chrome map palette
    data = lump[base:]
    put(0x8c, len(data))
    index = bytes(header) + lump[:base]
    gameplay = data[h(0x1f):]
    level = Level(TITLE_ID, b"", {}, index, data, gameplay, {"entry_point": 0, "sections": []})
    level.version = disc.version.key
    level.boundaries = core_boundaries(level)
    level.textures = textures(level, lump[h(0):base])
    return level


# The flight between planets: the transition lump (global table 0x13b8) that the levels'
# EnterSpaceLoadingLoop loads. As ReRAC reads it (crates/rc-formats/src/transition.rs; ISC License,
# Copyright (c) 2026 ReRAC contributors), header word 1 is the data base and word 0x12 the sky
# block (six shells with their own textures), which LoadSky makes the flight's sky. Exported as
# level 98 with that sky and the lump's moby classes (the ships, words 4 and 5, with their textures:
# words 6, 7 and 0xc); the planet picture and the caption are the game's draws.
FLIGHT_ID = 98


def load_flight(disc: Disc, survey: dict) -> Level:
    ref = next(r for r in survey["global_references"] if r["group"] == "transition")
    lump = decoded(disc.sectors(ref["lba"], (ref["bytes"] + SECTOR - 1) // SECTOR))

    def h(i: int) -> int:
        return unpack("<i", lump, i * 4)[0]

    base, sky_at, p = h(1), h(0x12), TITLE_INDEX_TABLES
    if not 0 < base <= len(lump) or not 0 < sky_at < len(lump) - base:
        raise FormatError(f"flight: data base {base:#x}, sky {sky_at:#x}")
    header = bytearray(TITLE_INDEX_TABLES)

    def put(at: int, value: int) -> None:
        struct.pack_into("<i", header, at, value)

    def table(at: int, count: int, offset: int) -> None:
        put(at, h(count))
        put(at + 4, h(offset) + p)

    table(0x00, 2, 3)          # the GS upload
    put(0x10, sky_at)          # sky
    table(0x18, 4, 5)          # moby classes: the ships (531, 532, 533)
    table(0x38, 6, 7)          # moby textures
    put(0x60, h(0xc))          # texture data
    put(0x90, h(0x10))         # chrome map texture
    put(0x94, h(0x11))         # chrome map palette
    data = lump[base:]
    put(0x8c, len(data))
    index = bytes(header) + lump[:base]
    level = Level(FLIGHT_ID, b"", {}, index, data, bytes(0x100), {"entry_point": 0, "sections": []})
    level.version = disc.version.key
    level.boundaries = core_boundaries(level)
    level.textures = textures(level, lump[h(0):base])
    level.no_terrain = True
    return level

