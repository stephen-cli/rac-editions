#!/usr/bin/env python3
"""
Per-level addresses in the hero's code that the catalogue cannot name.

The hero's per-state movement switch, its state update and hero_set_state
are built differently in groups of levels (the catalogue gives the level 0
build, func_L00_002180F8 and func_L00_00222B80, six levels; the others have
their own), so a feature that hooks inside them finds its places from each
level's own program:

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
SPEED_STEP = "func_L00_00212740"


def records(pal: Path, level: int):
    m = json.loads((pal / f"baserom/overlays/level_{level:02d}/manifest.json").read_text())
    out = {}
    for rec in m["records"]:
        if rec["name"] in ("text", "data", "lit"):
            out[rec["name"]] = (rec["address"], (pal / f"baserom/overlays/level_{level:02d}/{rec['name']}.bin").read_bytes())
    return out


def catalogue_place(openrac: Path, name: str, level: int) -> int:
    for line in (openrac / "games/rac1/pal/config/overlays/functions.tsv").read_text().splitlines():
        if line.startswith(name + "\t"):
            return {int(p.split(":")[0]): int(p.split(":")[1], 16) for p in line.split("\t")[5].split(",")}.get(level)
    raise SystemExit(f"{name} is not in the catalogue")


def find_level(openrac: Path, level: int) -> dict:
    pal = openrac / "games/rac1/pal"
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
                break
    if site is None:
        raise SystemExit(f"level {level}: the Comet-Strike movement block's speed_step call not found")

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
    return {"comet_site": site, "speed_step": speed_step, "hero_set_state": set_state, "set_state_calls": votes}


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
    for name in ("comet_site", "speed_step", "hero_set_state"):
        c.append(f"const u32 ee_{name}[19] = {{ {', '.join(f'0x{found[L][name]:08X}' for L in range(19))} }};")
    (args.out / "gen_hero.c").write_text("\n".join(c) + "\n")
    (args.out / "hero_sites.json").write_text(json.dumps({L: {"comet_site": f["comet_site"], "speed_step": f["speed_step"]}
                                                         for L, f in found.items()}, indent=1))
    print(f"wrote {args.out / 'gen_hero.c'} and hero_sites.json (19 levels)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
