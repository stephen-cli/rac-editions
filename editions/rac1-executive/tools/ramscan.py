#!/usr/bin/env python3
"""
Look inside a PCSX2 savestate (.p2s) of Ratchet & Clank PAL: EE RAM facts
for the mod-segment design and for checking patches at run time.

  python tools/ramscan.py STATE.p2s [--dump OUT.bin] [--word ADDR ...] [--map]

Reports the completes counter (D_0015EF20), the overlay staging pointer
(D_0015EF4C) and the record headers it points at, the loaded level's text
end, and with --map a 64 KB-granularity picture of which parts of the 32 MB
are all zero, to find memory the game never touches.
"""
import argparse
import struct
import sys
import zipfile
from pathlib import Path

RAM = 32 << 20
COMPLETES = 0x15EF20
STAGING = 0x15EF4C


def ee_memory(path: Path) -> bytes:
    """eeMemory.bin from a PCSX2 savestate; PCSX2 may zstd-compress the entry (zip method 93)."""
    z = zipfile.ZipFile(path)
    info = next(i for i in z.infolist() if i.filename.lower().startswith("eememory"))
    try:
        data = z.read(info)
    except NotImplementedError:
        import zstandard
        with path.open("rb") as f:
            f.seek(info.header_offset)
            sig, _ver, _flag, _method, _t, _d, _crc, csize, _usize, nlen, xlen = struct.unpack("<IHHHHHIIIHH", f.read(30))
            assert sig == 0x04034B50
            f.seek(nlen + xlen, 1)
            raw = f.read(info.compress_size or csize)
        data = zstandard.ZstdDecompressor().decompressobj().decompress(raw)
    if len(data) < RAM:
        raise SystemExit(f"eeMemory.bin is {len(data)} bytes, expected at least {RAM}")
    return data[:RAM]


def u32(mem, addr):
    return struct.unpack_from("<I", mem, addr)[0]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("state", type=Path)
    ap.add_argument("--dump", type=Path, help="write the 32 MB of EE RAM to this file")
    ap.add_argument("--word", type=lambda s: int(s, 16), nargs="*", default=[], help="hex addresses to print")
    ap.add_argument("--map", action="store_true", help="zero/non-zero map of RAM in 64 KB blocks")
    args = ap.parse_args()

    mem = ee_memory(args.state)
    if args.dump:
        args.dump.write_bytes(mem)
        print(f"wrote {args.dump}")

    print(f"completes (0x{COMPLETES:X}) = {u32(mem, COMPLETES)}")
    print(f"bolts     (0x15EE98) = {u32(mem, 0x15EE98)}")
    stage = u32(mem, STAGING)
    print(f"overlay staging pointer (0x{STAGING:X}) = 0x{stage:08X}")
    if 0 < stage < RAM - 16:
        first = u32(mem, stage)
        print(f"  first word (offset of records) = 0x{first:X}")
        pos = stage + first
        entry = None
        for _ in range(12):
            if pos + 16 > RAM:
                break
            addr, size, kind, ent = struct.unpack_from("<4I", mem, pos)
            if entry is None:
                entry = ent
            if ent != entry or size > RAM:
                break
            print(f"  record: dest 0x{addr:08X} size 0x{size:X} type {kind} entry 0x{ent:X}")
            pos += 16 + size
        print(f"  records end in staging at 0x{pos:08X}")
    for a in args.word:
        print(f"[0x{a:08X}] = 0x{u32(mem, a):08X}")
    if args.map:
        block = 64 << 10
        line = []
        for i in range(0, RAM, block):
            chunk = mem[i:i + block]
            nz = sum(1 for b in chunk if b) if any(chunk) else 0
            line.append("." if nz == 0 else ("o" if nz < block // 64 else "#"))
            if len(line) == 32:
                print(f"0x{i - 31 * block:08X} {''.join(line)}")
                line = []
        print("legend: . all zero   o under 1/64 non-zero   # in use  (one char = 64 KB, one row = 2 MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
