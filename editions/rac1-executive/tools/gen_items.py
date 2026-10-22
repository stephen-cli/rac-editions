#!/usr/bin/env python3
"""
Generate the Charge Boots item, per level, from each level's own data.

The game's item records (0x4C bytes each, 37 of them: text ids, equip slot,
attach bone, moby class, icon, description) and the pause menu's Gadgets
grids are level-local data: every level program carries its own copy at its
own addresses. Record 36 is a duplicate of the Devastator that no grid shows
and no vendor sells, so it becomes the Charge Boots: a copy of the
Grindboots record (slot 1, class 0xC3 on both feet) with its own name,
description and icon. It is the hoverboard races' weapon, though, so the
resident segment puts retail's words back while Ratchet is on the board
(src/charge_boots.c, ee_race_item_frame); both versions are written to
gen_items.c. The boots grid (Grindboots, Magneboots) becomes
a three-cell grid. This reads each level's data record and writes:

  build/modseg/gen_items.c       19 copies of the three-cell boots grid, and
                                 per level record 36's address and its
                                 retail and Charge Boots words
  build/modseg/items_sites.json  per level: the grid widget's cell pointer
                                 and column count, and the 19 words of
                                 record 36 (site, retail value, new value)

  python tools/gen_items.py [--openrac DIR]
"""
import argparse
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
EDITOR = HERE.parents[2] / "common/openrac_editor"   # OpenRAC's disc readers, carried (common/deps.py)
ROOT = HERE.parent

ITEM_GRINDBOOTS, ITEM_MAGNEBOOTS, ITEM_CHARGE_BOOTS = 29, 28, 36
ICON_BASE = 0xEA60                    # every item's icon id is ICON_BASE + item id
ICON_CHARGE_BOOTS = ICON_BASE + ITEM_CHARGE_BOOTS    # 0xEA84, added to the HUD header by tools/gen_hud.py
TEXT_NAME, TEXT_DESC, TEXT_SURFACE = 0x5406, 0x5407, 0x5408   # text/strings.tsv
RECORD_WORDS = 0x4C // 4
RECORD_COUNT = 37
PANEL_BOOTS, PANEL_HEADS = 0x26, 0x25     # menu panel class 0x472 sequences: the boots box, the Head Items box
PANEL_CLASS = 0x472
PANEL_SLOT = 219                          # the edition's sequence: the Head Items box moved down (src/charge_boots.c installs it)
PLACEMENT_WORDS = 8                   # ItemPreviewPlacement: 8 floats per item
PLACEMENT_COUNT = 36                  # retail's table; item 36 would read the words after it
PLACEMENT_SIGNATURE = struct.pack("<8f", 2, 3, 0, 0.2, 0, 0.5, 0, 0) * 2   # entries 28 and 29, the boots


def level_record(pal: Path, level: int):
    m = json.loads((pal / f"baserom/overlays/level_{level:02d}/manifest.json").read_text())
    rec = next(r for r in m["records"] if r["name"] == "data")
    data = (pal / f"baserom/overlays/level_{level:02d}/data.bin").read_bytes()
    return rec["address"], data


def find_items(pal: Path, level: int, boots_class: int | None = None) -> dict:
    base, d = level_record(pal, level)
    u32 = lambda a: struct.unpack_from("<I", d, a - base)[0]
    table = None
    for off in range(0, len(d) - 0x4C * RECORD_COUNT, 4):
        if all(struct.unpack_from("<H", d, off + 0x4C * i + 0x38)[0] == ICON_BASE + i for i in (2, 12, 28, 29)):
            table = base + off
            break
    if table is None:
        raise SystemExit(f"level {level}: item record table not found")
    rec = lambda i: [u32(table + 0x4C * i + 4 * k) for k in range(RECORD_WORDS)]
    grind, spare = rec(ITEM_GRINDBOOTS), rec(ITEM_CHARGE_BOOTS)
    if grind[2] != 1 or grind[4] != 0xC3 or (grind[14] & 0xFFFF) != ICON_BASE + ITEM_GRINDBOOTS:
        raise SystemExit(f"level {level}: the Grindboots record is not as expected")
    if spare[0] != 0x4E4D or spare[16] != 0:
        raise SystemExit(f"level {level}: record 36 is not the unused Devastator copy")
    new = list(grind)
    if boots_class is not None:
        new[4] = new[5] = boots_class                   # the model, on both feet (tools/gen_boots.py's class)
    new[0] = TEXT_NAME                                  # name
    new[14] = (grind[14] & 0xFFFF0000) | ICON_CHARGE_BOOTS   # icon; the preview class stays -1
    new[16] = TEXT_DESC                                 # description on the Gadgets page
    new[17] = TEXT_NAME | (TEXT_SURFACE << 16)          # name, and what it works on ("Grindrail")
    new[18] = TEXT_NAME
    record = [[table + 0x4C * ITEM_CHARGE_BOOTS + 4 * k, spare[k], new[k]] for k in range(RECORD_WORDS)]
    # The boots grid: two cells {icon, first frame, kind 0 = item, id, stream entry}.
    cells = None
    for off in range(0, len(d) - 20, 2):
        if struct.unpack_from("<HhhhH", d, off) == (ICON_BASE + ITEM_GRINDBOOTS, 0, 0, ITEM_GRINDBOOTS, 0) \
                and struct.unpack_from("<HhhhH", d, off + 10) == (ICON_BASE + ITEM_MAGNEBOOTS, 0, 0, ITEM_MAGNEBOOTS, 0):
            cells = base + off
            break
    if cells is None:
        raise SystemExit(f"level {level}: the boots grid cells not found")
    widget = next((base + off - 0x48 for off in range(0, len(d) - 4, 4) if u32(base + off) == cells), None)
    if widget is None or u32(widget + 0x40) != 1 or u32(widget + 0x44) != 2:
        raise SystemExit(f"level {level}: the boots grid widget is not a 1x2 grid")
    # The item preview's placement table (func_0021F238, func_0021F6A0): 36 entries of 8 floats.
    i = d.find(PLACEMENT_SIGNATURE)
    if i < 0:
        raise SystemExit(f"level {level}: the preview placement table not found")
    start = i - 28 * 4 * PLACEMENT_WORDS
    placements = d[start:start + PLACEMENT_COUNT * 4 * PLACEMENT_WORDS]
    # The Gadgets page (kind 4): screens[10] is the boots grid, moby_anims[10] its panel's sequence.
    page = next((a for a in range(widget - 0x1000, widget, 4)
                 if u32(a + 0x44 + 4 * 10) == widget and u32(a + 0x3C) == 4), None)
    if page is None or u32(page + 4 * 10) != PANEL_BOOTS:
        raise SystemExit(f"level {level}: the Gadgets page with the boots panel sequence not found")
    return {"table": table, "record": record, "cells": cells, "widget": widget, "placements": placements,
            "panel": page + 4 * 10}


def panel_sequence(openrac: Path) -> tuple[bytes, list[int], list[bytes]]:
    """The Foot Items box sequence: the Head Items box's (0x25), its root
    translation moved down to the boots box's (0x26) final height, frame by
    frame. Returns the 0x1C-byte sequence header, the frame offsets within
    the frames blob, and the frames (16-byte header, quaternions, scale and
    translation records; OpenRAC's editor/moby_anim.py has the layout)."""
    sys.path.insert(0, str(EDITOR))
    from disc import Disc
    from level import load_level
    with Disc(openrac / "games/rac1/baserom/SCES_509.16.iso") as disc:
        level = load_level(disc, disc.survey()["levels"][0])
    blob = None
    for entry in level.table(0x18, 32):
        start, class_id = struct.unpack("<ii", entry[:8])
        if class_id == PANEL_CLASS:
            blob = level.block(start)
    if blob is None:
        raise SystemExit(f"class {PANEL_CLASS:#x} not in level 0's core")
    count = blob[0xC]
    offsets = struct.unpack_from(f"<{count}i", blob, 0x48)

    def frames(n):
        at = offsets[n]
        fc = blob[at + 0x10]
        out = []
        for f in struct.unpack_from(f"<{fc}I", blob, at + 0x1C):
            rate, _, qwc, quat_bytes, scales, trans_at, trans = struct.unpack_from("<fh5H", blob, f)
            out.append((bytearray(blob[f:f + 16 + 16 * qwc]), 16 + trans_at, trans))
        return blob[at:at + 0x1C], out

    header, heads = frames(PANEL_HEADS)
    _, boots = frames(PANEL_BOOTS)

    def root_z(frame):
        data, trans_at, trans = frame
        for i in range(trans):
            x, y, z, joint, pad = struct.unpack_from("<3h2B", data, trans_at + 8 * i)
            if joint == 0:
                return z
        raise SystemExit("a panel frame has no root translation")
    delta = root_z(boots[-1]) - root_z(heads[-1])
    starts, blob_out = [], bytearray()
    for data, trans_at, trans in heads:
        for i in range(trans):
            x, y, z, joint, pad = struct.unpack_from("<3h2B", data, trans_at + 8 * i)
            if joint == 0:
                struct.pack_into("<3h2B", data, trans_at + 8 * i, x, y, z + delta, joint, pad)
        starts.append(len(blob_out))
        blob_out += data
        if len(blob_out) & 15:
            blob_out += bytes(16 - (len(blob_out) & 15))
    return header, starts, bytes(blob_out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/modseg")
    ap.add_argument("--boots-class", type=int, default=None,
                    help="moby class of the Charge Boots model (tools/gen_boots.py's, wrench route); "
                         "default: the Grindboots' (0xC3), for images without the new class")
    args = ap.parse_args()
    pal = args.openrac / "games/rac1"
    args.out.mkdir(parents=True, exist_ok=True)

    c = ["/* generated by tools/gen_items.py from each level's own item data; see src/charge_boots.c */", ""]
    sites = {}
    placements = None
    for L in range(19):
        m = find_items(pal, L, args.boots_class)
        if placements is None:
            placements = m["placements"]
        elif m["placements"] != placements:
            raise SystemExit(f"level {L}: the preview placement table differs from level 0's")
        s = f"{L:02d}"
        cells = [(ICON_BASE + ITEM_GRINDBOOTS, ITEM_GRINDBOOTS), (ICON_BASE + ITEM_MAGNEBOOTS, ITEM_MAGNEBOOTS),
                 (ICON_CHARGE_BOOTS, ITEM_CHARGE_BOOTS)]
        words = ", ".join(f"0x{icon:X}, 0, 0, {item}, 0" for icon, item in cells)
        c.append(f"unsigned short ee_boots_cells_{s}[{5 * len(cells)}] = {{ {words} }};")
        sites[L] = {"cells": {"site": m["widget"] + 0x48, "expect": m["cells"], "symbol": f"ee_boots_cells_{s}"},
                    "cols": {"site": m["widget"] + 0x44, "expect": 2, "value": len(cells)},
                    "panel": {"site": m["panel"], "expect": PANEL_BOOTS, "value": PANEL_SLOT},
                    "record": m["record"]}
    # The placement table with a 37th entry for the Charge Boots: the Grindboots' (29).
    words = list(struct.unpack(f"<{PLACEMENT_COUNT * PLACEMENT_WORDS}I", placements))
    words += words[ITEM_GRINDBOOTS * PLACEMENT_WORDS:(ITEM_GRINDBOOTS + 1) * PLACEMENT_WORDS]
    c.append("")
    c.append(f"const unsigned int ee_preview_placements[{len(words)}] __attribute__((aligned(16))) = {{")
    for k in range(0, len(words), PLACEMENT_WORDS):
        c.append("  " + ", ".join(f"0x{w:08X}" for w in words[k:k + PLACEMENT_WORDS]) + f",  /* item {k // PLACEMENT_WORDS} */")
    c.append("};")
    # The Foot Items box: a sequence of the menu panel class, installed at runtime in PANEL_SLOT.
    header, starts, frames_blob = panel_sequence(args.openrac)
    c.append("")
    c.append("/* The Foot Items box: the Head Items box sequence (0x25) of the menu panel class 0x472 with its root moved")
    c.append("   down to the boots box's height; header words, then the frames' addresses. Installed by src/charge_boots.c. */")
    c.append(f"static const unsigned char ee_panel_frames[{len(frames_blob)}] __attribute__((aligned(16))) = {{")
    for k in range(0, len(frames_blob), 16):
        c.append("  " + ", ".join(f"0x{b:02X}" for b in frames_blob[k:k + 16]) + ",")
    c.append("};")
    words = [f"0x{w:08X}" for w in struct.unpack("<7I", header)] + [f"(unsigned int)(ee_panel_frames + 0x{o:X})" for o in starts]
    c.append(f"const unsigned int ee_panel_seq[{len(words)}] __attribute__((aligned(16))) = {{ {', '.join(words)} }};")
    # Record 36 per level: its address, retail's words (the races' Devastator) and the Charge Boots'.
    c.append("")
    c.append("const unsigned int ee_item36_at[19] = { "
             + ", ".join(f"0x{sites[L]['record'][0][0]:08X}" for L in range(19)) + " };")
    c.append(f"const unsigned int ee_item36_words[19][2][{RECORD_WORDS}] = {{")
    for L in range(19):
        rec = sites[L]["record"]
        retail = ", ".join(f"0x{w[1]:X}" for w in rec)
        boots = ", ".join(f"0x{w[2]:X}" for w in rec)
        c.append(f"  {{ {{ {retail} }},\n    {{ {boots} }} }},  /* level {L} */")
    c.append("};")
    (args.out / "gen_items.c").write_text("\n".join(c) + "\n")
    (args.out / "items_sites.json").write_text(json.dumps(sites, indent=1))
    print(f"wrote {args.out / 'gen_items.c'} (19 levels) and items_sites.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
