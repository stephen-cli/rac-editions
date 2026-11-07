"""
A level's data archive, read once into memory and written once, for the patch stages that change it
(tools/patch_levels.py: splice_boots.py, splice_sign.py, extra_classes.py).

The table of contents (sector 1500) lists each level's header sector at LEVEL_TABLE; the header's +0x08 holds the
data archive's first sector and length in sectors, which the game reads whole (func_00204C60). The archive holds
eleven sections listed at its start as {offset, size} (OpenRAC editor/level.py SECTION_NAMES): among them the core
index (raw), GS RAM (raw) and the core data (WAD-compressed), which the stages change. The core index's header
words +0x88 and +0x8C give the core data's compressed and decompressed sizes.
"""
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
EDITOR = HERE.parents[2] / "common/openrac_editor"   # OpenRAC's disc readers, carried (common/deps.py)
sys.path.insert(0, str(HERE.parents[2] / "common"))
import core_tools  # noqa: E402

SECTOR = 2048
TOC_LBA, LEVEL_TABLE = 1500, 0x28C8
LEVELS = 19
TAIL_SECTORS = 32           # after an archive at the image's end: the stream reader reads past an archive's end
RAC_WAD = core_tools.tool("rac-wad")
MAX_CLASSES = 222          # moby_class_resources[224], less the two classes the level's loader registers after
                           # the table's (the shared overlay's 0x0023FA00, for the level's content variant): a
                           # table of 223 hung Batalia's and Umbris's load on the loading screen
MAX_SLOT = 0xFE            # texture slots are bytes; 0xFF ends a class's list


class Wad:
    """The edition's WAD codec, installer/core's rac-wad: decode reads a stream as the game does, encode compresses
    and checks the stream by decoding it back. The bytes go each way through files in WORK."""

    def __init__(self, exe: Path, work: Path):
        self.exe, self.work = exe, work
        work.mkdir(parents=True, exist_ok=True)

    def _run(self, op: str, data: bytes) -> bytes:
        src, dst = self.work / f"{op}.in", self.work / f"{op}.out"
        src.write_bytes(data)
        dst.unlink(missing_ok=True)
        core_tools.run(self.exe, op, src, "-o", dst)
        return dst.read_bytes()

    def decode(self, stream: bytes) -> bytes:
        return self._run("decompress", stream)

    def encode(self, data: bytes) -> bytes:
        return self._run("compress", data)


def section_names():
    sys.path.insert(0, str(EDITOR))
    from level import SECTION_NAMES
    return SECTION_NAMES


def level_archives(f) -> dict[int, tuple[int, int, int]]:
    """level -> (header sector, data sector, data sectors) from the table of contents."""
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
    """(the archive's head, {section: bytes}, the sections' (offset, size))."""
    sections = [struct.unpack_from("<ii", archive, 8 * i) for i in range(len(names))]
    data = {n: archive[o:o + s] for n, (o, s) in zip(names, sections)}
    first = min(o for o, s in sections if s)
    return archive[:first], data, sections


def assemble(head: bytes, data: dict, names, sections) -> bytes:
    """The archive back from its sections, each at a 0x100 boundary after the head, padded to whole sectors."""
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


@dataclass
class Level:
    """A level's data archive in memory: the core decoded, the core index and GS RAM editable."""
    number: int
    head: bytes
    data: dict
    sections: list
    index: bytearray
    gs: bytearray
    core: bytes


def read_level(archive: bytes, number: int, names, wad: Wad) -> Level:
    head, data, sections = parts(archive, names)
    index = bytearray(data["core_index"])
    core = wad.decode(data["core_data"])
    if len(core) != struct.unpack_from("<I", index, 0x8C)[0]:
        raise SystemExit(f"level {number}: the core data does not decode to the size its index gives")
    return Level(number, head, data, sections, index, bytearray(data["gs_ram"]), core)


def write_level(level: Level, names, wad: Wad) -> bytes:
    """The level's archive with its core compressed again and the index's sizes (+0x88, +0x8C) to match."""
    stream = wad.encode(level.core)       # checked by decoding it back, by the game's rules
    struct.pack_into("<I", level.index, 0x88, len(stream))
    struct.pack_into("<I", level.index, 0x8C, len(level.core))
    data = dict(level.data, core_index=bytes(level.index), gs_ram=bytes(level.gs), core_data=stream)
    return assemble(level.head, data, names, level.sections)


def table(index: bytes, at: int, size: int):
    """The core index's {count, offset} table at AT: (count, offset, its SIZE-byte entries)."""
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


def check_uploads(index: bytes, gs: bytes) -> None:
    """The GS RAM upload list must be in order: the game uploads it entry after entry, advancing by each entry's
    size, and ignores the stored addresses, so an entry appended lands at its file offset."""
    cursor = 0
    for e in table(index, 0x00, 16)[2]:
        psm, packed, address, _ = struct.unpack("<IIII", e)
        if address != cursor:
            raise SystemExit("the GS RAM upload list is not sequential")
        w, h = packed & 0xFFFF, packed >> 16
        cursor += max(w * h, 0x100) if psm == 0x13 else 0x200 if psm == 2 else 0x400
    if cursor != len(gs):
        raise SystemExit(f"the GS RAM image is {len(gs):#x} bytes but its upload list covers {cursor:#x}")


def moby_classes_end(index: bytes) -> int:
    """Where the moby class blobs end in the core data: the first core offset past the last class's start, inside
    the part of the core the engine keeps resident (+0x7C; Ratchet's sequences and the gadget blobs come after it,
    in memory the engine reuses: a class placed there hung the load)."""
    _, _, classes = table(index, 0x18, 32)
    textures_base, = struct.unpack_from("<I", index, 0x60)
    resident_end, = struct.unpack_from("<I", index, 0x7C)
    last = max(struct.unpack_from("<i", e)[0] for e in classes if struct.unpack_from("<i", e)[0] > 0)
    end = min(o for _, o in core_pointers(index) if o > last)
    if not textures_base < end <= resident_end:
        raise SystemExit(f"unexpected core layout: textures at {textures_base:#x}, moby classes end at {end:#x}, "
                         f"resident part ends at {resident_end:#x}")
    return end
