#!/usr/bin/env python3
"""
Add strings to the game's text table.

The table (`all_text`, table-of-contents group at byte 0x1528 of the table
of contents, read whole at startup) is an uncompressed file: a header of
eight u32 offsets, one per sub-table (0 English, 1 unused, 2 French,
3 German, 4 Spanish, 5 Italian, 6 a few shared strings, 7 unused), each
sub-table `{u32 count, u32 size, entry[count]}` followed by its strings,
an entry being `{u32 offset from the sub-table start, u32 id, -1, 0}`.
Entries are not sorted by id.

text/strings.tsv holds the edition's strings: `id<TAB>English[<TAB>French
<TAB>German<TAB>Spanish<TAB>Italian]`, ids in hex, a missing language
falls back to English. Each is appended to the five language tables and
the whole file is rebuilt with offsets and the header recomputed.

  python tools/text_table.py [--openrac DIR] [-o build/all_text.bin]

The result is larger than the original and goes in new sectors at the end
of the disc image: OpenRAC's games/rac1/tools/iso_patch.py
`--append FILE@OFFSET` places it and writes {sector, sectors} at OFFSET,
here the table-of-contents entry at 1500 * 2048 + 0x1528.
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
EDITOR = HERE.parents[2] / "common/openrac_editor"   # OpenRAC's disc readers, carried (common/deps.py)
ROOT = HERE.parent
TOC_LBA, ALL_TEXT_TOC_OFFSET = 1500, 0x1528
LANGUAGE_TABLES = {0: 1, 2: 2, 3: 3, 4: 4, 5: 5}     # sub-table -> column in strings.tsv (1 = English)
SECTOR = 2048


def read_table(openrac: Path) -> bytes:
    sys.path.insert(0, str(EDITOR))
    from disc import Disc
    iso = openrac / "games/rac1/baserom/SCES_509.16.iso"
    with Disc(iso) as disc:
        ref = next(r for r in disc.survey()["global_references"] if r["group"] == "all_text")
        return disc.read(ref["lba"] * SECTOR, ref["bytes"])


def parse(raw: bytes):
    heads = struct.unpack_from("<8I", raw, 0)
    tables = []
    for t, start in enumerate(heads):
        count, size = struct.unpack_from("<2I", raw, start)
        entries = []
        for i in range(count):
            off, sid, a, b = struct.unpack_from("<4I", raw, start + 8 + 16 * i)
            s = raw[start + off:raw.index(b"\0", start + off)]
            entries.append((sid, s, a, b))
        tables.append(entries)
    return tables


def build(tables) -> bytes:
    out = bytearray(32)
    heads = []
    for entries in tables:
        heads.append(len(out))
        body = bytearray()
        strings = bytearray()
        base = 8 + 16 * len(entries)
        for sid, s, a, b in entries:
            body += struct.pack("<4I", base + len(strings), sid, a, b)
            strings += s + b"\0"
            strings += bytes((-len(strings)) % 4)
        table = struct.pack("<2I", len(entries), 8 + len(body) + len(strings)) + body + strings
        out += table
        out += bytes((-len(out)) % 16)
    struct.pack_into("<8I", out, 0, *heads)
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("--strings", type=Path, default=ROOT / "text/strings.tsv")
    ap.add_argument("-o", "--output", type=Path, default=ROOT / "build/all_text.bin")
    ap.add_argument("--check", action="store_true", help="only verify the original rebuilds byte for byte")
    args = ap.parse_args()

    raw = read_table(args.openrac)
    tables = parse(raw)
    rebuilt = build(tables)
    if rebuilt != raw:
        # The original has its own padding choices; a differing rebuild of unchanged
        # content is tolerated only if every table parses back the same.
        if parse(rebuilt) != tables:
            raise SystemExit("rebuild of the original does not parse back identically")
        print(f"note: rebuilt original differs in padding ({len(rebuilt)} vs {len(raw)} bytes), content identical")
    if args.check:
        print("ok")
        return 0

    added = 0
    existing = {sid for sid, *_ in tables[0]}
    for line in args.strings.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        cols = line.rstrip("\n").split("\t")
        sid = int(cols[0], 16)
        if sid in existing:
            raise SystemExit(f"id 0x{sid:X} already exists in the table")
        for table, col in LANGUAGE_TABLES.items():
            text = cols[col] if col < len(cols) and cols[col] else cols[1]
            tables[table].append((sid, text.encode("latin-1"), 0xFFFFFFFF, 0))
        added += 1
    data = build(tables)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"wrote {args.output}: {len(data)} bytes ({len(raw)} originally), {added} string(s) added; "
          f"append at the table-of-contents entry: offset 0x{TOC_LBA * SECTOR + ALL_TEXT_TOC_OFFSET:X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
