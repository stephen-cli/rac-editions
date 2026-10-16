#!/usr/bin/env python3
"""
Per-level addresses in the hero's code that the catalogue cannot name.

The hero's per-state movement switch, its state update and hero_set_state
are built differently in groups of levels (the catalogue gives the level 0
build, func_L00_002180F8 and func_L00_00222B80, six levels; the others have
their own), so a feature that hooks inside them finds its places from each
level's own program:

  run block      the RUN (state 2) movement block's calls, in order: turn_to
                 (func_L00_002124E8) at the run start, the pivot turn
                 (func_L00_002125F0), turn_to again, speed_step, then
                 set_planar_vel (func_L00_00212790) twice (the drift path
                 with the heading, the normal path with 99999); the
                 strafe hooks (src/strafe.c) take the turns and the planar
                 calls
  jump block     the jump family's (state 7) movement block's first turn_to
                 and first set_planar_vel: the strafe hooks take them too
  air_turn       the air control's turn_to for plain jumps, the one whose
                 rate is hero + 0x414 (lwc1 0x414 within 8 words before it);
                 R&C2's air control turns toward the camera there while
                 strafing
  jump_press     the four calls of the jump-press handler
                 (func_L00_00229348: X pressed in a window -> high jump
                 with crouch, else plain jump), which the strafe flips hook
  twist          the six calls of the hero lean helper (func_L00_00215A90,
                 which writes the per-joint ease table), hooked for the
                 strafe twist
  runanim        the one call of the run animation updater
                 (func_L00_002293E8), hooked for the backpedal cycle
  flip_appr      the flip's two approach_value(target, step, &hero + 0x458)
                 calls in the air control (func_00214D28's level copy with
                 the &hero + 0x458 argument), hooked for R&C2's strafe flip
  comet_site     the Comet-Strike (state 0x15) movement block's call to
                 speed_step: of the jump tables indexed by the hero's state
                 (code entries; a bound of 0x83 where one is in reach), the
                 one whose state 0x15 entry leads, within the block, to a
                 jal to speed_step (func_L00_00212740, shared by all 19)
  speed_step     that call's target
  hero_set_state among the functions most often called with a small
                 constant state and a 0/1 flag, the one that stores the state
                 at hero + 0x2084; checked against the catalogue where it
                 names it

Writes build/modseg/gen_hero.c (three 19-entry tables, used by
src/comet_strike.c) and build/modseg/hero_sites.json (for
tools/overlay_patch.py).

  python tools/gen_hero.py [--openrac DIR] [--print]
"""
import argparse
import json
import struct
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
STATE_COUNT = 0x83
STATE_COMET = 0x15
STATE_RUN = 2
SPEED_STEP = "func_L00_00212740"
TURN_TO = "func_L00_002124E8"
PIVOT_TURN = "func_L00_002125F0"
SET_PLANAR_VEL = "func_L00_00212790"
JUMP_PRESS = "func_L00_00229348"
TWIST = "func_L00_00215A90"
RUN_ANIM = "func_L00_002293E8"
APPROACH = "func_00214D28"
RUN_ORDER = ["turn", "pivot", "turn", "speed", "planar", "planar"]
STATE_JUMP = 7


def records(pal: Path, level: int):
    m = json.loads((pal / f"baserom/overlays/level_{level:02d}/manifest.json").read_text())
    out = {}
    for rec in m["records"]:
        if rec["name"] in ("text", "data", "lit"):
            out[rec["name"]] = (rec["address"], (pal / f"baserom/overlays/level_{level:02d}/{rec['name']}.bin").read_bytes())
    return out


def catalogue_place(openrac: Path, name: str, level: int) -> int:
    for line in (openrac / "games/rac1/config/overlays/functions.tsv").read_text().splitlines():
        if line.startswith(name + "\t"):
            return {int(p.split(":")[0]): int(p.split(":")[1], 16) for p in line.split("\t")[5].split(",")}.get(level)
    raise SystemExit(f"{name} is not in the catalogue")


def find_level(openrac: Path, level: int) -> dict:
    pal = openrac / "games/rac1"
    recs = records(pal, level)
    tbase, text = recs["text"]
    n = len(text) // 4
    w = struct.unpack_from(f"<{n}I", text, 0)

    def word_at(addr):
        for base, body in recs.values():
            if base <= addr < base + len(body):
                return struct.unpack_from("<I", body, addr - base)[0]
        return None

    speed_step = catalogue_place(openrac, SPEED_STEP, level)
    if speed_step is None:
        raise SystemExit(f"level {level}: {SPEED_STEP} has no place")
    jal_speed_step = 0x0C000000 | ((speed_step >> 2) & 0x3FFFFFF)
    helpers = {}
    for name, kind in ((TURN_TO, "turn"), (PIVOT_TURN, "pivot"), (SET_PLANAR_VEL, "planar"), (SPEED_STEP, "speed")):
        a = catalogue_place(openrac, name, level)
        if a is None:
            raise SystemExit(f"level {level}: {name} has no place")
        helpers[a] = kind
    run = None
    jump = None
    jal_turn_to = 0x0C000000 | ((catalogue_place(openrac, TURN_TO, level) >> 2) & 0x3FFFFFF)

    # Every `jr` fed by a jump table with 0x83 entries: for each, the state 0x15
    # entry; the movement switch's block calls speed_step within 0x400 bytes.
    site = None
    for k in range(n):
        ins = w[k]
        if ins >> 26 != 0 or (ins & 0x3F) != 8 or ((ins >> 21) & 31) == 31:
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
        # a state table: its first entries and the 0x15 one are code addresses
        entries = [word_at(tbl + 4 * i) for i in (0, 1, 2, 3, STATE_COMET)]
        if any(e is None or not (tbase <= e < tbase + len(text)) for e in entries):
            continue
        entry = entries[-1]
        for a in range(entry, entry + 0x400, 4):
            if w[(a - tbase) // 4] == jal_speed_step:
                if site is not None and site != a:
                    raise SystemExit(f"level {level}: two Comet-Strike blocks call speed_step ({site:#x}, {a:#x})")
                site = a
                # The same table's RUN entry: its block's first six helper calls, in RUN_ORDER.
                hits = []
                for b in range(entries[2], entries[2] + 0x900, 4):
                    x = w[(b - tbase) // 4]
                    if x >> 26 == 3 and ((x & 0x3FFFFFF) << 2) in helpers:
                        hits.append((b, helpers[(x & 0x3FFFFFF) << 2]))
                        if len(hits) == len(RUN_ORDER):
                            break
                if [k for _, k in hits] != RUN_ORDER:
                    raise SystemExit(f"level {level}: the RUN movement block's calls are {[k for _, k in hits]}, expected {RUN_ORDER}")
                run = [b for b, _ in hits]
                # The same table's JUMP entry (shared by the jump family): its first turn_to and first set_planar_vel.
                e7 = word_at(tbl + 4 * STATE_JUMP)
                found = {}
                for b in range(e7, e7 + 0x700, 4):
                    x = w[(b - tbase) // 4]
                    if x >> 26 == 3 and helpers.get((x & 0x3FFFFFF) << 2) in ("turn", "planar") and helpers[(x & 0x3FFFFFF) << 2] not in found:
                        found[helpers[(x & 0x3FFFFFF) << 2]] = b
                    if len(found) == 2:
                        break
                if sorted(found) != ["planar", "turn"] or not found["turn"] < found["planar"]:
                    raise SystemExit(f"level {level}: the jump block's turn_to / set_planar_vel not found in order: {found}")
                jump = (found["turn"], found["planar"])
                break
    if site is None:
        raise SystemExit(f"level {level}: the Comet-Strike movement block's speed_step call not found")

    # Air control: the turn_to whose rate is hero + 0x414 (lwc1 0x414 within 8 words before the jal).
    air = [tbase + 4 * j for k, x in enumerate(w) if x >> 26 == 0x31 and (x & 0xFFFF) == 0x414
           for j in range(k + 1, min(k + 9, n)) if w[j] == jal_turn_to]
    if len(air) != 1:
        raise SystemExit(f"level {level}: the air control's turn_to (rate hero + 0x414) found {len(air)} times: {[hex(a) for a in air]}")
    # The jump-press handler's call sites.
    jp = catalogue_place(openrac, JUMP_PRESS, level)
    jal_jp = 0x0C000000 | ((jp >> 2) & 0x3FFFFFF)
    press = [tbase + 4 * k for k, x in enumerate(w) if x == jal_jp]
    if len(press) != 4:
        raise SystemExit(f"level {level}: {JUMP_PRESS} is called {len(press)} times, expected 4: {[hex(a) for a in press]}")
    # The lean helper's calls, the run animation updater's call, the flip's two approach calls.
    tw = catalogue_place(openrac, TWIST, level)
    twist = [tbase + 4 * k for k, x in enumerate(w) if x == (0x0C000000 | ((tw >> 2) & 0x3FFFFFF))]
    if len(twist) != 6:
        raise SystemExit(f"level {level}: {TWIST} is called {len(twist)} times, expected 6")
    ra = catalogue_place(openrac, RUN_ANIM, level)
    runanim = [tbase + 4 * k for k, x in enumerate(w) if x == (0x0C000000 | ((ra >> 2) & 0x3FFFFFF))]
    if len(runanim) != 1:
        raise SystemExit(f"level {level}: {RUN_ANIM} is called {len(runanim)} times, expected 1")
    ap = catalogue_place(openrac, APPROACH, level)
    jal_ap = 0x0C000000 | ((ap >> 2) & 0x3FFFFFF)
    appr = []
    for k, x in enumerate(w):
        if x == jal_ap:
            for j in range(k - 10, k):
                p = w[j]
                if p >> 26 == 9 and ((p >> 16) & 31) == 4 and (p & 0xFFFF) in (0x458, 0xF8A8):
                    appr.append(tbase + 4 * k)
                    break
    if len(appr) != 2:
        raise SystemExit(f"level {level}: approach_value(&hero + 0x458) found {len(appr)} times, expected 2: {[hex(a) for a in appr]}")

    # hero_set_state: the function most often called as f(const < 0x83, 0 or 1).
    calls = Counter()
    for k, ins in enumerate(w):
        if ins >> 26 != 3:
            continue
        a0 = a1 = None
        for j in range(1, 6):
            p = w[k - j]
            if p >> 26 == 9 and ((p >> 21) & 31) == 0:
                rt = (p >> 16) & 31
                if rt == 4 and a0 is None:
                    a0 = p & 0xFFFF
                if rt == 5 and a1 is None:
                    a1 = p & 0xFFFF
        d = w[k + 1]
        if d >> 26 == 9 and ((d >> 21) & 31) == 0:
            rt = (d >> 16) & 31
            if rt == 4:
                a0 = d & 0xFFFF
            if rt == 5:
                a1 = d & 0xFFFF
        if a0 is not None and a0 < STATE_COUNT and a1 in (0, 1):
            calls[(ins & 0x3FFFFFF) << 2] += 1
    # Of the most-called candidates, hero_set_state is the one that stores the
    # new state at hero + 0x2084 within its first 0x300 bytes.
    def stores_state(fn):
        if not (tbase <= fn < tbase + len(text)):
            return False
        k0 = (fn - tbase) // 4
        return any(w[k] >> 26 == 0x2B and (w[k] & 0xFFFF) == 0x2084 for k in range(k0, min(k0 + 0xC0, n)))
    picks = [(fn, c) for fn, c in calls.most_common(4) if stores_state(fn)]
    if len(picks) != 1:
        raise SystemExit(f"level {level}: hero_set_state ambiguous: {[(hex(f), c) for f, c in picks]} of {calls.most_common(4)}")
    set_state, votes = picks[0]
    known = catalogue_place(openrac, "func_L00_00222B80", level)
    if known is not None and known != set_state:
        raise SystemExit(f"level {level}: hero_set_state {set_state:#x} disagrees with the catalogue's {known:#x}")
    inv = {v: k for k, v in helpers.items()}
    return {"comet_site": site, "speed_step": speed_step, "hero_set_state": set_state, "set_state_calls": votes,
            "run_turn_a": run[0], "run_pivot": run[1], "run_turn_b": run[2], "run_planar_a": run[4], "run_planar_b": run[5],
            "turn_to": inv["turn"], "pivot_turn": inv["pivot"], "set_planar_vel": inv["planar"],
            "jump_turn_site": jump[0], "jump_planar_site": jump[1], "air_turn_site": air[0],
            "jump_press_0": press[0], "jump_press_1": press[1], "jump_press_2": press[2], "jump_press_3": press[3],
            "jump_press_fn": jp,
            "twist_0": twist[0], "twist_1": twist[1], "twist_2": twist[2], "twist_3": twist[3], "twist_4": twist[4], "twist_5": twist[5],
            "twist_fn": tw, "runanim_site": runanim[0], "run_anim_fn": ra,
            "flip_appr_0": appr[0], "flip_appr_1": appr[1], "approach_fn": ap}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parent / "OpenRAC")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/modseg")
    ap.add_argument("--print", action="store_true")
    args = ap.parse_args()
    found = {L: find_level(args.openrac, L) for L in range(19)}
    if args.print:
        for L, f in found.items():
            print(f"level {L:2d}: comet speed_step call {f['comet_site']:#x}, speed_step {f['speed_step']:#x}, "
                  f"hero_set_state {f['hero_set_state']:#x} ({f['set_state_calls']} calls)")
    args.out.mkdir(parents=True, exist_ok=True)
    c = ["/* generated by tools/gen_hero.py from each level's own program; see src/comet_strike.c */",
         "typedef unsigned int u32;"]
    names = ("comet_site", "speed_step", "hero_set_state", "run_turn_a", "run_pivot", "run_turn_b",
             "run_planar_a", "run_planar_b", "turn_to", "pivot_turn", "set_planar_vel",
             "jump_turn_site", "jump_planar_site", "air_turn_site", "jump_press_0", "jump_press_1", "jump_press_2", "jump_press_3", "jump_press_fn",
             "twist_0", "twist_1", "twist_2", "twist_3", "twist_4", "twist_5", "twist_fn", "runanim_site", "run_anim_fn",
             "flip_appr_0", "flip_appr_1", "approach_fn")
    for name in names:
        c.append(f"const u32 ee_{name}[19] = {{ {', '.join(f'0x{found[L][name]:08X}' for L in range(19))} }};")
    (args.out / "gen_hero.c").write_text("\n".join(c) + "\n")
    (args.out / "hero_sites.json").write_text(json.dumps({L: {k: f[k] for k in names} for L, f in found.items()}, indent=1))
    print(f"wrote {args.out / 'gen_hero.c'} and hero_sites.json (19 levels)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
