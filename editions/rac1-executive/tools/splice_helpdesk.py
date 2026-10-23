#!/usr/bin/env python3
"""
Put the edition's HelpDesk lines (build/helpdesk/, from tools/gen_helpdesk.py) on an image.

The game finds a HelpDesk line through the help-audio table, a table-of-
contents group at byte 0x1AB8: one word per line, the first sector of a VAG
file (Sector32), 150 slots per language in language order (0 English, 1 none,
2 French, 3 German, 4 Spanish, 5 Italian). Music_PlayId30000 (func_00216528)
reads slot `id + 150 * language` from the boot copy of the table at 0x137C80,
so English line 150 + k is language 1's slot k: a block no disc fills, which
the edition takes for its new English lines (src/helpdesk_voices.c,
src/strafe_help.c).

Each file is named for its slot (`150_L1_bombglove_f.vag`) and must be a VAG
in the format of the game's own lines: `VAGp`, version 0x20, big-endian data
size and 44,100 Hz, a 0x30-byte header, SPU-ADPCM mono frames ending in a
flag-7 frame, padded to whole sectors. The files go in new sectors at the end
of the image, in place of the blank margin the earlier steps leave there,
followed by a new margin (the game's stream reader reads a few sectors past a
file's end), and the volume size grows to match. A slot that is already
filled is refused: the step runs once, on a fresh image.

  python tools/splice_helpdesk.py IMAGE [--audio build/helpdesk]
"""
import argparse
import re
import struct
import sys
from pathlib import Path

from splice_boots import SECTOR, TAIL_SECTORS, TOC_LBA

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
HELP_AUDIO = 0x1AB8          # the help-audio group in the table of contents
SLOTS_PER_LANGUAGE = 150
FIRST_EDITION_SLOT = 150     # language 1's block
LAST_EDITION_SLOT = 299
VAG_HEADER = 0x30
SAMPLE_RATE = 44100


def check_vag(path: Path, data: bytes) -> None:
    if len(data) < VAG_HEADER + 32 or data[:4] != b"VAGp":
        raise SystemExit(f"{path.name}: not a VAG file")
    version, _, size, rate = struct.unpack_from(">4I", data, 4)
    if version != 0x20 or rate != SAMPLE_RATE:
        raise SystemExit(f"{path.name}: version 0x{version:X}, {rate} Hz; the game's lines are 0x20, {SAMPLE_RATE} Hz")
    if size % 16 or VAG_HEADER + size > len(data):
        raise SystemExit(f"{path.name}: the data size 0x{size:X} does not fit the file")
    if data[VAG_HEADER + size - 15] != 7:
        raise SystemExit(f"{path.name}: the last frame does not end the sound (flag 7)")


def lines(audio: Path) -> list[tuple[int, Path, bytes]]:
    found = []
    for path in sorted(audio.glob("*.vag")):
        match = re.match(r"(\d+)_", path.name)
        if not match:
            raise SystemExit(f"{path.name}: the name must start with its slot, e.g. 150_L1_bombglove_f.vag")
        slot = int(match.group(1))
        if not FIRST_EDITION_SLOT <= slot <= LAST_EDITION_SLOT:
            raise SystemExit(f"{path.name}: slot {slot} is outside the edition's "
                             f"{FIRST_EDITION_SLOT}-{LAST_EDITION_SLOT}")
        data = path.read_bytes()
        check_vag(path, data)
        found.append((slot, path, data))
    slots = [slot for slot, _, _ in found]
    if len(set(slots)) != len(slots):
        raise SystemExit("two files name the same slot")
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", type=Path)
    ap.add_argument("--audio", type=Path, default=ROOT / "build/helpdesk")
    args = ap.parse_args()

    found = lines(args.audio)
    if not found:
        raise SystemExit(f"no .vag files in {args.audio}")
    with args.image.open("r+b") as f:
        f.seek(0, 2)
        image_end = f.tell()
        if image_end % SECTOR:
            raise SystemExit("the image is not a whole number of sectors")
        end = image_end // SECTOR
        f.seek((end - TAIL_SECTORS) * SECTOR)
        if not any(f.read(TAIL_SECTORS * SECTOR)):
            end -= TAIL_SECTORS     # reuse the blank margin the earlier steps left
        table = TOC_LBA * SECTOR + HELP_AUDIO
        lba = end
        for slot, path, data in found:
            f.seek(table + 4 * slot)
            old, = struct.unpack("<I", f.read(4))
            if old:
                raise SystemExit(f"slot {slot} already points at sector {old}: run this on a fresh image")
            padded = data.ljust(-(-len(data) // SECTOR) * SECTOR, b"\0")
            f.seek(lba * SECTOR)
            f.write(padded)
            f.seek(table + 4 * slot)
            f.write(struct.pack("<I", lba))
            print(f"  slot {slot}: {path.name}, {len(data)} bytes at sector {lba}")
            lba += len(padded) // SECTOR
        f.seek(lba * SECTOR)
        f.write(bytes(TAIL_SECTORS * SECTOR))
        total = lba + TAIL_SECTORS
        f.truncate(total * SECTOR)
        f.seek(16 * SECTOR + 80)
        f.write(struct.pack("<I", total) + struct.pack(">I", total))
    print(f"{len(found)} HelpDesk lines in slots {found[0][0]}-{found[-1][0]}; image now {total} sectors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
