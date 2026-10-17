#!/usr/bin/env python3
"""
Generate the Charge Boots item, per level, from each level's own data.

The game's item records (0x4C bytes each, 37 of them: text ids, equip slot,
attach bone, moby class, icon, description) and the pause menu's Gadgets
grids are level-local data: every level program carries its own copy at its
own addresses. Record 36 is an unused duplicate of the Devastator in retail
(no grid shows it, no vendor sells it), so it becomes the Charge Boots: a
copy of the Grindboots record (slot 1, class 0xC3 on both feet) with its own
name, description and icon. The boots grid (Grindboots, Magneboots) becomes
a three-cell grid. This reads each level's data record and writes:

  build/modseg/gen_items.c       19 copies of the three-cell boots grid
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
ROOT = HERE.parent

ITEM_GRINDBOOTS, ITEM_MAGNEBOOTS, ITEM_CHARGE_BOOTS = 29, 28, 36
ICON_BASE = 0xEA60                    # every item's icon id is ICON_BASE + item id
ICON_CHARGE_BOOTS = ICON_BASE + ITEM_CHARGE_BOOTS    # 0xEA84, added to the HUD header by tools/gen_hud.py
TEXT_NAME, TEXT_DESC, TEXT_SURFACE = 0x5406, 0x5407, 0x5408   # text/strings.tsv
RECORD_WORDS = 0x4C // 4
RECORD_COUNT = 37
PANEL_BOOTS, PANEL_HEADS = 0x26, 0x25     # menu panel class 0x472 sequences: the boots box, the Head Items box
PLACEMENT_WORDS = 8                   # ItemPreviewPlacement: 8 floats per item
PLACEMENT_COUNT = 36                  # retail's table; item 36 would read the words after it
PLACEMENT_SIGNATURE = struct.pack("<8f", 2, 3, 0, 0.2, 0, 0.5, 0, 0) * 2   # entries 28 and 29, the boots


def level_record(pal: Path, level: int):
    m = json.loads((pal / f"baserom/overlays/level_{level:02d}/manifest.json").read_text())
    rec = next(r for r in m["records"] if r["name"] == "data")
    data = (pal / f"baserom/overlays/level_{level:02d}/data.bin").read_bytes()
    return rec["address"], data


def find_items(pal: Path, level: int) -> dict:
    base, d = level_record(pal, level)
    u32 = lambda a: struct.unpack_from("<I", d, a - base)[0]
    u16 = lambda a: struct.unpack_from("<H", d, a - base)[0]
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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parent / "OpenRAC")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/modseg")
    args = ap.parse_args()
    pal = args.openrac / "games/rac1"
    args.out.mkdir(parents=True, exist_ok=True)

    c = ["/* generated by tools/gen_items.py from each level's own item data; see src/charge_boots.c */", ""]
    sites = {}
    placements = None
    for L in range(19):
        m = find_items(pal, L)
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
                    "panel": {"site": m["panel"], "expect": PANEL_BOOTS, "value": PANEL_HEADS},
                    "record": m["record"]}
    # The placement table with a 37th entry for the Charge Boots: the Grindboots' (29).
    words = list(struct.unpack(f"<{PLACEMENT_COUNT * PLACEMENT_WORDS}I", placements))
    words += words[ITEM_GRINDBOOTS * PLACEMENT_WORDS:(ITEM_GRINDBOOTS + 1) * PLACEMENT_WORDS]
    c.append("")
    c.append(f"const unsigned int ee_preview_placements[{len(words)}] __attribute__((aligned(16))) = {{")
    for k in range(0, len(words), PLACEMENT_WORDS):
        c.append("  " + ", ".join(f"0x{w:08X}" for w in words[k:k + PLACEMENT_WORDS]) + f",  /* item {k // PLACEMENT_WORDS} */")
    c.append("};")
    (args.out / "gen_items.c").write_text("\n".join(c) + "\n")
    (args.out / "items_sites.json").write_text(json.dumps(sites, indent=1))
    print(f"wrote {args.out / 'gen_items.c'} (19 levels) and items_sites.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
