#!/usr/bin/env python3
"""
Per-level places for the Charge Boots dash (src/charge_dash.c), from each
level's own program: the hero's state code is built differently in groups of
levels, so the catalogue does not name these.

  trigger    the input handler's Thruster-Pack test (NTSC decomp
             hero_set_state.c, end of FUN_L01_00240ed8): the call
             slot_item(3) (func_L00_0020DB30) whose result is compared with
             3, the Thruster-Pack, before the R1 double tap enters state 0x81.
             The dash hooks this call: with the Charge Boots on, the R1
             double tap is theirs and the hook answers "no Thruster-Pack".
  scale      scale_game_frames as that block calls it (scale(0x1E) reloads
             the double-tap window): three functions share its code and its
             catalogue name, so the block's own call gives the right one
  move_site  the one call of the per-state movement function (the function
             holding the jump table of 0x83 states whose state 0x15 block
             calls speed_step, as tools/gen_hero.py finds it); the dash
             hooks it and moves the hero itself in its state
  move_fn    that function
  part_frames the particle engine's table of per-type frame lists (each a
             list of particle texture indices), read from particle type
             27's spawner (func_L00_0026EBC0: lui a1 / lw v0 of its own
             entry, the table + 27 * 4); the dash's jets take their texture
             from type 4's list, R&C2's jet texture
  camera_site the follow camera overrides' (func_L00_002E74B0) one call of
             collect_mobies_in_sphere (func_L00_001F2BE8), made every frame
             just before the overrides' Thruster-Pack block, where R&C2's
             hero camera pass has its charge block; the dash hooks it
  collect    that call's target, the level's collect_mobies_in_sphere
  collect_list where it leaves the mobies it found (a null-terminated list
             of moby pointers; lui $s6 / addiu $s6 near its start): the
             dash looks at what it runs into before any hit
  cheat_table the move-combo cheat detector's (func_L00_00207418) table of
             12 zero-terminated sequences, 16 bytes each, in the level's
             data (its lui $a1 / addiu off $a1 that is not gCheats'); checked:
             entry 0 is retail's Big Head sequence and entry 8 is empty.
             The Charge Boots cheat goes in entry 8 (src/charge_boots.c)
  cheats_enter the Goodies Cheats page's toggle-list widget's "enter"
             callback slot (widget + 8, in the level's data): the one data
             word holding the list builder func_0021B138's address
  cheats_builder that builder (rebuild_configured_text_label_list in the NTSC
             decomp: a row per unlocked cheat)
  cheats_list the widget's row list (widget + 0x34), 13 rows of 0x14 bytes
  cheats_update_slot the widget's "update" callback slot (widget + 0), run
             every frame the page is open (update_menu_option_list,
             func_00221E60, toggles a row's flag on Cross)
  cheats_update that function

Writes build/modseg/gen_charge.c (19-entry tables) and
build/modseg/charge_sites.json (for tools/overlay_patch.py).

  python tools/gen_charge.py [--openrac DIR] [--print]
"""
import argparse
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SLOT_ITEM = "func_L00_0020DB30"
SPEED_STEP = "func_L00_00212740"
TURN_TO = "func_L00_002124E8"
SPAWN_FADE = "func_L00_0026EBC0"
CAMERA_FN = "func_L00_002E74B0"
CAMERA_FN_SIZE = 1716
COLLECT = "func_L00_001F2BE8"
DETECTOR = "func_L00_00207418"
DETECTOR_SIZE = 720
CHEATS_BUILDER = "func_0021B138"
CHEATS_UPDATE = "func_00221E60"
RETAIL_CHEAT_0 = bytes([1, 2, 3, 3, 4, 5, 4, 6, 0])
CHEAT_ENTRY = 8
FADE_TYPE = 27
STATE_COUNT = 0x83
STATE_COMET = 0x15
STATE_RUN = 2
STATE_THRUSTER = 0x81
NAMES = ("trigger", "slot_item", "scale", "set_state", "move_site", "move_fn", "part_frames", "camera_site",
         "collect", "collect_list", "cheat_table", "cheats_enter", "cheats_builder", "cheats_list",
         "cheats_update_slot", "cheats_update")


def jal(target: int) -> int:
    return 0x0C000000 | ((target >> 2) & 0x3FFFFFF)


def catalogue(openrac: Path) -> dict:
    places = {}
    for line in (openrac / "games/rac1/config/overlays/functions.tsv").read_text().splitlines():
        if line.startswith("#"):
            continue
        p = line.split("\t")
        if p[0] in (SLOT_ITEM, SPEED_STEP, TURN_TO, SPAWN_FADE, CAMERA_FN, COLLECT, DETECTOR, CHEATS_BUILDER,
                    CHEATS_UPDATE):
            lv = {}
            for pl in p[5].split(","):
                L, a = pl.split(":")
                lv.setdefault(int(L), []).append(int(a, 16))
            places[p[0]] = lv
    return places


def find_level(openrac: Path, level: int, cat: dict) -> dict:
    pal = openrac / "games/rac1"
    m = json.loads((pal / f"baserom/overlays/level_{level:02d}/manifest.json").read_text())
    recs = {r["name"]: (r["address"], (pal / f"baserom/overlays/level_{level:02d}/{r['name']}.bin").read_bytes())
            for r in m["records"] if r["name"] in ("text", "data", "lit")}
    tbase, text = recs["text"]
    n = len(text) // 4
    w = struct.unpack_from(f"<{n}I", text, 0)

    def word_at(addr):
        for base, body in recs.values():
            if base <= addr < base + len(body):
                return struct.unpack_from("<I", body, addr - base)[0]
        return None

    def one(name):
        p = cat[name].get(level, [])
        if len(p) != 1:
            raise SystemExit(f"level {level}: {name} has {len(p)} places")
        return p[0]

    slot_item = one(SLOT_ITEM)
    # The Thruster-Pack test: jal slot_item / addiu a0,3 / addiu v1,3 / bne(l) v0,v1.
    # Many places test for the Thruster-Pack; the input handler's is the one
    # whose block enters state 0x81 and reloads the window with scale(0x1E).
    trig = []
    for k0 in range(n - 4):
        if not (w[k0] == jal(slot_item) and w[k0 + 1] == 0x24040003
                and w[k0 + 2] == 0x24030003 and (w[k0 + 3] >> 16) in (0x5443, 0x1443)):
            continue
        set_state = scale = None
        for k in range(k0 + 4, min(k0 + 80, n - 1)):
            x = w[k]
            if x >> 26 != 3:
                continue
            target = (x & 0x3FFFFFF) << 2
            if w[k - 1] == 0x24040000 | STATE_THRUSTER and w[k + 1] == 0x24050001:
                set_state = set_state or target
            if w[k + 1] == 0x2404001E:
                scale = scale or target
        if set_state is not None and scale is not None:
            trig.append((k0, set_state, scale))
    if len(trig) != 1:
        raise SystemExit(f"level {level}: the input handler's Thruster-Pack test found {len(trig)} times")
    k0, set_state, scale = trig[0]

    # The movement switch: a jr fed by a table of 0x83 code entries whose
    # state 0x15 entry calls speed_step within 0x400 bytes.
    jal_speed = jal(one(SPEED_STEP))
    move_fn = None
    for k in range(n):
        x = w[k]
        if x >> 26 != 0 or (x & 0x3F) != 8 or ((x >> 21) & 31) == 31:
            continue
        bound = tbl = None
        for j in range(1, 16):
            p = w[k - j]
            if p >> 26 == 0x0B:
                bound = p & 0xFFFF
            if p >> 26 == 9 and ((p >> 21) & 31) == ((p >> 16) & 31):
                lo = p & 0xFFFF
                lo = lo - 0x10000 if lo & 0x8000 else lo
                for q in range(j + 1, j + 8):
                    pp = w[k - q]
                    if pp >> 26 == 0x0F and ((pp >> 16) & 31) == ((p >> 16) & 31):
                        tbl = ((pp & 0xFFFF) << 16) + lo
                        break
                if tbl is not None:
                    break
        if tbl is None or (bound is not None and bound != STATE_COUNT):
            continue
        entries = [word_at(tbl + 4 * i) for i in (0, 1, STATE_RUN, 3, STATE_COMET)]
        if any(e is None or not (tbase <= e < tbase + len(text)) for e in entries):
            continue
        e15 = entries[-1]
        if not any(w[(a - tbase) // 4] == jal_speed for a in range(e15, e15 + 0x400, 4)):
            continue
        # walk back to the prologue (addiu $sp, $sp, -N)
        s = k
        while not (w[s] >> 16 == 0x27BD and w[s] & 0x8000):
            s -= 1
        fn = tbase + 4 * s
        if move_fn is not None and move_fn != fn:
            raise SystemExit(f"level {level}: two movement switches ({move_fn:#x}, {fn:#x})")
        move_fn = fn
    if move_fn is None:
        raise SystemExit(f"level {level}: the movement switch not found")
    calls = [tbase + 4 * k for k in range(n) if w[k] == jal(move_fn)]
    if len(calls) != 1:
        raise SystemExit(f"level {level}: the movement function {move_fn:#x} is called {len(calls)} times")

    # Type 27's spawner loads its own frame list: lui a1, hi ... lw v0, lo(a1).
    spawn = (one(SPAWN_FADE) - tbase) // 4
    hi = [w[k] & 0xFFFF for k in range(spawn, spawn + 56) if w[k] >> 16 == 0x3C05]
    lo = [w[k] & 0xFFFF for k in range(spawn, spawn + 56) if w[k] >> 16 == 0x8CA2]
    if len(hi) != 1 or len(lo) != 1:
        raise SystemExit(f"level {level}: type {FADE_TYPE}'s frame list not found in its spawner")
    part_frames = (hi[0] << 16) + (lo[0] - 0x10000 if lo[0] & 0x8000 else lo[0]) - 4 * FADE_TYPE

    # The camera overrides' one call of collect_mobies_in_sphere.
    camera, collect = one(CAMERA_FN), one(COLLECT)
    k = (camera - tbase) // 4
    cams = [tbase + 4 * j for j in range(k, k + CAMERA_FN_SIZE // 4) if w[j] == jal(collect)]
    if len(cams) != 1:
        raise SystemExit(f"level {level}: {COLLECT} called {len(cams)} times in {CAMERA_FN}")
    c = (collect - tbase) // 4
    lists = [((w[j] & 0xFFFF) << 16) + ((w[j + 1] & 0xFFFF) - 0x10000 if w[j + 1] & 0x8000 else w[j + 1] & 0xFFFF)
             for j in range(c, c + 0x40) if w[j] >> 16 == 0x3C16 and w[j + 1] >> 16 == 0x26D6]
    if len(lists) != 1:
        raise SystemExit(f"level {level}: {COLLECT}'s result list found {len(lists)} times")

    # The cheat detector's sequence table.
    det = one(DETECTOR)
    d = (det - tbase) // 4
    tables = set()
    for j in range(d, d + DETECTOR_SIZE // 4):
        if w[j] >> 16 == 0x3C05 and (w[j] & 0xFFFF) != 0x16:          # lui $a1, hi (0x16: gCheats)
            for y in w[j + 1:j + 6]:
                if y >> 26 == 9 and ((y >> 21) & 31) == 5:              # addiu rt, $a1, lo
                    lo = y & 0xFFFF
                    tables.add(((w[j] & 0xFFFF) << 16) + (lo - 0x10000 if lo & 0x8000 else lo))
                    break
    if len(tables) != 1:
        raise SystemExit(f"level {level}: the cheat table found {len(tables)} times")
    table = tables.pop()
    entry = lambda i: b"".join(struct.pack("<I", word_at(table + 16 * i + 4 * k)) for k in range(4))
    if not entry(0).startswith(RETAIL_CHEAT_0) or entry(CHEAT_ENTRY) != bytes(16):
        raise SystemExit(f"level {level}: the cheat table at {table:#x} is not retail's ({entry(0).hex()}, entry 8 {entry(CHEAT_ENTRY).hex()})")

    # The Cheats page's list widget: the data word holding the builder's
    # address is its "enter" slot (widget + 8); its "update" slot (+ 0) holds
    # the option list's update; its row list is at + 0x34.
    builder = one(CHEATS_BUILDER)
    dbase, dbody = recs["data"]
    slots = [dbase + i for i in range(0, len(dbody) - 3, 4) if struct.unpack_from("<I", dbody, i)[0] == builder]
    if len(slots) != 1:
        raise SystemExit(f"level {level}: the cheats builder {builder:#x} is referenced {len(slots)} times in the data")
    widget = slots[0] - 8
    rows = word_at(widget + 0x34)
    if not (dbase <= rows < widget and widget - rows >= 13 * 0x14):
        raise SystemExit(f"level {level}: the cheats widget at {widget:#x} has rows at {rows:#x}")
    update = one(CHEATS_UPDATE)
    if word_at(widget) != update:
        raise SystemExit(f"level {level}: the cheats widget at {widget:#x} updates with {word_at(widget):#x}, not {update:#x}")
    return {"trigger": tbase + 4 * k0, "slot_item": slot_item, "scale": scale, "set_state": set_state,
            "move_site": calls[0], "move_fn": move_fn, "part_frames": part_frames,
            "camera_site": cams[0], "collect": collect, "collect_list": lists[0],
            "cheat_table": table, "cheats_enter": slots[0], "cheats_builder": builder, "cheats_list": rows,
            "cheats_update_slot": widget, "cheats_update": update}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/modseg")
    ap.add_argument("--print", action="store_true")
    args = ap.parse_args()
    cat = catalogue(args.openrac)
    found = {L: find_level(args.openrac, L, cat) for L in range(19)}
    hero = args.out / "hero_sites.json"
    if hero.is_file():
        h = json.loads(hero.read_text())
        for L, f in found.items():
            if h[str(L)]["hero_set_state"] != f["set_state"]:
                raise SystemExit(f"level {L}: set_state {f['set_state']:#x} disagrees with gen_hero's {h[str(L)]['hero_set_state']:#x}")
    if args.print:
        for L, f in found.items():
            print(f"level {L:2d}: " + ", ".join(f"{k} {f[k]:#x}" for k in NAMES))
    args.out.mkdir(parents=True, exist_ok=True)
    c = ["/* generated by tools/gen_charge.py from each level's own program; see src/charge_dash.c */",
         "typedef unsigned int u32;"]
    for name in NAMES:
        c.append(f"const u32 ee_charge_{name}[19] = {{ {', '.join(f'0x{found[L][name]:08X}' for L in range(19))} }};")
    (args.out / "gen_charge.c").write_text("\n".join(c) + "\n")
    (args.out / "charge_sites.json").write_text(json.dumps({L: found[L] for L in found}, indent=1))
    print(f"wrote {args.out / 'gen_charge.c'} and charge_sites.json (19 levels)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
