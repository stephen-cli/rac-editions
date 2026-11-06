"""
The disc-image operations the installer needs, on a player's own PS2 image.

The PAL disc of Ratchet & Clank names only three files in its ISO 9660
directory (the boot executable SCES_509.16 among them); the game reaches
everything else by absolute sector through the table of contents it reads at
startup. So an edition changes the image in place: words written where they
are, the boot executable replaced, and anything larger than what it replaces
placed in new sectors after the end of the image, with the directory record
or the table-of-contents entry pointed there and the volume size grown.

These are OpenRAC's games/rac1 tools/iso_patch.py and tools/boot_elf.py
(add_segment) as functions, so the installer needs no OpenRAC checkout.
"""
import hashlib
import struct
from pathlib import Path

SECTOR = 2048
PVD_SECTOR = 16
VOLUME_SIZE = PVD_SECTOR * SECTOR + 80   # the primary volume descriptor's volume space size, both byte orders
# The game's stream reader reads past the end of a file by up to a few sectors (seen: one sector past the
# text table). In the middle of the disc that lands on the next file; at the end of the image it is an
# error, so an image that grows keeps this many blank sectors after its last file.
TAIL_SECTORS = 32
PAGE = 0x1000


def both_endian(value: int) -> bytes:
    """A 32-bit field as ISO 9660 stores it: little-endian, then big-endian."""
    return struct.pack("<I", value) + struct.pack(">I", value)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 24), b""):
            h.update(chunk)
    return h.hexdigest()


def root_directory(f) -> tuple[int, int]:
    """Extent and size of the root directory, from the primary volume descriptor."""
    f.seek(PVD_SECTOR * SECTOR)
    pvd = f.read(SECTOR)
    if pvd[0] != 1 or pvd[1:6] != b"CD001":
        raise SystemExit("not an ISO 9660 image (no primary volume descriptor)")
    record = pvd[156:156 + 34]
    return struct.unpack_from("<I", record, 2)[0], struct.unpack_from("<I", record, 10)[0]


def directory_record(f, name: str) -> tuple[int, int, int]:
    """NAME's root directory record: its byte offset in the image, its extent (sector) and its size."""
    extent, size = root_directory(f)
    f.seek(extent * SECTOR)
    data = f.read(size)
    pos = 0
    while pos < len(data):
        length = data[pos]
        if length == 0:                      # padding to the next sector
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        rec = data[pos:pos + length]
        if rec[33:33 + rec[32]].decode("ascii", "replace") == name:
            return extent * SECTOR + pos, struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
        pos += length
    raise SystemExit(f"{name} not found in the root directory")


def read_file(f, name: str) -> bytes:
    """The bytes of NAME in the root directory."""
    _, extent, size = directory_record(f, name)
    f.seek(extent * SECTOR)
    return f.read(size)


def padded(blob: bytes) -> bytes:
    return blob.ljust(-(-len(blob) // SECTOR) * SECTOR, b"\0")


class Image:
    """An image opened for writing, which tracks the sectors added at its end. Close it with finish(), which
    leaves TAIL_SECTORS blank sectors after the last added file and writes the volume size."""

    def __init__(self, path: Path):
        self.f = path.open("r+b")
        self.f.seek(0, 2)
        self.sectors = self.f.tell() // SECTOR
        self.grew = False

    def write(self, offset: int, blob: bytes) -> None:
        self.f.seek(offset)
        self.f.write(blob)

    def write_word(self, offset: int, word: int) -> None:
        self.write(offset, struct.pack("<I", word))

    def append(self, blob: bytes) -> int:
        """BLOB in new sectors at the end; returns its first sector."""
        first = self.sectors
        self.write(first * SECTOR, padded(blob))
        self.sectors += len(padded(blob)) // SECTOR
        self.grew = True
        return first

    def replace_file(self, name: str, blob: bytes) -> None:
        """NAME's bytes replaced by BLOB: in its own sectors when it fits, else in new sectors at the end with
        its directory record moved there (extent and size, both byte orders)."""
        rec, extent, size = directory_record(self.f, name)
        if len(blob) <= size:
            self.write(extent * SECTOR, blob.ljust(size, b"\0"))
            return
        first = self.append(blob)
        self.write(rec + 2, both_endian(first))
        self.write(rec + 10, both_endian(len(blob)))

    def finish(self) -> None:
        if self.grew:
            self.write(self.sectors * SECTOR, bytes(TAIL_SECTORS * SECTOR))
            self.sectors += TAIL_SECTORS
            self.write(VOLUME_SIZE, both_endian(self.sectors))
        self.f.close()


def add_segment(image: bytes, addr: int, data: bytes) -> bytes:
    """The ELF IMAGE with DATA appended as one more PT_LOAD at ADDR. Retail's program header table (one
    entry at 0x34) is followed by zeros up to the first segment at 0x1000, so the new entry goes right after
    it. The data goes after the file's end (the section headers), at an offset congruent to ADDR modulo the
    page size, as the ELF rules want."""
    image = bytearray(image)
    e_phoff, = struct.unpack_from("<I", image, 28)
    e_phentsize, e_phnum = struct.unpack_from("<HH", image, 42)
    new_hdr = e_phoff + e_phnum * e_phentsize
    if any(image[new_hdr:new_hdr + e_phentsize]):
        raise SystemExit("no room for another program header")
    offset = len(image)
    offset += (addr - offset) % PAGE
    image += bytes(offset - len(image)) + data
    image += bytes((-len(image)) % 16)
    # PT_LOAD, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, PF_R|PF_W|PF_X, p_align
    struct.pack_into("<8I", image, new_hdr, 1, offset, addr, addr, len(data), len(data), 7, PAGE)
    struct.pack_into("<H", image, 44, e_phnum + 1)
    return bytes(image)
