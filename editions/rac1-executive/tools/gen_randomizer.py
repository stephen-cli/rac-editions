#!/usr/bin/env python3
"""
The Randomizer's hook sites and giver classes, per level (src/randomizer_play.c).

Every gadget and weapon the game gives goes through GiveItem
(func_L00_002618D8), every infobot through UnlockPlanet (func_L00_00261848),
and the planet banner through ShowPlanetBanner (func_L00_00263DB0); the
givers autosave with memcard_Save (func_0020BFC8), which serialises the
inventory the moment it is called (a Gold Bolt spot's, inside GoldBoltUpdate,
func_L01_00309078, comes once its flag is set); the Gold Weapons' vendor counts
the bolts with count_nonzero_entries_up_to_40 (func_002160E0); the vendor
builds its list with VendorBuildItemList (func_L00_0029B770) on opening and
after each purchase, and shows the item through LoadMobyClassResources
(func_00205270) in Vendor_InitItemDisplay; a giver's own banner after its
GiveItem is a ShowBanner call in the same function; the save pages' slot
list (Pause_DrawSaveSlots, func_00224C30) ends its drawing with DoGifPaging
(func_001F4748), where the pause menu's Randomizer badges go; the moby
update loop (MobyUpdateLoop, func_00213C78) calls every moby's update at
its + 0x58 (jalr $3), and MobyLoadTimeUpdatePass (func_L00_002657B8) at
its + 0xC8 once at a level's load, before that loop's first run, which is
where a giver decides to stay or go; the level's init calls InitLevelResidentState
(func_L00_0023D9C0) at every load, a reload of the same level too; and
Hud_RegisterHealthBank (func_L00_00235790) loads
HudHealthDraw's address (lui/addiu), which the edition's drawer takes over
for the 7-orb health of the Ultra Nanotech without the Premium. This scans
each level's code for those places and:

  emits build/modseg/gen_rz.c    per level, the classes whose update gets the
                                 collected view (src/randomizer_play.c): the
                                 UpdateMoby roles (config/overlays/names.tsv) of
                                 the functions the GiveItem and UnlockPlanet
                                 calls sit in, plus the givers that set a flag
                                 instead (GIVER_FUNCTIONS); and the banners'
                                 words from the game's text table
  patches(level, at)             the call sites as (address, retail word, new
                                 word) for tools/overlay_patch.py's feature
                                 randomizer, redirected to that level's stubs
                                 (src/rz_hooks.s)

  python tools/gen_randomizer.py [--openrac DIR]
"""
import argparse
import bisect
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))

GIVE_ITEM = "func_L00_002618D8"
SHOW_BANNER = "func_L00_00264DB8"       # ShowBanner(text, ticks): GiveItem's own call shows the item's banner
UNLOCK_PLANET = "func_L00_00261848"
PLANET_BANNER = "func_L00_00263DB0"
GOLD_BOLT = "func_L01_00309078"
SAVE = "func_0020BFC8"
GOLD_COUNT = "func_002160E0"
BUILD_LIST = "func_L00_0029B770"        # VendorBuildItemList(gold)
PAUSE_SLOTS = "func_00224C30"           # Pause_DrawSaveSlots: the save pages' slot list, the pause menu's too
END_PAGING = "func_001F4748"            # DoGifPaging, its last call
ITEM_DISPLAY = "func_L00_0029C9E8"      # Vendor_InitItemDisplay: the item shown, through LoadMobyClassResources
LOAD_CLASS = "func_00205270"
HEALTH_BANK = "func_L00_00235790"       # Hud_RegisterHealthBank: HudHealthDraw's address in its lui/addiu
LEVEL_INIT = "func_L00_0023D9C0"        # InitLevelResidentState, called once per level load by the level's init
ICON_STRIP = "func_00239A00"            # VendorDrawIconStrip: GetIconFrame per entry (frame 0 a purchase, 2 ammo)
ITEM_PANEL = "func_00239CF8"            # VendorDrawItemPanel: msg_string for the entry's name
GET_ICON = "func_00200198"
MSG_STRING = "func_001FE540"
INIT_MOBY = "func_0020D440"             # InitMobyInstance: the item display's models (Vendor_InitItemDisplay)
PLACE_MOBY = "func_L00_00251E30"        # the display's placement of each model, after its pose is set
ADDIU_A0_HOLOGRAM = 0x24840300          # addiu $a0, $a0, 0x300: the vendor's hologram model
VEC_ADD = "func_001F9BD8"               # FastVecAdd(out, a, b)
ADDU_A2_A2_MASK, ADDU_A2_A2 = 0xFFE0FFFF, 0x00C03021   # addu $a2, $a2, $rt: b is a table's entry
MENU_UPDATE = "func_L00_0029D988"       # VendorMenuUpdate: msg_string for the selected item's slogan
HEALTH_DRAW = "func_L00_0023A1A0"
HEALTH_DRAW_HI, HEALTH_DRAW_LO = 0xC, 0x20
LUI_T0, ADDIU_T0_T0 = 0x3C080000, 0x25080000
UPDATE_LOOP = "func_00213C78"
UPDATE_CALL = 0x58                      # MobyUpdateLoop + 0x58: jalr $3, the moby in $a0
LOAD_PASS = "func_L00_002657B8"          # MobyLoadTimeUpdatePass: every moby's update once at the level's load,
LOAD_PASS_CALL = 0xC8                    # before the first MobyUpdateLoop, + 0xC8: jalr $3 as well
JALR_V1 = 0x0060F809
# Givers that set a flag or a planet rather than call GiveItem, and the pickups that
# delete themselves once their reward is owned: their classes get the view too.
GIVER_FUNCTIONS = ("func_L00_002D7298",    # InfobotUpdate: deletes itself once its planet is unlocked
                   "func_L10_002E9718",    # NanotechVendorUpdate: the two upgrades' flags
                   "func_L11_002CC950",    # Moby37Update: the Raritanium
                   "func_L00_002A5218",    # Moby1005Update: the Trespasser and Hydrodisplacer pickups
                   "func_L00_002C2A20")    # class 188's update, whose helper Moby188_StateStep gives an item
LEVELS = 19


def jal(target):
    return 0x0C000000 | ((target >> 2) & 0x3FFFFFF)


def roles(openrac: Path) -> dict:
    """function name -> {oClass: set(levels)} from names.tsv's UpdateMoby roles."""
    out = {}
    for line in (openrac / "games/rac1/config/overlays/names.tsv").read_text().splitlines():
        if line.startswith("#") or "\t" not in line:
            continue
        name, role_list = line.split("\t", 1)
        for role in role_list.split(","):
            if ":" not in role or not role.startswith("UpdateMoby_"):
                continue
            kind, levels = role.split(":", 1)
            cls = int(kind[len("UpdateMoby_"):])
            out.setdefault(name, {}).setdefault(cls, set()).update(int(x) for x in levels.split("/"))
    return out


class Level:
    """One level's code, with the catalogue's functions over it."""

    def __init__(self, level: int, places: dict, sizes: dict, text: bytes, base: int):
        self.level = level
        self.text = text
        self.base = base
        funcs = sorted((addrs[level], sizes.get(name, 4), name) for name, addrs in places.items() if level in addrs)
        self.starts = [f[0] for f in funcs]
        self.funcs = funcs
        self.place = {name: addrs[level] for name, addrs in places.items() if level in addrs}
        self.sizes = sizes

    def function_at(self, addr: int):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i >= 0 and self.funcs[i][0] <= addr < self.funcs[i][0] + self.funcs[i][1]:
            return self.funcs[i][2]
        return None

    def function_bounds(self, addr: int):
        """(start, size) of the catalogued function holding ADDR, or None."""
        i = bisect.bisect_right(self.starts, addr) - 1
        if i >= 0 and self.funcs[i][0] <= addr < self.funcs[i][0] + self.funcs[i][1]:
            return self.funcs[i][0], self.funcs[i][1]
        return None

    def calls(self, target: int, lo: int = 0, hi: int = 0xFFFFFFFF):
        """Addresses of every `jal target` in the code (within [lo, hi))."""
        want = jal(target)
        out = []
        for i, (w,) in enumerate(struct.iter_unpack("<I", self.text[:len(self.text) & ~3])):
            if w == want:
                a = self.base + 4 * i
                if lo <= a < hi:
                    out.append(a)
        return out

    def word(self, addr: int) -> int:
        return struct.unpack_from("<I", self.text, addr - self.base)[0]


def levels(openrac: Path, iso: Path | None = None):
    from overlay_patch import load_catalogue, level_text_locations, SIZES
    places = load_catalogue(openrac)
    iso, texts = level_text_locations(openrac, iso)
    out = {}
    with iso.open("rb") as f:
        for level in range(LEVELS):
            file_base, addr, size = texts[level][0]
            f.seek(file_base)
            out[level] = Level(level, places, SIZES, f.read(size), addr)
    return out


def sites(lv: Level) -> dict:
    """The hooked call sites of one level: kind -> [address]."""
    start = lv.place[GIVE_ITEM]
    out = {"give_item": lv.calls(start),
           "item_banner": lv.calls(lv.place[SHOW_BANNER], start, start + lv.sizes[GIVE_ITEM]),
           "unlock_planet": lv.calls(lv.place[UNLOCK_PLANET]),
           "vendor_class": lv.calls(lv.place[LOAD_CLASS], lv.place[ITEM_DISPLAY],
                                    lv.place[ITEM_DISPLAY] + lv.sizes[ITEM_DISPLAY]),
           "level_init": lv.calls(lv.place[LEVEL_INIT]),
           "vendor_icon": lv.calls(lv.place[GET_ICON], lv.place[ICON_STRIP],
                                   lv.place[ICON_STRIP] + lv.sizes[ICON_STRIP]),
           "vendor_text": lv.calls(lv.place[MSG_STRING], lv.place[ITEM_PANEL],
                                   lv.place[ITEM_PANEL] + lv.sizes[ITEM_PANEL]),
           "vendor_model": lv.calls(lv.place[INIT_MOBY], lv.place[ITEM_DISPLAY],
                                    lv.place[ITEM_DISPLAY] + lv.sizes[ITEM_DISPLAY]),
           "vendor_place": lv.calls(lv.place[PLACE_MOBY], lv.place[ITEM_DISPLAY],
                                    lv.place[ITEM_DISPLAY] + lv.sizes[ITEM_DISPLAY]),
           "vendor_slogan": lv.calls(lv.place[MSG_STRING], lv.place[MENU_UPDATE],
                                     lv.place[MENU_UPDATE] + lv.sizes[MENU_UPDATE]),
           # the hologram's placement each frame, after its pose is copied from the item's table:
           # the call whose delay slot makes the hologram the argument (addiu $a0, $a0, 0x300)
           "vendor_holo": [a for a in lv.calls(lv.place[PLACE_MOBY], lv.place[MENU_UPDATE],
                                               lv.place[MENU_UPDATE] + lv.sizes[MENU_UPDATE])
                           if lv.word(a + 4) == ADDIU_A0_HOLOGRAM],
           "planet_banner": lv.calls(lv.place[PLANET_BANNER]),
           "gold_count": lv.calls(lv.place[GOLD_COUNT]),
           "save_call": lv.calls(lv.place[SAVE]),
           "vendor_list": lv.calls(lv.place[BUILD_LIST]),
           "slots_end": []}
    # the givers' own banners after their GiveItem (Al's "You got the Heli-Pack upgrade!"): every
    # ShowBanner call in a function that holds a GiveItem or UnlockPlanet call
    for kind in ("give_item", "unlock_planet"):
        for addr in out[kind]:
            bounds = lv.function_bounds(addr)
            if bounds is not None and bounds[0] != start:
                out["item_banner"] += [a for a in lv.calls(lv.place[SHOW_BANNER], bounds[0], bounds[0] + bounds[1])
                                       if a not in out["item_banner"]]
    out["item_banner"].sort()
    start = lv.place[PAUSE_SLOTS]
    out["slots_end"] = lv.calls(lv.place[END_PAGING], start, start + lv.sizes[PAUSE_SLOTS])
    if len(out["slots_end"]) != 1:
        raise SystemExit(f"level {lv.level}: Pause_DrawSaveSlots calls DoGifPaging {len(out['slots_end'])} times, "
                         "not once")
    # the hologram's two per-item offsets, tuned to the original weapon: its place in the beam (the
    # FastVecAdd before the placement whose delay slot indexes the table, addu $a2, $a2, $rt) and the
    # pivot its spin swings around (the first FastVecAdd after the placement)
    out["vendor_holo_add"] = []
    if len(out["vendor_holo"]) == 1:
        h = out["vendor_holo"][0]
        adds = lv.calls(lv.place[VEC_ADD], lv.place[MENU_UPDATE], lv.place[MENU_UPDATE] + lv.sizes[MENU_UPDATE])
        before = [a for a in adds if a < h and (lv.word(a + 4) & ADDU_A2_A2_MASK) == ADDU_A2_A2]
        after = [a for a in adds if a > h]
        if before and after:
            out["vendor_holo_add"] = [before[-1], after[0]]
    if len(out["vendor_holo_add"]) != 2:
        raise SystemExit(f"level {lv.level}: the hologram's offset additions were not found")
    return out


def classes(lv: Level, role_map: dict, found: dict) -> list[int]:
    """The giver classes of the level: of the functions holding the GiveItem and UnlockPlanet
    calls (the function itself, or the update function of its class), and GIVER_FUNCTIONS."""
    names = set(GIVER_FUNCTIONS)
    for kind in ("give_item", "unlock_planet"):
        for addr in found[kind]:
            name = lv.function_at(addr)
            if name is None:
                print(f"level {lv.level}: a call at 0x{addr:08X} is in no catalogued function", file=sys.stderr)
                continue
            names.add(name)
    out = set()
    for name in sorted(names):
        for cls, in_levels in role_map.get(name, {}).items():
            if lv.level in in_levels:
                out.add(cls)
        if name not in role_map and name not in GIVER_FUNCTIONS and name in lv.place:
            # a helper, not an update function: the view must come from the class that calls it
            print(f"level {lv.level}: {name} gives an item but is no moby update (its class needs listing)",
                  file=sys.stderr)
    return sorted(out)


def patches(level: int, at) -> list[tuple[int, int | None, int]]:
    """(address, retail word, new word) for level LEVEL (tools/overlay_patch.py feature randomizer)."""
    from overlay_patch import modseg_symbol
    lv = Level(level, at.places, at.sizes, at.blob(*at.text), at.text[0])
    found = sites(lv)
    out = []
    for kind, stub in (("give_item", "ee_rz_give_item"), ("item_banner", "ee_rz_item_banner"),
                       ("unlock_planet", "ee_rz_unlock_planet"), ("planet_banner", "ee_rz_planet_banner"),
                       ("save_call", "ee_rz_save"), ("gold_count", "ee_rz_gold_count"),
                       ("vendor_list", "ee_rz_vendor_list"), ("slots_end", "ee_rz_slots_end"),
                       ("vendor_class", "ee_rz_vendor_class"), ("level_init", "ee_rz_level_init"),
                       ("vendor_icon", "ee_rz_vendor_icon"), ("vendor_text", "ee_rz_vendor_text"),
                       ("vendor_model", "ee_rz_vendor_model"), ("vendor_place", "ee_rz_vendor_place"),
                       ("vendor_slogan", "ee_rz_vendor_slogan"), ("vendor_holo", "ee_rz_vendor_holo"),
                       ("vendor_holo_add", "ee_rz_vendor_holo_add")):
        for addr in found[kind]:
            out.append((addr, lv.word(addr), jal(modseg_symbol(f"{stub}_{level:02d}"))))
    # HudHealthDraw's address, as Hud_RegisterHealthBank materialises it: the edition's drawer instead
    bank = lv.place[HEALTH_BANK]
    hi_word, lo_word = lv.word(bank + HEALTH_DRAW_HI), lv.word(bank + HEALTH_DRAW_LO)
    draw = lv.place[HEALTH_DRAW]
    if (hi_word & 0xFFFF0000) != LUI_T0 or (lo_word & 0xFFFF0000) != ADDIU_T0_T0 \
            or ((hi_word & 0xFFFF) << 16) + ((lo_word & 0xFFFF) ^ 0x8000) - 0x8000 != draw:
        raise SystemExit(f"level {level}: Hud_RegisterHealthBank does not load HudHealthDraw at +0x{HEALTH_DRAW_HI:X}")
    stub = modseg_symbol(f"ee_rz_health_draw_{level:02d}")
    lo = stub & 0xFFFF
    hi = (stub >> 16) + (1 if lo & 0x8000 else 0)
    out.append((bank + HEALTH_DRAW_HI, hi_word, LUI_T0 | hi))
    out.append((bank + HEALTH_DRAW_LO, lo_word, ADDIU_T0_T0 | lo))
    loop = lv.place[UPDATE_LOOP] + UPDATE_CALL
    if lv.word(loop) != JALR_V1:
        raise SystemExit(f"level {level}: MobyUpdateLoop + 0x{UPDATE_CALL:X} is not jalr $3 ({lv.word(loop):08X})")
    out.append((loop, JALR_V1, jal(modseg_symbol(f"ee_rz_moby_update_{level:02d}"))))
    # the load-time pass: a giver decides to stay or go at its first update, which is this one
    # (the Magneboots pickup on a revisit of Orxon decided there without the collected view)
    load = lv.place[LOAD_PASS] + LOAD_PASS_CALL
    if lv.word(load) != JALR_V1:
        raise SystemExit(f"level {level}: MobyLoadTimeUpdatePass + 0x{LOAD_PASS_CALL:X} is not jalr $3 "
                         f"({lv.word(load):08X})")
    out.append((load, JALR_V1, jal(modseg_symbol(f"ee_rz_moby_update_{level:02d}"))))
    return out


# The Randomizer's banners (src/randomizer_play.c) are the edition's "You found the @!" / "You got
# the @!" / "You won a @!" (text/strings.tsv) with the game's own name of the item at the @, from the
# game's text table in all five languages: each item's name (the item records), the special items'
# (Zoomerator, Raritanium, CodeBot, Premium and Ultra Nanotech) and "Gold Bolt"; an infobot shows
# retail's own banner for its planet instead (1 Novalis .. 18 Veldin: "Infobot for Planet Aridia
# acquired").
INFOBOT_BANNERS = (0x0006, 0x03F1, 0x03F2, 0x0BC6, 0x177B, 0x0FA6, 0x1394, 0x1B58, 0x1F47, 0x1F48, 0x2719, 0x271A,
                   0x2EE5, 0x32C9, 0x36B1, 0x3AA1, 0x3AA2, 0x4269)
SPECIAL_NAMES = (0x4E74, 0x4E75, 0x4E77, 0x4E71, 0x4E72)
GOLD_BOLT_WORD = 0x4E9F
INFOBOT_WORD = 0x4E7C                   # "Infobot": redacted in an infobot check's prompts
LANGUAGE_TABLES = (0, 2, 3, 4, 5)       # the text table's sub-tables, which are the game's language ids
ITEM_COUNT = 37
BANNER_MAX = 60                         # a name or an infobot banner; src/randomizer_play.c's buffers hold more


def c_bytes_string(raw: bytes) -> str:
    """A C string of the game's own bytes (its font codes included), octal-escaped where needed."""
    out = []
    for b in raw:
        if b == 0x22 or b == 0x5C:
            out.append("\\" + chr(b))
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            out.append("\\%03o" % b)
    return '"' + "".join(out) + '"'


def load_texts(openrac: Path) -> dict:
    """The text table by language id: {sid: bytes}."""
    import text_table
    tables = text_table.parse(text_table.read_table(openrac))
    return {t: {sid: s for sid, s, _a, _b in tables[t]} for t in LANGUAGE_TABLES}


def row(cells: dict, comment: str) -> str:
    """A C initializer of 8 strings by language id from {language: bytes} (0 where a language lacks it)."""
    out = ["0"] * 8
    for t, s in cells.items():
        if len(s) > BANNER_MAX:
            raise SystemExit(f"{comment}: a banner of {len(s)} bytes, over BANNER_MAX")
        out[t] = c_bytes_string(s)
    return "    { " + ", ".join(out) + " },  /* " + comment + " */"


def text_cells(texts: dict, sid: int) -> dict:
    return {t: texts[t][sid] for t in LANGUAGE_TABLES if sid in texts[t]}


def item_name_ids(openrac: Path) -> list[int]:
    """The name text id of each item record (0-36), from level 1's data."""
    import struct
    import gen_items
    pal = openrac / "games/rac1"
    base, d = gen_items.level_record(pal, 1)
    table = gen_items.find_items(pal, 1)["table"]
    return [struct.unpack_from("<I", d, table + 0x4C * i - base)[0] & 0xFFFF for i in range(ITEM_COUNT)]


def banner_tables(openrac: Path) -> list[str]:
    """The C tables of the banners' words (src/randomizer_play.c)."""
    texts = load_texts(openrac)
    lines = [f"const char *const ee_rz_item_names[{ITEM_COUNT}][8] = {{"]
    for i, sid in enumerate(item_name_ids(openrac)):
        lines.append(row(text_cells(texts, sid) if sid else {}, f"item {i}: 0x{sid:04X}"))
    lines.append("};")
    lines.append(f"const char *const ee_rz_infobot_banners[{len(INFOBOT_BANNERS)}][8] = {{")
    lines += [row(text_cells(texts, sid), f"planet {p + 1}: 0x{sid:04X}") for p, sid in enumerate(INFOBOT_BANNERS)]
    lines.append("};")
    lines.append(f"const char *const ee_rz_special_names[{len(SPECIAL_NAMES)}][8] = {{")
    lines += [row(text_cells(texts, sid), f"0x{sid:04X}") for sid in SPECIAL_NAMES]
    lines.append("};")
    lines.append("const char *const ee_rz_word_gold_bolt[8] =")
    lines.append(row(text_cells(texts, GOLD_BOLT_WORD), f"0x{GOLD_BOLT_WORD:04X}").replace("},  /*", "};  /*", 1))
    lines.append("const char *const ee_rz_word_infobot[8] =")
    lines.append(row(text_cells(texts, INFOBOT_WORD), f"0x{INFOBOT_WORD:04X}").replace("},  /*", "};  /*", 1))
    return lines


def emit(openrac: Path, out: Path) -> None:
    role_map = roles(openrac)
    lines = ["/* generated by tools/gen_randomizer.py: per level, the classes whose moby update runs under",
             "   the Randomizer's collected view (src/randomizer_play.c); 0xFFFF ends a list. Then the",
             "   banners' words from the game's text table, by the game's language id. */",
             '#include "modseg.h"']
    lines += banner_tables(openrac)
    for level, lv in sorted(levels(openrac).items()):
        found = sites(lv)
        cls = classes(lv, role_map, found)
        counts = ", ".join(f"{len(v)} {k}" for k, v in found.items())
        print(f"level {level:2d}: {counts}; classes {cls}")
        body = ", ".join(str(c) for c in cls + [0xFFFF])
        lines.append(f"static const u16 classes_{level:02d}[] = {{ {body} }};")
    lines.append("const u16 *const ee_rz_classes[19] = {")
    lines.append("    " + ", ".join(f"classes_{level:02d}" for level in range(LEVELS)))
    lines.append("};")
    out.mkdir(parents=True, exist_ok=True)
    (out / "gen_rz.c").write_text("\n".join(lines) + "\n")
    print(f"wrote {out / 'gen_rz.c'}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/modseg")
    args = ap.parse_args()
    emit(args.openrac, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
