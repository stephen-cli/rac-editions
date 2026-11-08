#!/usr/bin/env python3
"""
The Randomizer's hook sites and giver classes, per level (src/randomizer_play.h).

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
for the 7-orb health of the Ultra Nanotech without the Premium. The pause
menu's Missions page and map ask Mission_UpdateObjectives (func_0020C7A0)
for the shown planet's mission statuses, and the map draws its markers'
icons through GetIconFrame (func_00200198) in DrawMapView (func_00205E70).
This scans each level's code for those places and:

  emits build/modseg/gen_rz.c    per level, the classes whose update gets the
                                 collected view (src/randomizer_play.c): the
                                 UpdateMoby roles (config/overlays/names.tsv) of
                                 the functions the GiveItem and UnlockPlanet
                                 calls sit in, plus the givers that set a flag
                                 instead (GIVER_FUNCTIONS); and the banners'
                                 words from the game's text table; and the
                                 Missions page's and the map's texts and
                                 item markers that name a check's reward,
                                 from the executable's mission tables
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
PLACE_MOBYS = "func_L00_002422D8"       # InitLevelRenderGlobals, which places the level's mobys from their records
CLEAR_MOBYS = 0xF18                     # + 0xF18: fill_transfer_words(table, 0, size), the moby table cleared right
FILL_WORDS = "func_001F99B0"            # after the records are found and before the first is read
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
MISSION_UPDATE = "func_0020C7A0"        # Mission_UpdateObjectives: the shown planet's mission statuses (pause menu)
MISSION_UPDATE_CALLS = 5                # EnterMenuMode, DrawMapScreen, MissionMenu_Init, LevelSelectMenu_Update,
                                        # initialize_pause_menu
MAP_VIEW = "func_00205E70"              # DrawMapView: the pause map's markers, each through GetIconFrame
MAP_VIEW_ICON_CALLS = 5
HEALTH_DRAW_HI, HEALTH_DRAW_LO = 0xC, 0x20
LUI_T0, ADDIU_T0_T0 = 0x3C080000, 0x25080000
UPDATE_LOOP = "func_00213C78"
UPDATE_CALL = 0x58                      # MobyUpdateLoop + 0x58: jalr $3, the moby in $a0
LOAD_PASS = "func_L00_002657B8"          # MobyLoadTimeUpdatePass: every moby's update once at the level's load,
LOAD_PASS_CALL = 0xC8                    # before the first MobyUpdateLoop, + 0xC8: jalr $3 as well
JALR_V1 = 0x0060F809
# Givers that set a flag or a planet rather than call GiveItem, the pickups that delete themselves
# once their reward is owned, and the sequences that follow a pickup once its reward is owned: their
# classes get the view too.
GIVER_FUNCTIONS = ("func_L00_002D7298",    # InfobotUpdate: deletes itself once its planet is unlocked
                   "func_L10_002E9718",    # NanotechVendorUpdate: the two upgrades' flags
                   "func_L11_002CC950",    # Moby37Update: the Raritanium
                   "func_L00_002A5218",    # Moby1005Update: the Trespasser and Hydrodisplacer pickups
                   "func_L00_002C2A20",    # class 188's update, whose helper Moby188_StateStep gives an item
                   "func_L05_00317F80",    # UpdateMoby_918, the Rilgar race's host: the Zoomerator's flag at a win
                   "func_L06_002F69D0",    # UpdateMoby_1028: Blarg Station's Hydrodisplacer, once owned with Clank
                                           # out: the bridge, trigger, HelpDesk line, banner and autosave
                   "func_L06_002FDA70")    # UpdateMoby_1061: its scene 3, once the Hydrodisplacer is owned
# Givers that set a flag and show their own banner, right after it or on a later update after their
# autosave: the Rilgar race's "You won a Platinum Zoomerator!", the Nanotech vendor's "Premium/Ultra
# Nanotech" message and Blarg Station's "You've found the Hydrodisplacer!" would replace the Randomizer's
# banner.
FLAG_BANNER_FUNCTIONS = ("func_L05_00317F80",    # UpdateMoby_918
                         "func_L10_002E9718",    # NanotechVendorUpdate
                         "func_L06_002F69D0")    # UpdateMoby_1028
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
    for name in FLAG_BANNER_FUNCTIONS:
        if name in lv.place:
            begin = lv.place[name]
            out["item_banner"] += [a for a in lv.calls(lv.place[SHOW_BANNER], begin, begin + lv.sizes[name])
                                   if a not in out["item_banner"]]
    out["item_banner"].sort()
    out["mission_update"] = lv.calls(lv.place[MISSION_UPDATE])
    if len(out["mission_update"]) != MISSION_UPDATE_CALLS:
        raise SystemExit(f"level {lv.level}: {len(out['mission_update'])} calls of Mission_UpdateObjectives, "
                         f"not {MISSION_UPDATE_CALLS}")
    view = lv.place[MAP_VIEW]
    out["map_icon"] = lv.calls(lv.place[GET_ICON], view, view + lv.sizes[MAP_VIEW])
    if len(out["map_icon"]) != MAP_VIEW_ICON_CALLS:
        raise SystemExit(f"level {lv.level}: DrawMapView calls GetIconFrame {len(out['map_icon'])} times, "
                         f"not {MAP_VIEW_ICON_CALLS}")
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
                       ("vendor_holo_add", "ee_rz_vendor_holo_add"),
                       ("mission_update", "ee_rz_mission_update"), ("map_icon", "ee_rz_map_icon")):
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
    # the placing of the level's mobys, where their records are in place and not yet read (Umbris's exit lift)
    clear = lv.place[PLACE_MOBYS] + CLEAR_MOBYS
    if lv.word(clear) != jal(lv.place[FILL_WORDS]):
        raise SystemExit(f"level {level}: InitLevelRenderGlobals + 0x{CLEAR_MOBYS:X} is not jal {FILL_WORDS} "
                         f"({lv.word(clear):08X})")
    out.append((clear, lv.word(clear), jal(modseg_symbol(f"ee_rz_place_mobys_{level:02d}"))))
    return out


# The Randomizer's banners (src/randomizer_text.c) are the edition's "You found the @!" / "You got
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
BANNER_MAX = 60                         # a name or an infobot banner; src/randomizer_text.c's buffers hold more
RAC1_ISO = "games/rac1/baserom/SCES_509.16.iso"   # in the OpenRAC checkout
NO_WORD = 0xFFFF                        # a table entry with no word: src/randomizer_text.c NO_WORD


def load_texts(iso: Path) -> dict:
    """The PAL disc's text table by language id: {sid: bytes}."""
    import text_table
    tables = text_table.parse(text_table.read_table(iso))
    return {t: {sid: s for sid, s, _a, _b in tables[t]} for t in LANGUAGE_TABLES}


class Words:
    """The game's words for the banners, gathered into one blob, ee_rz_words: each string once, NUL-ended, in the
    order first met. The tables name a string by its offset in the blob, NO_WORD where a language lacks it, so the
    blob is plain bytes the installer fills from the player's disc while the tables are only numbers. The recipe
    is the blob's strings as (language, text id), in order: words_blob replays it on the disc's text table."""

    def __init__(self, texts: dict):
        self.texts = texts
        self.blob = bytearray()
        self.at = {}
        self.recipe = []

    def row(self, sid: int, comment: str) -> str:
        """A C initializer of 8 offsets by language id for text SID (none for SID 0)."""
        out = [f"0x{NO_WORD:04X}"] * 8
        for t in LANGUAGE_TABLES:
            s = self.texts[t].get(sid) if sid else None
            if s is None:
                continue
            if len(s) > BANNER_MAX:
                raise SystemExit(f"{comment}: a banner of {len(s)} bytes, over BANNER_MAX")
            if s not in self.at:
                self.at[s] = len(self.blob)
                self.blob += s + b"\0"
                self.recipe.append((t, sid))
            out[t] = f"0x{self.at[s]:04X}"
        return "    { " + ", ".join(out) + " },  /* " + comment + " */"


def words_blob(iso: Path, recipe) -> bytes:
    """ee_rz_words from the PAL disc's text table and Words' recipe."""
    texts = load_texts(iso)
    return b"".join(texts[t][sid] + b"\0" for t, sid in recipe)


def item_name_ids(openrac: Path) -> list[int]:
    """The name text id of each item record (0-36), from level 1's data."""
    import struct
    import gen_items
    pal = openrac / "games/rac1"
    base, d = gen_items.level_record(pal, 1)
    table = gen_items.find_items(pal, 1)["table"]
    return [struct.unpack_from("<I", d, table + 0x4C * i - base)[0] & 0xFFFF for i in range(ITEM_COUNT)]


def banner_words(openrac: Path) -> tuple[list[str], bytes, list]:
    """The C tables of the banners' words (src/randomizer_text.c), the blob they index and its recipe."""
    words = Words(load_texts(openrac / RAC1_ISO))
    lines = [f"const u16 ee_rz_item_names[{ITEM_COUNT}][8] = {{"]
    for i, sid in enumerate(item_name_ids(openrac)):
        lines.append(words.row(sid, f"item {i}: 0x{sid:04X}"))
    lines.append("};")
    lines.append(f"const u16 ee_rz_infobot_banners[{len(INFOBOT_BANNERS)}][8] = {{")
    lines += [words.row(sid, f"planet {p + 1}: 0x{sid:04X}") for p, sid in enumerate(INFOBOT_BANNERS)]
    lines.append("};")
    lines.append(f"const u16 ee_rz_special_names[{len(SPECIAL_NAMES)}][8] = {{")
    lines += [words.row(sid, f"0x{sid:04X}") for sid in SPECIAL_NAMES]
    lines.append("};")
    lines.append("const u16 ee_rz_word_gold_bolt[8] =")
    lines.append(words.row(GOLD_BOLT_WORD, f"0x{GOLD_BOLT_WORD:04X}").replace("},  /*", "};  /*", 1))
    lines.append("const u16 ee_rz_word_infobot[8] =")
    lines.append(words.row(INFOBOT_WORD, f"0x{INFOBOT_WORD:04X}").replace("},  /*", "};  /*", 1))
    return lines, bytes(words.blob), words.recipe


def banner_tables(openrac: Path, out: Path) -> list[str]:
    """banner_words' tables and the blob as C; the blob's recipe goes to OUT/gen_rz_words.json."""
    import json
    lines, blob, recipe = banner_words(openrac)
    (out / "gen_rz_words.json").write_text(json.dumps(recipe) + "\n")
    lines.append(f"const char ee_rz_words[{len(blob)}] __attribute__((aligned(16))) = {{")
    lines += ["    " + ", ".join(f"0x{b:02X}" for b in blob[i:i + 16]) + "," for i in range(0, len(blob), 16)]
    lines.append("};")
    return lines


# The world's stand-ins (src/randomizer_world.c) of the weapons and the gadgets Ratchet holds are unpacked from the
# level's gadget directory into the free RAM above the effect pool. The directory is the core index's {count, offset}
# at +0x80, entries {core offset, class id, compressed size, 0}, each a WAD stream; the same 21 classes in every level.
GADGET_TABLE = 0x80
# The effect pool's start, a word every level program reads gp-relative in create_part2 (func_L00_00268760, its
# third lw from $gp, which it keeps in $t6); $gp is the same in every level program.
EFFECT_POOL_FUNC = "func_L00_00268760"
EFFECT_POOL_LW = 0x1C
GAME_GP = 0x166D00


# The pause menu's Missions page and map (src/randomizer_text.c, src/randomizer_map.c). Each planet's missions
# are a list of 0x28-byte nodes in the executable's data (D_001A2EF0[planet], ended by id 0): +0x00 the short
# text, +0x02/+0x04 the reveal condition (type, argument), +0x08/+0x0C the done condition, +0x14 up to four
# long texts, +0x1C a callback choosing among them. Mission_CheckCondition (func_0020C940) reads type 1 the
# planet unlocked, 2 the item owned, 3 the item "had", 6 a flag byte (0-7 the special items, higher ones level
# events), 7 a predicate's answer; types 4 and 5 read level progress. The map's markers are 0x28-byte icons
# (D_001A2F40[planet], ended by flags & 4): +0x02 the node whose status shows the marker, +0x06/+0x08 the
# texture and frame, +0x0A the label text. Every level program carries the same tables, relocated, so they are
# read once from the executable.
EXECUTABLE = "games/rac1/baserom/SCES_509.16"   # in the OpenRAC checkout
MISSION_LISTS, MAP_ICON_LISTS = 0x1A2EF0, 0x1A2F40
NODE_SIZE = ICON_SIZE = 0x28
ICON_LIST_END = 0x4
SOURCE_PLANET, SOURCE_ITEM, SOURCE_FLAG = 1, 2, 6   # src/randomizer_play.h RZ_SOURCE_*
SPECIAL_FLAGS = 8
MISSION_SOURCES = 4                     # src/randomizer_play.h RZ_MISSION_SOURCES
# The inventory bytes the type-7 conditions read (MissionCond_*, src/game/missionfunc.c in rac1-decomp); their
# other reads are level progress and level events.
PREDICATE_ITEMS = {
    0x20CB80: [],                                           # MissionCond_Obj18AndSpecial18
    0x20CBA8: [],                                           # MissionCond_Obj2FAndSpecials
    0x20CBE0: [],                                           # MissionCond_Obj3FAnd40
    0x20CC10: [],                                           # MissionCond_Obj42AndSpecial55
    0x20CC38: [],                                           # MissionCond_Obj46AndSpecial61
    0x20CC60: [(SOURCE_ITEM, 21)],                          # MissionCond_Obj57AndItem3D: the Morph-o-Ray had
    0x20CC88: [(SOURCE_ITEM, 31), (SOURCE_ITEM, 33)],       # MissionCond_Items1FAnd21: Hologuise, Map-o-Matic
}
ITEM_ICON_BASE = 0xEA60                 # an item's HUD icon: ITEM_ICON_BASE + its id
MAP_MARKERS = 0xE99A                    # the map's own markers, by frame: 2 the quest marker, 4 an infobot's
INFOBOT_MARKER = 4
ICON_BACKDROP = 0x40                    # a marker drawn on a dark backdrop (the item icons)
SPECIAL_ICONS = {(0xEAC8, 0): (SOURCE_FLAG, 0),             # the Zoomerator
                 (0xEAC9, 0): (SOURCE_FLAG, 1),             # the Raritanium
                 (0xEAC9, 3): (SOURCE_FLAG, 4),             # the Premium Nanotech
                 (0xEAC9, 4): (SOURCE_FLAG, 5)}             # the Ultra Nanotech


def executable_reader(openrac: Path):
    """read(address, size) over the executable's loaded segments."""
    elf = (openrac / EXECUTABLE).read_bytes()
    phoff, = struct.unpack_from("<I", elf, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", elf, 0x2A)
    segments = []
    for i in range(phnum):
        kind, offset, vaddr, _paddr, filesz, _memsz = struct.unpack_from("<6I", elf, phoff + i * phentsize)
        if kind == 1:                   # PT_LOAD
            segments.append((vaddr, elf[offset:offset + filesz]))

    def read(addr: int, size: int) -> bytes:
        for base, data in segments:
            if base <= addr and addr + size <= base + len(data):
                return data[addr - base:addr - base + size]
        raise SystemExit(f"{EXECUTABLE}: 0x{addr:08X} is in no loaded segment")
    return read


def condition_sources(kind: int, arg: int) -> list[tuple[int, int]]:
    """The rewards a mission condition names, as (RZ_SOURCE_*, argument)."""
    if kind == 1:
        return [(SOURCE_PLANET, arg)]
    if kind in (2, 3):
        return [(SOURCE_ITEM, arg)]
    if kind == 6:
        return [(SOURCE_FLAG, arg)] if arg < SPECIAL_FLAGS else []
    if kind == 7:
        if arg not in PREDICATE_ITEMS:
            raise SystemExit(f"a mission condition calls 0x{arg:08X}, which PREDICATE_ITEMS does not know")
        return PREDICATE_ITEMS[arg]
    return []


def icon_source(texture: int, frame: int, node_sources: list):
    """The reward a map marker's icon shows (an item's, or an infobot's: its mission's planet), or None for a
    generic marker."""
    if ITEM_ICON_BASE <= texture < ITEM_ICON_BASE + ITEM_COUNT and frame == 0:
        return (SOURCE_ITEM, texture - ITEM_ICON_BASE)
    if (texture, frame) == (MAP_MARKERS, INFOBOT_MARKER):
        planets = [s for s in node_sources if s[0] == SOURCE_PLANET]
        if len(planets) != 1:
            raise SystemExit(f"an infobot marker's mission names {len(planets)} planets, not one")
        return planets[0]
    return SPECIAL_ICONS.get((texture, frame))


def mission_tables(openrac: Path) -> list[str]:
    """ee_rz_mission_texts: each text of the Missions page and the map labels that names a reward, its planet and
    the rewards its mission's conditions name (a label: its marker's node's, and the reward its icon shows);
    ee_rz_map_icons: per planet, the markers whose icon is a reward's."""
    read = executable_reader(openrac)

    def word(addr):
        return struct.unpack("<I", read(addr, 4))[0]
    texts = {}
    icons = {}

    def add(text, planet, sources):
        if text in texts and texts[text][0] != planet:
            raise SystemExit(f"text 0x{text:04X} is shown on planets {texts[text][0]} and {planet}")
        have = texts.setdefault(text, (planet, []))[1]
        for source in sources:
            if source not in have:
                have.append(source)
    for planet in range(LEVELS):
        nodes = []
        at = word(MISSION_LISTS + 4 * planet)
        while True:
            short, reveal, reveal_arg, done, done_arg = struct.unpack_from("<hhih2xi", read(at, 0x10))
            if short == 0:
                break
            longs = [t for t in struct.unpack_from("<4h", read(at + 0x14, 8)) if t > 0]
            sources = condition_sources(reveal, reveal_arg) + condition_sources(done, done_arg)
            nodes.append(sources)
            for text in ([short] if short > 0 else []) + longs:
                add(text, planet, sources)
            at += NODE_SIZE
        at = word(MAP_ICON_LISTS + 4 * planet)
        while True:
            _id, link, flags, texture, frame, label = struct.unpack_from("<hhHHhh", read(at, 12))
            if flags & ICON_LIST_END:
                break
            source = icon_source(texture, frame, nodes[link] if link >= 0 else [])
            if source is not None:
                icons[(planet, texture, frame)] = (source, int(bool(flags & ICON_BACKDROP)))
            if label > 0:
                add(label, planet, (nodes[link] if link >= 0 else []) + ([source] if source else []))
            at += ICON_SIZE
    named = sorted((text, planet, sources) for text, (planet, sources) in texts.items() if sources)
    lines = ["/* the Missions page's and the map labels' texts that name a reward: the text, its planet, and the",
             "   rewards its mission's conditions name (src/randomizer_play.h RZ_SOURCE_*) */",
             f"const struct RzMissionText ee_rz_mission_texts[{len(named)}] = {{"]
    for text, planet, sources in named:
        if len(sources) > MISSION_SOURCES:
            raise SystemExit(f"text 0x{text:04X} names {len(sources)} rewards, more than {MISSION_SOURCES}")
        cells = ", ".join(f"{{ {k}, {a} }}" for k, a in sources + [(0, 0)] * (MISSION_SOURCES - len(sources)))
        lines.append(f"    {{ 0x{text:04X}, {planet}, {len(sources)}, {{ {cells} }} }},")
    lines.append("};")
    lines.append(f"const s32 ee_rz_mission_text_count = {len(named)};")
    lines.append("/* the map markers whose icon is a reward's: the planet, the texture, the frame, whether it has a")
    lines.append("   backdrop, the reward */")
    lines.append(f"const struct RzMapIcon ee_rz_map_icons[{len(icons)}] = {{")
    for (planet, texture, frame), ((kind, arg), backdrop) in sorted(icons.items()):
        lines.append(f"    {{ {planet}, 0x{texture:04X}, {frame}, {backdrop}, {{ {kind}, {arg} }} }},")
    lines.append("};")
    lines.append(f"const s32 ee_rz_map_icon_count = {len(icons)};")
    return lines


def gadget_sizes(openrac: Path) -> dict[int, int]:
    """class id -> its size unpacked, from level 1's gadget directory (every level's is the same)."""
    sys.path.insert(0, str(ROOT.parents[1] / "common/openrac_editor"))
    from formats import wad
    from level import SECTION_NAMES
    from overlay_patch import level_data_ranges, SECTOR
    from level_data import parts
    iso = openrac / "games/rac1/baserom/SCES_509.16.iso"
    lba, sectors = level_data_ranges(iso)[1]
    with iso.open("rb") as f:
        f.seek(lba * SECTOR)
        _, data, _ = parts(f.read(sectors * SECTOR), SECTION_NAMES)
    index, core = data["core_index"], wad(data["core_data"])
    count, offset = struct.unpack_from("<II", index, GADGET_TABLE)
    out = {}
    for i in range(count):
        at, cls, _size, _ = struct.unpack_from("<4i", index, offset + 16 * i)
        out[cls] = len(wad(core[at:]))
    return out


def effect_pool_word(lv: "Level") -> int:
    """The address of the word holding level LV's effect pool start."""
    word = lv.word(lv.place[EFFECT_POOL_FUNC] + EFFECT_POOL_LW)
    if word >> 16 != 0x8F8E:                     # lw $t6, off($gp)
        raise SystemExit(f"level {lv.level}: create_part2 + 0x{EFFECT_POOL_LW:X} is not lw $t6 from $gp ({word:08X})")
    return GAME_GP + ((word & 0xFFFF) ^ 0x8000) - 0x8000


def emit(openrac: Path, out: Path) -> None:
    role_map = roles(openrac)
    lines = ["/* generated by tools/gen_randomizer.py: per level, the classes whose moby update runs under",
             "   the Randomizer's collected view (src/randomizer_play.c); 0xFFFF ends a list. Then the",
             "   banners' words from the game's text table, by the game's language id. */",
             '#include "modseg.h"',
             '#include "randomizer_play.h"']
    out.mkdir(parents=True, exist_ok=True)
    lines += banner_tables(openrac, out)
    lines += mission_tables(openrac)
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
    pools = [effect_pool_word(lv) for _, lv in sorted(levels(openrac).items())]
    lines.append("/* per level, the word holding the effect pool's start */")
    lines.append("const u32 ee_rz_effect_pool[19] = { " + ", ".join(f"0x{a:06X}" for a in pools) + " };")
    sizes = gadget_sizes(openrac)
    lines.append("/* the gadget directory's classes and their sizes unpacked */")
    lines.append(f"const s32 ee_rz_gadget_count = {len(sizes)};")
    lines.append("const u16 ee_rz_gadget_classes[] = { " + ", ".join(str(c) for c in sizes) + " };")
    lines.append("const u32 ee_rz_gadget_sizes[] = { " + ", ".join(f"0x{s:X}" for s in sizes.values()) + " };")
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
