# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
Reading a PlayStation 2 disc image: its ISO 9660 file system, and the executable it boots.

  from openrac_common import iso9660
  serial = iso9660.boot_serial(image)          # "SCES_509.16", or None for no PS2 disc
  boot = iso9660.read(image, serial)           # that file's bytes
  for path, lba, size in iso9660.files(image): # every file, "DIR/NAME" without its ";1"
      ...

What a PS2 disc needs and no more: the primary volume descriptor, directory records in 2048-byte
sectors, names without their version. No Joliet, no Rock Ridge, no multi-extent files, no
interleaving; the discs do not use them. The format is ECMA-119 (documented); the offsets below
are its fields.

Written from tools/openrac.py's iso_find, boot_serial and read_boot and tools/extractor.py's
walk and files_of (with their limits and errors), which use this since P1-05. editor/disc.py
(Disc.files, BOOT) reads the same records with its own checks and stays as it is until the
editor moves onto this module. Never give this a path to a disc in a test: the tests build an
image of their own.
"""
import re
import struct
from collections.abc import Iterator
from pathlib import Path
from typing import BinaryIO

# Every ISO 9660 image the PS2 reads has 2048-byte logical sectors.
SECTOR = 2048

# The primary volume descriptor is sector 16 (ECMA-119 6.2.1, 8.4): its type byte, then "CD001".
PVD_SECTOR = 16
STANDARD_ID = b"CD001"

# The root directory's record sits at byte 156 of the descriptor; a record holds its extent's
# first sector at +2 and its length in bytes at +10 (both little-endian halves of a both-endian
# field), its flags at +25, its name's length at +32 and the name from +33 (ECMA-119 9.1).
PVD_ROOT_RECORD = 156
RECORD_EXTENT = 2
RECORD_LENGTH = 10
RECORD_FLAGS = 25
RECORD_NAME_LENGTH = 32
RECORD_NAME = 33
FLAG_DIRECTORY = 0x02

# The names of a directory's own record and of its parent's (ECMA-119 6.8.2.2).
SELF_AND_PARENT = (b"\0", b"\1")

# The extractor's limits on a damaged or hostile image: the discs nest no deeper than this,
# and their root directories are a sector or two.
MAX_DEPTH = 8
MAX_ROOT_SECTORS = 64

# A name a tool may turn into a path: no separator, no "..", nothing a file system treats specially.
SAFE_NAME = re.compile(r"[A-Za-z0-9_.\-]+")


class IsoError(ValueError):
    """An image that is not ISO 9660, or whose directories cannot be trusted. DETAIL says what
    was wrong ("" when the image is not ISO 9660 at all), worded for a player's error report."""

    def __init__(self, detail: str = ""):
        super().__init__(detail or "the image is not an ISO 9660 disc image")
        self.detail = detail


def root_directory(f: BinaryIO) -> tuple[int, int] | None:
    """(first sector, size in bytes) of the root directory of the image open as F, or None when
    F has no ISO 9660 volume descriptor."""
    f.seek(PVD_SECTOR * SECTOR)
    pvd = f.read(SECTOR)
    if len(pvd) < SECTOR or pvd[1:6] != STANDARD_ID:
        return None
    return (struct.unpack_from("<I", pvd, PVD_ROOT_RECORD + RECORD_EXTENT)[0],
            struct.unpack_from("<I", pvd, PVD_ROOT_RECORD + RECORD_LENGTH)[0])


def records(f: BinaryIO, lba: int, size: int) -> Iterator[tuple[bytes, int, int, int]]:
    """(raw name, first sector, size, flags) of each record of the directory at LBA of SIZE bytes,
    its own and its parent's included, in the order the disc has them. Raises IsoError for a
    record cut short by the end of the directory (or of the image)."""
    f.seek(lba * SECTOR)
    data, at = f.read(size), 0
    while at < len(data):
        length = data[at]
        # A record never crosses a sector: a zero length is padding up to the next one.
        if length == 0:
            at = (at // SECTOR + 1) * SECTOR
            continue
        # Its fixed fields end where its name starts; a damaged image can end inside them.
        if at + RECORD_NAME > len(data):
            raise IsoError("a directory record is cut short")
        name = data[at + RECORD_NAME:at + RECORD_NAME + data[at + RECORD_NAME_LENGTH]]
        yield (name, struct.unpack_from("<I", data, at + RECORD_EXTENT)[0],
               struct.unpack_from("<I", data, at + RECORD_LENGTH)[0], data[at + RECORD_FLAGS])
        at += length


def plain_name(raw: bytes) -> str:
    """A record's name without its version (";1"), decoded as Latin-1 so any byte reads."""
    return raw.decode("latin-1").split(";")[0]


def find(f: BinaryIO, path: str) -> tuple[int, int] | None:
    """(first sector, size) of the file or directory at PATH ("DIR/NAME", any case) in the image
    open as F, or None when F is no ISO 9660 image or has no such entry."""
    where = root_directory(f)
    if where is None:
        return None
    for part in path.upper().split("/"):
        found = None
        for raw, extent, length, _flags in records(f, *where):
            if plain_name(raw).upper() == part:
                found = extent, length
                break
        if found is None:
            return None
        where = found
    return where


def walk(f: BinaryIO, lba: int, size: int, prefix: str = "", depth: int = 0) -> list[tuple[str, int, int]]:
    """(path, first sector, size) of every file under the directory at LBA of SIZE bytes, in the
    disc's order, a directory's files where it is listed. Raises IsoError past MAX_DEPTH or on a
    name that is not SAFE_NAME, since callers write these paths to disk."""
    if depth > MAX_DEPTH:
        raise IsoError("directories nested too deep")
    out = []
    for raw, extent, length, flags in records(f, lba, size):
        if raw in SELF_AND_PARENT:
            continue
        name = plain_name(raw)
        if not SAFE_NAME.fullmatch(name) or name in (".", ".."):
            raise IsoError(f"a file name OpenRAC will not write: {name!r}")
        if flags & FLAG_DIRECTORY:
            out += walk(f, extent, length, prefix + name + "/", depth + 1)
        else:
            out.append((prefix + name, extent, length))
    return out


def files(image: Path | str) -> list[tuple[str, int, int]]:
    """(path, first sector, size) of every file on the disc image at IMAGE. Raises IsoError when
    it is not ISO 9660 or its directories are damaged."""
    with open(image, "rb") as f:
        where = root_directory(f)
        if where is None:
            raise IsoError()
        if where[1] > MAX_ROOT_SECTORS * SECTOR:
            raise IsoError("the root directory is damaged")
        return walk(f, *where)


def read(image: Path | str, path: str) -> bytes:
    """The bytes of the file at PATH on the disc image at IMAGE. Raises FileNotFoundError when
    the image has no such file."""
    with open(image, "rb") as f:
        where = find(f, path)
        if where is None:
            raise FileNotFoundError(f"{path} is not on {image}")
        f.seek(where[0] * SECTOR)
        return f.read(where[1])


def parse_boot2(text: str) -> str | None:
    """The executable a SYSTEM.CNF's BOOT2 line names (`BOOT2 = cdrom0:\\SCES_509.16;1` gives
    SCES_509.16), or None when it has no BOOT2 line."""
    for line in text.splitlines():
        key, _, value = line.partition("=")
        if key.strip() == "BOOT2":
            return value.strip().split("\\")[-1].split(";")[0]
    return None


def boot_serial(image: Path | str) -> str | None:
    """The executable the disc image at IMAGE boots, named by its serial (SCES_509.16), or None
    when it is no PS2 disc: not ISO 9660, no SYSTEM.CNF, or no BOOT2 line in it."""
    with open(image, "rb") as f:
        where = find(f, "SYSTEM.CNF")
        if where is None:
            return None
        f.seek(where[0] * SECTOR)
        return parse_boot2(f.read(where[1]).decode("latin-1"))
