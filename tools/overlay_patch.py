#!/usr/bin/env python3
"""
Level-code patches for the Executive Edition, written as disc sectors.

Gameplay code lives in the 19 level programs (overlays), not in the boot
executable, and each level has its own copy of the shared functions at its
own addresses. OpenRAC's catalogue (games/rac1/config/overlays/
functions.tsv) gives every function's address in every level, and the level
code is stored raw on the disc, so a patch is the same words written at 19
places, with every address resolved per level.

Each feature here is a list of word patches and, where more than a word or
two is needed, a shim placed in a code cave: one of the linker remnants the
catalogue lists (config/overlays/linker_remnants.txt), dead words retail's
linker left of stripped functions, which nothing reaches.

Output is the editor's patch format, which tools/iso_patch.py in OpenRAC's
games/rac1 applies: <out>/patches.tsv listing first sector, sector
count and a file of those sectors with the patch applied.

Usage (from anywhere):
  python tools/overlay_patch.py [--openrac DIR] [-o build/patches] [--dry-run] [FEATURE ...]
"""
import argparse
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SECTOR = 2048

# --- MIPS encoders ------------------------------------------------------------

AT, ZERO, A4, T2 = 1, 0, 8, 10


def lui(rt, imm16):
    return 0x3C000000 | (rt << 16) | (imm16 & 0xFFFF)


def lw(rt, base, imm16):
    return 0x8C000000 | (base << 21) | (rt << 16) | (imm16 & 0xFFFF)


def addiu(rt, rs, imm16):
    return 0x24000000 | (rs << 21) | (rt << 16) | (imm16 & 0xFFFF)


def movn(rd, rs, rt):  # rd = rs if rt != 0
    return (rs << 21) | (rt << 16) | (rd << 11) | 0x0B


def j(target):
    return 0x08000000 | ((target >> 2) & 0x3FFFFFF)


def jal(target):
    return 0x0C000000 | ((target >> 2) & 0x3FFFFFF)


NOP = 0


def hi_lo(addr):
    """%hi/%lo of ADDR as the assembler splits them (the low half is signed)."""
    lo = addr & 0xFFFF
    hi = (addr >> 16) + (1 if lo & 0x8000 else 0)
    return hi, lo


# --- Features -----------------------------------------------------------------

# Game state the executable keeps resident. US addresses are from ReRAC's
# game_state.md; PAL is +0x100 for this block (bolts: US 0x15ED98, PAL
# 0x15EE98), and PAL func_00227DB0 (the challenge-mode reset, US 0x226B08)
# increments this word.
COMPLETES = 0x0015EF20      # s32: times the game was completed; > 0 in a challenge-mode run


def metal_detector_10x(at):
    """Metal Detector pays 10x in challenge mode.

    The detector's update function (class 585, func_L00_002D4398) drains the
    hidden spot's bolt count in steps of 5, spawning one bolt per step with
    func_L00_002A86D8(target, pos, dir, 0x13, value=5, 0). The spawner only
    knows the denominations 1-5, 20 and 50, each with its own model. The
    shim turns value 5 into 50 (circlenut bolts) when completes > 0, so the
    payout is exactly 10x with the same number of spawns.
    """
    cave = at("func_L00_001F3DF0")                 # 44-byte linker remnant, in every level
    spawn = at("func_L00_002A86D8")
    site = at("func_L00_002D4398") + (0x2D49D0 - 0x2D4398)   # the jal in the drain loop
    hi, lo = hi_lo(COMPLETES)
    shim = [
        lui(AT, hi),
        lw(AT, AT, lo),              # $at = completes
        addiu(T2, ZERO, 50),
        movn(A4, T2, AT),            # value = 50 if completes != 0
        j(spawn),                    # tail call; $ra still returns into the caller
        NOP,
    ]
    return [
        (site, jal(spawn), jal(cave)),
        *[(cave + 4 * i, None, w) for i, w in enumerate(shim)],
    ]


def site_offset(name: str) -> int:
    """A hooked call site's offset in its function, from src/modseg.h (EE_SITE_*),
    the one definition the C hooks and this tool share."""
    text = (HERE.parent / "src/modseg.h").read_text()
    m = re.search(rf"#define\s+{name}\s+(0x[0-9A-Fa-f]+)", text)
    if not m:
        raise SystemExit(f"{name} is not defined in src/modseg.h")
    return int(m.group(1), 16)


def modseg_symbol(name: str) -> int:
    """Address of an exported mod-segment function (tools/modseg.py's modseg.sym)."""
    path = HERE.parent / "build/modseg/modseg.sym"
    if not path.is_file():
        raise SystemExit(f"missing {path}: run tools/modseg.py first")
    for line in path.read_text().splitlines():
        n, a = line.split("	")
        if n == name:
            return int(a, 16)
    raise SystemExit(f"{name} is not exported by the mod segment")


def metal_detector_10x_modseg(at):
    """The same feature with its logic in C in the resident mod segment
    (src/metal_detector.c): the detector's spawn call goes to
    ee_metal_detector_spawn (src/hooks.s), which calls the level's spawner
    through the generated level table. No cave needed."""
    spawn = at("func_L00_002A86D8")
    site = at("func_L00_002D4398") + site_offset("EE_SITE_DETECTOR_SPAWN")
    return [(site, jal(spawn), jal(modseg_symbol("ee_metal_detector_spawn")))]


def quick_select_pause(at):
    """q3: pause while quick select is open, with a toggle under Options
    (src/quick_select_pause.c). The frame's HudUpdate call goes to the hook
    (src/hooks.s), and the Options list widget's item pointer (executable
    data, the same in every level's data record) points at the rows in the
    mod segment."""
    import json
    hud = at("func_L00_0023B610")
    site = at("func_L00_00299250") + site_offset("EE_SITE_HUD_UPDATE")
    menu = HERE.parent / "build/modseg/menu_sites.json"
    if not menu.is_file():
        raise SystemExit(f"missing {menu}: run tools/gen_menu.py first")
    m = json.loads(menu.read_text())[str(at.level)]
    return [(site, jal(hud), jal(modseg_symbol("ee_hud_update_hook")))] + [
        (m[k]["site"], m[k]["expect"], modseg_symbol(m[k]["symbol"])) for k in ("items", "desc", "image")]


def edition_text(at):
    """The edition's strings (src/text_hook.s, src/text.c): every level's
    msg_string (func_001FE540) jumps to ee_msg_string_hook from its first
    instruction, with the level's id in $at from the delay slot (the hook
    must not read the game's current-level variable: see src/modseg.h);
    the hook re-executes the two replaced prologue words for ids that are
    not the edition's and continues at that level's msg_string + 8."""
    f = at("func_001FE540")
    target = modseg_symbol("ee_msg_string_hook")
    return [(f, 0x27BDFFF0, j(target)), (f + 4, 0x7FBF0000, addiu(AT, ZERO, at.level))]



def comet_strike_run(at):
    """x3b: the Comet-Strike can be cancelled into a run once the wrench has
    left the hand, as in Going Commando (src/comet_strike.c). The throw state's
    movement block calls speed_step every frame; that call goes to the hook
    (src/hooks.s), which calls speed_step and then applies R&C2's rule. The
    hero's code is built differently in groups of levels, so the site comes
    from tools/gen_hero.py's search of each level's program."""
    import json
    hero = HERE.parent / "build/modseg/hero_sites.json"
    if not hero.is_file():
        raise SystemExit(f"missing {hero}: run tools/gen_hero.py first")
    h = json.loads(hero.read_text())[str(at.level)]
    return [(h["comet_site"], jal(h["speed_step"]), jal(modseg_symbol("ee_comet_speed_step")))]


def strafe(at):
    """x5 (first increment): L2/R2 strafe (src/strafe.c). The pad begin-frame's
    parse call goes to a hook that records the triggers and hides them from
    the game; the RUN movement block's two turn_to calls, its pivot turn and
    its two set_planar_vel calls go to hooks that, while strafing, face the
    camera and move along the stick; the jump block's turn and planar calls
    and the air control's turn do the same in the air, and the jump-press
    handler's four calls go to a hook that turns a strafe jump into R&C1's
    own flips (state 0xB) as R&C2 does. Sites from tools/gen_hero.py."""
    import json
    hero = HERE.parent / "build/modseg/hero_sites.json"
    if not hero.is_file():
        raise SystemExit(f"missing {hero}: run tools/gen_hero.py first")
    h = json.loads(hero.read_text())[str(at.level)]
    pad_site = at("func_00217F68") + 0x1C4
    return [(pad_site, jal(at("func_002181F0")), jal(modseg_symbol("ee_pad_parse"))),
            (h["run_turn_a"], jal(h["turn_to"]), jal(modseg_symbol("ee_strafe_turn"))),
            (h["run_turn_b"], jal(h["turn_to"]), jal(modseg_symbol("ee_strafe_turn"))),
            (h["run_pivot"], jal(h["pivot_turn"]), jal(modseg_symbol("ee_strafe_pivot"))),
            (h["run_planar_a"], jal(h["set_planar_vel"]), jal(modseg_symbol("ee_strafe_planar"))),
            (h["run_planar_b"], jal(h["set_planar_vel"]), jal(modseg_symbol("ee_strafe_planar"))),
            # jumps and flips: the jump block's first turn and planar call, the air control's turn,
            # and the four calls of the jump-press handler
            (h["jump_turn_site"], jal(h["turn_to"]), jal(modseg_symbol("ee_jump_turn"))),
            (h["jump_planar_site"], jal(h["set_planar_vel"]), jal(modseg_symbol("ee_jump_planar"))),
            (h["air_turn_site"], jal(h["turn_to"]), jal(modseg_symbol("ee_air_turn")))] + [
            (h[f"jump_press_{k}"], jal(h["jump_press_fn"]), jal(modseg_symbol("ee_jump_press"))) for k in range(4)] + [
            # the twist (six lean helper calls), the backpedal (the run animation updater's call),
            # the strafe flip (the flip's two approach calls)
            (h[f"twist_{k}"], jal(h["twist_fn"]), jal(modseg_symbol("ee_twist"))) for k in range(6)] + [
            (h["runanim_site"], jal(h["run_anim_fn"]), jal(modseg_symbol("ee_run_anim"))),
            (h["flip_appr_0"], jal(h["approach_fn"]), jal(modseg_symbol("ee_flip_approach"))),
            (h["flip_appr_1"], jal(h["approach_fn"]), jal(modseg_symbol("ee_flip_approach"))),
            # the run update's high-speed stop: the short stop while strafing
            (h["stop_site"], jal(h["hero_set_state"]), jal(modseg_symbol("ee_stop"))),
            (h["stop_site_low"], jal(h["hero_set_state"]), jal(modseg_symbol("ee_stop")))]


def strafe_camera(at):
    """x5 (camera): the follow camera holds its yaw while strafing, as in
    Going Commando (src/strafe.c). R&C1's per-frame follow camera overrides
    (func_L00_002E74B0) already carry R&C2's strafe camera block for the
    thruster pack's hover, gated on `hero state == 0x81 && pad raw held & 3`
    (L2 | R2):

        addiu $a0, $zero, 0x81
        lw    $v1, 0x2084(hero)
        bne   $v1, $a0, skip          -> nop
        lw    $v0, D_1D94($gp)
        lui   $v1, %hi(pad + 0x1A0)   -> lui $v1, %hi(ee_strafe_held)
        lw    $v0, %lo(pad + 0x1A0)($v1) -> lw $v0, %lo(ee_strafe_held)($v1)
        andi  $v0, $v0, 3
        beqz  $v0, skip

    so the block (auto-turn off, slide, lateral spring 0.04/0.2, timers
    cleared, return) runs whenever a trigger is held: in the hover as in
    retail, and while strafing on foot. The sequence is found in each
    level's own build of the function."""
    fn = at("func_L00_002E74B0")
    w = at.words(fn, at.size("func_L00_002E74B0") // 4)
    hits = [i for i in range(len(w) - 7)
            if w[i] == 0x24040081 and (w[i + 1] & 0xFC1FFFFF) == 0x8C032084 and (w[i + 2] >> 16) == 0x1464
            and w[i + 4] == 0x3C030014 and w[i + 5] == 0x8C62CBE0 and w[i + 6] == 0x30420003
            and (w[i + 7] >> 16) == 0x1040]
    if len(hits) != 1:
        raise SystemExit(f"level {at.level}: the hover strafe-camera test in func_L00_002E74B0 found {len(hits)} times")
    i = hits[0]
    hi, lo = hi_lo(modseg_symbol("ee_strafe_held"))
    return [(fn + 4 * (i + 2), w[i + 2], NOP),
            (fn + 4 * (i + 4), 0x3C030014, lui(3, hi)),
            (fn + 4 * (i + 5), 0x8C62CBE0, lw(2, 3, lo))]


def commercials(at):
    """The Goodies menu's Commercials row, as the NTSC release shows it.

    Both discs carry the row (label 0x4FCC, after 'Making Of'), its page
    (three rows that play the Decoy Glove, Devastator and Morph-o-ray ads
    through the same movie action as 'Making Of'), its preview images and
    the three commercial movies (the PAL table of contents even has the PAL
    encodes, picked by the region flag like 'Making Of'). PAL's Goodies
    unlock routine (func_00228160, every level) only differs from NTSC's in
    its last step: when the region flag D_0015EF90 is set it clears the
    row's label halfword, which ends the list before the row. The patch
    makes that store a no-op, so the row appears, locked until all ten Gold
    Weapons are bought and unlocked with 'Making Of' as the routine already
    arranges."""
    f = at("func_00228160")
    site = f + (0x286050 - 0x285F68)          # sh $zero, 0x60($v0): rows[8].label = 0
    return [(site, 0xA4400060, NOP)]


def charge_boots(at):
    """x2, first step: the Charge Boots item (src/charge_boots.c).

    Per level (tools/gen_items.py's items_sites.json): item record 36, the
    unused Devastator copy, rewritten as a Grindboots copy with its own
    name, description and icon; the boots grid's cell pointer sent to a
    three-cell copy in the mod segment, and its column count made 3.
    The level loader's HUD header copy (func_002032D0 + 0xB8, jal
    FastMemCopy) goes to ee_hud_header_copy, which appends the icon to the
    header's tables in the resident segment (src/hud_icons.c). The item
    preview's placement table (36 entries, read by lui/addiu pairs in
    func_0021F238 and func_0021F6A0) is replaced by a 37-entry copy in the
    segment, so item 36 gets the Grindboots placement instead of the words
    after the table."""
    import json
    sites = HERE.parent / "build/modseg/items_sites.json"
    if not sites.is_file():
        raise SystemExit(f"missing {sites}: run tools/gen_items.py first")
    m = json.loads(sites.read_text())[str(at.level)]
    out = [(m["cells"]["site"], m["cells"]["expect"], modseg_symbol(m["cells"]["symbol"])),
           (m["cols"]["site"], m["cols"]["expect"], m["cols"]["value"]),
           (m["panel"]["site"], m["panel"]["expect"], m["panel"]["value"])] + [tuple(w) for w in m["record"]]
    loader = at("func_002032D0") + site_offset("EE_SITE_HUD_HEADER_COPY")
    out.append((loader, jal(at("func_001F9A98")), jal(modseg_symbol("ee_hud_header_copy"))))
    link = at("func_002032D0") + site_offset("EE_SITE_HUD_LINK_BANK")
    out.append((link, jal(at("func_001FF7F0")), jal(modseg_symbol("ee_hud_link_bank"))))
    hi, lo = hi_lo(modseg_symbol("ee_preview_placements"))
    for fn, luis, addius in (("func_0021F238", (552, 576), (560, 584, 604)), ("func_0021F6A0", (80, 108), (88, 116, 140))):
        f = at(fn)
        for o in luis:
            old, = at.words(f + o, 1)
            if (old >> 26) != 0x0F:
                raise SystemExit(f"level {at.level}: {fn} + {o} is not a lui")
            out.append((f + o, old, (old & 0xFFFF0000) | hi))
        for o in addius:
            old, = at.words(f + o, 1)
            if (old >> 26) != 0x09:
                raise SystemExit(f"level {at.level}: {fn} + {o} is not an addiu")
            out.append((f + o, old, (old & 0xFFFF0000) | lo))
    # The worn boots' pose: the boots predicate's slot_item call answers 36 as the
    # Magneboots, so the item updater poses them from Ratchet's leg (src/charge_boots.c).
    out.append((at("func_L00_0020DBD8") + 8, jal(at("func_L00_0020DB30")), jal(modseg_symbol("ee_boots_pose_item_hook"))))
    return out + charge_dash(at)


def charge_dash(at):
    """x2: the Charge Boots dash on R1's double tap, Going Commando's hero
    state 0x7B, R&C1's state 0x48 (src/charge_dash.c), part of the
    charge_boots feature. The input handler's Thruster-Pack test (its
    slot_item(3) call) goes to
    ee_charge_trigger_hook, which takes the double tap for the boots when
    they are worn, the call of the per-state movement function goes to
    ee_charge_move_hook, which moves the hero while it dashes, and the
    follow camera overrides' collect_mobies_in_sphere call goes to
    ee_charge_camera_hook, which adds R&C2's charge camera. Sites from
    tools/gen_charge.py."""
    import json
    sites = HERE.parent / "build/modseg/charge_sites.json"
    if not sites.is_file():
        raise SystemExit(f"missing {sites}: run tools/gen_charge.py first")
    c = json.loads(sites.read_text())[str(at.level)]
    return [(c["trigger"], jal(c["slot_item"]), jal(modseg_symbol("ee_charge_trigger_hook"))),
            (c["move_site"], jal(c["move_fn"]), jal(modseg_symbol("ee_charge_move_hook"))),
            (c["camera_site"], jal(c["collect"]), jal(modseg_symbol("ee_charge_camera_hook")))]


FEATURES = {
    "charge_boots": charge_boots,
    "edition_text": edition_text,
    "metal_detector_10x": metal_detector_10x,
    "metal_detector_10x_modseg": metal_detector_10x_modseg,
    "quick_select_pause": quick_select_pause,
    "commercials": commercials,
    "comet_strike_run": comet_strike_run,
    "strafe": strafe,
    "strafe_camera": strafe_camera,
}
DEFAULT_FEATURES = ["edition_text", "metal_detector_10x_modseg", "quick_select_pause", "commercials", "comet_strike_run", "strafe", "strafe_camera", "charge_boots"]


# --- Disc plumbing ------------------------------------------------------------

SIZES: dict[str, int] = {}


def load_catalogue(openrac: Path) -> dict[str, dict[int, int]]:
    """name -> level -> address; the sizes go to SIZES."""
    places = {}
    path = openrac / "games/rac1/config/overlays/functions.tsv"
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        name, _kind, size, _fp, _levels, pl = line.split("\t")[:6]
        places[name] = {int(p.split(":")[0]): int(p.split(":")[1], 16) for p in pl.split(",")}
        SIZES[name] = int(size)
    return places


TOC_LBA, LEVEL_TABLE = 1500, 0x28C8      # table of contents; its level table: {header LBA, sectors} x 19


def level_data_ranges(iso: Path) -> dict[int, tuple[int, int]]:
    """level id -> (first sector, sectors) of its data archive, read through the table of
    contents and each level header (+0x08). Works on the retail image and on wrench-built
    ones, whose directory layout the editor's Disc reader does not accept."""
    out = {}
    with iso.open("rb") as f:
        f.seek(TOC_LBA * SECTOR)
        toc = f.read(6 * SECTOR)
        for level in range(19):
            header, _ = struct.unpack_from("<2I", toc, LEVEL_TABLE + 8 * level)
            f.seek(header * SECTOR + 8)
            out[level] = struct.unpack("<2I", f.read(8))
    return out


def level_text_locations(openrac: Path, iso: Path | None = None):
    """level id -> list of (first byte of a record in the image, its load address, its size),
    text record first, then the level's data record (the executable's data, menus included,
    is part of every level's data record at the same addresses)."""
    sys.path.insert(0, str(openrac / "editor"))
    from formats import unpack, span, overlay_sections
    from level import SECTION_NAMES
    iso = iso or openrac / "games/rac1/baserom/SCES_509.16.iso"
    out = {}
    with iso.open("rb") as f:
        for level, (lba, sectors) in level_data_ranges(iso).items():
            f.seek(lba * SECTOR)
            data = f.read(sectors * SECTOR)
            off, size = unpack("<ii", data, SECTION_NAMES.index("overlay") * 8)
            secs = overlay_sections(span(data, off, size))["sections"]
            out[level] = [(lba * SECTOR + off + rec["offset"], rec["address"], rec["bytes"])
                          for rec in (secs[-1], secs[2])]
    return iso, out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("features", nargs="*", default=DEFAULT_FEATURES)
    ap.add_argument("--openrac", type=Path, default=HERE.parent.parent / "OpenRAC")
    ap.add_argument("-o", "--output", type=Path, default=HERE.parent / "build/patches")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--iso", type=Path, default=None,
                    help="image whose level code to patch (default: the retail image); e.g. a wrench-built one")
    args = ap.parse_args()

    for f in args.features:
        if f not in FEATURES:
            print(f"unknown feature {f}; known: {', '.join(FEATURES)}", file=sys.stderr)
            return 2
    places = load_catalogue(args.openrac)
    iso, texts = level_text_locations(args.openrac, args.iso)

    sectors: dict[int, bytearray] = {}
    with iso.open("rb") as f:
        def sector(n):
            if n not in sectors:
                f.seek(n * SECTOR)
                sectors[n] = bytearray(f.read(SECTOR))
            return sectors[n]

        total = 0
        for level in range(19):
            records = texts[level]

            def at(name, level=level):
                try:
                    return places[name][level]
                except KeyError:
                    raise SystemExit(f"{name} has no place in level {level}")
            at.level = level
            at.size = SIZES.__getitem__

            def file_offset(addr):
                for file_base, rec_addr, rec_size in records:
                    if rec_addr <= addr < rec_addr + rec_size:
                        return file_base + (addr - rec_addr)
                raise SystemExit(f"level {level}: 0x{addr:X} is in neither the text nor the data record")

            def words(addr, n):
                """N words of this level's program at ADDR, as patched so far (for features that find their sites)."""
                out = []
                for k in range(n):
                    s, i = divmod(file_offset(addr + 4 * k), SECTOR)
                    out.append(struct.unpack_from("<I", sector(s), i)[0])
                return out
            at.words = words

            for feature in args.features:
                for addr, expect, word in FEATURES[feature](at):
                    off = file_offset(addr)
                    s, i = divmod(off, SECTOR)
                    buf = sector(s)
                    old, = struct.unpack_from("<I", buf, i)
                    if expect is not None and old != expect:
                        raise SystemExit(f"level {level} {feature}: at 0x{addr:X} found {old:08X}, expected {expect:08X}; "
                                         f"is this the PAL v2.00 image, unpatched?")
                    struct.pack_into("<I", buf, i, word)
                    total += 1
        print(f"{total} words patched across {len(sectors)} sectors in 19 levels "
              f"({', '.join(args.features)})")

    if args.dry_run:
        return 0
    args.output.mkdir(parents=True, exist_ok=True)
    runs = []
    for n in sorted(sectors):
        if runs and runs[-1][0] + runs[-1][1] == n:
            runs[-1][1] += 1
        else:
            runs.append([n, 1])
    lines = ["# first sector\tsectors\tfile (beside this list)"]
    for first, count in runs:
        name = f"sectors_{first}.bin"
        (args.output / name).write_bytes(b"".join(sectors[first + k] for k in range(count)))
        lines.append(f"{first}\t{count}\t{name}")
    (args.output / "patches.tsv").write_text("\n".join(lines) + "\n")
    print(f"wrote {args.output / 'patches.tsv'} ({len(runs)} sector runs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
