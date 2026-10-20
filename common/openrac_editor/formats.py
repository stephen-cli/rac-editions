"""Bounded readers for the containers on the PAL disc. Evidence: games/rac1/docs/ASSETS.md.

Every reader checks sizes and offsets before using them and raises
FormatError on anything outside the layouts we have evidence for.
"""

from dataclasses import dataclass
from functools import cached_property
import struct
import zlib


class FormatError(ValueError):
    """Data outside the layouts this extractor supports."""


def span(data: bytes, offset: int, size: int) -> bytes:
    """data[offset:offset + size], or FormatError if any byte is missing."""
    if offset < 0 or size < 0 or offset + size > len(data):
        raise FormatError(f"range {offset:#x}+{size:#x} exceeds {len(data):#x} bytes")
    return data[offset:offset + size]


def unpack(fmt: str, data: bytes, offset: int = 0) -> tuple:
    return struct.unpack(fmt, span(data, offset, struct.calcsize(fmt)))


def wad(data: bytes, limit: int = 64 << 20) -> bytes:
    """Decompress a WAD stream the way func_0020C468 does.

    After a 16-byte header ("WAD", then the stream size at +3), packets
    are LZO-like: literal runs, three match forms, and up to three
    trailing literals in the low bits of each match. A zero-distance long
    match of length 1 is a no-op; any other length skips to the next
    0x2000-byte block, which the game refills by DMA.
    """
    span(data, 0, 16)
    if data[:3] != b"WAD":
        raise FormatError("expected WAD compression header")
    end, = unpack("<I", data, 3)
    if not 16 <= end <= len(data):
        raise FormatError("invalid compressed WAD size")
    pos = 16
    out = bytearray()

    def byte() -> int:
        nonlocal pos
        if pos >= end:
            raise FormatError("truncated WAD packet")
        pos += 1
        return data[pos - 1]

    def literal(count: int) -> None:
        nonlocal pos
        if pos + count > end or len(out) + count > limit:
            raise FormatError("WAD literal exceeds input or output limit")
        out.extend(data[pos:pos + count])
        pos += count

    if pos < end and data[pos] > 0x11:
        literal(byte() - 0x11)
    # Retail traps (teq) on a literal run that follows literals.
    needs_match = bool(out)
    while pos < end:
        tag = byte()
        if tag < 0x10:
            if needs_match:
                raise FormatError("consecutive WAD literal runs")
            literal(tag + 3 if tag else byte() + 18)
            needs_match = True
            continue
        if tag >= 0x40:
            distance = 1 + ((tag >> 2) & 7) + byte() * 8
            count = (tag >> 5) + 1
        elif tag >= 0x20:
            count = (tag & 31 or byte() + 31) + 2
            lo, hi = byte(), byte()
            distance = 1 + (lo >> 2) + hi * 64
        else:
            count = tag & 7 or byte() + 7
            lo, hi = byte(), byte()
            distance = (tag & 8) * 2048 + (lo >> 2) + hi * 64
            if distance:
                distance += 0x4000
                count += 2
            elif count == 1:
                count = 0
            else:
                pos = 16 + (pos - 16 + 0x1fff) // 0x2000 * 0x2000
                if pos > end:
                    raise FormatError("WAD block skip exceeds compressed size")
                needs_match = False
                continue
        if distance > len(out) or len(out) + count > limit:
            raise FormatError("WAD match exceeds history or output limit")
        for _ in range(count):
            out.append(out[-distance])
        trailing = data[pos - 2] & 3
        literal(trailing)
        needs_match = trailing != 0
    return bytes(out)


def overlay_sections(data: bytes) -> dict:
    """Section records of a level's code overlay (ParseBin, func_0012DA38).

    Each record is (load address, size, type, entry point) and its bytes.
    ParseBin copies records until the entry point changes and returns it;
    here the records must fill the section exactly instead.
    """
    entry, = unpack("<I", data, 12)
    if not entry or entry % 4:
        raise FormatError("invalid overlay entry point")
    records, pos = [], 0
    while pos < len(data):
        address, size, kind, record_entry = unpack("<4I", data, pos)
        if record_entry != entry:
            raise FormatError("inconsistent overlay entry point")
        span(data, pos + 16, size)
        if address % 4 or size % 4 or address + size > 32 << 20:
            raise FormatError("overlay section outside EE RAM or not word aligned")
        records.append({"offset": pos + 16, "address": address, "bytes": size, "type": kind})
        pos += 16 + size
    if not any(r["address"] <= entry < r["address"] + r["bytes"] for r in records):
        raise FormatError("overlay entry point outside its sections")
    return {"entry_point": entry, "sections": records}


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload)))


def png(width: int, height: int, kind: int, pixels: bytes, *chunks: bytes) -> bytes:
    """An 8-bit PNG (kind 2: RGB, 3: indexed) of unfiltered rows; chunks go before the data."""
    stride = len(pixels) // height
    rows = b"".join(b"\0" + pixels[y * stride:(y + 1) * stride] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, kind, 0, 0, 0))
            + b"".join(chunks) + png_chunk(b"IDAT", zlib.compress(rows, 9)) + png_chunk(b"IEND", b""))


@dataclass(frozen=True)
class Texture:
    """An 8-bit indexed texture with a 256-entry RGBA32 palette (PSMT8, CSM1).

    The GS reads CSM1 palettes with index bits 3 and 4 swapped, and PS2
    alpha runs from 0 to 0x80 (opaque). See func_00203958 for the uploads.
    """
    width: int
    height: int
    pixels: bytes
    palette: bytes

    def __post_init__(self):
        if not (0 < self.width <= 4096 and 0 < self.height <= 4096):
            raise FormatError("unsupported texture dimensions")
        if len(self.pixels) != self.width * self.height or len(self.palette) != 1024:
            raise FormatError("incorrect texture pixel or palette size")

    @cached_property
    def colours(self) -> list[tuple[int, int, int, int]]:
        """RGBA in index order, alpha scaled to 0..255."""
        result = []
        for i in range(256):
            j = (i & ~0x18) | ((i & 8) << 1) | ((i & 0x10) >> 1)
            r, g, b, a = self.palette[j * 4:j * 4 + 4]
            result.append((r, g, b, min(255, a * 2)))
        return result

    @cached_property
    def cutout(self) -> bool:
        """True if any pixel in use is fully transparent."""
        return any(self.colours[i][3] == 0 for i in set(self.pixels))

    def png(self) -> bytes:
        """An indexed PNG with straight alpha in tRNS."""
        return png(self.width, self.height, 3, self.pixels,
                   png_chunk(b"PLTE", b"".join(bytes(c[:3]) for c in self.colours)),
                   png_chunk(b"tRNS", bytes(c[3] for c in self.colours)))
