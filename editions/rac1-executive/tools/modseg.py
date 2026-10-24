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
  lvl_func_[LNN_]XXXXXXXX        a function every program has its own copy
                                 of: the tool emits a 21-entry address table,
                                 the 19 levels' from OpenRAC's overlay
                                 catalogue, then the title screen's two
                                 programs' (the boot executable, frontbin;
                                 tools/gen_frontend.py), 0 where a program
                                 has none; src/modseg.h's LVL() indexes it by
                                 the running program, which a hook learns
                                 from its call site (src/level.c), never from
                                 the game's current-level variable
  lvl_data_NAME                  the same for data, from LEVEL_DATA
Outputs in build/modseg/: modseg.elf, modseg.bin (the segment image),
modseg.sym (exported function -> address, for tools/overlay_patch.py).

  python tools/modseg.py [--openrac DIR] [--base 0x9E000]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
EDITOR = HERE.parents[2] / "common/openrac_editor"   # OpenRAC's disc readers, carried (common/deps.py)
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
    # the hero's per-joint ease table (lean and, in the edition, the strafe twist), from a rate setter's lui/addiu
    "ease_table": ("func_L00_0020A858", 0x0, 0x4),
    # the pause menu state (NTSC menu_system; D_001D5F70 in the executable), from PageMenuClose's lui/addiu
    "menu_state": ("func_L00_002777C0", 0x4, 0x14),
    # moby class id -> resident class slot (bytes; D_001B3E40), from create_menu_preview_moby's lui/addiu
    "class_slot_by_id": ("func_00226720", 0x4, 0xC),
    # resident class slot -> class data (D_001B3580), from the class loader's lui/addiu
    "class_resources": ("func_00205270", 0x70, 0x13C),
    # the HUD arena (D_0019A4E8 in the executable: header pointer at +0x18, table pointers at +0x1C..+0x28),
    # from load_hud_banks' lui/addiu
    "hud_arena": ("func_002032D0", 0x84, 0x8C),
    # the quick-select ring's 8 slot records of 0x1C (D_L00_0017DBC0 in level 0), from QuickSelectInit's lui/addiu
    "qs_slots": ("func_L00_00238478", 0x54, 0x70),
    # the item table, 37 records of 0x4C (D_L00_00179BC0 in level 0), from the same function's lui/addiu
    "item_table": ("func_L00_00238478", 0x5C, 0x6C),
    # q4: the view record (D_0018CE00: fov +0xB0, screen half sizes +0x200), from UpdateViewContext's lui/addiu
    "view": ("func_001F3140", 0x50, 0x60),
    # q4: the VIF1 chain's write cursor (D_00161000, a pointer), from the letterbox bars' lui/lw
    "chain_cursor": ("func_001F5148", 0x68, 0x6C),
    # q4: the letterbox request (D_0015F544) and its 0..24 line counter (D_0015F548), same function
    "letterbox_on": ("func_001F5148", 0x0, 0x4),
    "letterbox_lines": ("func_001F5148", 0x24, 0x28),
    # q4: the game mode (D_0015F6E8: 0 play, 2 cinematic, 3 pause...), from DrawWorld's subtitle test
    "game_mode": ("func_001F3D78", 0x4D8, 0x4DC),
    # the loaded text table (D_0015F780: entries {text, id, voice, 0}) and the help state (D_001997D0: + 0x2C
    # the table's entry count), from Help_FindIndex's lui/lw and lui/addiu (src/control_prompts.c)
    "text_table": ("func_001FE4D0", 0x1C, 0x20),
    "help_state": ("func_001FE4D0", 0x0, 0xC),
    # rz3: the vendor's record (D_L00_001CA7C0 in level 0: the item just bought at +0x48, -1 none, which
    # decides the weapon demo on leaving), from VendorBuyDialogUpdate's lui/addiu
    "vendor_state": ("func_L00_0029CDE0", 0x4, 0x14),
    # rz3: the level's item info table (D_L00_001C43B0 in level 0: 37 records of 0x18, the ammo price at +8) and
    # its item sound table (D_L00_001C2488: a word per item, stride 0x14), from VendorBuildItemList's lui/addiu pairs
    "vendor_items": ("func_L00_0029B770", 0x78, 0x80),
    # Vendor_InitItemDisplay's per-item offsets of the panel's model (D_001C8C20, 0x30 an item)
    "vendor_pose": ("func_L00_0029C9E8", 0xA0, 0xAC),
    "vendor_sounds": ("func_L00_0029B770", 0x74, 0x7C),
    # rz2: the save layout tables (D_001A05C0 the game's records, D_001A08C0 a level page's: {address, size, id, 0}
    # to a zero address), from memcard_RestoreGame's lui/addiu pairs; the Randomizer's record offset in a file
    "save_game_table": ("func_00209CE8", 0x4, 0x10),
    "save_level_table": ("func_00209CE8", 0x34, 0x3C),
}
# Where the title screen's programs have the data, for LEVEL_DATA entries read from a level's
# own function: the executable function that addresses it there (its address is the same in both).
LEVEL_DATA_FRONTEND = {
    "menu_state": ("func_0021DB30", 0x54, 0x58),     # Pause_SoundOptionsInput's lui/addiu of D_001D5F70
}
PROGRAMS = 21                     # 19 levels, the boot executable, frontbin (FE_BOOT, FE_BIN in src/modseg.h)
# Only the resident core is at one address: below 0x15F000 (core text, data,
# bss, lit). Everything above, the executable's own game text and data
# included, is re-linked into each level program at level-specific
# addresses, so a func_XXXXXXXX or D_XXXXXXXX above that is a different
# address in every level and must go through a per-level table.
CORE_END = 0x15F000
DEFAULT_BASE = 0x0009E000
# The highest end a booting build has had. A segment ending at 0xF2580 (base 0xA0000) left the title screen
# deaf to the pad in PCSX2, while the same code ending at 0xF0580 (base 0x9E000) and a build ending at 0xF2380
# worked; the memory there reads zero at boot and on the title screen, so what uses it is unknown. The base moved
# down 8 KB to 0x9E000 (2026-10-25), which the owner's PS3 had already run, and a build past this end stops.
SEGMENT_END_LIMIT = 0x000F2380


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
    sys.path.insert(0, str(EDITOR))
    from disc import Disc, SECTOR
    from formats import unpack, span, overlay_sections
    from level import SECTION_NAMES
    func, hi_off, lo_off = spec
    iso = openrac / "games/rac1/baserom/SCES_509.16.iso"
    out = [None] * 19
    with Disc(iso) as disc, iso.open("rb") as f:
        for info in disc.survey()["levels"]:
            L = info["id"]
            r = info["ranges"]["data"]
            data = disc.sectors(r["lba"], r["sectors"])
            off, size = unpack("<ii", data, SECTION_NAMES.index("overlay") * 8)
            text = overlay_sections(span(data, off, size))["sections"][-1]
            base = r["lba"] * SECTOR + off + text["offset"]
            fn = places[func][L]
            f.seek(base + fn + hi_off - text["address"])
            lui, = struct.unpack("<I", f.read(4))
            f.seek(base + fn + lo_off - text["address"])
            lo, = struct.unpack("<I", f.read(4))
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
                    and not GAME_SYM.match(s.name) and not LEVEL_SYM.match(s.name)
                    and not LEVEL_DATA_SYM.match(s.name)):
                out[s.name] = s["st_value"]
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
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

    # The per-program functions and data the code asks for, as 21-entry tables.
    undefined = set().union(*(undefined_symbols(o) for o in objects))
    level_syms = sorted(s for s in undefined if LEVEL_SYM.match(s) or LEVEL_DATA_SYM.match(s))
    if level_syms:
        sys.path.insert(0, str(HERE))
        import gen_frontend
        places = catalogue(args.openrac)
        names = [LEVEL_SYM.match(s).group(1) for s in level_syms if LEVEL_SYM.match(s)]
        fe = gen_frontend.places(args.openrac, names)
        lines = ["/* generated by tools/modseg.py from OpenRAC's overlay catalogue and the disc:",
                 "   the 19 levels, then the boot executable and frontbin (tools/gen_frontend.py) */"]
        for s in level_syms:
            md = LEVEL_DATA_SYM.match(s)
            if md:
                name = md.group(1)
                if name not in LEVEL_DATA:
                    raise SystemExit(f"{s}: no LEVEL_DATA entry for {name}")
                addrs = level_data_addresses(args.openrac, places, LEVEL_DATA[name])
                addrs += gen_frontend.data_place(args.openrac, LEVEL_DATA_FRONTEND.get(name, LEVEL_DATA[name]))
            else:
                name = LEVEL_SYM.match(s).group(1)
                if name in places:
                    addrs = [places[name].get(level) for level in range(19)]
                    if any(a is None for a in addrs):
                        missing = [level for level, a in enumerate(addrs) if a is None]
                        raise SystemExit(f"{name} is not in every level (missing {missing});"
                                         " LVL() needs a shared function")
                elif not name.startswith("func_L") and any(fe[name]):
                    addrs = [0] * 19                  # only the executable has it: the title screen's
                else:
                    raise SystemExit(f"{name} is not in the overlay catalogue")
                addrs += list(fe[name])
            lines.append(f"const unsigned int {s}[{PROGRAMS}] = {{ {', '.join(f'0x{a:08X}' for a in addrs)} }};")
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
    if args.base + len(data) > SEGMENT_END_LIMIT:
        raise SystemExit(f"the mod segment ends at 0x{args.base + len(data):08X}, past 0x{SEGMENT_END_LIMIT:08X}, "
                         "the highest end known to boot (SEGMENT_END_LIMIT)")
    binary.write_bytes(data)
    syms = exported(elf)
    (out / "modseg.sym").write_text("".join(f"{n}\t0x{a:08X}\n" for n, a in sorted(syms.items(), key=lambda x: x[1])))
    print(f"mod segment: 0x{args.base:08X}..0x{args.base + len(data):08X} ({len(data)} bytes), "
          f"{len(syms)} exported: {', '.join(sorted(syms))}")
    print(f"wrote {binary} and {out / 'modseg.sym'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
