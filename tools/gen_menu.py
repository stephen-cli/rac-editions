#!/usr/bin/env python3
"""
Generate the pause-menu additions, per level, from each level's own data.

The pause menu's pages, widgets and rows are level-local data (every level
program carries a copy at its own addresses, with pointers between them),
so anything that points at them has to exist once per level. This reads
each level's data record (OpenRAC's tools/overlays.py dump), finds the
Options list and the Subtitles page, and writes:

  build/modseg/gen_menu.c      19 copies of: the Options rows with a
                               "Quick Select" row added before Quit Game, a
                               page for it (a copy of the Subtitles page), its
                               toggle widget, entries and title widget
  build/modseg/menu_sites.json per level: the Options list widget's item
                               pointer (the word tools/overlay_patch.py
                               redirects) and its retail value

  python tools/gen_menu.py [--openrac DIR]
"""
import argparse
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SECTOR_BYTES = 2048

OPTION_LABELS = [0x4F1E, 0x4F1F, 0x4F20, 0x4F22, 0x4F23, 0x4F24, 0x4F26]   # HelpDesk .. Quit Game
LABEL_SUBTITLES = 0x4F24
LABEL_QUIT = 0x4F26
LABEL_CONTROLS = 0x5402          # "Controls" (text/strings.tsv, added by tools/text_table.py)
LABEL_QS_PAUSE = 0x5403          # "Quick Select Pause"
DESC_CONTROLS = 0x5404           # "Adjust controls settings"
TITLE_CONTROLS = 0x5405          # "Control Options"
ROW_INDEX = 3                    # the new row's place: after Load, before Sound
SETTING_FLAG = 0x0013D508        # saved global flag 120
DESC_IDS = [0x4F31, 0x4F32, 0x4F33, 0x4F35, 0x4F36, 0x4F37, 0x4F39]   # the Options rows' descriptions
IMAGE_TABLE = 0x00138140         # core data: {first frame, frames} per Options row, 8 bytes each


def level_record(pal: Path, level: int):
    m = json.loads((pal / f"baserom/overlays/level_{level:02d}/manifest.json").read_text())
    rec = next(r for r in m["records"] if r["name"] == "data")
    data = (pal / f"baserom/overlays/level_{level:02d}/data.bin").read_bytes()
    return rec["address"], data


def find_menu(pal: Path, level: int) -> dict:
    base, d = level_record(pal, level)
    u32 = lambda a: struct.unpack_from("<I", d, a - base)[0]
    items = None
    for off in range(0, len(d) - 96, 4):
        if all(struct.unpack_from("<H", d, off + 12 * i)[0] == lab for i, lab in enumerate(OPTION_LABELS)) \
                and struct.unpack_from("<I", d, off + 84)[0] == 0:
            items = base + off
            break
    if items is None:
        raise SystemExit(f"level {level}: Options rows not found")
    widget = next(base + off - 0x34 for off in range(0, len(d) - 4, 4) if u32(base + off) == items)
    rows = [struct.unpack_from("<3I", d, items - base + 12 * i) for i in range(8)]
    sub_page = rows[5][1]
    options = u32(sub_page + 0x38)
    # The template is the cut "Controller vibration" page (kind 0x1C, under
    # Options, never reachable in retail): a toggle page like Subtitles, but its
    # frame-moby sequences (118, 119, 120, 123, 122) give a panel wide enough for
    # a twenty-character row label; Subtitles' (73..77) cut "Quick Select Pause".
    template = None
    for off in range(0, len(d) - 0x88, 4):
        a = base + off
        if u32(a + 0x3C) == 0x1C and u32(a + 0x38) == options:
            template = a
            break
    if template is None:
        raise SystemExit(f"level {level}: the vibration page (kind 0x1C under Options) not found")
    page = [u32(template + 4 * i) for i in range(0x88 // 4)]
    toggle, title = page[16], page[17]
    tog = [u32(toggle + 4 * i) for i in range(16)]
    tit = [u32(title + 4 * i) for i in range(22)]
    entry = [u32(tog[13] + 4 * i) for i in range(5)]
    sub_tog = u32(sub_page + 0x40)
    sub_entry = [u32(u32(sub_tog + 0x34) + 4 * i) for i in range(5)]
    if sub_entry[0] != LABEL_SUBTITLES or sub_entry[1] != 0x15EF40:
        raise SystemExit(f"level {level}: the Subtitles entry is not where expected")
    if tog[0] != u32(sub_tog) or tog[1] != u32(sub_tog + 4):
        raise SystemExit(f"level {level}: the vibration page's toggle widget differs from Subtitles'")
    entry = [entry[0], entry[1], sub_entry[2], sub_entry[3], 0]    # On/Off ids as Subtitles'
    page[15] = 0x1B                                                # behave as the Subtitles page
    # The Options page's other widgets: the description widget (+0x34 -> the
    # row description ids) and the image widget (+0x30 -> the core image table).
    desc_site = image_site = None
    for i in range(18):
        w = u32(options + 0x40 + 4 * i)
        if not w:
            continue
        arr = u32(w + 0x34)
        if desc_site is None and 0 < arr - base < len(d) - 32 and all(u32(arr + 4 * k) == v for k, v in enumerate(DESC_IDS)):
            desc_site = (w + 0x34, arr)
        if u32(w + 0x30) == IMAGE_TABLE:
            image_site = (w + 0x30, IMAGE_TABLE)
    if desc_site is None or image_site is None:
        raise SystemExit(f"level {level}: Options description or image widget not found")
    return {"items": items, "widget": widget, "rows": rows, "page": page, "toggle_addr": toggle,
            "title_addr": title, "toggle": tog, "title": tit, "entry": entry,
            "desc_site": desc_site, "image_site": image_site}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parent / "OpenRAC")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/modseg")
    args = ap.parse_args()
    pal = args.openrac / "games/rac1/pal"
    args.out.mkdir(parents=True, exist_ok=True)

    c = ["/* generated by tools/gen_menu.py from each level's own menu data; see src/quick_select_pause.c */",
         "typedef unsigned int u32;", ""]
    # The preview table: {first sector, sectors} per row, streamed straight from
    # the disc by the image widget (func_00220DF0). The core copy at 0x138140 is
    # filled at boot from the table of contents' options_ss group, so the values
    # come from the disc's table of contents here; our row shows the Help page's
    # "Controls" preview, help_ss[1] (the table right before, at 0x138108; its
    # rows are Help Log, Controls, Moves, Weapons, Gadgets).
    sys.path.insert(0, str(args.openrac / "editor"))
    from disc import Disc
    with Disc(pal / "baserom/SCES_509.16.iso") as disc:
        refs = disc.survey()["global_references"]
    def group(name):
        rows = sorted((r for r in refs if r["group"] == name), key=lambda r: r["index"])
        return [(r["lba"], r["bytes"] // SECTOR_BYTES) for r in rows]
    options_ss, help_ss = group("options_ss"), group("help_ss")
    if len(options_ss) != 7 or len(help_ss) < 1:
        raise SystemExit(f"unexpected preview groups: options_ss {len(options_ss)}, help_ss {len(help_ss)}")
    pairs = options_ss[:ROW_INDEX] + [help_ss[1]] + options_ss[ROW_INDEX:]   # help_ss: Help Log, Controls, Moves, ...
    c.append(f"u32 ee_options_images[{len(pairs) * 2}] = {{ {', '.join(f'0x{a:X}, 0x{b:X}' for a, b in pairs)} }};")
    c.append("")
    sites = {}
    for L in range(19):
        m = find_menu(pal, L)
        s = f"{L:02d}"
        on, off = m["entry"][2], m["entry"][3]
        c.append(f"u32 ee_qs_entries_{s}[10] = {{ 0x{LABEL_QS_PAUSE:X}, 0x{SETTING_FLAG:08X}, 0x{on:X}, 0x{off:X}, 0, 0, 0, 0, 0, 0 }};")
        tog = [f"0x{w:08X}" for w in m["toggle"]]
        tog[13] = f"(u32)ee_qs_entries_{s}"
        c.append(f"u32 ee_qs_toggle_{s}[16] = {{ {', '.join(tog)} }};")
        tit = [f"0x{w:08X}" for w in m["title"]]
        tit[13] = f"0x{TITLE_CONTROLS:X}"
        c.append(f"u32 ee_qs_title_{s}[22] = {{ {', '.join(tit)} }};")
        page = []
        for w in m["page"]:
            if w == m["toggle_addr"]:
                page.append(f"(u32)ee_qs_toggle_{s}")
            elif w == m["title_addr"]:
                page.append(f"(u32)ee_qs_title_{s}")
            else:
                page.append(f"0x{w:08X}")
        c.append(f"u32 ee_qs_page_{s}[34] = {{ {', '.join(page)} }};")
        words = []
        for i, (w0, w1, w2) in enumerate(m["rows"][:7]):
            if i == ROW_INDEX:
                words += [f"0x{(3 << 16) | LABEL_CONTROLS:08X}", f"(u32)ee_qs_page_{s}", "0"]
            words += [f"0x{w0:08X}", f"0x{w1:08X}", f"0x{w2:08X}"]
        words += ["0", "0", "0"]
        c.append(f"u32 ee_options_items_{s}[27] = {{ {', '.join(words)} }};")
        desc = DESC_IDS[:ROW_INDEX] + [DESC_CONTROLS] + DESC_IDS[ROW_INDEX:] + [0]
        c.append(f"u32 ee_options_desc_{s}[{len(desc)}] = {{ {', '.join(f'0x{x:X}' for x in desc)} }};")
        c.append("")
        sites[L] = {"items": {"site": m["widget"] + 0x34, "expect": m["items"], "symbol": f"ee_options_items_{s}"},
                    "desc": {"site": m["desc_site"][0], "expect": m["desc_site"][1], "symbol": f"ee_options_desc_{s}"},
                    "image": {"site": m["image_site"][0], "expect": m["image_site"][1], "symbol": "ee_options_images"}}
    (args.out / "gen_menu.c").write_text("\n".join(c) + "\n")
    (args.out / "menu_sites.json").write_text(json.dumps(sites, indent=1))
    print(f"wrote {args.out / 'gen_menu.c'} (19 levels) and menu_sites.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
