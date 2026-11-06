#!/usr/bin/env python3
"""
Make the Executive Edition's image from a release payload (tools/payload.py,
written by tools/release.py) and the player's own discs: Ratchet & Clank
(PAL), and Going Commando (USA) for the parts built from it. This is the
installer's reference implementation: the C installer core makes the same
image byte for byte.

  python tools/install.py [--rac1 RAC1.iso] [--rac2 RAC2.iso | --no-rac2] [-o OUT.iso]
                          [--economy skill|classic] [--without SWITCH ...]
                          [--payload build/payload] [--wrench DIR] [--unpacked DIR]

The discs default to the OpenRAC checkout's baseroms beside rac-editions,
the image to build/iso/executive-edition.iso; when an emulator has that
open, the image is numbered instead (executive-edition-2.iso, and so on).
--economy is Challenge Mode's bolts: skill (the default: x1 base and the
no-hit bolt multiplier) or classic (retail's doubled drops, without the
bolt_multiplier feature's words). --without leaves out one of the
features a player may switch off (payload.SWITCHES: charge_boots,
strafing, quick_select_pages); without Going Commando (--no-rac2) the
Charge Boots and strafing are left out.

In order: the discs are checked by hash; the image is a copy of the PAL disc;
the mod segment gets its holes filled from the discs and is added to the
player's boot executable as a second loadable segment, with the
executable's own changed words, and the executable goes in new sectors at
the end of the image; the level programs' changed words are written in
place; then the levels' data archives get the classes the edition adds
(the Charge Boots, Kalebo III's sign, the infobot and the one-planet items)
and the seed glyphs in their fonts, and the edition's HelpDesk lines are
added at the end.

A feature left out loses its words, its holes stay zero, and its HelpDesk
lines and its class are not added; the mod segment's ee_features word
(src/features.c) says which features are in.

Until the edition's own converters and compressor replace them, the class
splices still use wrench (--wrench: the edition's fork, built) and wrench's
unpacks of the two discs (--unpacked: OpenRAC's assets/wrench, with
rac1-pal and rac2-ntsc in it).
"""
import argparse
import json
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
COMMON = HERE.parents[2] / "common"
sys.path.insert(0, str(COMMON))
sys.path.insert(0, str(HERE))

import iso_image  # noqa: E402
import payload  # noqa: E402

EXE_NAME = "SCES_509.16;1"


def free_name(path: Path) -> Path:
    """PATH, or a numbered name beside it while an emulator has PATH open (it cannot be overwritten)."""
    n = 1
    out = path
    while out.exists():
        try:
            out.open("ab").close()
            return out
        except OSError:
            n += 1
            out = path.with_name(f"{path.stem}-{n}{path.suffix}")
    return out


def run(*cmd):
    print("==", " ".join(str(c) for c in cmd[1:2]))
    subprocess.run([str(c) for c in cmd], check=True)


def check_discs(edition: dict, discs: dict) -> None:
    for name, path in discs.items():
        want = edition["discs"][name]
        if iso_image.sha256(path) != want:
            raise SystemExit(f"{path}: not the disc this edition is built from ({name}, SHA-256 {want})")


def switches_in(discs: dict, without: list[str]) -> list[str]:
    """The switchable features this image has: all but those left out and those whose disc is missing."""
    return [name for name, s in payload.SWITCHES.items() if name not in without and s["disc"] in discs]


def mod_segment(pay: Path, discs: dict, left_out: set[str]) -> bytes:
    """The mod segment with its holes filled, but for LEFT_OUT's, which stay zero."""
    seg = bytearray((pay / "modseg.bin").read_bytes())
    filler = payload.Filler(discs)
    for hole in json.loads((pay / "holes.json").read_text()):
        if hole["symbol"] in left_out:
            continue
        blob = filler.fill(hole["symbol"], hole["fill"])
        if len(blob) != hole["size"]:
            raise SystemExit(f"{hole['symbol']}: {len(blob)} bytes made, the hole holds {hole['size']}")
        seg[hole["offset"]:hole["offset"] + hole["size"]] = blob
    return bytes(seg)


def tsv(path: Path) -> list[list[str]]:
    return [line.split("\t") for line in path.read_text().splitlines() if line and not line.startswith("#")]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    openrac = ROOT.parents[2] / "OpenRAC"
    ap.add_argument("--rac1", type=Path, default=openrac / "games/rac1/baserom/SCES_509.16.iso",
                    help="Ratchet & Clank (PAL)")
    ap.add_argument("--rac2", type=Path, default=openrac / "games/rac2/ntsc/baserom/SCUS_972.68.iso",
                    help="Ratchet & Clank: Going Commando (USA)")
    ap.add_argument("--no-rac2", action="store_true", help="build without Going Commando")
    ap.add_argument("--economy", choices=payload.ECONOMIES, default="skill")
    ap.add_argument("--without", action="append", default=[], choices=payload.SWITCHES)
    ap.add_argument("--payload", type=Path, default=ROOT / "build/payload")
    ap.add_argument("--wrench", type=Path, default=ROOT.parents[2] / "wrench")
    ap.add_argument("--unpacked", type=Path, default=openrac / "assets/wrench")
    ap.add_argument("-o", "--output", type=Path, default=None)
    args = ap.parse_args()
    args.output = args.output or free_name(ROOT / "build/iso/executive-edition.iso")
    pay = args.payload
    edition = json.loads((pay / "edition.json").read_text())
    discs = {"rac1": args.rac1} if args.no_rac2 else {"rac1": args.rac1, "rac2": args.rac2}
    check_discs(edition, discs)
    if args.output.resolve() == args.rac1.resolve():
        raise SystemExit("refusing to overwrite the disc image")

    on = switches_in(discs, args.without)
    off = [name for name in payload.SWITCHES if name not in on]
    print(f"features left out: {', '.join(off) or 'none'}")
    left_out_holes = {h for name in off for h in payload.SWITCHES[name]["holes"]}
    left_out_words = set(payload.ECONOMIES[args.economy])
    left_out_words |= {f for name in off for f in payload.SWITCHES[name]["features"]}
    left_out_lines = {u for name in off for u in payload.SWITCHES[name]["helpdesk"]}

    seg = bytearray(mod_segment(pay, discs, left_out_holes))
    struct.pack_into("<I", seg, edition["features_word"], sum(payload.SWITCHES[name]["bit"] for name in on))
    switch_data = json.loads((pay / "switches.json").read_text())
    for name in off:
        for at, blob in switch_data.get(name, []):
            seg[at:at + len(blob) // 2] = bytes.fromhex(blob)
    with args.rac1.open("rb") as f:
        exe = bytearray(iso_image.add_segment(iso_image.read_file(f, EXE_NAME), edition["modseg_base"], bytes(seg)))
    for off_text, word in tsv(pay / "exe.tsv"):
        struct.pack_into("<I", exe, int(off_text, 16), int(word, 16))

    print(f"copying {args.rac1} -> {args.output}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.rac1, args.output)
    image = iso_image.Image(args.output)
    image.replace_file(EXE_NAME, bytes(exe))
    words = [w for w in tsv(pay / "overlay.tsv") if w[0] not in left_out_words]
    for _feature, off_text, word in words:
        image.write_word(int(off_text, 16), int(word, 16))
    image.finish()
    print(f"the boot executable with the mod segment ({len(exe)} bytes); {len(words)} level-code words")

    py = sys.executable
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        unpacked = args.unpacked
        sign = tmp / "gadgetron_sign.bin"
        openrac = unpacked.parents[1]           # gen_boots and gen_sign find the unpacks there
        wrenchbuild = args.wrench / "bin/wrenchbuild.exe"
        if "charge_boots" in on:
            boots = tmp / "charge_boots.bin"
            run(py, HERE / "gen_boots.py", "--openrac", openrac, "--wrench", args.wrench, "-o", boots)
            run(py, HERE / "splice_boots.py", args.output, "--class-core", boots,
                "--texture", unpacked / "rac2-ntsc/moby_classes/unsorted/3696/0.png", "--wrench", wrenchbuild)
        else:
            run(py, HERE / "splice_boots.py", args.output, "--fonts-only", "--wrench", wrenchbuild)
        run(py, HERE / "gen_sign.py", "--openrac", openrac, "--wrench", args.wrench,
            "--bank", unpacked / "rac1-pal", "-o", sign)
        run(py, HERE / "splice_sign.py", args.output, "--class-core", sign, "--retail", args.rac1,
            "--wrench", wrenchbuild)
        run(py, HERE / "extra_classes.py", "splice", args.output, "--wrench", wrenchbuild)
        helpdesk = tmp / "helpdesk"
        run(py, HERE / "gen_helpdesk.py", "--iso", args.rac1, "--recipes", pay / "helpdesk.json", "-o", helpdesk,
            "--c-out", tmp / "gen_helpdesk.c")
        for line in json.loads((pay / "helpdesk.json").read_text())["lines"]:
            if line["use"] in left_out_lines:
                for vag in helpdesk.glob(f"{line['slot']}_*.vag"):
                    vag.unlink()
        run(py, HERE / "splice_helpdesk.py", args.output, "--audio", helpdesk)
    print(f"-> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
