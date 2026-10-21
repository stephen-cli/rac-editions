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
EDITOR = HERE.parents[2] / "common/openrac_editor"   # OpenRAC's disc readers, carried (common/deps.py)
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


def quick_select_pages(at):
    """q2: three quick-select pages (src/quick_select_pages.c), at the sites
    tools/gen_qs.py finds per level: the ring's HudShow call loads
    ee_qs_update and ee_qs_draw instead of the game's update and draw
    callbacks; the pause page's ring widget runs the level's stubs; the
    page's prompt widget is the edition's copy with three rows (the retail
    widget is shared with Weapons and Gadgets); the acquisition function's
    empty-slot store becomes a call that tries the other pages; the
    challenge-mode carry-over's reset call goes through ee_qs_carry."""
    import json
    sites = HERE.parent / "build/modseg/qs_sites.json"
    if not sites.is_file():
        raise SystemExit(f"missing {sites}: run tools/gen_qs.py first")
    m = json.loads(sites.read_text())[str(at.level)]
    out = []
    for name, sym in (("update", "ee_qs_update"), ("draw", "ee_qs_draw")):
        hi, lo = hi_lo(modseg_symbol(sym))
        for part, half in (("lui_", hi), ("addiu_", lo)):
            s = m["show"][part + name]
            out.append((s["site"], s["expect"], (s["expect"] & 0xFFFF0000) | half))
    r = m["ring"]
    out.append((r["site"], r["expect_update"], modseg_symbol(f"ee_qs_pause_update_{at.level:02d}")))
    out.append((r["site"] + 4, r["expect_draw"], modseg_symbol(f"ee_qs_pause_draw_{at.level:02d}")))
    out.append((m["prompt"]["site"], m["prompt"]["expect"], modseg_symbol(m["prompt"]["symbol"])))
    a = m["autoslot"]
    words = [jal(modseg_symbol("ee_qs_autoslot")), NOP, NOP, NOP, NOP, NOP]
    out += [(a["site"] + 4 * i, a["expect"][i], w) for i, w in enumerate(words)]
    e = a["loop_exit"]
    out.append((e["site"], e["expect"], e["value"]))      # a full page reaches the hook too, with index 8
    out.append((m["carry"]["site"], m["carry"]["expect"], jal(modseg_symbol("ee_qs_carry"))))
    return out


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


COMET_RECORD = 3                      # hero.attack_record of the Comet-Strike (tools/gen_hero.py checks it)
COMET_F14 = COMET_RECORD * 0x2C + 0x14   # its f14: the frame after which Cross jumps out of the attack
COMET_F14_RETAIL, COMET_F14_RAC2 = 87, 35


def comet_strike_jump(at):
    """x3b: the Comet-Strike can be cancelled into a jump once the wrench has
    left the hand, as in Going Commando (src/comet_strike.c). R&C1's state
    update already has the cancel in the block states 0x13-0x15 share (past
    the attack record's f14 frame, Cross within a window jumps; with R1 held,
    the crouch jumps), but the Comet-Strike's record opens it at frame 87 of
    91. R&C2's record opens it at 35, and its update gives the crouch throw a
    fixed 8-frame window. So record 3's f14 (level data) becomes 35, and the
    block's pad_pressed_within call goes to the hook, which gives state 0x15
    R&C2's window. Sites from tools/gen_hero.py."""
    import json
    hero = HERE.parent / "build/modseg/hero_sites.json"
    if not hero.is_file():
        raise SystemExit(f"missing {hero}: run tools/gen_hero.py first")
    h = json.loads(hero.read_text())[str(at.level)]
    return [(h["comet_jump_site"], jal(at("func_L00_00267BA8")), jal(modseg_symbol("ee_comet_jump_window"))),
            (h["attack_records"] + COMET_F14, COMET_F14_RETAIL, COMET_F14_RAC2)]


def strafe(at):
    """x5 (first increment): L2/R2 strafe (src/strafe.c). The pad begin-frame's
    parse call goes to a hook that records the triggers and hides them from
    the game; the RUN movement block's two turn_to calls, its pivot turn and
    its two set_planar_vel calls go to hooks that, while strafing, face the
    camera and move along the stick; the jump block's turn and planar calls
    and the air control's turn do the same in the air, and the jump-press
    handler's four calls go to a hook that turns a strafe jump into R&C1's
    own flips (state 0xB) as R&C2 does. The gun-arm layer manager's
    identity and mapper calls go to src/gun_arm.c, and its two tests of the
    arm sequence (< 0x17) admit the arm classes' 24th sequence (< 0x18), so
    that the backpedal holds a trigger weapon as R&C2's does. Sites from
    tools/gen_hero.py."""
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
            (h["stop_site_low"], jal(h["hero_set_state"]), jal(modseg_symbol("ee_stop"))),
            # the gun arm over the backpedal (src/gun_arm.c)
            (h["arm_ident_site"], jal(h["arm_ident_fn"]), jal(modseg_symbol("ee_arm_ident"))),
            (h["arm_map_0"], jal(h["arm_mapper"]), jal(modseg_symbol("ee_arm_map"))),
            (h["arm_map_1"], jal(h["arm_mapper"]), jal(modseg_symbol("ee_arm_map"))),
            (h["arm_check_0"], SLTIU_ARM_RETAIL, SLTIU_ARM_EDITION),
            (h["arm_check_1"], SLTIU_ARM_RETAIL, SLTIU_ARM_EDITION)]


SLTIU_ARM_RETAIL = 0x2C420017    # sltiu $v0, $v0, 0x17: arm sequences 0..0x16 (R&C1's 23)
SLTIU_ARM_EDITION = 0x2C420018   # sltiu $v0, $v0, 0x18: and 0x17, R&C2's backpedal arm (src/gun_arm.c)


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


HEALTH_ROW_SITE = 0x23A47C - 0x23A1A0   # in HudHealthDraw: lw $2, D_0015F8EC (delay slot of beqz $19)
HEALTH_ROW_STEP = 32                    # the orb tables' second row (D_0017E560, D_0017E578) is 32 px down


def health_backing(at):
    """The health HUD's second row of nanotech orbs, with Premium (5) or
    Ultra (8) Nanotech, sits on its backing again. HudHealthDraw
    (func_L00_0023A1A0) draws the backing bar 16 px above the orbs' row
    (D_0015F8F0 = -16, bar 32 px tall) and both orb tables put the second
    row 32 px below the first, but the second bar is offset by
    D_0015F8EC = 16 (gp-0x7414; gp-0x7404 in levels 5 and 16), so it sat
    16 px too high, half behind the first. Retail's own bug, in every
    level; nothing else reads that constant, but the patch leaves it alone
    and loads 32 in its place."""
    site = at("func_L00_0023A1A0") + HEALTH_ROW_SITE
    branch, load = at.words(site - 4, 2)
    if branch != 0x12600019 or load >> 16 != 0x8F82:   # beqz $19, .L_orbs; lw $2, imm($gp)
        raise SystemExit(f"level {at.level}: HudHealthDraw's second-row offset is not at 0x{site:X} "
                         f"({branch:08X} {load:08X})")
    return [(site, load, addiu(2, ZERO, HEALTH_ROW_STEP))]


OLTANIS_SEEN_BRANCH = 0x2EE780 - 0x2EE6E0   # bnel $3, $zero, .L_out (the intro-seen flag); lq $22 in its slot
OLTANIS_SET_STATE = 0x2EE79C - 0x2EE6E0     # sb $2, 0x20($22) (state = 2), then b .L_epilogue


def oltanis_lightning(at):
    """Oltanis keeps its lightning strikes. One moby of the level
    (func_L14_002EE6E0, OpenRAC's CameraIntroController, decompiled in
    src/overlays/l14_oltanis/vendor_002E0538.c) runs both: in state 1 it
    plays the arrival cutscene and marks it seen in the saved per-level
    flags (0x13D4F8), state 2 waits for the cutscene to end, and state 3
    throws a strike with its thunder every few seconds. When the flag is
    already set, retail's state 1 just returns, so the moby never reaches
    state 3: after a death, a load or a return trip the sky stays quiet for
    the rest of the game. The patch sends that case to state 3 instead. The
    flag test's branch-likely becomes a plain branch to the existing state
    store before the function's exit (OLTANIS_SET_STATE), with state 3 put
    in $2 by its delay slot (harmless when not taken: the cutscene call
    that follows overwrites $2). The ship hide/show pair around the
    cutscene (Ship_ReleaseCurrent, ShowTrackedMoby) is not needed when the
    cutscene does not play. Level 14 only; the next frame runs state 3 as
    after the cutscene."""
    if at.level != 14:
        return []
    f = at("func_L14_002EE6E0")
    site = f + OLTANIS_SEEN_BRANCH
    store, exit_branch = at.words(f + OLTANIS_SET_STATE, 2)
    if store != 0xA2C20020 or exit_branch != 0x1000006F:   # sb $2, 0x20($22); b .L002EE960
        raise SystemExit(f"level 14: CameraIntroController's state store is not at 0x{f + OLTANIS_SET_STATE:X} "
                         f"({store:08X} {exit_branch:08X})")
    target = (f + OLTANIS_SET_STATE - (site + 4)) >> 2
    return [(site, 0x54600078, 0x14600000 | target),    # bnel $3, $0, .L002EE964 -> bne $3, $0, (the store)
            (site + 4, 0x7BB60090, addiu(2, ZERO, 3))]  # lq $22, 0x90($29) -> addiu $2, $0, 3


PREVIEW_TAIL = 0x44     # func_0021F610 + 0x44: the branch on the cursor cell's owned flag


def item_preview(at):
    """The Weapons/Gadgets preview never flashes an unowned item
    (src/item_preview.c). func_0021F610 tests the cursor cell's owned flag,
    then draws the page's primary and secondary preview mobys. From that
    test on it is rewritten in place (same size; the prologue, the cell
    lookup and the epilogue stay): the primary moby goes through
    ee_preview_shown, which hides a moby whose own item is not owned, and
    nothing is drawn (result 0, as for an unowned cell) when it does."""
    f = at("func_0021F610")
    draw = jal(at("func_0020E180"))
    retail = [0x54600003, 0x8E040044, 0x1000000B, 0x0000102D,   # bnel $3,.draw; lw $4,0x44($16); b .ret; v0=0
              0x50800004, 0x8E040048, draw, 0x24050001,         # .draw: beql $4,.second; draw(primary, 1)
              0x8E040048, 0x10800004, 0x24020008, draw,         # .second: lw $4,0x48($16); beqz $4,.ret; v0=8; draw(secondary, 1)
              0x24050001, 0x24020008]                           # v0 = 8; .ret at +0x7C
    patched = [0x1060000D, 0x0000102D,                          # beqz $3,.ret; v0 = 0
               jal(modseg_symbol("ee_preview_shown")), 0x8E040044,   # v0 = ee_preview_shown(primary)
               0x10400009, 0x0040202D,                          # beqz $v0,.ret; a0 = v0
               draw, 0x24050001,                                # draw(primary, 1)
               0x8E040048, 0x10800004, 0x24020008, draw,        # the secondary as retail
               0x24050001, 0x24020008]
    found = at.words(f + PREVIEW_TAIL, len(retail) + 1)
    if found[:-1] != retail or found[-1] != 0x7BBF0010:         # lq $31,0x10($29) at .ret
        raise SystemExit(f"level {at.level}: func_0021F610 is not retail's at 0x{f:X}")
    return [(f + PREVIEW_TAIL + 4 * k, old, new) for k, (old, new) in enumerate(zip(retail, patched))]


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
    # Saves a retail game can load: every memcard_MakeWholeSave call writes the
    # save without the Charge Boots worn or owned (src/charge_boots.c, ee_save_c).
    save = jal(at("func_0020BA00"))
    for fn, off in SAVE_SITES:
        out.append((at(fn) + off, save, jal(modseg_symbol("ee_save_hook"))))
    if at.level == 18:
        out.append((at("func_L18_00282F48") + 0x1C, save, jal(modseg_symbol("ee_save_hook_l18"))))
    return out + charge_dash(at)


# memcard_MakeWholeSave's callers in every level: the pause menu's two saves and the
# challenge-mode carry-over's (Veldin 2's ending adds func_L18_00282F48 + 0x1C).
SAVE_SITES = (("func_00227C78", 0x64), ("func_00227D20", 0x3C), ("func_00227DB0", 0x304))


# x2: the Charge Boots cheat (src/charge_boots.c): move ids as the game's cheat
# detector records them; crouch for a second (7), back flip (3) twice,
# Comet-Strike (4), Hyper-Strike (6) twice. Entry 8 of the detector's table.
CHARGE_CHEAT = (7, 3, 3, 4, 6, 6)
CHARGE_CHEAT_ENTRY = 8
FRAME_START_SITE = 0x6D4   # func_L00_00299250 + 0x6D4: jal MobyFreeSlotBookkeeping
PAUSE_ENTRY_SITES = (0x36C, 0x434)   # func_L00_00299250: its two calls of the pause entry, func_00219C70


def charge_dash(at):
    """x2: the Charge Boots dash on R1's double tap, Going Commando's hero
    state 0x7B, R&C1's state 0x48 (src/charge_dash.c), part of the
    charge_boots feature. The input handler's Thruster-Pack test (its
    slot_item(3) call) goes to
    ee_charge_trigger_hook, which takes the double tap for the boots when
    they are worn, the call of the per-state movement function goes to
    ee_charge_move_hook, which moves the hero while it dashes, and the
    follow camera overrides' collect_mobies_in_sphere call goes to
    ee_charge_camera_hook, which adds R&C2's charge camera. The mode-0
    frame's call of MobyFreeSlotBookkeeping (func_L00_00299250 + 0x6D4,
    every level) goes to ee_frame_start_hook, which keeps the edition's
    sequences in Ratchet's class after a pause and runs the Charge Boots
    cheat; entry 8 of the detector's sequence table gets the cheat's moves
    (CHARGE_CHEAT); and the Goodies Cheats page's list widget's "enter"
    slot (its row builder) goes to the level's ee_cheats_enter_NN stub,
    which adds the Charge Boots row, and its "update" slot to
    ee_cheats_update_NN, which makes the boots follow a toggle at once.
    Sites from tools/gen_charge.py."""
    import json
    sites = HERE.parent / "build/modseg/charge_sites.json"
    if not sites.is_file():
        raise SystemExit(f"missing {sites}: run tools/gen_charge.py first")
    c = json.loads(sites.read_text())[str(at.level)]
    seq = bytes(CHARGE_CHEAT) + bytes(16 - len(CHARGE_CHEAT))
    entry = c["cheat_table"] + 16 * CHARGE_CHEAT_ENTRY
    return [(c["trigger"], jal(c["slot_item"]), jal(modseg_symbol("ee_charge_trigger_hook"))),
            (c["move_site"], jal(c["move_fn"]), jal(modseg_symbol("ee_charge_move_hook"))),
            (c["camera_site"], jal(c["collect"]), jal(modseg_symbol("ee_charge_camera_hook"))),
            (at("func_L00_00299250") + FRAME_START_SITE, jal(at("func_L00_0024F7E0")),
             jal(modseg_symbol("ee_frame_start_hook"))),
            (at("func_L00_00299250") + PAUSE_ENTRY_SITES[0], jal(at("func_00219C70")), jal(modseg_symbol("ee_pause_entry_hook"))),
            (at("func_L00_00299250") + PAUSE_ENTRY_SITES[1], jal(at("func_00219C70")), jal(modseg_symbol("ee_pause_entry_hook"))),
            (c["cheats_enter"], c["cheats_builder"], modseg_symbol(f"ee_cheats_enter_{at.level:02d}")),
            (c["cheats_update_slot"], c["cheats_update"], modseg_symbol(f"ee_cheats_update_{at.level:02d}"))] + \
        [(entry + 4 * k, 0, struct.unpack_from("<I", seq, 4 * k)[0]) for k in range(4)]


VIEW_PROLOGUE = (0x27BDFFA0, 0x3C020016)   # UpdateViewContext: addiu $sp,$sp,-0x60; lui $v0,0x16
VIEW_FOV_READ = 0x68                         # lwc1 $f0,0xB0($v1): the horizontal value the vertical one is made from
LWC1_FOV, LWC1_FOV_SRC = 0xC46000B0, 0xC46000BC
PANEL_DRAW_SITE = 0xFC                       # func_L00_002781D8 + 0xFC: jal func_0020E180(panel, 1)
PANEL_PROJECT_SITE = 0x1EC                   # func_L00_002781D8 + 0x1EC: jal func_00238D90(corners -> rectangle)
JALR_A2 = 0x00C0F809                         # jalr $a2: the pause renderer's call of a panel that draws itself
JALR_V0 = 0x0040F809                         # jalr $v0: its call of a panel drawing into its texture


def widescreen(at):
    """q4: 16:9 (src/widescreen.c). Every level's UpdateViewContext starts
    with a jump to ee_view_hook, the level id in the delay slot, and its read
    of the horizontal field of view for the vertical one reads +0xBC, which
    the hook fills with the unwidened value. DrawWorld's calls of HudDraw,
    draw_help, the letterbox bars, the subtitles and the DoGifPaging after
    them (the pillars) go to the 2D hooks. The pause renderer's panel draw
    and corner projection, the vendor's monitor projections and its
    VendorDrawScreens call, and the 2D draws of both (fills, content quads,
    panels that draw themselves, the vendor screen's HudDraw) go to the
    hooks and bracketed calls of src/ws_hooks.s. The movie decoder's
    setImageTag call goes to the pillarboxed movie packet. The Camera page's
    list widget points at a copy of its entries with the 16:9 row
    (tools/gen_menu.py)."""
    import json
    f = at("func_001F3140")
    dw = at("func_001F3D78")
    menu = HERE.parent / "build/modseg/menu_sites.json"
    if not menu.is_file():
        raise SystemExit(f"missing {menu}: run tools/gen_menu.py first")
    cam = json.loads(menu.read_text())[str(at.level)]["camera"]
    return [(f, VIEW_PROLOGUE[0], j(modseg_symbol("ee_view_hook"))),
            (f + 4, VIEW_PROLOGUE[1], addiu(AT, ZERO, at.level)),
            (f + VIEW_FOV_READ, LWC1_FOV, LWC1_FOV_SRC),
            (dw + site_offset("EE_SITE_WS_HUD"), jal(at("func_001FFFB8")), jal(modseg_symbol("ee_ws_hud"))),
            (dw + site_offset("EE_SITE_WS_HELP"), jal(at("func_001FF1B0")), jal(modseg_symbol("ee_ws_help"))),
            (dw + site_offset("EE_SITE_WS_LETTERBOX"), jal(at("func_001F5148")), jal(modseg_symbol("ee_ws_letterbox"))),
            (dw + site_offset("EE_SITE_WS_SUBTITLES"), jal(at("func_001F4F90")), jal(modseg_symbol("ee_ws_subtitles"))),
            (dw + site_offset("EE_SITE_WS_PILLARS"), jal(at("func_001F4748")), jal(modseg_symbol("ee_ws_pillars"))),
            (at("func_L00_002781D8") + PANEL_DRAW_SITE, jal(at("func_0020E180")), jal(modseg_symbol("ee_ws_panel_draw"))),
            (at("func_L00_002781D8") + PANEL_PROJECT_SITE, jal(at("func_00238D90")), jal(modseg_symbol("ee_ws_panel_project"))),
            (at("func_L00_002A0F60") + 0x188, jal(at("func_00238D90")), jal(modseg_symbol("ee_ws_vendor_project"))),
            # bracketed 2D calls (src/widescreen.c WS_SITE_*)
            (at("func_L00_002781D8") + 0x298, jal(at("func_00201640")), jal(modseg_symbol("ee_ws_br_pause_fill"))),
            (at("func_L00_002781D8") + 0x388, JALR_A2, jal(modseg_symbol("ee_ws_br_pause_direct"))),
            (at("func_L00_002781D8") + 0x458, jal(at("func_001F7A50")), jal(modseg_symbol("ee_ws_panel_target"))),
            (at("func_L00_002781D8") + 0x49C, JALR_V0, jal(modseg_symbol("ee_ws_panel_texture"))),
            (at("func_L00_002781D8") + 0x600, jal(at("func_001F5800")), jal(modseg_symbol("ee_ws_br_pause_quad"))),
            (at("func_L00_002781D8") + 0x698, jal(at("func_002250B8")), jal(modseg_symbol("ee_ws_br_pause_last"))),
            (at("func_L00_002A0F60") + 0x258, jal(at("func_001F5800")), jal(modseg_symbol("ee_ws_br_vendor_quad"))),
            (at("func_L00_002A1540") + 0x400, jal(at("func_001FFFB8")), jal(modseg_symbol("ee_ws_br_vendor_hud"))),
            # the vendor's six monitor screens (VendorDrawScreens): drawn with the 4:3 view; projections and quads
            (at("func_L00_002A1540") + 0x3C4, jal(at("func_0023AA38")), jal(modseg_symbol("ee_ws_vendor_screens"))),
            (at("func_0023AA38") + 0x20C, jal(at("func_00238F98")), jal(modseg_symbol("ee_ws_screens_project"))),
            (at("func_0023AA38") + 0x374, jal(at("func_00238F98")), jal(modseg_symbol("ee_ws_screens_project"))),
            (at("func_0023AA38") + 0x50C, jal(at("func_001F5988")), jal(modseg_symbol("ee_ws_br_screens_quad_a"))),
            (at("func_0023AA38") + 0x568, jal(at("func_001F5988")), jal(modseg_symbol("ee_ws_br_screens_quad_b"))),
            (at("func_0023E298") + site_offset("EE_SITE_WS_MOVIE"), jal(at("func_0023C5E0")), jal(modseg_symbol("ee_ws_movie_tag"))),
            # game mode 4's prompt ("Quit Race?" in the ships and races)
            (at("func_L00_001F9248") + 0x60, jal(at("func_001FBE80")), jal(modseg_symbol("ee_ws_br_quit_prompt"))),
            # the credits (game mode 7): the picture pillarboxed, the name tags squeezed
            (at("func_L00_001F92E8") + 0x24, jal(at("func_001F7680")), jal(modseg_symbol("ee_ws_credits_picture"))),
            (at("func_L00_001F92E8") + 0xC8, jal(at("func_L00_001FCC50")), jal(modseg_symbol("ee_ws_br_credits_name"))),
            (at("func_002305A0") + 0x140, jal(at("func_0022FD20")), jal(modseg_symbol("ee_ws_br_planet_name"))),
            (cam["site"], cam["expect"], modseg_symbol(cam["symbol"]))] + ship_hud_patches(at) + \
        trespasser_patches(at) + timer_patches(at)


# The ship levels register their HUD's draw callback each frame with a lui/addiu
# pair of $a0: {level: (function, lui offsets, addiu offset)}.
SHIP_HUD_SITES = {
    11: ("func_L11_00313D00", (0x924, 0x934), 0x948),   # func_L11_00313218 (a bnel's delay slot holds one lui)
    13: ("func_L13_002BB3E8", (0xE34,), 0xE48),         # func_L13_002BAA68
    17: ("func_L17_002EDE50", (0xC00,), 0xC14),         # func_L17_002ED498
}


def ship_hud_patches(at):
    """q4: the ship's HUD callback goes through ee_ws_br_ship_NN (src/widescreen.c ship_hud)."""
    if at.level not in SHIP_HUD_SITES:
        return []
    fn, luis, lo_site = SHIP_HUD_SITES[at.level]
    f = at(fn)
    hi, lo = hi_lo(modseg_symbol(f"ee_ws_br_ship_{at.level}"))
    out = []
    for o in luis:
        old, = at.words(f + o, 1)
        if old >> 16 != 0x3C04:
            raise SystemExit(f"level {at.level}: {fn} + 0x{o:X} is not lui $a0")
        out.append((f + o, old, (old & 0xFFFF0000) | hi))
    old, = at.words(f + lo_site, 1)
    if old >> 16 != 0x2484:
        raise SystemExit(f"level {at.level}: {fn} + 0x{lo_site:X} is not addiu $a0, $a0")
    out.append((f + lo_site, old, (old & 0xFFFF0000) | lo))
    return out


# The Trespasser puzzle (TrespasserPuzzleUpdate) registers its ring draw callback
# (RingMarkersDraw) with a lui/addiu pair of $a0 around its AddDrawCallback call.
TRESPASSER_LEVELS = (2, 4, 6, 8, 11, 13, 18)
TRESPASSER_LUI, TRESPASSER_ADDIU = 0x3A8, 0x3B4   # func_L02_002D9F08 + 0x3A8 (a bc1f's delay slot), + 0x3B4 (the jal's)


def trespasser_patches(at):
    """q4: the Trespasser puzzle's rings go through ee_ws_br_tres_NN (src/widescreen.c trespasser_draw)."""
    if at.level not in TRESPASSER_LEVELS:
        return []
    f = at("func_L02_002D9F08")
    draw = at("func_L02_002DA820")
    hi, lo = hi_lo(modseg_symbol(f"ee_ws_br_tres_{at.level}"))
    old_hi, old_lo = hi_lo(draw)
    return [(f + TRESPASSER_LUI, 0x3C040000 | old_hi, 0x3C040000 | hi),
            (f + TRESPASSER_ADDIU, 0x24840000 | old_lo, 0x24840000 | lo)]


# The countdowns (src/widescreen.c timer_draw) register their draw callback with a
# lui/addiu pair of $a0 around AddDrawCallback: {level: (function, lui, addiu, callback)}.
TIMER_SITES = {
    6: ("func_L06_00302FE8", 0x534, 0x540, "func_L06_00303630"),    # Blarg Station's self-destruct
    15: ("func_L15_002D7C00", 0xCA8, 0xCB0, "func_L15_002D8BB0"),   # TimerMoby_Draw (Quartu)
    17: ("func_L15_002D7C00", 0xCA8, 0xCB0, "func_L15_002D8BB0"),   # TimerMoby_Draw (Drek's Fleet)
}


def timer_patches(at):
    """q4: a countdown's frame and digits go through ee_ws_br_timer_NN (squeezed in place)."""
    if at.level not in TIMER_SITES:
        return []
    fn, lui_off, lo_off, draw = TIMER_SITES[at.level]
    f = at(fn)
    hi, lo = hi_lo(modseg_symbol(f"ee_ws_br_timer_{at.level}"))
    old_hi, old_lo = hi_lo(at(draw))
    return [(f + lui_off, 0x3C040000 | old_hi, 0x3C040000 | hi),
            (f + lo_off, 0x24840000 | old_lo, 0x24840000 | lo)]


FEATURES = {
    "widescreen": widescreen,
    "charge_boots": charge_boots,
    "edition_text": edition_text,
    "metal_detector_10x": metal_detector_10x,
    "metal_detector_10x_modseg": metal_detector_10x_modseg,
    "quick_select_pause": quick_select_pause,
    "quick_select_pages": quick_select_pages,
    "commercials": commercials,
    "health_backing": health_backing,
    "oltanis_lightning": oltanis_lightning,
    "item_preview": item_preview,
    "comet_strike_run": comet_strike_run,
    "comet_strike_jump": comet_strike_jump,
    "strafe": strafe,
    "strafe_camera": strafe_camera,
}
DEFAULT_FEATURES = ["edition_text", "metal_detector_10x_modseg", "quick_select_pause", "quick_select_pages", "commercials", "health_backing", "oltanis_lightning", "comet_strike_run", "comet_strike_jump", "strafe", "strafe_camera", "charge_boots", "item_preview", "widescreen"]


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
    sys.path.insert(0, str(EDITOR))
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
    ap.add_argument("--openrac", type=Path, default=HERE.parents[3] / "OpenRAC")
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

        if "widescreen" in args.features:
            # the frontend's flight after a title-screen load: frontbin's copy (tools/gen_frontend.py;
            # the boot executable's copy is patched by its patch-elf step)
            from gen_frontend import frontbin_patches
            for off, expect, word in frontbin_patches(args.openrac, iso, modseg_symbol):
                s, i = divmod(off, SECTOR)
                buf = sector(s)
                old, = struct.unpack_from("<I", buf, i)
                if old != expect:
                    raise SystemExit(f"frontbin widescreen: at image byte 0x{off:X} found {old:08X}, expected {expect:08X}")
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
