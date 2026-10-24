#!/usr/bin/env python3
"""
The title screen's two programs, for every feature that reaches them.

After power-on the title runs the boot executable's own code (SCES_509.16,
every function at its own address). Quitting a level to the title loads
`frontbin` (table of contents + 0x4F8) instead: chunks {destination, size,
pad, entry} copied into place by the core's ParseBin (func_0012DA38), the
executable's functions linked at other addresses, some left out. Data stays
at the executable's addresses in both. The hooks treat the two as programs
19 and 20 after the 19 levels (FE_BOOT, FE_BIN in src/modseg.h): tools/
modseg.py's per-program tables (LVL, LVL_DATA) get their two entries from
places() and data_place() here. A function is found in frontbin by its
code: the executable's instructions with jump targets and %hi/%lo halves
masked; where short functions match in several places, the right one is at
the same distance from the executable's address as its neighbours'.

  emit         writes build/modseg/gen_frontend.c: the title screen's Video
               page entries with the 16:9 row (src/widescreen.c)
  patch-elf F  patches the boot executable F in place (after OpenRAC's
               tools/boot_elf.py), as every level is patched:
               edition_text  msg_string's entry -> ee_msg_string_hook
               widescreen    UpdateViewContext's entry -> ee_view_hook and its
                             +0x68 read; DrawWorld's 2D calls; the menu
                             renderer's panel hooks and bracketed calls (the
                             title's own renderer, func_0021A610, a sibling of
                             the levels' func_L00_002781D8 at other offsets);
                             the movie frame's tags; the flight's planet name;
                             the title cards' draws (PlayStoryTransition);
                             the title screen's logo and PRESS START;
                             the Video page's toggle list -> ee_fe_video_entries
                             (a 16:9 row); every call of the game-state reset ->
                             ee_fe_reset_19/20, which keeps the 16:9 setting
               movie_volume  the volume calls (overlay_patch.VOLUME_SITES),
                             LoadHudBanks' header copy and bank link (the
                             orange bar's icon), the Sound page's widget
               goodies       Pause_Update's call of Pause_Open ->
                             ee_title_menu_open_19/20 (the start menu's
                             Goodies row, src/goodies.c); the main loop's
                             call of MemCard_UpdateState ->
                             ee_title_card_19/20 (the card scan behind it)
  frontbin_patches(iso, features)   the same words for frontbin, as image
               offsets, for tools/overlay_patch.py

  python tools/gen_frontend.py [--openrac DIR] emit
  python tools/gen_frontend.py [--openrac DIR] patch-elf build-sn/SCES_509.16
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SECTOR = 2048
TOC_LBA, FRONTBIN_TOC = 1500, 0x4F8         # the table of contents; frontbin's {LBA, sectors}
FE_BOOT, FE_BIN = 19, 20                    # the programs' ids after the 19 levels (src/modseg.h)

UPDATE_VIEW = "func_001F3140"               # UpdateViewContext
FLIGHT = "func_002305A0"                    # DrawSpaceFlightFrame
PLANET_NAME = "func_0022FD20"               # the flight screen's planet name picture
DRAW_WORLD = "func_001F3D78"
LETTERBOX = "func_001F5148"
LOAD_HUD = "func_002032D0"                  # LoadHudBanks (src/hud_icons.c's two hooks)
SOUND_INPUT = "func_0021DB30"               # Pause_SoundOptionsInput, the Sound page's input
SOUND_DRAW = "func_0021DE08"                # DrawSoundMenu
SOUND_INIT = "func_0022DBE8"                # Sound_InitState
SOUND_UPDATE = "func_0022DD68"              # sound_update
MOVIE_LOOP = "func_0023B740"                # the movie playback loop
RESET = "func_00209DC0"                     # the game-state reset from the disc's initial state (new game)
MSG_STRING = "func_001FE540"
MOVIE_FRAME = "func_0023E298"               # the movie decoder's frame setup (calls setImageTag)
RENDERER = "func_0021A610"                  # the title's menu renderer (the levels': func_L00_002781D8)
STORY = "func_00232EF0"                     # PlayStoryTransition: the title cards before the story's movies
TITLE_FRAME = "func_001EB7C0"               # Transition_DefaultDraw: the title screen's frame
TITLE_SITES = (0x19C, 0x210)                # its DrawTexturedQuad calls: the logo, PRESS START
PAUSE_UPDATE = "func_0021A1A0"              # Pause_Update: the menus' frame, the title's start menu included
PAUSE_OPEN = "func_00219E90"                # Pause_Open: makes the start page current, creates the 14 panels
PAUSE_OPEN_SITE = 0x120                     # Pause_Update + 0x120: jal Pause_Open
MC_UPDATE = "func_00209E68"                 # MemCard_UpdateState, the card machine
# The title screen's main loop (func_001EBB48 in the executable; frontbin's differs elsewhere, so it is
# found by its calls): PutDrawBufferLarge, MemCard_UpdateState, Menu_RunStateHandler, each with a nop.
DRAW_BUFFER = "func_001FB498"               # PutDrawBufferLarge
MENU_STATES = "func_00209070"               # Menu_RunStateHandler
FUNCS = (UPDATE_VIEW, FLIGHT, PLANET_NAME, DRAW_WORLD, LETTERBOX, LOAD_HUD, SOUND_INPUT, SOUND_DRAW,
         SOUND_INIT, SOUND_UPDATE, MOVIE_LOOP, RESET, MSG_STRING, MOVIE_FRAME, RENDERER, STORY, TITLE_FRAME,
         PAUSE_UPDATE, PAUSE_OPEN, MC_UPDATE, DRAW_BUFFER, MENU_STATES)
PLANET_NAME_SITE = 0x140                    # DrawSpaceFlightFrame + 0x140: jal the planet name
# The title renderer's calls the 16:9 hooks take, as the levels' (tools/overlay_patch.py
# widescreen) are at func_L00_002781D8 + PANEL_DRAW_SITE etc.: offset, the callee, the hook.
RENDERER_SITES = ((0xBC, "func_0020E180", "ee_ws_panel_draw"),     # the panel loop's DrawMobyList(panel, 1)
                  (0x1A4, "func_00238D90", "ee_ws_panel_project"),
                  (0x250, "func_00201640", "ee_ws_br_pause_fill"),
                  (0x358, None, "ee_ws_br_pause_direct"),            # jalr $a2
                  (0x430, "func_001F7A50", "ee_ws_panel_target"),
                  (0x474, None, "ee_ws_panel_texture"),              # jalr $v0
                  (0x5E0, "func_001F5800", "ee_ws_br_pause_quad"),
                  (0x670, "func_002250B8", "ee_ws_br_pause_last"))
JALR_A2, JALR_V0 = 0x00C0F809, 0x0040F809
# DrawWorld's 2D calls (EE_SITE_WS_* in src/modseg.h), as in every level.
DRAW_WORLD_SITES = (("EE_SITE_WS_HUD", "ee_ws_hud"), ("EE_SITE_WS_HELP", "ee_ws_help"),
                    ("EE_SITE_WS_LETTERBOX", "ee_ws_letterbox"), ("EE_SITE_WS_SUBTITLES", "ee_ws_subtitles"),
                    ("EE_SITE_WS_PILLARS", "ee_ws_pillars"))
# The title screen's Options rows (Language, Video, Sound) and the Video page's one toggle
# entry {label, flag, on, off, special}: "Video mode (50Hz or 60Hz)", special 1 (its popup).
TITLE_OPTIONS = (0x4F2B, 0x4F21, 0x4F22)
VIDEO_MODE_LABEL = 0x4F3B
TOGGLE_ON, TOGGLE_OFF = 0x4F5A, 0x4F5B       # "On", "Off", as the Subtitles and Controls pages' toggles
TEXT_WIDESCREEN = 0x540D                    # "16:9 (widescreen)" (src/modseg.h EE_TEXT_WIDESCREEN)
SETTING_WIDESCREEN = 0x0015EEBA             # gCheats[10] (src/modseg.h EE_SETTING_WIDESCREEN)
# The start menu (src/goodies.c TITLE_*): page, its three one-row lists New Game, Load Game, Options,
# and their labels; screens 0 and 1 hold an empty placeholder, and panel 0 takes the Goodies row.
TITLE_PAGE, TITLE_LISTS, TITLE_PLACEHOLDER = 0x1D4948, (0x1D4A18, 0x1D4A68, 0x1D4AB8), 0x1CE678
TITLE_LABELS = (0x4F2A, 0x4F28, 0x4EE7)
# The pages the Goodies pages are made from (src/goodies.c OPTIONS_*, VIDEO_*, EXIT_PROMPT, NEW_GAME_PAGE).
OPTIONS_PAGE, OPTIONS_PAGE_BOXES = 0x1D4B08, (0x1D0C58, 0x1D4BE8, 0x1CFE90)
VIDEO_PAGE, VIDEO_TITLE, EXIT_PROMPT, NEW_GAME_PAGE = 0x1D4E38, 0x1D4EC0, 0x1CE7B8, 0x1D5008
MSG_PROLOGUE = (0x27BDFFF0, 0x7FBF0000)     # addiu $sp,$sp,-0x10; sq $ra,0($sp) (src/text_hook.s runs them)
VIEW_PROLOGUE = (0x27BDFFA0, 0x3C020016)    # addiu $sp,$sp,-0x60; lui $v0,0x16 (ee_view_hook runs them)
VIEW_FOV_READ, LWC1_FOV, LWC1_FOV_SRC = 0x68, 0xC46000B0, 0xC46000BC


def j(target):
    return 0x08000000 | ((target >> 2) & 0x3FFFFFF)


def jal(target):
    return 0x0C000000 | ((target >> 2) & 0x3FFFFFF)


def addiu_at(imm):
    return 0x24010000 | (imm & 0xFFFF)          # addiu $at, $zero, imm


class Program:
    """A program's words by address: the boot executable's one segment, or frontbin's chunks."""

    def __init__(self, ranges):
        self.ranges = ranges                    # [(address, size, bytes, offset of the first byte in the file)]

    def where(self, addr):
        for base, size, blob, file_off in self.ranges:
            if base <= addr < base + size:
                return blob, addr - base, file_off + addr - base
        raise SystemExit(f"0x{addr:08X} is outside the program")

    def word(self, addr):
        blob, i, _ = self.where(addr)
        return struct.unpack_from("<I", blob, i)[0]

    def words(self, addr, n):
        return [self.word(addr + 4 * k) for k in range(n)]

    def file_offset(self, addr):
        return self.where(addr)[2]


def elf_program(exe: bytes) -> Program:
    phoff, = struct.unpack_from("<I", exe, 28)
    _, off, vaddr, _, filesz = struct.unpack_from("<5I", exe, phoff)
    return Program([(vaddr, filesz, exe[off:off + filesz], off)])


def frontbin_location(f) -> tuple[int, int]:
    f.seek(TOC_LBA * SECTOR + FRONTBIN_TOC)
    return struct.unpack("<2I", f.read(8))


def frontbin_program(data: bytes, base: int = 0) -> Program:
    """ParseBin's view of frontbin: chunks of {destination, size, pad, entry} while the entry agrees."""
    ranges, p, entry = [], 0, None
    while p + 16 <= len(data):
        dst, size, _pad, e = struct.unpack_from("<4I", data, p)
        if entry is None:
            entry = e
        elif e != entry:
            break
        ranges.append((dst, size, data[p + 16:p + 16 + size], base + p + 16))
        p += 16 + size
    return Program(ranges)


def function_size(openrac: Path, name: str) -> int:
    """A function's size: the overlay catalogue's, or for one only the executable has, its
    assembly's (OpenRAC's asm/, made by its setup)."""
    import re
    for line in (openrac / "games/rac1/config/overlays/functions.tsv").read_text().splitlines():
        cols = line.split("\t")
        if cols[0] == name:
            return int(cols[2])
    asm = openrac / f"games/rac1/asm/nonmatchings/text/{name}.s"
    m = re.search(rf"nonmatching {name}, (0x[0-9A-F]+)", asm.read_text()) if asm.is_file() else None
    if not m:
        raise SystemExit(f"{name}: size unknown (not in the catalogue, no {asm})")
    return int(m.group(1), 16)


def mask(w):
    """An instruction with what the linker fills in cleared: jump targets, %hi and %lo halves."""
    op = w >> 26
    if op in (2, 3):
        return w & 0xFC000000
    if op in (0x09, 0x0D, 0x0F, 0x1E, 0x1F, 0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B, 0x31, 0x37, 0x39, 0x3F):
        return w & 0xFFFF0000
    return w


_MASKED = {}


def masked_words(prog: Program):
    if id(prog) not in _MASKED:
        _MASKED[id(prog)] = [(base, [mask(w) for w in struct.unpack_from(f"<{length // 4}I", blob, 0)])
                             for base, length, blob, _ in prog.ranges]
    return _MASKED[id(prog)]


def matches(name: str, size: int, exe: Program, fb: Program) -> list[int]:
    """Every place in frontbin whose code matches NAME's in the executable, masked."""
    n = size // 4
    pattern = [mask(w) for w in exe.words(int(name[5:], 16), n)]
    hits = []
    for base, words in masked_words(fb):
        hits += [base + 4 * i for i in range(len(words) - n + 1)
                 if words[i] == pattern[0] and words[i:i + n] == pattern]
    return hits


def locate_all(openrac: Path, names, exe: Program, fb: Program) -> dict[str, int]:
    """NAMES' addresses in frontbin, 0 for one frontbin leaves out. A name matching in several
    places takes the one at its nearest uniquely found neighbour's distance (frontbin keeps the
    executable's order, shifted by what was left out before)."""
    hits = {n: matches(n, function_size(openrac, n), exe, fb) for n in set(names) | set(FUNCS)}
    unique = sorted((int(n[5:], 16), h[0]) for n, h in hits.items() if len(h) == 1)
    out = {}
    for n in names:
        h = hits[n]
        if len(h) <= 1:
            out[n] = h[0] if h else 0
            continue
        addr = int(n[5:], 16)
        below = [f - e for e, f in unique if e < addr]
        above = [f - e for e, f in unique if e > addr]
        for shift in below[-1:] + above[:1]:
            pick = [a for a in h if a - addr == shift]
            if len(pick) == 1:
                out[n] = pick[0]
                break
        else:
            raise SystemExit(f"{n}: {len(h)} matches in frontbin, none at a neighbour's distance")
    return out


_LOADED = {}


def load(openrac: Path, iso: Path | None = None) -> tuple[Program, Program]:
    iso = iso or openrac / "games/rac1/baserom/SCES_509.16.iso"
    if iso not in _LOADED:
        exe = elf_program((openrac / "games/rac1/baserom/SCES_509.16").read_bytes())
        with iso.open("rb") as f:
            lba, count = frontbin_location(f)
            f.seek(lba * SECTOR)
            fb = frontbin_program(f.read(count * SECTOR), lba * SECTOR)
        _LOADED[iso] = exe, fb
    return _LOADED[iso]


def places(openrac: Path, names) -> dict[str, tuple[int, int]]:
    """{name: (its address in the boot executable, in frontbin)} for the executable's
    functions; (0, 0) for a level's own (func_Lnn_...). For tools/modseg.py's tables."""
    exe, fb = load(openrac)
    own = [n for n in names if not n.startswith("func_L")]
    at = locate_all(openrac, own, exe, fb)
    return {n: (int(n[5:], 16), at[n]) if n in at else (0, 0) for n in names}


def data_place(openrac: Path, spec: tuple) -> tuple[int, int]:
    """The address a function's %hi/%lo pair resolves to in the boot executable and in
    frontbin; (0, 0) when the function is a level's own."""
    func, hi, lo = spec
    if func.startswith("func_L"):
        return 0, 0
    exe, fb = load(openrac)
    at = locate_all(openrac, [func], exe, fb)[func]
    return hi_lo_target(exe, int(func[5:], 16), hi, lo), (hi_lo_target(fb, at, hi, lo) if at else 0)


def hi_lo_target(prog: Program, fn: int, hi_off: int, lo_off: int) -> int:
    lui, lo = prog.word(fn + hi_off), prog.word(fn + lo_off)
    if lui >> 26 != 0x0F:
        raise SystemExit(f"0x{fn:08X} + 0x{hi_off:X} is not a lui")
    lo16 = lo & 0xFFFF
    return ((lui & 0xFFFF) << 16) + (lo16 - 0x10000 if lo16 & 0x8000 else lo16)


def word_pairs(prog: Program, first: int, second: int) -> list[int]:
    """Every word-aligned place in PROG holding FIRST then SECOND."""
    out = []
    for base, length, blob, _ in prog.ranges:
        words = struct.unpack_from(f"<{length // 4}I", blob, 0)
        out += [base + 4 * i for i in range(len(words) - 1) if words[i] == first and words[i + 1] == second]
    return out


def video_list(prog: Program) -> tuple[int, int]:
    """The title screen's Video page toggle list: (its widget, its entries)."""
    for base, length, blob, _ in prog.ranges:
        words = struct.unpack_from(f"<{length // 4}I", blob, 0)
        for i in range(len(words) - 9):
            if all(words[i + 3 * k] & 0xFFFF == lab for k, lab in enumerate(TITLE_OPTIONS)):
                widget = prog.word(words[i + 4] + 0x40)          # the Video row's page, its first widget
                entries = prog.word(widget + 0x34)
                if prog.words(entries, 6)[0] != VIDEO_MODE_LABEL or prog.word(entries + 20) != 0:
                    raise SystemExit("the Video page's toggle list is not the one-row list expected")
                return widget, entries
    raise SystemExit("the title screen's Options rows not found")


def check_title_page(prog: Program, label: str) -> None:
    """The start menu and the pages the Goodies pages copy are laid out as src/goodies.c expects:
    three one-row lists in panels 2-4, 0x50 bytes apart, linked in a loop; screens 0 and 1 the empty
    placeholder list (no update, no draw), panel 0 parked (sequence 0); the pages the rows open
    have it as their back page; and the Options, Video Options and New Game pages and the "Exit"
    box as build_pages copies them."""
    anims = prog.words(TITLE_PAGE, 14)
    screens = prog.words(TITLE_PAGE + 0x44, 14)
    ok = screens[:5] == [TITLE_PLACEHOLDER, TITLE_PLACEHOLDER] + list(TITLE_LISTS) and anims[0] == 0
    ok = ok and prog.words(TITLE_PLACEHOLDER, 2) == [0, 0]
    for i, lst in enumerate(TITLE_LISTS):
        items, up, down = prog.words(lst + 0x34, 3)
        opens = prog.word(items + 4)
        ok = ok and lst == TITLE_LISTS[0] + 0x50 * i
        ok = ok and prog.word(items) & 0xFFFF == TITLE_LABELS[i] and prog.word(items + 12) & 0xFFFF == 0
        ok = ok and up == TITLE_LISTS[i - 1] and down == TITLE_LISTS[(i + 1) % 3]
        ok = ok and prog.word(opens + 0x38) == TITLE_PAGE
    # The Goodies pages' models: Options (title box, list, "Select / Exit"), Video Options (title
    # box, wider box), the "Exit" box alone, and New Game (src/goodies.c build_pages).
    list_draw = prog.word(OPTIONS_PAGE_BOXES[1] + 4)
    ok = ok and prog.words(OPTIONS_PAGE + 0x44, 5)[2:] == list(OPTIONS_PAGE_BOXES)
    ok = ok and prog.words(VIDEO_PAGE + 0x44, 5)[2] == VIDEO_TITLE
    ok = ok and prog.word(OPTIONS_PAGE_BOXES[0] + 0x34) == 0x4F1D and prog.word(VIDEO_TITLE + 0x34) == 0x4F3A
    ok = ok and prog.word(EXIT_PROMPT + 4) == list_draw and prog.word(prog.word(EXIT_PROMPT + 0x34)) == 0x4EE0
    ok = ok and prog.word(prog.word(EXIT_PROMPT + 0x34) + 12) == 0
    ok = ok and prog.word(NEW_GAME_PAGE + 0x38) == TITLE_PAGE
    if not ok:
        raise SystemExit(f"{label}: the start menu is not the three-row page src/goodies.c expects")


def jal_sites(prog: Program, target: int) -> list[int]:
    """Every place in PROG holding a jal to TARGET."""
    word = jal(target)
    out = []
    for base, length, blob, _ in prog.ranges:
        words = struct.unpack_from(f"<{length // 4}I", blob, 0)
        out += [base + 4 * i for i, w in enumerate(words) if w == word]
    return out


def frontends(openrac: Path, iso: Path | None = None):
    """[(Program, {name: address}, {...})] for the boot executable and frontbin: FUNCS'
    addresses, and the Sound page's slider widgets (records whose first two words are its
    input and draw), the Video page's toggle list and the reset's call sites."""
    exe, fb = load(openrac, iso)
    in_fb = locate_all(openrac, FUNCS, exe, fb)
    out = []
    for prog, place in ((exe, lambda n: int(n[5:], 16)), (fb, lambda n: in_fb[n])):
        funcs = {n: place(n) for n in FUNCS}
        if not all(funcs.values()):
            raise SystemExit(f"not in frontbin: {', '.join(n for n, a in funcs.items() if not a)}")
        data = {}
        data["sound_widgets"] = word_pairs(prog, funcs[SOUND_INPUT], funcs[SOUND_DRAW])
        if not data["sound_widgets"]:
            raise SystemExit("no Sound page widget in a frontend")
        data["video_widget"], data["video_entries"] = video_list(prog)
        data["reset_calls"] = jal_sites(prog, funcs[RESET])
        if not 1 <= len(data["reset_calls"]) <= 8:
            raise SystemExit(f"{len(data['reset_calls'])} calls of the reset in a frontend")
        check_title_page(prog, "frontbin" if prog is fb else "boot executable")
        out.append((prog, funcs, data))
    return out


def patches(prog: Program, funcs: dict, data: dict, fe_id: int, symbol,
            features=("edition_text", "widescreen", "movie_volume", "goodies")) -> list[tuple[int, int, int]]:
    """(address, retail word, new word) for one frontend."""
    out = []
    sys.path.insert(0, str(HERE))
    from overlay_patch import volume_patches, site_offset
    label = "frontbin" if fe_id == FE_BIN else "boot executable"

    def call(addr, jalr=None):
        """A call site's retail word: a jal (to the program's own copy), or the JALR given."""
        old = prog.word(addr)
        if (old >> 26 != 3) if jalr is None else (old != jalr):
            raise SystemExit(f"{label}: 0x{addr:08X} holds {old:08X}, not the call expected")
        return old

    if "edition_text" in features:
        # msg_string answers the edition's ids, as in every level (src/text_hook.s)
        f = funcs[MSG_STRING]
        out += [(f, MSG_PROLOGUE[0], j(symbol("ee_msg_string_hook"))), (f + 4, MSG_PROLOGUE[1], addiu_at(fe_id))]
    if "widescreen" in features:
        f = funcs[UPDATE_VIEW]
        out += [(f, VIEW_PROLOGUE[0], j(symbol("ee_view_hook"))),
                (f + 4, VIEW_PROLOGUE[1], addiu_at(fe_id)),
                (f + VIEW_FOV_READ, LWC1_FOV, LWC1_FOV_SRC),
                (funcs[FLIGHT] + PLANET_NAME_SITE, jal(funcs[PLANET_NAME]), jal(symbol("ee_ws_br_planet_name")))]
        for site, hook in DRAW_WORLD_SITES:
            a = funcs[DRAW_WORLD] + site_offset(site)
            out.append((a, call(a), jal(symbol(hook))))
        for off, callee, hook in RENDERER_SITES:
            a = funcs[RENDERER] + off
            out.append((a, call(a, None if callee else JALR_A2 if hook.endswith("direct") else JALR_V0),
                        jal(symbol(hook))))
        a = funcs[MOVIE_FRAME] + site_offset("EE_SITE_WS_MOVIE")
        out.append((a, call(a), jal(symbol("ee_ws_movie_tag"))))
        from overlay_patch import STORY_CARD_SITES
        for off, _ in STORY_CARD_SITES:              # the title cards before the intro's movies
            a = funcs[STORY] + off
            out.append((a, call(a), jal(symbol("ee_ws_br_story_card"))))
        for off in TITLE_SITES:                      # the title screen's logo and PRESS START
            a = funcs[TITLE_FRAME] + off
            out.append((a, call(a), jal(symbol("ee_ws_br_title"))))
        # the Video page's 16:9 row, kept across a new game's reset (src/widescreen.c)
        out.append((data["video_widget"] + 0x34, data["video_entries"], symbol("ee_fe_video_entries")))
        out += [(a, jal(funcs[RESET]), jal(symbol(f"ee_fe_reset_{fe_id}"))) for a in data["reset_calls"]]
    if "movie_volume" in features:
        # the volume calls, the HUD loader's header copy and bank link (the movie bar's
        # icon, src/hud_icons.c), and the Sound page's widgets (src/movie_volume.c)
        out += volume_patches(lambda n: funcs[n], prog.words, label)
        for site, hook in (("EE_SITE_HUD_HEADER_COPY", "ee_hud_header_copy"),
                           ("EE_SITE_HUD_LINK_BANK", "ee_hud_link_bank")):
            a = funcs[LOAD_HUD] + site_offset(site)
            old = prog.word(a)
            if old >> 26 != 3:
                raise SystemExit(f"{label}: LoadHudBanks + 0x{a - funcs[LOAD_HUD]:X} is not a jal ({old:08X})")
            out.append((a, old, jal(symbol(hook))))
        for w in data["sound_widgets"]:
            out += [(w, funcs[SOUND_INPUT], symbol(f"ee_sound_input_{fe_id:02d}")),
                    (w + 4, funcs[SOUND_DRAW], symbol(f"ee_sound_draw_{fe_id:02d}"))]
    if "goodies" in features:
        # the start menu's Goodies row, once the game has been beaten (src/goodies.c)
        a = funcs[PAUSE_UPDATE] + PAUSE_OPEN_SITE
        if prog.word(a) != jal(funcs[PAUSE_OPEN]):
            raise SystemExit(f"{label}: Pause_Update + 0x{PAUSE_OPEN_SITE:X} is not its call of Pause_Open")
        out.append((a, jal(funcs[PAUSE_OPEN]), jal(symbol(f"ee_title_menu_open_{fe_id}"))))
        sites = [a for a in jal_sites(prog, funcs[MC_UPDATE])
                 if prog.word(a - 8) == jal(funcs[DRAW_BUFFER]) and prog.word(a + 8) == jal(funcs[MENU_STATES])]
        if len(sites) != 1:
            raise SystemExit(f"{label}: {len(sites)} main loop calls of MemCard_UpdateState, not 1")
        out.append((sites[0], jal(funcs[MC_UPDATE]), jal(symbol(f"ee_title_card_{fe_id}"))))
    return out


def frontbin_patches(openrac: Path, iso: Path, symbol, features) -> list[tuple[int, int, int]]:
    """(byte offset in ISO, retail word, new word) of frontbin's patches for FEATURES."""
    prog, funcs, data = frontends(openrac, iso)[1]
    return [(prog.file_offset(a), old, new) for a, old, new in patches(prog, funcs, data, FE_BIN, symbol, features)]


def emit(openrac: Path, out: Path) -> None:
    fes = frontends(openrac)
    lines = ["/* generated by tools/gen_frontend.py */"]
    # The Video page's entries with the 16:9 row: no program's own addresses in them, so one copy serves both.
    video = [prog.words(d["video_entries"], 5) for prog, _, d in fes]
    if video[0] != video[1]:
        raise SystemExit("the two frontends' Video entries differ")
    rows = video[0] + [TEXT_WIDESCREEN, SETTING_WIDESCREEN, TOGGLE_ON, TOGGLE_OFF, 0] + [0] * 5
    lines.append("/* the title screen's Video page: Video mode, then 16:9 (widescreen) (src/widescreen.c) */")
    lines.append(f"unsigned int ee_fe_video_entries[{len(rows)}] = {{ {', '.join(f'0x{w:X}' for w in rows)} }};")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    for (_, fn, d), label in zip(fes, ("boot", "frontbin")):
        print(f"{label}: " + ", ".join(f"{k} 0x{v:08X}" for k, v in fn.items())
              + f", Sound widgets {', '.join(f'0x{w:08X}' for w in d['sound_widgets'])}"
              + f", Video list 0x{d['video_widget']:08X}"
              f", reset calls {', '.join(f'0x{a:08X}' for a in d['reset_calls'])}")
    print(f"wrote {out}")


def patch_elf(openrac: Path, path: Path) -> None:
    sys.path.insert(0, str(HERE))
    from overlay_patch import modseg_symbol
    image = bytearray(path.read_bytes())
    prog, funcs, data = frontends(openrac)[0]
    live = elf_program(bytes(image))
    words = patches(prog, funcs, data, FE_BOOT, modseg_symbol)
    for addr, old, new in words:
        off = live.file_offset(addr)
        cur, = struct.unpack_from("<I", image, off)
        if cur not in (old, new):
            raise SystemExit(f"{path}: at 0x{addr:08X} found {cur:08X}, expected {old:08X}")
        struct.pack_into("<I", image, off, new)
    path.write_bytes(image)
    print(f"{path}: the title screen's hooks ({len(words)} words)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("emit")
    e.add_argument("-o", "--output", type=Path, default=ROOT / "build/modseg/gen_frontend.c")
    p = sub.add_parser("patch-elf")
    p.add_argument("elf", type=Path)
    args = ap.parse_args()
    if args.cmd == "emit":
        emit(args.openrac, args.output)
    else:
        patch_elf(args.openrac, args.elf)
    return 0


if __name__ == "__main__":
    sys.exit(main())
