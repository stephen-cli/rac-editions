#!/usr/bin/env python3
"""
Build the Executive Edition's release payload (tools/payload.py) on the
development machine: the OpenRAC decompilation built and audited, the
edition's generators run against OpenRAC's dumps of the discs, the mod
segment compiled with the game's compiler, the level-code patches computed;
then what ships is written to build/payload with every byte taken from a
disc cut out. tools/install.py makes the image from the payload and the
player's discs.

  python tools/release.py [--openrac DIR] [--wrench DIR] [--skip-build] [-o build/payload]

--skip-build packages the outputs of the last build as they are.

Before it writes the payload, the tool proves the holes: each one's bytes are
made from the discs the way the installer makes them and must equal what
the build linked there.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
COMMON = HERE.parents[2] / "common"
sys.path.insert(0, str(COMMON))
sys.path.insert(0, str(HERE))

import iso_image  # noqa: E402
import payload  # noqa: E402

RAC1_ISO = "games/rac1/baserom/SCES_509.16.iso"
RAC2_ISO = "games/rac2/ntsc/baserom/SCUS_972.68.iso"
EXE_NAME = "SCES_509.16;1"


def run(*cmd, cwd=None, env=None):
    print("==", " ".join(str(c) for c in cmd[1:3]) if cmd[0] == sys.executable else " ".join(map(str, cmd)))
    subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, check=True)


def windows_build_env(openrac: Path) -> dict:
    """Native Windows (Git Bash): OpenRAC's Makefile.sn needs a GNU make newer than the toolchain's 3.77
    ($(if)), and two objects are assembled by Linux assemblers (OpenRAC tools/gas210.py, tools/dtls29_as.py),
    run through WSL with common/wsl_linux_run.sh. Each is set only when unset and present."""
    env = dict(os.environ)
    tcs = openrac / "toolchains"
    make = tcs / "mingw64-dl/mingw64/bin/mingw32-make.exe"
    if "MAKE_SN" not in env and make.is_file():
        env["MAKE_SN"] = str(make)
    if shutil.which("wsl"):
        def wsl_path(p: Path) -> str:
            out = subprocess.run(["wsl", "-d", "Ubuntu", "-e", "wslpath", "-a", str(p)],
                                 capture_output=True, text=True, check=True)
            return out.stdout.strip()
        w = wsl_path(tcs)
        runner = f"wsl -d Ubuntu -e sh {wsl_path(COMMON / 'wsl_linux_run.sh')}"
        env.setdefault("EE_GAS210", f"{runner} {w}/sn-prodg-24/local/sce/ee/gcc/bin/ee-as")
        if "EE_DTLS29_AS" not in env and (tcs / "ee-as-dtls13010/as.bin").is_file():
            env["EE_DTLS29_AS"] = (f"{runner} {w}/ee-as-dtls13010/lib/ld-2.3.6.so --library-path "
                                   f"{w}/ee-as-dtls13010/lib {w}/ee-as-dtls13010/as.bin")
    env["MSYS_NO_PATHCONV"] = "1"
    return env


def build(openrac: Path, wrench: Path) -> None:
    """The decompilation, the generated tables, the mod segment, the boot executable and the level-code
    patches, as the developer build makes them."""
    pal = openrac / "games/rac1"
    py = sys.executable
    run(py, COMMON / "deps.py", "check", "--openrac", openrac, "--wrench", wrench)
    env = windows_build_env(openrac) if os.name == "nt" else None
    # By PATH: Windows would otherwise start System32's bash.exe, which is WSL's, before Git Bash's.
    run(shutil.which("bash"), "tools/build_sn.sh", cwd=pal, env=env)
    modseg = ROOT / "build/modseg"
    o = ["--openrac", openrac]
    run(py, HERE / "gen_text.py")
    run(py, COMMON / "rac2_anim.py", *o, "emit", "0x14", "-o", modseg / "gen_anim.c")
    run(py, COMMON / "rac2_anim.py", *o, "emit-arm", "0x14", "-o", modseg / "gen_anim_arm.c")
    run(py, HERE / "gen_menu.py", *o)
    run(py, HERE / "gen_hero.py", *o)
    run(py, HERE / "gen_items.py", *o, "--boots-class", "2000")
    run(py, HERE / "gen_charge.py", *o)
    run(py, HERE / "gen_qs.py", *o)
    run(py, HERE / "gen_enemy_ids.py", *o)
    run(py, COMMON / "rac2_anim.py", *o, "emit", "0x8B", "--name", "ee_charge_seq", "-o", modseg / "gen_anim_charge.c")
    run(py, HERE / "gen_hud.py", *o)
    run(py, HERE / "gen_glyphs.py", *o)
    run(py, HERE / "gen_randomizer.py", *o)
    run(py, HERE / "gen_frontend.py", *o, "emit")
    run(py, HERE / "gen_helpdesk.py", *o)
    run(py, HERE / "modseg.py", *o)
    base = (modseg / "modseg.base").read_text().strip()
    run(py, "tools/boot_elf.py", "--segment", f"{base}:{modseg / 'modseg.bin'}", cwd=pal)
    run(py, HERE / "gen_frontend.py", *o, "patch-elf", pal / "build-sn/SCES_509.16")
    run(py, HERE / "overlay_patch.py", "-o", ROOT / "build/overlay.tsv")


def symbols(elf_path: Path) -> dict[str, tuple[int, int]]:
    """name -> (address, size) of the mod segment's symbols."""
    with elf_path.open("rb") as f:
        tab = ELFFile(f).get_section_by_name(".symtab")
        return {s.name: (s["st_value"], s["st_size"]) for s in tab.iter_symbols() if s.name}


def holes(modseg: bytearray, base: int, syms: dict, discs: dict, recipe: list) -> list[dict]:
    """The holes as payload records; MODSEG gets them zeroed. Each hole's bytes are made from the discs
    and must equal the build's."""
    filler = payload.Filler(discs)
    out = []
    for symbol, disc, fill in payload.HOLES:
        fill = dict(fill, recipe=recipe) if fill["kind"] == "words" else fill
        addr, size = syms[symbol]
        blob = filler.fill(symbol, fill)
        if fill.get("part") == "head":
            size = payload.HEAD
        offset = addr - base
        if len(blob) != size or modseg[offset:offset + size] != blob:
            raise SystemExit(f"{symbol}: the bytes made from the {disc} disc are not the build's")
        modseg[offset:offset + size] = bytes(size)
        out.append({"symbol": symbol, "offset": offset, "size": size, "disc": disc, "fill": fill})
        print(f"hole {symbol}: 0x{offset:X}, {size} bytes from the {disc} disc")
    return out


def exe_words(retail_exe: bytes, base: int, modseg: bytes, final: bytes) -> list[tuple[int, int]]:
    """The words where FINAL, the boot executable the build made, differs from the player's executable with
    the mod segment added."""
    plain = iso_image.add_segment(retail_exe, base, modseg)
    if len(plain) != len(final):
        raise SystemExit(f"the boot executable is {len(final)} bytes, the player's with the segment {len(plain)}")
    out = []
    for off in range(0, len(final), 4):
        if final[off:off + 4] != plain[off:off + 4]:
            out.append((off, struct.unpack_from("<I", final, off)[0]))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("--wrench", type=Path, default=ROOT.parents[2] / "wrench")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/payload")
    args = ap.parse_args()
    if not args.skip_build:
        build(args.openrac, args.wrench)

    discs = {"rac1": args.openrac / RAC1_ISO, "rac2": args.openrac / RAC2_ISO}
    modseg_dir = ROOT / "build/modseg"
    base = int((modseg_dir / "modseg.base").read_text().strip(), 16)
    real = (modseg_dir / "modseg.bin").read_bytes()
    seg = bytearray(real)
    recipe = json.loads((modseg_dir / "gen_rz_words.json").read_text())
    hole_list = holes(seg, base, symbols(modseg_dir / "modseg.elf"), discs, recipe)

    with discs["rac1"].open("rb") as f:
        retail_exe = iso_image.read_file(f, EXE_NAME)
    final = (args.openrac / "games/rac1/build-sn/SCES_509.16").read_bytes()
    exe = exe_words(retail_exe, base, real, final)

    out = args.out
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    commit = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True).stdout
    edition = {
        "edition": "rac1-executive",
        "commit": commit.strip(),
        "discs": {name: iso_image.sha256(path) for name, path in discs.items()},
        "modseg_base": base,
    }
    (out / "edition.json").write_text(json.dumps(edition, indent=2) + "\n")
    (out / "modseg.bin").write_bytes(bytes(seg))
    (out / "holes.json").write_text(json.dumps(hole_list, indent=1) + "\n")
    (out / "exe.tsv").write_text("".join(f"{off:X}\t{word:08X}\n" for off, word in exe))
    shutil.copyfile(ROOT / "build/overlay.tsv", out / "overlay.tsv")
    shutil.copyfile(ROOT / "audio/helpdesk.json", out / "helpdesk.json")
    print(f"wrote {out}: {len(hole_list)} holes, {len(exe)} executable words")
    return 0


if __name__ == "__main__":
    sys.exit(main())
