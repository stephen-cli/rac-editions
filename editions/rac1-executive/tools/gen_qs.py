#!/usr/bin/env python3
"""
Per-level sites of the quick-select pages feature (src/quick_select_pages.c).

The ring and its pause page are shared code with a copy in every level
program, each at its own addresses, and two of the places to patch are
menu data (also per level), so this finds them from each level's own
program and records, checked against the retail words:

  show       the hero's HudShow(3, 0, QuickSelectInit, QuickSelectUpdate,
             QuickSelectDraw, 0, 0) call: the lui/addiu pairs that load the
             update and draw callbacks (tools/overlay_patch.py points them
             at ee_qs_update and ee_qs_draw)
  ring       the pause menu's Quick Select page: its ring widget record
             {update func_0021D7A0, draw func_00221B58, enter, leave} in the
             level's data; the update and draw words go to the level's stubs
  prompt     the same page's button-prompt widget, whose list pointer (+0x34)
             names {"Equip", "Exit"}. The widget is shared with the Weapons and
             Gadgets pages, so the Quick Select page's entry for it (in the
             page record, just after the ring widget's) goes to a copy
             (build/modseg/gen_qs.c) that lists the edition's three rows
  autoslot   in the item-acquisition function (func_L00_002618D8), the six
             words after its search for an empty ring slot: the test of the
             index found and the store into the active page, replaced by a
             call that also tries the other pages; and the search loop's
             exit branch when the page is full, which skipped the store and
             now reaches the call with index 8
  carry      the challenge-mode carry-over's call of the save-state reset
             (func_00227DB0 + EE_SITE_QS_CARRY), through which the pages are
             kept like the ring itself

Writes build/modseg/qs_sites.json for tools/overlay_patch.py and
build/modseg/gen_qs.c (the prompt widget copies) for tools/modseg.py.

  python tools/gen_qs.py [--openrac DIR]
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

INIT, UPDATE, DRAW, HUD_SHOW = "func_L00_00238478", "func_L00_00238530", "func_L00_00238C30", "func_001FFB38"
RING = ("func_0021D7A0", "func_00221B58", "func_0021D9C8", "func_0021DA60")
PROMPT_DRAW = "func_0021C1B0"
ACQUIRE = "func_L00_002618D8"
CARRY, RESET = "func_00227DB0", "func_00209DC0"
PROMPT_EQUIP, PROMPT_EXIT = 0x4EEC, 0x4EE0
PROMPT_WIDGET_WORDS = 0x60 // 4   # the widget record and its state (the draw reads up to +0x40)
NAMES = (INIT, UPDATE, DRAW, HUD_SHOW, *RING, PROMPT_DRAW, ACQUIRE, CARRY, RESET)


def header_define(name: str) -> int:
    m = re.search(rf"#define\s+{name}\s+(0x[0-9A-Fa-f]+|\d+)", (ROOT / "src/modseg.h").read_text())
    if not m:
        raise SystemExit(f"{name} is not defined in src/modseg.h")
    return int(m.group(1), 0)


def jal(target: int) -> int:
    return 0x0C000000 | ((target >> 2) & 0x3FFFFFF)


def hi(addr: int) -> int:
    return ((addr + 0x8000) >> 16) & 0xFFFF


def catalogue(openrac: Path) -> dict:
    places = {}
    for line in (openrac / "games/rac1/config/overlays/functions.tsv").read_text().splitlines():
        if line.startswith("#"):
            continue
        p = line.split("\t")
        if p[0] in NAMES:
            lv = {}
            for pl in p[5].split(","):
                L, a = pl.split(":")
                lv.setdefault(int(L), []).append(int(a, 16))
            places[p[0]] = lv
    return places


def find_level(openrac: Path, level: int, cat: dict, carry_site: int, autoslot_site: int) -> dict:
    pal = openrac / "games/rac1"
    m = json.loads((pal / f"baserom/overlays/level_{level:02d}/manifest.json").read_text())
    recs = {r["name"]: (r["address"], (pal / f"baserom/overlays/level_{level:02d}/{r['name']}.bin").read_bytes())
            for r in m["records"] if r["name"] in ("text", "data")}
    tbase, text = recs["text"]
    w = struct.unpack_from(f"<{len(text) // 4}I", text)
    dbase, data = recs["data"]
    dw = struct.unpack_from(f"<{len(data) // 4}I", data)

    def word_at(addr):
        for base, body in recs.values():
            if base <= addr < base + len(body):
                return struct.unpack_from("<I", body, addr - base)[0]
        raise SystemExit(f"level {level}: 0x{addr:X} is in neither record")

    def one(name):
        p = cat[name].get(level, [])
        if len(p) != 1:
            raise SystemExit(f"level {level}: {name} has {len(p)} places")
        return p[0]

    init, upd, drw, show = one(INIT), one(UPDATE), one(DRAW), one(HUD_SHOW)
    # lui $6/$7/$8 of the three callbacks, three addiu, two moves, a move, jal HudShow, addiu $4, 3
    sites = [k for k in range(len(w) - 10)
             if w[k] == 0x3C060000 | hi(init) and w[k + 1] == 0x3C070000 | hi(upd) and w[k + 2] == 0x3C080000 | hi(drw)
             and w[k + 4] == 0x24E70000 | (upd & 0xFFFF) and w[k + 5] == 0x25080000 | (drw & 0xFFFF)
             and w[k + 9] == jal(show) and w[k + 10] == 0x24040003]
    if len(sites) != 1:
        raise SystemExit(f"level {level}: {len(sites)} HudShow(3, ...) sites for the ring")
    k = sites[0]
    show_words = {"lui_update": tbase + 4 * (k + 1), "lui_draw": tbase + 4 * (k + 2),
                  "addiu_update": tbase + 4 * (k + 4), "addiu_draw": tbase + 4 * (k + 5)}

    ring = tuple(one(n) for n in RING)
    rings = [dbase + 4 * k for k in range(len(dw) - 3) if dw[k:k + 4] == ring]
    if len(rings) != 1:
        raise SystemExit(f"level {level}: {len(rings)} ring widget records")

    prompt_draw = one(PROMPT_DRAW)
    prompts = []
    for k in range(len(dw) - 14):
        if dw[k] == 0 and dw[k + 1] == prompt_draw and dw[k + 12] == 0x4002:
            lst = dw[k + 13]
            if dbase <= lst < dbase + len(data) and word_at(lst) == PROMPT_EQUIP and word_at(lst + 12) == PROMPT_EXIT \
                    and word_at(lst + 24) == 0:
                prompts.append((dbase + 4 * k, lst))
    if len(prompts) != 1:
        raise SystemExit(f"level {level}: {len(prompts)} Quick Select prompt widgets")
    widget, lst = prompts[0]
    # The prompt widget is shared by the Weapons, Gadgets and Quick Select pages;
    # only Quick Select's widget list entry (the one in the page record that also
    # lists the ring widget) is sent to the edition's copy.
    ring_refs = [dbase + 4 * k for k, x in enumerate(dw) if x == rings[0]]
    page_refs = [dbase + 4 * k for k, x in enumerate(dw) if x == widget
                 and any(0 < dbase + 4 * k - r < 0x48 for r in ring_refs)]
    if len(ring_refs) != 1 or len(page_refs) != 1:
        raise SystemExit(f"level {level}: the Quick Select page's widget list not found "
                         f"({len(ring_refs)} ring, {len(page_refs)} prompt references)")
    widget_words = [word_at(widget + 4 * i) for i in range(PROMPT_WIDGET_WORDS)]

    acquire = one(ACQUIRE)
    # The search loop's exit when the page is full (slti $2, $4, 8; beqz $2 -> the
    # function's end, 0x18 bytes before the store's test) skips the store; it is
    # sent to the hooked site too, so a full page reaches the hook with index 8.
    loop_exit = acquire + autoslot_site - 0x18
    if word_at(loop_exit - 4) != 0x28820008 or word_at(loop_exit) != 0x1040000B:
        raise SystemExit(f"level {level}: the search loop's exit in {ACQUIRE} is not as expected")
    auto = [word_at(acquire + autoslot_site + 4 * i) for i in range(6)]
    # slti $2, $4, 8; beqz $2; addiu $3, $5, %lo; sll $2, $4, 2; addu $2, $2, $3; sw $16, 0($2)
    if auto[0] != 0x28820008 or auto[1] >> 16 != 0x1040 or auto[2] >> 16 != 0x24A3 or auto[3] != 0x00041080 \
            or auto[4] != 0x00431021 or auto[5] != 0xAC500000:
        raise SystemExit(f"level {level}: the empty-slot store in {ACQUIRE} is not as expected: "
                         + " ".join(f"{x:08X}" for x in auto))

    carry = one(CARRY) + carry_site
    if word_at(carry) != jal(one(RESET)):
        raise SystemExit(f"level {level}: {CARRY} + 0x{carry_site:X} is not jal {RESET}")

    return {"show": {k: {"site": a, "expect": word_at(a)} for k, a in show_words.items()},
            "ring": {"site": rings[0], "expect_update": ring[0], "expect_draw": ring[1]},
            "prompt": {"site": page_refs[0], "expect": widget, "symbol": f"ee_qs_prompt_widget_{level:02d}",
                       "words": widget_words},
            "autoslot": {"site": acquire + autoslot_site, "expect": auto,
                         "loop_exit": {"site": loop_exit, "expect": 0x1040000B,
                                       "value": 0x10400000 | ((0x18 - 4) // 4)}},
            "carry": {"site": carry, "expect": jal(one(RESET))}}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/modseg")
    args = ap.parse_args()
    cat = catalogue(args.openrac)
    missing = [n for n in NAMES if n not in cat]
    if missing:
        raise SystemExit(f"not in the overlay catalogue: {', '.join(missing)}")
    carry_site = header_define("EE_SITE_QS_CARRY")
    autoslot_site = header_define("EE_SITE_QS_AUTOSLOT")
    sites = {L: find_level(args.openrac, L, cat, carry_site, autoslot_site) for L in range(19)}
    args.out.mkdir(parents=True, exist_ok=True)
    c = ["/* generated by tools/gen_qs.py: per level, a copy of the pause menu's button-prompt widget for the",
         " * Quick Select page alone (the retail widget is shared with Weapons and Gadgets), listing the",
         " * edition's three rows; see src/quick_select_pages.c */",
         "typedef unsigned int u32;", "extern const unsigned short ee_qs_prompts[];", ""]
    for L, s in sites.items():
        w = [f"0x{x:08X}" for x in s["prompt"]["words"]]
        w[0x34 // 4] = "(u32)ee_qs_prompts"
        c.append(f"u32 {s['prompt']['symbol']}[{PROMPT_WIDGET_WORDS}] = {{ {', '.join(w)} }};")
        del s["prompt"]["words"]
    (args.out / "gen_qs.c").write_text("\n".join(c) + "\n")
    (args.out / "qs_sites.json").write_text(json.dumps(sites, indent=1))
    print(f"wrote {args.out / 'gen_qs.c'} and {args.out / 'qs_sites.json'} (19 levels)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
