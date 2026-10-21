"""Going Commando, Up Your Arsenal and Deadlocked discs: their table of contents and level files.

Ratchet & Clank keeps its table of contents at sector 1500 (disc.py). The three games after it
keep theirs at sector 1001 (on Going Commando's disc it is also the file RC2.HDR): a run of global
WAD headers, each starting with its own size and the sector of its file, then the level table:
three (sector, sector count) pairs per level, each pointing at a level, audio or scene header that
sits in the table of contents itself. A header's word +4 is the sector of its file, and the sector
ranges inside it are relative to that sector. Which pair holds which kind differs (Going
Commando: level, audio, scene; Up Your Arsenal and Deadlocked: audio, level, scene), so a pair's
kind is told by its header's size, as Wrench does (src/iso/table_of_contents.cpp,
wad_identifier.cpp; GPL-3.0-or-later; re-implemented here, nothing copied).

A level file's header names its sections (level_ranges): the level data, the gameplay file (the
instances), the occlusion and, in Deadlocked, the art instances, and the streamed chunks of a
large level's terrain and collision (chunk_ranges). The level data is a WAD of its own whose
header lists the code overlay, the core index, the GS RAM lump and the core data
(level_data_wad.cpp's GcUyaLevelDataHeader and DlLevelDataHeader): the same core the first game
has, which level.load_wad_level reads with the shared readers.
"""

from dataclasses import dataclass
import hashlib

from disc import BOOT, SECTOR, Disc
from formats import FormatError, span, unpack

TOC_LBA = 1001
# How much of the table of contents is read (Wrench's TOC_MAX_SIZE); the level headers end
# before +0x72000 on these discs.
TOC_LIMIT = 0x200000
MAX_LEVELS = 100
LEVEL_PAIR = 24              # three (sector, sector count) pairs

# A header's kind by its size (Wrench's WAD_FILE_TYPES, for these games' NTSC-U discs).
LEVEL, AUDIO, SCENE = "level", "audio", "scene"
HEADER_KINDS = {0x60: LEVEL, 0xc68: LEVEL, 0x1018: AUDIO, 0x1818: AUDIO, 0x2a0: AUDIO, 0x137c: SCENE, 0x26f0: SCENE}


@dataclass(frozen=True)
class Version:
    """One disc: the game, what identifies it, and the shape of its level files."""
    game: str               # rac2, rac3, rac4 (games/<game>/game.json)
    key: str                # the version's name there (ntsc)
    region: str
    label: str
    executable: str
    executable_size: int
    executable_sha1: str
    level_header: int       # the level header's size: 0x60 (Going Commando, Up Your Arsenal) or 0xc68 (Deadlocked)


RAC2_NTSC = Version("rac2", "ntsc", "NTSC-U", "NTSC-U v1.01", "SCUS_972.68", 2618684,
                    "bacda860cbd6723142e86db56f197e47e728970f", 0x60)
RAC3_NTSC = Version("rac3", "ntsc", "NTSC-U", "NTSC-U v1.00", "SCUS_973.53", 771008,
                    "601012bb03ef1213ad1253baebae8a071b8a7df7", 0x60)
RAC4_NTSC = Version("rac4", "ntsc", "NTSC-U", "NTSC-U v1.00", "SCUS_974.65", 1692216,
                    "aa91b1c3b9b1a244320c47580b77342ef9856e95", 0xc68)
VERSIONS = (RAC2_NTSC, RAC3_NTSC, RAC4_NTSC)


def boot_name(disc: Disc) -> str | None:
    """The executable SYSTEM.CNF boots."""
    cnf = next(((lba, size) for path, lba, size in walk(disc) if path == "SYSTEM.CNF"), None)
    if cnf is None:
        return None
    boot = BOOT.search(disc.read(cnf[0] * SECTOR, min(cnf[1], SECTOR)))
    return boot.group(1).decode("ascii", "replace").strip() if boot else None


def identify(disc: Disc) -> Version | None:
    """The later game this disc is, by the executable SYSTEM.CNF boots, or None for any other disc
    (Ratchet & Clank's among them: disc.Disc reads those). The executable is checked by SHA-1."""
    name = boot_name(disc)
    version = next((v for v in VERSIONS if v.executable == name), None)
    if version is None:
        return None
    found = next(((lba, size) for path, lba, size in walk(disc) if path == name), None)
    if found is None or found[1] != version.executable_size:
        raise FormatError(f"{name} is not {version.label}'s executable")
    if hashlib.sha1(disc.read(found[0] * SECTOR, found[1])).hexdigest() != version.executable_sha1:
        raise FormatError(f"the executable's SHA-1 does not match {version.game} {version.label}")
    return version


def walk(disc: Disc) -> list[tuple[str, int, int]]:
    """(path, sector, size) of the root directory's entries; a directory is listed (its name
    ending in /), not entered: these discs have some, and they hold nothing the readers need."""
    pvd = disc.read(16 * SECTOR, SECTOR)
    if pvd[:7] != b"\x01CD001\x01" or unpack("<H", pvd, 128)[0] != SECTOR:
        raise FormatError("expected ISO 9660 with 2048-byte sectors")
    lba, size = unpack("<I", pvd, 158)[0], unpack("<I", pvd, 166)[0]
    directory, pos, out = disc.read(lba * SECTOR, min(size, 16 * SECTOR)), 0, []
    while pos < len(directory):
        length = directory[pos]
        if not length:
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        record = span(directory, pos, length)
        raw = span(record, 33, record[32])
        pos += length
        if raw in (b"\0", b"\1"):
            continue
        name = raw.decode("latin-1").split(";")[0]
        out.append((name + ("/" if record[25] & 2 else ""), unpack("<I", record, 2)[0], unpack("<I", record, 10)[0]))
    return out


class WadDisc:
    """A Going Commando, Up Your Arsenal or Deadlocked disc, read through disc.Disc's bounded reads."""

    def __init__(self, disc: Disc, version: Version):
        self.disc, self.version = disc, version
        self.toc = disc.read(TOC_LBA * SECTOR, min(TOC_LIMIT, disc.size - TOC_LBA * SECTOR))

    def header(self, lba: int) -> bytes | None:
        """The header a level table pair points at (in the table of contents), or None."""
        at = (lba - TOC_LBA) * SECTOR
        if lba <= TOC_LBA or at + 8 > len(self.toc):
            return None
        size, = unpack("<i", self.toc, at)
        return span(self.toc, at, size) if size in HEADER_KINDS else None

    def level_table(self) -> int:
        """The level table's offset in the table of contents: the global headers come first, each
        sized by its first word (the last padded with zeros on Up Your Arsenal's disc); the first
        entry whose first two pairs point at level, audio or scene headers, and whose third is
        one too or empty, starts the table."""
        at = 0
        while at + LEVEL_PAIR <= len(self.toc):
            first, second, third = unpack("<6i", self.toc, at)[::2]
            if (self.header(first) is not None and self.header(second) is not None
                    and (third == 0 or self.header(third) is not None)):
                return at
            size, = unpack("<i", self.toc, at)
            if size == 0:  # zeros pad the last global header (Up Your Arsenal's)
                at += 4
                continue
            if not 8 <= size <= 0x10000 or size % 4:
                break
            at += size
        raise FormatError("no level table in the table of contents")

    def levels(self) -> list[dict]:
        """Each level the table names: {"id", "slot", "level" / "audio" / "scene": {"header_lba",
        "lba" (the file's first sector), "sectors", "header" (bytes)}} with the parts it has. A
        level's id is the number its level header gives (+8), which the games load it by and
        Wrench names it by: its slot in the table on Deadlocked's disc, the slot plus one on Up
        Your Arsenal's, and the slot on Going Commando's but for the museum (slot 21, level 30).
        An entry without a level part (Up Your Arsenal's multiplayer menu audio) keeps its slot."""
        start = self.level_table()
        pairs, first_header = [], len(self.toc)
        for slot in range(MAX_LEVELS):
            at = start + slot * LEVEL_PAIR
            if at + LEVEL_PAIR > min(first_header, len(self.toc)):
                break
            entry = unpack("<6i", self.toc, at)
            pairs.append(entry)
            for lba in entry[::2]:
                if lba > TOC_LBA:
                    first_header = min(first_header, (lba - TOC_LBA) * SECTOR)
        result = []
        for slot, entry in enumerate(pairs):
            level = {"id": slot, "slot": slot}
            for lba, sectors in zip(entry[::2], entry[1::2]):
                if not lba:
                    continue
                header = self.header(lba)
                if header is None:
                    raise FormatError(f"level {slot}: the table points at sector {lba}, which holds no known header")
                kind = HEADER_KINDS[len(header)]
                if kind in level:
                    raise FormatError(f"level {slot} has two {kind} headers")
                file_lba, = unpack("<i", header, 4)
                self.disc.check(file_lba * SECTOR, sectors * SECTOR)
                level[kind] = {"header_lba": lba, "lba": file_lba, "sectors": sectors, "header": header}
            if LEVEL in level:
                level["id"], = unpack("<i", level[LEVEL]["header"], 8)
            if len(level) > 2:
                result.append(level)
        ids = [level["id"] for level in result if LEVEL in level]
        if len(set(ids)) != len(ids):
            raise FormatError("two levels in the table have the same number")
        return result

    def level(self, number: int) -> dict:
        """The level numbered NUMBER (levels()), which must have a level part."""
        found = next((level for level in self.levels() if level["id"] == number and LEVEL in level), None)
        if found is None:
            raise FormatError(f"{self.version.game} has no level {number}")
        return found

    def survey(self) -> dict:
        """The disc's levels, as JSON (headers left out)."""
        levels = []
        for level in self.levels():
            entry = {"id": level["id"], "slot": level["slot"]}
            for kind in (LEVEL, AUDIO, SCENE):
                if kind in level:
                    part = level[kind]
                    entry[kind] = {"header_lba": part["header_lba"], "lba": part["lba"], "sectors": part["sectors"],
                                   "header_bytes": len(part["header"])}
            if LEVEL in level:
                entry["ranges"] = level_ranges(level[LEVEL]["header"], self.version)
            levels.append(entry)
        return {"schema": 1, "game": self.version.game, "version": self.version.key,
                "disc_bytes": self.disc.size, "executable_sha1": self.version.executable_sha1, "levels": levels}


def level_ranges(header: bytes, version: Version) -> dict[str, dict]:
    """A level header's sector ranges, relative to its file: Wrench's GcUyaLevelWadHeader (0x60:
    data +0x10, sound bank +0x18, gameplay +0x20, occlusion +0x28) and DlLevelWadHeader (0xc68:
    data +0x18, sound bank +0x20, gameplay +0x58, art instances +0xc60)."""
    if len(header) != version.level_header:
        raise FormatError(f"a {version.game} level header is {version.level_header:#x} bytes, not {len(header):#x}")
    fields = ({"data": 0x10, "sound_bank": 0x18, "gameplay": 0x20, "occlusion": 0x28} if len(header) == 0x60 else
              {"data": 0x18, "sound_bank": 0x20, "gameplay": 0x58, "art_instances": 0xc60})
    ranges = {}
    for name, at in fields.items():
        start, count = unpack("<ii", header, at)
        if count:
            ranges[name] = {"sector": start, "sectors": count}
    if "data" not in ranges:
        raise FormatError("level header without level data")
    return ranges


def chunk_ranges(header: bytes) -> list[tuple[int, int, int]]:
    """(chunk, sector, sectors) of a level's streamed chunks, relative to its file: Wrench's
    ChunkWadHeader at +0x30 of a 0x60 level header and +0x28 of Deadlocked's, three sector ranges
    (then three sound banks, not read). A chunk is its own terrain and collision, which the game
    swaps in as the player moves between parts of a large level; chunk 0's are the core's."""
    at = 0x30 if len(header) == 0x60 else 0x28
    ranges = unpack("<6i", header, at)
    return [(i, ranges[2 * i], ranges[2 * i + 1]) for i in range(3) if ranges[2 * i + 1] > 0]


# The level data WAD: (offset, size) byte ranges from its start (Wrench's level_data_wad.cpp).
DATA_SECTIONS = {
    0x60: ("overlay", "core_index", "gs_ram", "hud_header", "hud_bank_0", "hud_bank_1", "hud_bank_2",
           "hud_bank_3", "hud_bank_4", "core_data", "transition_textures"),
    0xc68: ("moby8355_pvars", "overlay", "core_index", "gs_ram", "hud_header", "hud_bank_0", "hud_bank_1",
            "hud_bank_2", "hud_bank_3", "hud_bank_4", "core_data", "art_instances", "gameplay_core", "global_nav_data"),
}

