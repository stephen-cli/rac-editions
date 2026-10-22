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
  stop_site      the run update's stops: after the speed test against
                 frame_time * 4.4 and the stick history test against 0.8 (the
                 constants 0x408CCCCD and 0x3F4CCCCD), the bc1f to the shared
                 tail calling hero_set_state(STOP, 1) (the skid) and the
                 fall-through hero_set_state(STOP, 0) (the short stop); the
                 strafe hook stops the hero dead at both while a trigger is held
  comet_site     the Comet-Strike (state 0x15) movement block's call to
                 speed_step: of the jump tables indexed by the hero's state
                 (code entries; a bound of 0x83 where one is in reach), the
                 one whose state 0x15 entry leads, within the block, to a
                 jal to speed_step (func_L00_00212740, shared by all 19)
  speed_step     that call's target
  comet_jump_site  the state update's attack block (states 0x13-0x15 share
                 it; NTSC 00242930.c case 19-21): its pad_pressed_within
                 (func_L00_00267BA8) call, Cross within the window after the
                 attack record's f14 frame -> jump; of the 0x83-entry state
                 tables, the one whose entries 0x13-0x15 are one block that
                 indexes the attack records (addiu $a0, 0x2C, then the
                 table's lui/addiu) and makes that call. src/comet_strike.c
                 gives the Comet-Strike R&C2's window there
  attack_records that table (0x2C-byte records; level data), checked by the
                 Comet-Strike record 3's fC/f14/f18 (83, 87, 91);
                 tools/overlay_patch.py writes record 3's f14 (hero_sites.json only)
  arm_*          the gun-arm layer manager's sites (func_L00_0020E3B8, shared
                 code, so at fixed offsets, each checked): its identity call on
                 Ratchet's current sequence, its two calls of the body-to-arm
                 mapper (func_L00_0020DC50) and its two "arm sequence < 0x17"
                 tests; src/gun_arm.c sends the backpedal to the arm classes'
                 24th sequence, R&C2's backpedal arm
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
PAD_PRESSED_WITHIN = "func_L00_00267BA8"
ARM_MANAGER = "func_L00_0020E3B8"   # the gun-arm layer manager (NTSC FUN_L00_0020dd20), shared by all 19
ARM_MAPPER = "func_L00_0020DC50"    # its body-to-arm sequence mapper (0xFF -> moby + 0xA5, else the id)
# the manager's sites, at fixed offsets in the shared code: (name, offset, word or None for a jal, delay slot)
ARM_SITES = [("arm_ident_site", 0x29C, None, 0x90640053),   # identity(moby[0x53]): lbu $a0, 0x53($v1)
             ("arm_map_0", 0x3E0, None, 0x90850053),        # mapper(moby, moby[0x53]): lbu $a1, 0x53($a0)
             ("arm_check_0", 0x478, 0x2C420017, None),      # sltiu $v0, $v0, 0x17: the arm sequence test
             ("arm_map_1", 0x4F0, None, 0x90450052),        # mapper(moby, moby[0x52 + j]): lbu $a1, 0x52($v0)
             ("arm_check_1", 0x5B8, 0x2C420017, None)]
RUN_ORDER = ["turn", "pivot", "turn", "speed", "planar", "planar"]
STATE_JUMP = 7
COMET_RECORD = 3                    # hero.attack_record in state 0x15 (hero_set_state case 0x15)
COMET_RECORD_FRAMES = [83, 87, 91]  # its fC, f14, f18 in the retail levels


def records(pal: Path, level: int):
    m = json.loads((pal / f"baserom/overlays/level_{level:02d}/manifest.json").read_text())
    out = {}
    for rec in m["records"]:
        if rec["name"] in ("text", "data", "lit"):
            out[rec["name"]] = (rec["address"],
                                (pal / f"baserom/overlays/level_{level:02d}/{rec['name']}.bin").read_bytes())
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
    tables = set()
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
        tables.add(tbl)
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
                    raise SystemExit(f"level {level}: the RUN movement block's calls are {[k for _, k in hits]}, "
                                     f"expected {RUN_ORDER}")
                run = [b for b, _ in hits]
                # The same table's JUMP entry (shared by the jump family): its first turn_to and first set_planar_vel.
                e7 = word_at(tbl + 4 * STATE_JUMP)
                found = {}
                for b in range(e7, e7 + 0x700, 4):
                    x = w[(b - tbase) // 4]
                    if (x >> 26 == 3 and helpers.get((x & 0x3FFFFFF) << 2) in ("turn", "planar")
                            and helpers[(x & 0x3FFFFFF) << 2] not in found):
                        found[helpers[(x & 0x3FFFFFF) << 2]] = b
                    if len(found) == 2:
                        break
                if sorted(found) != ["planar", "turn"] or not found["turn"] < found["planar"]:
                    raise SystemExit(f"level {level}: the jump block's turn_to / set_planar_vel "
                                     f"not found in order: {found}")
                jump = (found["turn"], found["planar"])
                break
    if site is None:
        raise SystemExit(f"level {level}: the Comet-Strike movement block's speed_step call not found")

    # The state update's attack block: entries 0x13-0x15 of one state table are one block
    # that indexes the attack records and then calls pad_pressed_within(Cross, window, 0).
    jal_ppw = 0x0C000000 | ((catalogue_place(openrac, PAD_PRESSED_WITHIN, level) >> 2) & 0x3FFFFFF)
    attack = set()
    for tbl in tables:
        e = [word_at(tbl + 4 * s) for s in (0x13, 0x14, STATE_COMET)]
        if None in e or len(set(e)) != 1 or not (tbase <= e[0] < tbase + len(text)):
            continue
        k0 = (e[0] - tbase) // 4
        for a in range(k0, min(k0 + 0xC0, n)):
            if w[a] != 0x2404002C:          # addiu $a0, $zero, 0x2C: the record size
                continue
            records_at = None
            for b in range(a + 1, a + 8):   # the table's lui, then its addiu
                if w[b] >> 26 == 0x0F:
                    rt = (w[b] >> 16) & 31
                    for c in range(b + 1, b + 4):
                        if w[c] >> 26 == 9 and ((w[c] >> 21) & 31) == rt and ((w[c] >> 16) & 31) == rt:
                            lo = w[c] & 0xFFFF
                            records_at = ((w[b] & 0xFFFF) << 16) + (lo - 0x10000 if lo & 0x8000 else lo)
                    break
            if records_at is None:
                continue
            call = next((tbase + 4 * c for c in range(a, min(a + 0x40, n)) if w[c] == jal_ppw), None)
            if call is not None:
                attack.add((call, records_at))
                break
    if len(attack) != 1:
        raise SystemExit(f"level {level}: the state update's attack block found {len(attack)} times: "
                         f"{[(hex(c), hex(r)) for c, r in attack]}")
    comet_jump_site, attack_records = attack.pop()
    if w[(comet_jump_site - tbase) // 4 - 1] != 0x24040040:     # addiu $a0, $zero, 0x40 (Cross)
        raise SystemExit(f"level {level}: the attack block's pad_pressed_within at {comet_jump_site:#x} "
                         "does not test Cross")
    frames = [word_at(attack_records + COMET_RECORD * 0x2C + o) for o in (0xC, 0x14, 0x18)]
    if frames != COMET_RECORD_FRAMES:
        raise SystemExit(f"level {level}: attack record {COMET_RECORD} at {attack_records:#x} has fC/f14/f18 {frames}, "
                         f"expected {COMET_RECORD_FRAMES}")

    # Air control: the turn_to whose rate is hero + 0x414 (lwc1 0x414 within 8 words before the jal).
    air = [tbase + 4 * j for k, x in enumerate(w) if x >> 26 == 0x31 and (x & 0xFFFF) == 0x414
           for j in range(k + 1, min(k + 9, n)) if w[j] == jal_turn_to]
    if len(air) != 1:
        raise SystemExit(f"level {level}: the air control's turn_to (rate hero + 0x414) found {len(air)} times: "
                         f"{[hex(a) for a in air]}")
    # The jump-press handler's call sites.
    jp = catalogue_place(openrac, JUMP_PRESS, level)
    jal_jp = 0x0C000000 | ((jp >> 2) & 0x3FFFFFF)
    press = [tbase + 4 * k for k, x in enumerate(w) if x == jal_jp]
    if len(press) != 4:
        raise SystemExit(f"level {level}: {JUMP_PRESS} is called {len(press)} times, expected 4: "
                         f"{[hex(a) for a in press]}")
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
        raise SystemExit(f"level {level}: approach_value(&hero + 0x458) found {len(appr)} times, expected 2: "
                         f"{[hex(a) for a in appr]}")

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
        raise SystemExit(f"level {level}: hero_set_state ambiguous: {[(hex(f), c) for f, c in picks]} "
                         f"of {calls.most_common(4)}")
    set_state, votes = picks[0]
    known = catalogue_place(openrac, "func_L00_00222B80", level)
    if known is not None and known != set_state:
        raise SystemExit(f"level {level}: hero_set_state {set_state:#x} disagrees with the catalogue's {known:#x}")
    # The run update's high-speed stop: speed constants, then the bc1f whose target calls set_state.
    jal_set_state = 0x0C000000 | ((set_state >> 2) & 0x3FFFFFF)
    stops = []
    for k in range(n - 40):
        if w[k] == 0x3C01408C and w[k + 1] == 0x3421CCCD and any(w[j] == 0x3C013F4C for j in range(k, k + 20)):
            j0 = next(j for j in range(k, k + 20) if w[j] == 0x3C013F4C)
            for j in range(j0, j0 + 12):
                if (w[j] >> 16) == 0x4500:          # bc1f
                    off = w[j] & 0xFFFF
                    off = off - 0x10000 if off & 0x8000 else off
                    target = tbase + 4 * j + 4 + (off << 2)
                    if (tbase <= target < tbase + len(text) and w[(target - tbase) // 4] == jal_set_state
                            and w[j + 1] == 0x24040003 and w[(target - tbase) // 4 + 1] == 0x24050001
                            and w[j + 2] == jal_set_state and w[j + 3] == 0x0000282D):
                        stops.append((target, tbase + 4 * (j + 2)))   # (STOP, 1) tail; (STOP, 0) fall-through
                    break
    if len(stops) != 1:
        raise SystemExit(f"level {level}: the run update's high-speed stop found {len(stops)} times: "
                         f"{[hex(a) for a in stops]}")
    # The gun-arm layer manager: its identity call on Ratchet's current sequence, its two
    # mapper calls and its two "arm sequence < 0x17" tests (src/gun_arm.c).
    mgr = catalogue_place(openrac, ARM_MANAGER, level)
    mapper = catalogue_place(openrac, ARM_MAPPER, level)
    if mgr is None or mapper is None:
        raise SystemExit(f"level {level}: {ARM_MANAGER} or {ARM_MAPPER} has no place")
    arm = {}
    for name, off, word, delay in ARM_SITES:
        a = mgr + off
        x, d = w[(a - tbase) // 4], w[(a - tbase) // 4 + 1]
        if word is not None:
            ok = x == word
        else:
            ok = x >> 26 == 3 and d == delay and (name == "arm_ident_site" or (x & 0x3FFFFFF) << 2 == mapper)
        if not ok:
            raise SystemExit(f"level {level}: the arm manager's {name} at {a:#x} is {x:#010x} {d:#010x}")
        arm[name] = a
    arm["arm_mapper"] = mapper
    arm["arm_ident_fn"] = (w[(arm["arm_ident_site"] - tbase) // 4] & 0x3FFFFFF) << 2
    inv = {v: k for k, v in helpers.items()}
    return {"comet_site": site, "speed_step": speed_step, "hero_set_state": set_state, "set_state_calls": votes,
            "run_turn_a": run[0], "run_pivot": run[1], "run_turn_b": run[2],
            "run_planar_a": run[4], "run_planar_b": run[5],
            "turn_to": inv["turn"], "pivot_turn": inv["pivot"], "set_planar_vel": inv["planar"],
            "jump_turn_site": jump[0], "jump_planar_site": jump[1], "air_turn_site": air[0],
            "jump_press_0": press[0], "jump_press_1": press[1], "jump_press_2": press[2], "jump_press_3": press[3],
            "jump_press_fn": jp,
            "twist_0": twist[0], "twist_1": twist[1], "twist_2": twist[2],
            "twist_3": twist[3], "twist_4": twist[4], "twist_5": twist[5],
            "twist_fn": tw, "runanim_site": runanim[0], "run_anim_fn": ra,
            "flip_appr_0": appr[0], "flip_appr_1": appr[1], "approach_fn": ap,
            "stop_site": stops[0][0], "stop_site_low": stops[0][1],
            "comet_jump_site": comet_jump_site, "attack_records": attack_records, **arm}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/modseg")
    ap.add_argument("--print", action="store_true")
    args = ap.parse_args()
    found = {L: find_level(args.openrac, L) for L in range(19)}
    if args.print:
        for L, f in found.items():
            print(f"level {L:2d}: comet speed_step call {f['comet_site']:#x}, speed_step {f['speed_step']:#x}, "
                  f"hero_set_state {f['hero_set_state']:#x} ({f['set_state_calls']} calls), "
                  f"attack jump test {f['comet_jump_site']:#x}, attack records {f['attack_records']:#x}")
    args.out.mkdir(parents=True, exist_ok=True)
    c = ["/* generated by tools/gen_hero.py from each level's own program; see src/comet_strike.c */",
         "typedef unsigned int u32;"]
    names = ("comet_site", "speed_step", "hero_set_state", "run_turn_a", "run_pivot", "run_turn_b",
             "run_planar_a", "run_planar_b", "turn_to", "pivot_turn", "set_planar_vel",
             "jump_turn_site", "jump_planar_site", "air_turn_site",
             "jump_press_0", "jump_press_1", "jump_press_2", "jump_press_3", "jump_press_fn",
             "twist_0", "twist_1", "twist_2", "twist_3", "twist_4", "twist_5", "twist_fn",
             "runanim_site", "run_anim_fn",
             "flip_appr_0", "flip_appr_1", "approach_fn", "stop_site", "stop_site_low", "comet_jump_site",
             "arm_ident_site", "arm_ident_fn", "arm_map_0", "arm_map_1", "arm_mapper", "arm_check_0", "arm_check_1")
    for name in names:
        c.append(f"const u32 ee_{name}[19] = {{ {', '.join(f'0x{found[L][name]:08X}' for L in range(19))} }};")
    (args.out / "gen_hero.c").write_text("\n".join(c) + "\n")
    (args.out / "hero_sites.json").write_text(json.dumps({L: {k: f[k] for k in names + ("attack_records",)}
                                                          for L, f in found.items()}, indent=1))
    print(f"wrote {args.out / 'gen_hero.c'} and hero_sites.json (19 levels)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
