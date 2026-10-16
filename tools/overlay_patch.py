#!/usr/bin/env python3
"""
Level-code patches for the Executive Edition, written as disc sectors.

Gameplay code lives in the 19 level programs (overlays), not in the boot
executable, and each level has its own copy of the shared functions at its
own addresses. OpenRAC's catalogue (games/rac1/pal/config/overlays/
functions.tsv) gives every function's address in every level, and the level
code is stored raw on the disc, so a patch is the same words written at 19
places, with every address resolved per level.

Each feature here is a list of word patches and, where more than a word or
two is needed, a shim placed in a code cave: one of the linker remnants the
catalogue lists (config/overlays/linker_remnants.txt), dead words retail's
linker left of stripped functions, which nothing reaches.

Output is the editor's patch format, which tools/iso_patch.py in OpenRAC's
games/rac1/pal applies: <out>/patches.tsv listing first sector, sector
count and a file of those sectors with the patch applied.

Usage (from anywhere):
  python tools/overlay_patch.py [--openrac DIR] [-o build/patches] [--dry-run] [FEATURE ...]
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SECTOR = 2048

# --- MIPS encoders ------------------------------------------------------------

AT, ZERO, A4, T2 = 1, 0, 8, 10


def lui(rt, imm16):
    return 0x3C000000 | (rt << 16) | (imm16 & 0xFFFF)


def lw(rt, base, imm16):
    return 0x8C000000 | (base << 21) | (rt << 16) | (imm16 & 0xFFFF)


def addiu(rt, rs, imm16):
    return 0x24000000 | (rs << 21) | (rt << 16) | (imm16 & 0xFFFF)


def movn(rd, rs, rt):  # rd = rs if rt != 0
    return (rs << 21) | (rt << 16) | (rd << 11) | 0x0B


def j(target):
    return 0x08000000 | ((target >> 2) & 0x3FFFFFF)


def jal(target):
    return 0x0C000000 | ((target >> 2) & 0x3FFFFFF)


NOP = 0


def hi_lo(addr):
    """%hi/%lo of ADDR as the assembler splits them (the low half is signed)."""
    lo = addr & 0xFFFF
    hi = (addr >> 16) + (1 if lo & 0x8000 else 0)
    return hi, lo


# --- Features -----------------------------------------------------------------

# Game state the executable keeps resident. US addresses are from ReRAC's
# game_state.md; PAL is +0x100 for this block (bolts: US 0x15ED98, PAL
# 0x15EE98), and PAL func_00227DB0 (the challenge-mode reset, US 0x226B08)
# increments this word.
COMPLETES = 0x0015EF20      # s32: times the game was completed; > 0 in a challenge-mode run


def metal_detector_10x(at):
    """Metal Detector pays 10x in challenge mode.

    The detector's update function (class 585, func_L00_002D4398) drains the
    hidden spot's bolt count in steps of 5, spawning one bolt per step with
    func_L00_002A86D8(target, pos, dir, 0x13, value=5, 0). The spawner only
    knows the denominations 1-5, 20 and 50, each with its own model. The
    shim turns value 5 into 50 (circlenut bolts) when completes > 0, so the
    payout is exactly 10x with the same number of spawns.
    """
    cave = at("func_L00_001F3DF0")                 # 44-byte linker remnant, in every level
    spawn = at("func_L00_002A86D8")
    site = at("func_L00_002D4398") + (0x2D49D0 - 0x2D4398)   # the jal in the drain loop
    hi, lo = hi_lo(COMPLETES)
    shim = [
        lui(AT, hi),
        lw(AT, AT, lo),              # $at = completes
        addiu(T2, ZERO, 50),
        movn(A4, T2, AT),            # value = 50 if completes != 0
        j(spawn),                    # tail call; $ra still returns into the caller
        NOP,
    ]
    return [
        (site, jal(spawn), jal(cave)),
        *[(cave + 4 * i, None, w) for i, w in enumerate(shim)],
    ]


def modseg_symbol(name: str) -> int:
    """Address of an exported mod-segment function (tools/modseg.py's modseg.sym)."""
    path = HERE.parent / "build/modseg/modseg.sym"
    if not path.is_file():
        raise SystemExit(f"missing {path}: run tools/modseg.py first")
    for line in path.read_text().splitlines():
        n, a = line.split("	")
        if n == name:
            return int(a, 16)
    raise SystemExit(f"{name} is not exported by the mod segment")


def metal_detector_10x_modseg(at):
    """The same feature with its logic in C in the resident mod segment
    (src/metal_detector.c): the detector's spawn call goes to
    ee_metal_detector_spawn, which calls the level's spawner through the
    generated level table. No cave needed."""
    spawn = at("func_L00_002A86D8")
    site = at("func_L00_002D4398") + (0x2D49D0 - 0x2D4398)
    return [(site, jal(spawn), jal(modseg_symbol("ee_metal_detector_spawn")))]


def quick_select_pause(at):
    """q3: pause while quick select is open, with a toggle under Options
    (src/quick_select_pause.c). The frame's HudUpdate call goes to the hook,
    and the Options list widget's item pointer (executable data, the same
    in every level's data record) points at the rows in the mod segment."""
    import json
    hud = at("func_L00_0023B610")
    site = at("func_L00_00299250") + (0x299A88 - 0x299250)
    menu = HERE.parent / "build/modseg/menu_sites.json"
    if not menu.is_file():
        raise SystemExit(f"missing {menu}: run tools/gen_menu.py first")
    m = json.loads(menu.read_text())[str(at.level)]
    return [(site, jal(hud), jal(modseg_symbol("ee_hud_update_hook")))] + [
        (m[k]["site"], m[k]["expect"], modseg_symbol(m[k]["symbol"])) for k in ("items", "desc", "image")]


FEATURES = {
    "metal_detector_10x": metal_detector_10x,
    "metal_detector_10x_modseg": metal_detector_10x_modseg,
    "quick_select_pause": quick_select_pause,
}
DEFAULT_FEATURES = ["metal_detector_10x_modseg", "quick_select_pause"]


# --- Disc plumbing ------------------------------------------------------------

def load_catalogue(openrac: Path) -> dict[str, dict[int, int]]:
    places = {}
    path = openrac / "games/rac1/pal/config/overlays/functions.tsv"
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        name, _kind, _size, _fp, _levels, pl = line.split("\t")[:6]
        places[name] = {int(p.split(":")[0]): int(p.split(":")[1], 16) for p in pl.split(",")}
    return places


def level_text_locations(openrac: Path):
    """level id -> list of (first byte of a record in the image, its load address, its size),
    text record first, then the level's data record (the executable's data, menus included,
    is part of every level's data record at the same addresses)."""
    sys.path.insert(0, str(openrac / "editor"))
    from disc import Disc
    from formats import unpack, span, overlay_sections
    from level import SECTION_NAMES
    iso = openrac / "games/rac1/pal/baserom/SCES_509.16.iso"
    out = {}
    with Disc(iso) as disc:
        for info in disc.survey()["levels"]:
            r = info["ranges"]["data"]
            data = disc.sectors(r["lba"], r["sectors"])
            off, size = unpack("<ii", data, SECTION_NAMES.index("overlay") * 8)
            secs = overlay_sections(span(data, off, size))["sections"]
            out[info["id"]] = [(r["lba"] * SECTOR + off + rec["offset"], rec["address"], rec["bytes"])
                               for rec in (secs[-1], secs[2])]
    return iso, out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("features", nargs="*", default=DEFAULT_FEATURES)
    ap.add_argument("--openrac", type=Path, default=HERE.parent.parent / "OpenRAC")
    ap.add_argument("-o", "--output", type=Path, default=HERE.parent / "build/patches")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    for f in args.features:
        if f not in FEATURES:
            print(f"unknown feature {f}; known: {', '.join(FEATURES)}", file=sys.stderr)
            return 2
    places = load_catalogue(args.openrac)
    iso, texts = level_text_locations(args.openrac)

    sectors: dict[int, bytearray] = {}
    with iso.open("rb") as f:
        def sector(n):
            if n not in sectors:
                f.seek(n * SECTOR)
                sectors[n] = bytearray(f.read(SECTOR))
            return sectors[n]

        total = 0
        for level in range(19):
            records = texts[level]

            def at(name, level=level):
                try:
                    return places[name][level]
                except KeyError:
                    raise SystemExit(f"{name} has no place in level {level}")
            at.level = level

            def file_offset(addr):
                for file_base, rec_addr, rec_size in records:
                    if rec_addr <= addr < rec_addr + rec_size:
                        return file_base + (addr - rec_addr)
                raise SystemExit(f"level {level}: 0x{addr:X} is in neither the text nor the data record")

            for feature in args.features:
                for addr, expect, word in FEATURES[feature](at):
                    off = file_offset(addr)
                    s, i = divmod(off, SECTOR)
                    buf = sector(s)
                    old, = struct.unpack_from("<I", buf, i)
                    if expect is not None and old != expect:
                        raise SystemExit(f"level {level} {feature}: at 0x{addr:X} found {old:08X}, expected {expect:08X}; "
                                         f"is this the PAL v2.00 image, unpatched?")
                    struct.pack_into("<I", buf, i, word)
                    total += 1
        print(f"{total} words patched across {len(sectors)} sectors in 19 levels "
              f"({', '.join(args.features)})")

    if args.dry_run:
        return 0
    args.output.mkdir(parents=True, exist_ok=True)
    runs = []
    for n in sorted(sectors):
        if runs and runs[-1][0] + runs[-1][1] == n:
            runs[-1][1] += 1
        else:
            runs.append([n, 1])
    lines = ["# first sector\tsectors\tfile (beside this list)"]
    for first, count in runs:
        name = f"sectors_{first}.bin"
        (args.output / name).write_bytes(b"".join(sectors[first + k] for k in range(count)))
        lines.append(f"{first}\t{count}\t{name}")
    (args.output / "patches.tsv").write_text("\n".join(lines) + "\n")
    print(f"wrote {args.output / 'patches.tsv'} ({len(runs)} sector runs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
