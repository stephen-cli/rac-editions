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
from pathlib import Path
import sys

sys.path.insert(1, str(next(p for p in Path(__file__).resolve().parents if (p / "tools/openrac_common").is_dir()) / "tools"))
from openrac_common import iso9660  # noqa: E402

from disc import BOOT, SECTOR, Disc  # noqa: E402
from formats import FormatError, span, unpack  # noqa: E402
import util  # noqa: E402

TOC_LBA = 1001
# How much of the root directory walk reads: these discs' are a sector or two.
ROOT_SECTORS = 16
# How much of the table of contents is read (Wrench's TOC_MAX_SIZE); the level headers end
# before +0x72000 on these discs.
TOC_LIMIT = 0x200000
MAX_LEVELS = 100
ITEM_BANKS = 48
SHIP_BODIES = 0x48d8
SHIP_PAINTS = 0x48f0        # Going Commando
SHIP_PAINT_COUNT = 18       # the pairs there: the Ship Shack's paint jobs (0x152C30 holds the loaded one)
FLIGHT_BASE = 0x3e24        # Going Commando: the sector the flights' ranges start from
LEVEL_PAIR = 24              # three (sector, sector count) pairs

# A header's kind by its size (Wrench's WAD_FILE_TYPES, for these games' NTSC-U discs).
LEVEL, AUDIO, SCENE = "level", "audio", "scene"
HEADER_KINDS = {0x60: LEVEL, 0xc68: LEVEL, 0x1018: AUDIO, 0x1818: AUDIO, 0x2a0: AUDIO, 0x137c: SCENE, 0x26f0: SCENE}


@dataclass(frozen=True)
class Version:
    """One disc: the game, what identifies it, and the shape of its level files. What identifies
    it (region to executable_sha1) is read from games/<game>/game.json (util.disc_identity); the
    shape of its level files is known here."""
    game: str               # rac2, rac3, rac4 (games/<game>/game.json)
    key: str                # the version's name there (ntsc)
    region: str
    label: str
    executable: str
    executable_size: int
    executable_sha1: str
    level_header: int       # the level header's size: 0x60 (Going Commando, Up Your Arsenal) or 0xc68 (Deadlocked)
    hero_packs: int = 0     # where the table of contents keeps the packs of Ratchet's mesh and textures, if it does
    hero_armours: int = 0   # how many of those packs are his looks; the wrench's follow them
    item_banks: int = 0     # where it keeps the banks of the items he holds, if it does
    flights: int = 0        # where it keeps the flights' (sector, sector count) pairs, after their base sector
    flight_count: int = 0
    normal_table: int = 0   # the executable's (cos, sin) table the terrain is lit by (lighting.py), if known


RAC2_NTSC = Version(game="rac2", key="ntsc", **util.disc_identity("rac2", "ntsc"), level_header=0x60,
                    hero_packs=0x4f04, hero_armours=9, item_banks=0x4b3c, flights=0x3e58, flight_count=4,
                    normal_table=0x238f00)
RAC3_NTSC = Version(game="rac3", key="ntsc", **util.disc_identity("rac3", "ntsc"), level_header=0x60)
RAC4_NTSC = Version(game="rac4", key="ntsc", **util.disc_identity("rac4", "ntsc"), level_header=0xc68)
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
    """(path, sector, size) of the root directory's entries, from its first ROOT_SECTORS sectors;
    a directory is listed (its name ending in /), not entered: these discs have some, and they
    hold nothing the readers need. The volume descriptor is checked as disc.Disc checks it."""
    lba, size = disc.root_directory()
    return [(iso9660.plain_name(raw) + ("/" if flags & iso9660.FLAG_DIRECTORY else ""), extent, length)
            for raw, extent, length, flags in disc.records(lba, min(size, ROOT_SECTORS * SECTOR))]


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

    def hero_pack(self, pack: int = 0) -> tuple[bytes, bytes]:
        """Ratchet's mesh and textures: pack PACK of the table the game's LoadPlayerDisplayTextures
        (0x00292078) reads, (mesh sector, sectors, textures sector, sectors) each, sectors relative
        to the word before the table. Going Commando's level holds only his skeleton: the game
        picks a pack by his armour, 0 the one he starts in."""
        base, = unpack("<i", self.toc, self.version.hero_packs)
        mesh, mesh_sectors, textures, texture_sectors = unpack(
            "<4i", self.toc, self.version.hero_packs + 4 + pack * 16)
        return self.disc.sectors(base + mesh, mesh_sectors), self.disc.sectors(base + textures, texture_sectors)

    def item_banks(self) -> list[tuple[int, bytes, bytes]]:
        """(class, class WAD, texture WAD) of each item in the banks Ratchet's weapons and gadgets
        load from when he takes them out (func_00294988): ITEM_BANKS rows of (class, sector, sectors,
        sound sector, sound sectors), sectors relative to the word before the table. A bank starts
        with the offset and size of each part, from the disc's sector of the table's base. The rows
        of class 0 are no item."""
        base, = unpack("<i", self.toc, self.version.item_banks)
        result = []
        for row in range(ITEM_BANKS):
            class_id, sector, sectors = unpack("<3i", self.toc, self.version.item_banks + 4 + row * 20)
            if class_id:
                bank = self.disc.sectors(base + sector, sectors)
                item, _, texture, _ = unpack("<4I", bank)
                start = sector * SECTOR
                result.append((class_id, bank[item - start:], bank[texture - start:]))
        return result

    def ship_texture(self, number: int = 0) -> bytes:
        """The picture on his ship, as func_002926B8 reads it: paint NUMBER of the (sector, sector
        count) pairs at SHIP_PAINTS, from the flights' base sector."""
        base, = unpack("<I", self.toc, FLIGHT_BASE)
        sector, count = unpack("<II", self.toc, SHIP_PAINTS + number * 8)
        return self.disc.sectors(base + sector, count)

    def ship_textures(self) -> list[bytes]:
        """Every paint job (SHIP_PAINT_COUNT pairs at SHIP_PAINTS): func_002926B8 loads the one the
        low five bits of 0x001A7AF2 name on each load, the Ship Shack's preview another, and keeps
        its number at 0x00152C30."""
        return [self.ship_texture(number) for number in range(SHIP_PAINT_COUNT)]

    def ship_body(self, number: int = 0) -> bytes:
        """His ship's hull as func_002925F0 reads it on every level's and flight's load: a class, of
        the (sector, sector count) pairs at SHIP_BODIES, from the flights' base sector."""
        base, = unpack("<I", self.toc, FLIGHT_BASE)
        sector, count = unpack("<II", self.toc, SHIP_BODIES + number * 8)
        return self.disc.sectors(base + sector, count)

    def ship_bodies(self) -> list[bytes]:
        """Every hull: the pairs from SHIP_BODIES up to the paints' (the game picks one by the low two
        bits of 0x001A7AF0)."""
        return [self.ship_body(number) for number in range((SHIP_PAINTS - SHIP_BODIES) // 8)]

    def executable(self) -> bytes:
        """The executable SYSTEM.CNF boots (identify checked its size and SHA-1)."""
        found = next((lba, size) for path, lba, size in walk(self.disc) if path == self.version.executable)
        return self.disc.read(found[0] * SECTOR, found[1])

    def normals(self) -> list[tuple[int, int]] | None:
        """The executable's (cos, sin) table the game lights its terrain by, or None where it is
        not known. Going Commando's PatchTfragVertexLighting (0x2F0468) copies it from 0x238F00 to
        the scratchpad and lights each terrain vertex as Ratchet & Clank's LightTfrags does."""
        if not self.version.normal_table:
            return None
        from lighting import normal_table
        return normal_table(self.executable(), self.version.normal_table)

    def flight(self, number: int) -> bytes:
        """A flight's lump as stored: the sector ranges at `flights`, relative to the sector the
        word at 0x3e24 holds (the transition loader, func_002EAB18, reads them so)."""
        base, = unpack("<I", self.toc, FLIGHT_BASE)
        sector, count = unpack("<II", self.toc, self.version.flights + number * 8)
        return self.disc.sectors(base + sector, count)

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

