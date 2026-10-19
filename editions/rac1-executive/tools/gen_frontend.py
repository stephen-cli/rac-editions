#!/usr/bin/env python3
"""
q4: the frontend's flight to a planet in 16:9 (src/widescreen.c, FE_BOOT/FE_BIN).

Loading a save from the title screen plays the flight to the planet
(DoSpaceTransition, game mode 6) from the frontend's program, not from a
level's, so none of the level patches apply to it: the picture came out
stretched and the planet's name unmoved (owner, 2026-10-20). An in-game load
runs the same code from the level program, which is patched.

There are two frontends. After power-on the title runs the boot
executable's own code (SCES_509.16, every function at its own address).
Quitting a level to the title loads `frontbin` (table of contents + 0x4F8)
instead: chunks {destination, size, pad, entry} copied into place by the
core's ParseBin (func_0012DA38), the same functions linked at other
addresses. This finds the few the hooks need in frontbin by their
instructions (the executable's, with jump targets and %hi/%lo halves
masked), reads the data they address from their own lui/addiu pairs, and

  emit         writes build/modseg/gen_frontend.c: per frontend, the hooked
               functions and the data the hooks read (tools/modseg.py
               compiles it into the mod segment)
  patch-elf F  patches the boot executable F in place (after OpenRAC's
               tools/boot_elf.py): UpdateViewContext's entry goes to
               ee_view_hook with the frontend's id, its +0x68 read of the
               field of view reads +0xBC, and DrawSpaceFlightFrame's planet
               name call goes to ee_ws_br_planet_name, as in every level
  frontbin_patches(iso)   the same words for frontbin, as image offsets, for
               tools/overlay_patch.py (feature widescreen)

The hooks widen the view only in game mode 6, so the title and its menus stay
4:3 as the owner chose.

  python tools/gen_frontend.py [--openrac DIR] emit
  python tools/gen_frontend.py [--openrac DIR] patch-elf build-sn/SCES_509.16
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SECTOR = 2048
TOC_LBA, FRONTBIN_TOC = 1500, 0x4F8         # the table of contents; frontbin's {LBA, sectors}
FE_BOOT, FE_BIN = 19, 20                    # the frontends' ids after the 19 levels (src/widescreen.c)

UPDATE_VIEW = "func_001F3140"               # UpdateViewContext
FLIGHT = "func_002305A0"                    # DrawSpaceFlightFrame
PLANET_NAME = "func_0022FD20"               # the flight screen's planet name picture
DRAW_WORLD = "func_001F3D78"
LETTERBOX = "func_001F5148"
FUNCS = (UPDATE_VIEW, FLIGHT, PLANET_NAME, DRAW_WORLD, LETTERBOX)
PLANET_NAME_SITE = 0x140                    # DrawSpaceFlightFrame + 0x140: jal the planet name
# The data the hooks read, as tools/modseg.py LEVEL_DATA: a function and its %hi/%lo offsets.
DATA = {
    "view": (UPDATE_VIEW, 0x50, 0x60),
    "chain_cursor": (LETTERBOX, 0x68, 0x6C),
    "letterbox_lines": (LETTERBOX, 0x24, 0x28),
    "game_mode": (DRAW_WORLD, 0x4D8, 0x4DC),
}
VIEW_PROLOGUE = (0x27BDFFA0, 0x3C020016)    # addiu $sp,$sp,-0x60; lui $v0,0x16 (ee_view_hook runs them)
VIEW_FOV_READ, LWC1_FOV, LWC1_FOV_SRC = 0x68, 0xC46000B0, 0xC46000BC


def j(target):
    return 0x08000000 | ((target >> 2) & 0x3FFFFFF)


def jal(target):
    return 0x0C000000 | ((target >> 2) & 0x3FFFFFF)


def addiu_at(imm):
    return 0x24010000 | (imm & 0xFFFF)          # addiu $at, $zero, imm


class Program:
    """A program's words by address: the boot executable's one segment, or frontbin's chunks."""

    def __init__(self, ranges):
        self.ranges = ranges                    # [(address, size, bytes, offset of the first byte in the file)]

    def where(self, addr):
        for base, size, blob, file_off in self.ranges:
            if base <= addr < base + size:
                return blob, addr - base, file_off + addr - base
        raise SystemExit(f"0x{addr:08X} is outside the program")

    def word(self, addr):
        blob, i, _ = self.where(addr)
        return struct.unpack_from("<I", blob, i)[0]

    def words(self, addr, n):
        return [self.word(addr + 4 * k) for k in range(n)]

    def file_offset(self, addr):
        return self.where(addr)[2]


def elf_program(exe: bytes) -> Program:
    phoff, = struct.unpack_from("<I", exe, 28)
    _, off, vaddr, _, filesz = struct.unpack_from("<5I", exe, phoff)
    return Program([(vaddr, filesz, exe[off:off + filesz], off)])


def frontbin_location(f) -> tuple[int, int]:
    f.seek(TOC_LBA * SECTOR + FRONTBIN_TOC)
    return struct.unpack("<2I", f.read(8))


def frontbin_program(data: bytes, base: int = 0) -> Program:
    """ParseBin's view of frontbin: chunks of {destination, size, pad, entry} while the entry agrees."""
    ranges, p, entry = [], 0, None
    while p + 16 <= len(data):
        dst, size, _pad, e = struct.unpack_from("<4I", data, p)
        if entry is None:
            entry = e
        elif e != entry:
            break
        ranges.append((dst, size, data[p + 16:p + 16 + size], base + p + 16))
        p += 16 + size
    return Program(ranges)


def sizes(openrac: Path) -> dict[str, int]:
    out = {}
    for line in (openrac / "games/rac1/config/overlays/functions.tsv").read_text().splitlines():
        cols = line.split("\t")
        if len(cols) > 2 and cols[0] in FUNCS:
            out[cols[0]] = int(cols[2])
    return out


def mask(w):
    """An instruction with what the linker fills in cleared: jump targets, %hi and %lo halves."""
    op = w >> 26
    if op in (2, 3):
        return w & 0xFC000000
    if op in (0x09, 0x0D, 0x0F, 0x1E, 0x1F, 0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B, 0x31, 0x37, 0x39, 0x3F):
        return w & 0xFFFF0000
    return w


def locate(name: str, size: int, exe: Program, fb: Program) -> int:
    """NAME's address in frontbin: the one place its code matches the executable's, masked."""
    n = size // 4
    pattern = [mask(w) for w in exe.words(int(name[5:], 16), n)]
    hits = []
    for base, length, blob, _ in fb.ranges:
        words = [mask(w) for w in struct.unpack_from(f"<{length // 4}I", blob, 0)]
        hits += [base + 4 * i for i in range(len(words) - n + 1) if words[i] == pattern[0] and words[i:i + n] == pattern]
    if len(hits) != 1:
        raise SystemExit(f"{name}: {len(hits)} matches in frontbin, expected one")
    return hits[0]


def hi_lo_target(prog: Program, fn: int, hi_off: int, lo_off: int) -> int:
    lui, lo = prog.word(fn + hi_off), prog.word(fn + lo_off)
    if lui >> 26 != 0x0F:
        raise SystemExit(f"0x{fn:08X} + 0x{hi_off:X} is not a lui")
    lo16 = lo & 0xFFFF
    return ((lui & 0xFFFF) << 16) + (lo16 - 0x10000 if lo16 & 0x8000 else lo16)


def frontends(openrac: Path, iso: Path | None = None):
    """[(Program, {name: address}, {data: address})] for the boot executable and frontbin."""
    exe = elf_program((openrac / "games/rac1/baserom/SCES_509.16").read_bytes())
    iso = iso or openrac / "games/rac1/baserom/SCES_509.16.iso"
    with iso.open("rb") as f:
        lba, count = frontbin_location(f)
        f.seek(lba * SECTOR)
        fb = frontbin_program(f.read(count * SECTOR), lba * SECTOR)
    size = sizes(openrac)
    out = []
    for prog, place in ((exe, lambda n: int(n[5:], 16)), (fb, lambda n: locate(n, size[n], exe, fb))):
        funcs = {n: place(n) for n in FUNCS}
        data = {k: hi_lo_target(prog, funcs[fn], hi, lo) for k, (fn, hi, lo) in DATA.items()}
        out.append((prog, funcs, data))
    return out


def patches(prog: Program, funcs: dict, fe_id: int, symbol) -> list[tuple[int, int, int]]:
    """(address, retail word, new word) for one frontend."""
    f = funcs[UPDATE_VIEW]
    return [(f, VIEW_PROLOGUE[0], j(symbol("ee_view_hook"))),
            (f + 4, VIEW_PROLOGUE[1], addiu_at(fe_id)),
            (f + VIEW_FOV_READ, LWC1_FOV, LWC1_FOV_SRC),
            (funcs[FLIGHT] + PLANET_NAME_SITE, jal(funcs[PLANET_NAME]), jal(symbol("ee_ws_br_planet_name")))]


def frontbin_patches(openrac: Path, iso: Path, symbol) -> list[tuple[int, int, int]]:
    """(byte offset in ISO, retail word, new word) of frontbin's patches."""
    prog, funcs, _ = frontends(openrac, iso)[1]
    return [(prog.file_offset(a), old, new) for a, old, new in patches(prog, funcs, FE_BIN, symbol)]


def emit(openrac: Path, out: Path) -> None:
    fes = frontends(openrac)
    rows = [("ee_fe_update_view", lambda fn, d: fn[UPDATE_VIEW]),
            ("ee_fe_flight", lambda fn, d: fn[FLIGHT]),
            ("ee_fe_planet_name", lambda fn, d: fn[PLANET_NAME]),
            ("ee_fe_view", lambda fn, d: d["view"]),
            ("ee_fe_chain_cursor", lambda fn, d: d["chain_cursor"]),
            ("ee_fe_letterbox_lines", lambda fn, d: d["letterbox_lines"]),
            ("ee_fe_game_mode", lambda fn, d: d["game_mode"])]
    lines = ["/* generated by tools/gen_frontend.py: the boot executable's frontend, then frontbin's */"]
    for name, get in rows:
        lines.append(f"const unsigned int {name}[2] = {{ {', '.join(f'0x{get(fn, d):08X}' for _, fn, d in fes)} }};")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    for (_, fn, d), label in zip(fes, ("boot", "frontbin")):
        print(f"{label}: " + ", ".join(f"{k} 0x{v:08X}" for k, v in {**fn, **d}.items()))
    print(f"wrote {out}")


def patch_elf(openrac: Path, path: Path) -> None:
    sys.path.insert(0, str(HERE))
    from overlay_patch import modseg_symbol
    image = bytearray(path.read_bytes())
    prog, funcs, _ = frontends(openrac)[0]
    live = elf_program(bytes(image))
    for addr, old, new in patches(prog, funcs, FE_BOOT, modseg_symbol):
        off = live.file_offset(addr)
        cur, = struct.unpack_from("<I", image, off)
        if cur not in (old, new):
            raise SystemExit(f"{path}: at 0x{addr:08X} found {cur:08X}, expected {old:08X}")
        struct.pack_into("<I", image, off, new)
    path.write_bytes(image)
    print(f"{path}: the frontend's UpdateViewContext and planet name hooked (4 words)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parent / "OpenRAC")
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("emit")
    e.add_argument("-o", "--output", type=Path, default=ROOT / "build/modseg/gen_frontend.c")
    p = sub.add_parser("patch-elf")
    p.add_argument("elf", type=Path)
    args = ap.parse_args()
    if args.cmd == "emit":
        emit(args.openrac, args.output)
    else:
        patch_elf(args.openrac, args.elf)
    return 0


if __name__ == "__main__":
    sys.exit(main())
