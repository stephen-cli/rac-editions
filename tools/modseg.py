#!/usr/bin/env python3
"""
Build the Executive Edition's resident code: the "mod segment".

C in src/ is compiled with the game's own compiler (SN GCC 2.95.3 from
OpenRAC's toolchain), linked at MODSEG_BASE against the game's symbols, and
written out as a flat image the boot executable carries as a second
loadable segment (OpenRAC games/rac1: tools/boot_elf.py --segment). The
PS2 kernel's ELF loader puts it in RAM at boot, before the game's first
program runs, and it stays there: level programs replace everything from
0x15F000 up, never this region.

Symbols:
  func_XXXXXXXX / D_XXXXXXXX     the executable's functions and data; the
                                 name is the address, the same in every level
  lvl_func_LNN_XXXXXXXX          a level-only function: the tool emits a
                                 19-entry address table from OpenRAC's
                                 overlay catalogue; src/modseg.h's LVL()
                                 indexes it by the running level, which a
                                 hook learns from its call site (src/level.c),
                                 never from the game's current-level variable
Outputs in build/modseg/: modseg.elf, modseg.bin (the segment image),
modseg.sym (exported function -> address, for tools/overlay_patch.py).

  python tools/modseg.py [--openrac DIR] [--base 0xA0000]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
GAME_SYM = re.compile(r"^(func|D)_([0-9A-F]{8})$")
LEVEL_SYM = re.compile(r"^lvl_(func_(?:L\d\d_)?[0-9A-F]{8})$")
LEVEL_DATA_SYM = re.compile(r"^lvl_data_([A-Za-z0-9_]+)$")

# Level-local data has a different address in every level, like level code.
# Each entry names a shared function that addresses it and the offsets in
# that function of the %hi (lui) and %lo instructions; the build reads the
# pair from each level's copy on the disc and emits a 19-entry table.
LEVEL_DATA = {
    # the gameplay update gate: *(struct+8) bits 1 hero, 2 mobys+tick, 4 particles, 8 camera, 0x10 step
    "update_flags": ("func_L00_00299250", 0x4, 0x69C),
    # the HUD slot table (13 records of 0x90), from remove_hud_item's lui/addiu
    "hud_slots": ("func_001FFCB0", 0x4, 0xC),
    # the camera yaw the stick is relative to (camera record + 0x158), from stick_target's lui/lwc1
    "camera_yaw": ("func_L00_00211F80", 0x280, 0x294),
}
# Only the resident core is at one address: below 0x15F000 (core text, data,
# bss, lit). Everything above, the executable's own game text and data
# included, is re-linked into each level program at level-specific
# addresses, so a func_XXXXXXXX or D_XXXXXXXX above that is a different
# address in every level and must go through a per-level table.
CORE_END = 0x15F000
DEFAULT_BASE = 0x000A0000


def run(cmd, cwd=None):
    r = subprocess.run([str(c) for c in cmd], cwd=cwd, capture_output=True, text=True)
    if r.returncode:
        print(" ".join(str(c) for c in cmd), file=sys.stderr)
        print(r.stdout + r.stderr, file=sys.stderr)
        raise SystemExit(f"command failed ({r.returncode})")
    return r.stdout


def catalogue(openrac: Path) -> dict[str, dict[int, int]]:
    places = {}
    for line in (openrac / "games/rac1/config/overlays/functions.tsv").read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        name, _k, _s, _f, _l, pl = line.split("\t")[:6]
        places[name] = {int(p.split(":")[0]): int(p.split(":")[1], 16) for p in pl.split(",")}
    return places


def level_data_addresses(openrac: Path, places: dict, spec: tuple) -> list[int]:
    """The address a shared function's %hi/%lo pair resolves to, per level, read from the disc."""
    import struct
    sys.path.insert(0, str(openrac / "editor"))
    from disc import Disc, SECTOR
    from formats import unpack, span, overlay_sections
    from level import SECTION_NAMES
    func, hi_off, lo_off = spec
    iso = openrac / "games/rac1/baserom/SCES_509.16.iso"
    out = [None] * 19
    with Disc(iso) as disc, iso.open("rb") as f:
        for info in disc.survey()["levels"]:
            L = info["id"]; r = info["ranges"]["data"]
            data = disc.sectors(r["lba"], r["sectors"])
            off, size = unpack("<ii", data, SECTION_NAMES.index("overlay") * 8)
            text = overlay_sections(span(data, off, size))["sections"][-1]
            base = r["lba"] * SECTOR + off + text["offset"]
            fn = places[func][L]
            f.seek(base + fn + hi_off - text["address"]); lui, = struct.unpack("<I", f.read(4))
            f.seek(base + fn + lo_off - text["address"]); lo, = struct.unpack("<I", f.read(4))
            if lui >> 26 != 0x0F:
                raise SystemExit(f"{func}+0x{hi_off:X} in level {L} is not a lui")
            lo16 = lo & 0xFFFF
            out[L] = ((lui & 0xFFFF) << 16) + (lo16 - 0x10000 if lo16 & 0x8000 else lo16)
    return out


def undefined_symbols(obj: Path) -> set[str]:
    from elftools.elf.elffile import ELFFile
    with obj.open("rb") as f:
        elf = ELFFile(f)
        symtab = elf.get_section_by_name(".symtab")
        return {s.name for s in symtab.iter_symbols() if s["st_shndx"] == "SHN_UNDEF" and s.name}


def exported(elf_path: Path) -> dict[str, int]:
    from elftools.elf.elffile import ELFFile
    out = {}
    with elf_path.open("rb") as f:
        elf = ELFFile(f)
        for s in elf.get_section_by_name(".symtab").iter_symbols():
            if (s["st_info"]["type"] in ("STT_FUNC", "STT_OBJECT") and s["st_info"]["bind"] == "STB_GLOBAL"
                    and not GAME_SYM.match(s.name) and not LEVEL_SYM.match(s.name) and not LEVEL_DATA_SYM.match(s.name)):
                out[s.name] = s["st_value"]
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parent / "OpenRAC")
    ap.add_argument("--base", type=lambda s: int(s, 16), default=DEFAULT_BASE)
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/modseg")
    args = ap.parse_args()

    pal = args.openrac / "games/rac1"
    sn114 = pal / "toolchain/sn-prodg-24/local/sce/ee/gcc/bin"
    tc301 = pal / "toolchain/sn-prodg-3.01/usr/local/sce/ee/gcc/bin"
    cc, ld, objcopy = sn114 / "ee-gcc2953.exe", tc301 / "ee-ld.exe", tc301 / "ee-objcopy.exe"
    for t in (cc, ld, objcopy):
        if not t.is_file():
            raise SystemExit(f"missing {t} (toolchains/README.md in OpenRAC)")
    sources = sorted((ROOT / "src").glob("*.c")) + sorted((ROOT / "src").glob("*.s"))
    if not sources:
        raise SystemExit("no sources in src/")
    out = args.out
    out.mkdir(parents=True, exist_ok=True)

    cflags = ["-O2", "-G0", "-fno-builtin", f"-I{ROOT / 'src'}", f"-I{pal / 'include'}"]
    objects = []
    sources += sorted(p for p in out.glob("gen_*.c") if p.name != "gen_levels.c")   # tools/gen_menu.py's output
    for src in sources:
        obj = out / (src.stem + ".o")
        run([cc, *cflags, "-c", src, "-o", obj], cwd=pal)
        objects.append(obj)

    # Level-only functions the code asks for, as 19-entry tables.
    undefined = set().union(*(undefined_symbols(o) for o in objects))
    level_syms = sorted(s for s in undefined if LEVEL_SYM.match(s) or LEVEL_DATA_SYM.match(s))
    if level_syms:
        places = catalogue(args.openrac)
        lines = ["/* generated by tools/modseg.py from OpenRAC's overlay catalogue and the disc */"]
        for s in level_syms:
            md = LEVEL_DATA_SYM.match(s)
            if md:
                if md.group(1) not in LEVEL_DATA:
                    raise SystemExit(f"{s}: no LEVEL_DATA entry for {md.group(1)}")
                addrs = level_data_addresses(args.openrac, places, LEVEL_DATA[md.group(1)])
                lines.append(f"const unsigned int {s}[19] = {{ {', '.join(f'0x{a:08X}' for a in addrs)} }};")
                continue
            name = LEVEL_SYM.match(s).group(1)
            if name not in places:
                raise SystemExit(f"{name} is not in the overlay catalogue")
            addrs = [places[name].get(l) for l in range(19)]
            if any(a is None for a in addrs):
                missing = [l for l, a in enumerate(addrs) if a is None]
                raise SystemExit(f"{name} is not in every level (missing {missing}); LVL() needs a shared function")
            lines.append(f"const unsigned int {s}[19] = {{ {', '.join(f'0x{a:08X}' for a in addrs)} }};")
        gen = out / "gen_levels.c"
        gen.write_text("\n".join(lines) + "\n")
        gen_obj = out / "gen_levels.o"
        run([cc, *cflags, "-c", gen, "-o", gen_obj], cwd=pal)
        objects.append(gen_obj)
        undefined -= set(level_syms)

    provides = []
    for s in sorted(undefined):
        m = GAME_SYM.match(s)
        if m:
            addr = int(m.group(2), 16)
            if addr >= CORE_END:
                raise SystemExit(f"{s} is not resident (above 0x{CORE_END:X}): a function goes through "
                                 f"DECLARE_LVL(func_...)/LVL(), data through a LEVEL_DATA table")
            provides.append(f"  PROVIDE({s} = 0x{addr:08X});")
    script = out / "modseg.ld"
    script.write_text(f"""OUTPUT_FORMAT("elf32-littlemips")
OUTPUT_ARCH(mips)
ENTRY(modseg_start)
_gp = 0x00166D00;
{chr(10).join(provides)}
SECTIONS
{{
  . = 0x{args.base:08X};
  .text   : {{ *(.text) }}
  .rodata : {{ *(.rodata) *(.rdata) *(.lit8) *(.lit4) }}
  .data   : {{ *(.data) *(.sdata) }}
  .bss    : {{ *(.bss) *(.sbss) *(.scommon) *(COMMON) }}
  modseg_end = .;
}}
""")
    elf = out / "modseg.elf"
    run([ld, "-T", script, "-o", elf, *objects], cwd=pal)
    binary = out / "modseg.bin"
    run([objcopy, "-O", "binary", elf, binary], cwd=pal)
    # objcopy -O binary leaves .bss out; pad so the loaded image zeroes it.
    from elftools.elf.elffile import ELFFile
    with elf.open("rb") as f:
        e = ELFFile(f)
        end = max(s["sh_addr"] + s["sh_size"] for s in e.iter_sections() if s["sh_flags"] & 2)
        still_undefined = [s.name for s in e.get_section_by_name(".symtab").iter_symbols()
                           if s["st_shndx"] == "SHN_UNDEF" and s.name and s.name not in ("_gp",)]
    if still_undefined:
        raise SystemExit(f"unresolved symbols: {', '.join(still_undefined)}")
    data = bytearray(binary.read_bytes())
    data += bytes(end - args.base - len(data)) if end - args.base > len(data) else b""
    data += bytes((-len(data)) % 16)
    binary.write_bytes(data)
    syms = exported(elf)
    (out / "modseg.sym").write_text("".join(f"{n}\t0x{a:08X}\n" for n, a in sorted(syms.items(), key=lambda x: x[1])))
    print(f"mod segment: 0x{args.base:08X}..0x{args.base + len(data):08X} ({len(data)} bytes), "
          f"{len(syms)} exported: {', '.join(sorted(syms))}")
    print(f"wrote {binary} and {out / 'modseg.sym'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
