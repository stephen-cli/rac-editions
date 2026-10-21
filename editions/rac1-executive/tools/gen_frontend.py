#!/usr/bin/env python3
"""
The title screen's programs: q4, the flight to a planet in 16:9
(src/widescreen.c), and the Movie volume row on the title screen's Sound
page (src/movie_volume.c). FE_BOOT/FE_BIN in src/modseg.h.

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
               name call goes to ee_ws_br_planet_name, as in every level;
               and for the Movie volume as in every level: the volume calls
               (overlay_patch.VOLUME_SITES), LoadHudBanks' header copy and
               bank link (the orange bar's icon, src/hud_icons.c), and the
               Sound page's widget (its input and draw words -> stubs 19/20)
  frontbin_patches(iso, features)   the same words for frontbin, as image
               offsets, for tools/overlay_patch.py (features widescreen and
               movie_volume)

The Sound page's functions in frontbin are the jal targets of its own copies
of Pause_SoundOptionsInput and DrawSoundMenu, read against the executable's.
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
LOAD_HUD = "func_002032D0"                  # LoadHudBanks (src/hud_icons.c's two hooks)
SOUND_INPUT = "func_0021DB30"               # Pause_SoundOptionsInput, the Sound page's input
SOUND_DRAW = "func_0021DE08"                # DrawSoundMenu
SOUND_INIT = "func_0022DBE8"                # Sound_InitState
SOUND_UPDATE = "func_0022DD68"              # sound_update
MOVIE_LOOP = "func_0023B740"                # the movie playback loop
FUNCS = (UPDATE_VIEW, FLIGHT, PLANET_NAME, DRAW_WORLD, LETTERBOX,
         LOAD_HUD, SOUND_INPUT, SOUND_DRAW, SOUND_INIT, SOUND_UPDATE, MOVIE_LOOP)
PLANET_NAME_SITE = 0x140                    # DrawSpaceFlightFrame + 0x140: jal the planet name
# The data the hooks read, as tools/modseg.py LEVEL_DATA: a function and its %hi/%lo offsets.
DATA = {
    "view": (UPDATE_VIEW, 0x50, 0x60),
    "chain_cursor": (LETTERBOX, 0x68, 0x6C),
    "letterbox_lines": (LETTERBOX, 0x24, 0x28),
    "game_mode": (DRAW_WORLD, 0x4D8, 0x4DC),
    "menu_state": (SOUND_INPUT, 0x54, 0x58),
    "hud_arena": (LOAD_HUD, 0x84, 0x8C),
}
# The functions the Sound page's input and draw call, in src/movie_volume.c's F_* order.
# In frontbin they are read from the jal targets of its copies of those two functions.
SOUND_FNS = ("func_0022ED80", "func_001F4630", "func_001F4748", "func_001FE540", "func_001F6CF8",
             "func_001F68E8", "func_00200198", "func_00201190", "func_00201640")
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


def call_targets(exe: Program, prog: Program, name: str, fn: int, size: int) -> dict[int, int]:
    """The executable's jal targets in NAME -> the same calls' targets in PROG's copy at FN."""
    out = {}
    for k in range(size // 4):
        a, b = exe.word(int(name[5:], 16) + 4 * k), prog.word(fn + 4 * k)
        if a >> 26 == 3:
            if b >> 26 != 3:
                raise SystemExit(f"{name} + 0x{4 * k:X}: a jal in the executable, {b:08X} in the copy")
            out[(a & 0x3FFFFFF) << 2] = (b & 0x3FFFFFF) << 2
    return out


def word_pairs(prog: Program, first: int, second: int) -> list[int]:
    """Every word-aligned place in PROG holding FIRST then SECOND."""
    out = []
    for base, length, blob, _ in prog.ranges:
        words = struct.unpack_from(f"<{length // 4}I", blob, 0)
        out += [base + 4 * i for i in range(len(words) - 1) if words[i] == first and words[i + 1] == second]
    return out


def frontends(openrac: Path, iso: Path | None = None):
    """[(Program, {name: address}, {data: address, ...})] for the boot executable and frontbin.
    The third also holds "sound_fns" (SOUND_FNS' addresses) and "sound_widgets" (the
    Sound page's slider widgets: records whose first two words are its input and draw)."""
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
        calls = {**call_targets(exe, prog, SOUND_INPUT, funcs[SOUND_INPUT], size[SOUND_INPUT]),
                 **call_targets(exe, prog, SOUND_DRAW, funcs[SOUND_DRAW], size[SOUND_DRAW])}
        data["sound_fns"] = [calls[int(n[5:], 16)] for n in SOUND_FNS]
        data["sound_widgets"] = word_pairs(prog, funcs[SOUND_INPUT], funcs[SOUND_DRAW])
        if not data["sound_widgets"]:
            raise SystemExit("no Sound page widget in a frontend")
        out.append((prog, funcs, data))
    return out


def patches(prog: Program, funcs: dict, data: dict, fe_id: int, symbol,
            features=("widescreen", "movie_volume")) -> list[tuple[int, int, int]]:
    """(address, retail word, new word) for one frontend."""
    out = []
    if "widescreen" in features:
        f = funcs[UPDATE_VIEW]
        out += [(f, VIEW_PROLOGUE[0], j(symbol("ee_view_hook"))),
                (f + 4, VIEW_PROLOGUE[1], addiu_at(fe_id)),
                (f + VIEW_FOV_READ, LWC1_FOV, LWC1_FOV_SRC),
                (funcs[FLIGHT] + PLANET_NAME_SITE, jal(funcs[PLANET_NAME]), jal(symbol("ee_ws_br_planet_name")))]
    if "movie_volume" in features:
        # the volume calls, the HUD loader's header copy and bank link (the movie bar's
        # icon, src/hud_icons.c), and the Sound page's widgets (src/movie_volume.c)
        sys.path.insert(0, str(HERE))
        from overlay_patch import volume_patches, site_offset
        label = "frontbin" if fe_id == FE_BIN else "boot executable"
        out += volume_patches(lambda n: funcs[n], prog.words, label)
        for site, hook in (("EE_SITE_HUD_HEADER_COPY", "ee_hud_header_copy"), ("EE_SITE_HUD_LINK_BANK", "ee_hud_link_bank")):
            a = funcs[LOAD_HUD] + site_offset(site)
            old = prog.word(a)
            if old >> 26 != 3:
                raise SystemExit(f"{label}: LoadHudBanks + 0x{a - funcs[LOAD_HUD]:X} is not a jal ({old:08X})")
            out.append((a, old, jal(symbol(hook))))
        for w in data["sound_widgets"]:
            out += [(w, funcs[SOUND_INPUT], symbol(f"ee_sound_input_{fe_id:02d}")),
                    (w + 4, funcs[SOUND_DRAW], symbol(f"ee_sound_draw_{fe_id:02d}"))]
    return out


def frontbin_patches(openrac: Path, iso: Path, symbol, features) -> list[tuple[int, int, int]]:
    """(byte offset in ISO, retail word, new word) of frontbin's patches for FEATURES."""
    prog, funcs, data = frontends(openrac, iso)[1]
    return [(prog.file_offset(a), old, new) for a, old, new in patches(prog, funcs, data, FE_BIN, symbol, features)]


def emit(openrac: Path, out: Path) -> None:
    fes = frontends(openrac)
    rows = [("ee_fe_update_view", lambda fn, d: fn[UPDATE_VIEW]),
            ("ee_fe_flight", lambda fn, d: fn[FLIGHT]),
            ("ee_fe_planet_name", lambda fn, d: fn[PLANET_NAME]),
            ("ee_fe_view", lambda fn, d: d["view"]),
            ("ee_fe_chain_cursor", lambda fn, d: d["chain_cursor"]),
            ("ee_fe_letterbox_lines", lambda fn, d: d["letterbox_lines"]),
            ("ee_fe_game_mode", lambda fn, d: d["game_mode"]),
            ("ee_fe_load_hud", lambda fn, d: fn[LOAD_HUD]),
            ("ee_fe_hud_arena", lambda fn, d: d["hud_arena"]),
            ("ee_fe_menu_state", lambda fn, d: d["menu_state"])]
    # src/hud_icons.c tells the loaders apart by return address: no level's copy may share one
    for line in (openrac / "games/rac1/config/overlays/functions.tsv").read_text().splitlines():
        cols = line.split("\t")
        if cols[0] == LOAD_HUD:
            levels = {int(p.split(":")[1], 16) for p in cols[5].split(",")}
            if levels & {fn[LOAD_HUD] for _, fn, _ in fes}:
                raise SystemExit("a frontend's LoadHudBanks is at a level's address")
    lines = ["/* generated by tools/gen_frontend.py: the boot executable's frontend, then frontbin's */"]
    for name, get in rows:
        lines.append(f"const unsigned int {name}[2] = {{ {', '.join(f'0x{get(fn, d):08X}' for _, fn, d in fes)} }};")
    lines.append(f"/* the Sound page's functions ({', '.join(SOUND_FNS)}) */")
    lines.append(f"const unsigned int ee_fe_sound_fns[2][{len(SOUND_FNS)}] = {{ "
                 + ", ".join("{ " + ", ".join(f"0x{a:08X}" for a in d["sound_fns"]) + " }" for _, _, d in fes) + " };")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    for (_, fn, d), label in zip(fes, ("boot", "frontbin")):
        print(f"{label}: " + ", ".join(f"{k} 0x{v:08X}" for k, v in {**fn, **d}.items() if isinstance(v, int))
              + f", Sound widgets {', '.join(f'0x{w:08X}' for w in d['sound_widgets'])}")
    print(f"wrote {out}")


def patch_elf(openrac: Path, path: Path) -> None:
    sys.path.insert(0, str(HERE))
    from overlay_patch import modseg_symbol
    image = bytearray(path.read_bytes())
    prog, funcs, data = frontends(openrac)[0]
    live = elf_program(bytes(image))
    words = patches(prog, funcs, data, FE_BOOT, modseg_symbol)
    for addr, old, new in words:
        off = live.file_offset(addr)
        cur, = struct.unpack_from("<I", image, off)
        if cur not in (old, new):
            raise SystemExit(f"{path}: at 0x{addr:08X} found {cur:08X}, expected {old:08X}")
        struct.pack_into("<I", image, off, new)
    path.write_bytes(image)
    print(f"{path}: the frontend's UpdateViewContext and planet name, volume calls, HUD loader and "
          f"Sound page hooked ({len(words)} words)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
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
