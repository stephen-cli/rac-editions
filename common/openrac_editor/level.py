"""Load one level from the disc: its data sections, core assets and textures."""

from bisect import bisect_right
from dataclasses import dataclass, field
import struct

from disc import LEVEL_HEADER_SIZE, SECTOR, Disc
from formats import FormatError, Texture, overlay_sections, span, unpack, wad

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
    words = [unpack("<I", level.index, at)[0] for at in (0x08, 0x0c, 0x10, 0x14, 0x60)]
    bounds = {len(level.core), *words}
    for at, size in CLASS_TABLES.values():
        bounds.update(unpack("<I", entry)[0] for entry in level.table(at, size))
    sequences, = unpack("<I", level.index, 0x78)
    if sequences:  # 256 ratchet sequence offsets, stored in the index.
        bounds.update(o for o in unpack("<256i", level.index, sequences) if o > 0)
    bounds.update(unpack("<I", entry)[0] for entry in level.table(0x80, 16))  # Gadgets.
    return sorted(b for b in bounds if 0 < b <= len(level.core))


def textures(level: Level, gs: bytes) -> dict[tuple[str, int], Texture]:
    """Base mip levels of every texture, checked against the GS RAM table.

    An entry is (pixel offset from the texture base, width, height, type,
    palette in 256-byte units, mips, pad). Palettes must be RGBA32 CLUTs
    (PSM 0) that the level's GS RAM table uploads.
    """
    base, = unpack("<I", level.index, 0x60)
    clut32 = {unpack("<I", e, 8)[0] for e in level.table(0x00, 16) if unpack("<I", e)[0] == 0}
    result = {}
    for group, at in TEXTURE_TABLES.items():
        for i, entry in enumerate(level.table(at, 16)):
            pixels, width, height, _kind, palette, _mips, _pad = unpack("<I6H", entry)
            if not width and not height:
                continue
            if palette * 256 not in clut32:
                raise FormatError(f"{group} texture {i} uses an unknown palette")
            result[(group, i)] = Texture(width, height, span(level.core, base + pixels, width * height),
                                         span(gs, palette * 256, 1024))
    # The chrome (128 x 128) and glass (64 x 64) maps the metal packets are drawn with: byte offsets
    # into the GS RAM lump of their PSMT8 pixels and CLUT, at core index 0x90 / 0x94 and 0x98 / 0x9C
    # (ReRAC moby_skinning_lighting.md 9, "Texture"). Blurry environment pictures, alpha 13..0x80.
    for key, at, size in ((("metal", -2), 0x90, 128), (("metal", -3), 0x98, 64)):
        pixels, palette = unpack("<II", level.index, at)
        if pixels and pixels + size * size <= len(gs) and palette + 1024 <= len(gs):
            result[key] = Texture(size, size, span(gs, pixels, size * size), span(gs, palette, 1024))
    return result


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

