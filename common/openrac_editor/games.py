"""The four games' formats side by side: what their levels share and what each one changes.

The readers (terrain.py, ties.py, shrubs.py, moby_class.py, sky.py, collision.py, mobys.py) are
written once, for what Ratchet & Clank, Going Commando, Up Your Arsenal and Deadlocked have in
common, and take the little that differs from the game's Format here. Ratchet & Clank's entry
holds the values the readers always used, so its output does not change.

What is shared (Wrench's readers, which serve all four, and the levels on the discs agree):

- the level core: one index header layout (gs_ram, tfrags, occlusion, sky, collision, the class
  and texture tables at the same offsets, textures base at +0x60, the chrome and glass maps at
  +0x90..+0x9c), the class entries (32 bytes for mobys and ties, 48 for shrubs), the 16-byte
  texture entries and the WAD compression;
- tfrags, ties, shrubs and collision, whose blocks the same decoders read;
- the moby class header and its packets; the tie and shrub instance records (from Going Commando on
  a tie keeps its ambient colours in a block of its own, so its record is 0x60 bytes, not 0xe0).

What differs, per game, is the fields of Format: where the gameplay file keeps each table, the
moby instance record, the moby packets' vertex table header, Deadlocked's swizzled textures and
3x4 skeleton matrices, and the containers (disc.py for Ratchet & Clank, wads.py for the others).
Sources: Wrench (GPL-3.0-or-later; src/instancemgr/gameplay.cpp, gameplay_impl_classes.inl,
src/engine/moby_low.cpp, moby_vertex.cpp, src/wrenchbuild/level/level_core.cpp,
level_textures.cpp), read and re-implemented; no code is copied. docs/engine/formats/LEVELS.md
lists each difference and how it was checked on the discs.
"""

from dataclasses import dataclass, field
from pathlib import Path
import struct
import sys

sys.path.insert(1, str(next(p for p in Path(__file__).resolve().parents if (p / "tools/openrac_common").is_dir()) / "tools"))
from openrac_common import games as manifests  # noqa: E402  (this module is the editor's games)


@dataclass(frozen=True)
class MobyRecord:
    """A gameplay file's moby instance record: its size, struct layout and field names (the
    names mobys.py writes as metadata, "class_id", "x".."rz", "scale", "r", "g", "b" among them)."""
    size: int
    layout: str
    fields: tuple[str, ...]
    usual: dict = field(default_factory=dict)  # the value most instances store: only others are kept

    def __post_init__(self):
        if struct.calcsize(self.layout) != self.size or len(self.fields) != len(struct.unpack(self.layout, bytes(self.size))):
            raise ValueError(f"moby record layout {self.layout} disagrees with its size or fields")


RAC1_MOBY = MobyRecord(
    0x78, "<7if4i3f3f2if4i3i2i",
    ("size", "unknown_4", "spawn_flags", "spawn_id", "unknown_10", "unknown_14", "class_id", "scale",
     "draw_distance", "update_distance", "unused_28", "unused_2c", "x", "y", "z", "rx", "ry", "rz",
     "group", "is_rooted", "rooted_distance", "unknown_54", "pvar_index", "occlusion", "mode_bits",
     "r", "g", "b", "light", "unknown_74"),
    # Measured on all 19 PAL levels (mobys.py); the last four never vary on the disc.
    {"unknown_4": -1, "spawn_flags": 0, "unknown_10": 0, "unknown_14": 0, "draw_distance": 64,
     "update_distance": 64, "is_rooted": 0, "rooted_distance": 0.0, "occlusion": 1, "mode_bits": 32,
     "light": 0, "unknown_74": -1, "unused_28": 32, "unused_2c": 64, "unknown_54": 1})
# Going Commando and Up Your Arsenal: Wrench's GcUyaMobyInstance (0x88): the mission, the
# instance's uid (spawn_id here, as Ratchet & Clank's) and its bolts come before the class. The
# usual values were measured on every level of each NTSC-U disc (17,422 and 23,221 instances);
# the two games differ in mode_bits only.
GC_UYA_FIELDS = (
    "size", "mission", "unknown_8", "unknown_c", "spawn_id", "bolts", "unknown_18", "unknown_1c",
    "unknown_20", "unknown_24", "class_id", "scale", "draw_distance", "update_distance", "unused_38",
    "unused_3c", "x", "y", "z", "rx", "ry", "rz", "group", "is_rooted", "rooted_distance", "unknown_64",
    "pvar_index", "occlusion", "mode_bits", "r", "g", "b", "light", "unknown_84")
GC_UYA_USUAL = {"mission": -1, "unknown_8": 0, "unknown_c": 0, "bolts": 0, "unknown_18": 0, "unknown_1c": 0,
                "unknown_20": 0, "unknown_24": 0, "draw_distance": 64, "update_distance": 64, "unused_38": 32,
                "unused_3c": 64, "is_rooted": 0, "rooted_distance": 0.0, "unknown_64": 1, "occlusion": 1,
                "light": 0, "unknown_84": -1}
GC_MOBY = MobyRecord(0x88, "<11if4i3f3f2if4i3i2i", GC_UYA_FIELDS, {**GC_UYA_USUAL, "mode_bits": 0})
UYA_MOBY = MobyRecord(0x88, "<11if4i3f3f2if4i3i2i", GC_UYA_FIELDS, {**GC_UYA_USUAL, "mode_bits": 32})
# Deadlocked: Wrench's DlMobyInstance (0x70); usual values from its 9,740 instances.
DL_MOBY = MobyRecord(
    0x70, "<5if4i3f3f2if4i3i2i",
    ("size", "mission", "spawn_id", "bolts", "class_id", "scale", "draw_distance", "update_distance",
     "unused_20", "unused_24", "x", "y", "z", "rx", "ry", "rz", "group", "is_rooted",
     "rooted_distance", "unused_4c", "pvar_index", "occlusion", "mode_bits", "r", "g", "b", "light",
     "unused_6c"),
    {"mission": -1, "bolts": 0, "draw_distance": 64, "update_distance": 64, "unused_20": 32,
     "unused_24": 64, "is_rooted": 0, "rooted_distance": -1.0, "unused_4c": 1, "occlusion": 1,
     "mode_bits": 0, "unused_6c": -1})


@dataclass(frozen=True)
class TieHeader:
    """Where a tie class header keeps what ties.py reads: the high LOD's packet count (a byte),
    the texture count (a byte), the AD GIF records' offset and the light-slot normals' offset.
    Wrench's RacTieClassHeader and GcUyaDlTieClassHeader (src/engine/tie.h); the packet table
    offsets at +0 and the scale at +0x40 are where they were."""
    packet_count: int
    texture_count: int
    ad_gifs: int
    normals: int
    light_slots: bool           # packet bytes 0xa and 0xb locate each vertex's light slot (Ratchet & Clank)
    winding_flag: bool = False  # a strip's last byte is a flag (any non-zero turns it), not 0 or 1


RAC1_TIE = TieHeader(packet_count=0x20, texture_count=0x23, ad_gifs=0x2c, normals=0x0c, light_slots=True)
GC_TIE = TieHeader(packet_count=0x0c, texture_count=0x0f, ad_gifs=0x1c, normals=0x34, light_slots=False)
UYA_DL_TIE = TieHeader(packet_count=0x0c, texture_count=0x0f, ad_gifs=0x1c, normals=0x34, light_slots=False,
                       winding_flag=True)


@dataclass(frozen=True)
class Format:
    """One game's level formats, as far as the shared readers need them."""
    game: str                 # OpenRAC's id: rac1, rac2, rac3, rac4
    title: str
    # The gameplay file's header words that point at the tie, shrub and moby instance tables.
    # Deadlocked keeps its ties and shrubs in a second file (its art instances: level.Level.art),
    # so its tie and shrub words are that file's.
    ties: int
    shrubs: int
    mobys: int
    moby: MobyRecord          # the moby instance record
    tie_record: int = 0x60    # bytes per tie instance
    tie_ambient: bool = False  # the record holds its 64 ambient colours (Ratchet & Clank's 0xe0 bytes)
    # The level core.
    tie_header: TieHeader = RAC1_TIE
    gadgets: bool = False               # index +0x80 is a gadget table (Ratchet & Clank)
    ratchet_sequences: bool = True      # +0x78 points at Ratchet's 256 sequence offsets (not Deadlocked's)
    moby_sound_remap: bool = False      # +0xb4 starts a core block (Going Commando, Up Your Arsenal)
    stash: bool = False                 # +0x84 counts GS RAM entries of permanently stashed moby textures
    compact_vertex_table: bool = False  # moby packets' vertex table header: 8 u16 (else 8 u32)
    swizzled_textures: bool = False     # 8-bit textures stored in the GS's PSMT8 order (Deadlocked)
    skeleton_3x4: bool = False          # moby bind matrices are 3x4 (Deadlocked), not 4x4
    moby_animations: bool = True        # the moby sequences are read (moby_anim.py's frame format)
    moby_glow: bool = True              # a class header's byte 0xa is its first glow packet
    moby_empty_slots: bool = False      # a face may use an empty texture slot: drawn untextured
    sky_short_shells: bool = False      # a sky shell's cluster count and flags are s16 (Up Your Arsenal, Deadlocked)
    # What only Ratchet & Clank has so far: its levels lit as the game lights them at load
    # (lighting.py needs the executable's normal table, disc.Version.normal_table), its moby
    # class names (moby_classes.tsv) and update functions (games/rac1), and the level packer.
    lighting: bool = False
    class_names: bool = False
    packable: bool = False
    # Early access: a class or block that does not decode is left out and listed (level.problems)
    # instead of stopping the level. Ratchet & Clank's readers stay strict.
    lenient: bool = False
    level_names: dict = field(default_factory=dict)  # level number (the level header's id) -> name

    def level_name(self, level: int) -> str:
        return self.level_names.get(level, f"Level {level}")


# The levels' names, by the number in their level header (which the games load them by): from
# Wrench's data/underlay/{gc,uya,dl}_levels.asset (GPL-3.0-or-later), names only.
LEVEL_NAMES = {
    "rac1": dict(enumerate((
        "Veldin", "Novalis", "Aridia", "Kerwan", "Eudora", "Rilgar", "Blarg Station", "Umbris", "Batalia",
        "Gaspar", "Orxon", "Pokitaru", "Hoven", "Gemlik Base", "Oltanis", "Quartu", "Kalebo III",
        "Drek's Fleet", "Veldin (the return)"))),
    "rac2": {0: "Aranos Tutorial", 1: "Oozla", 2: "Maktar Nebula", 3: "Endako", 4: "Barlow",
             5: "Feltzin System", 6: "Notak", 7: "Siberius", 8: "Tabora", 9: "Dobbo", 10: "Hrugis Cloud",
             11: "Joba", 12: "Todano", 13: "Boldan", 14: "Aranos Prison", 15: "Gorn", 16: "Snivelak",
             17: "Smolg", 18: "Damosel", 19: "Grelbin", 20: "Yeedil", 22: "Dobbo Orbit", 23: "Damosel Orbit",
             24: "Ship Shack", 25: "Wupash Nebula", 26: "Jamming Array", 30: "Insomniac Museum"},
    "rac3": {1: "Veldin", 2: "Florana", 3: "Starship Phoenix", 4: "Marcadia", 5: "Daxx",
             6: "Starship Phoenix (Under Attack)", 7: "Annihilation Nation", 8: "Aquatos", 9: "Tyhrranosis",
             10: "Zeldrin Starport", 11: "Obani Moons", 12: "Rilgar", 13: "Holostar Studios Ratchet",
             14: "Koros", 16: "Kerwan", 17: "Crash Site", 18: "Aridia", 19: "Thran Asteroid Belt",
             20: "Final Boss", 21: "Obani Draco", 22: "Mylon", 23: "Holostar Studios Clank",
             24: "Insomniac Museum", 26: "Kerwan Ranger Missions", 27: "Aquatos Base", 28: "Aquatos Sewers",
             29: "Tyhrranosis Ranger Missions", 30: "Vid-Comic Unnamed", 31: "Vid-Comic 1", 32: "Vid-Comic 4",
             33: "Vid-Comic 2", 34: "Vid-Comic 3", 35: "Vid-Comic 5", 36: "Vid-Comic 1 Special Edition",
             38: "Multiplayer Menu Audio", 39: "Multiplayer Menu", 40: "Bakisi Isles", 41: "Hoven Gorge",
             42: "Outpost X12", 43: "Korgon Outpost", 44: "Metropolis", 45: "Blackwater City",
             46: "Command Center", 47: "Blackwater Docks", 48: "Aquatos Sewers (multiplayer)",
             49: "Marcadia Palace", 50: "Bakisi Isles (split screen)", 51: "Hoven Gorge (split screen)",
             52: "Outpost X12 (split screen)", 53: "Korgon Outpost (split screen)",
             54: "Metropolis (split screen)", 55: "Blackwater City (split screen)"},
    "rac4": {0: "Multiplayer Menu", 1: "DreadZone Station", 2: "Catacrom Graveyard", 4: "Sarathos Swamp",
             5: "Dark Cathedral", 6: "Temple of Shaar", 7: "Valix Lighthouse", 8: "Mining Facility",
             10: "Torval Ruins", 11: "Tempus Station", 13: "Maraxus Prison", 14: "Ghost Station",
             15: "Control Level", 21: "DreadZone Station (split screen)", 22: "Catacrom Graveyard (split screen)",
             24: "Sarathos Swamp (split screen)", 25: "Dark Cathedral (split screen)",
             26: "Temple of Shaar (split screen)", 27: "Valix Lighthouse (split screen)",
             28: "Mining Facility (split screen)", 30: "Torval Ruins (split screen)",
             31: "Tempus Station (split screen)", 33: "Maraxus Prison (split screen)",
             34: "Ghost Station (split screen)", 35: "Control Level (split screen)", 41: "Battledome Tower",
             42: "Catacrom Graveyard (multiplayer)", 44: "Sarathos Swamp (multiplayer)",
             45: "Dark Cathedral (multiplayer)", 46: "Temple of Shaar (multiplayer)",
             47: "Valix Lighthouse (multiplayer)", 48: "Mining Facility (multiplayer)",
             50: "Torval Ruins (multiplayer)", 51: "Tempus Station (multiplayer)",
             53: "Maraxus Prison (multiplayer)", 54: "Ghost Station (multiplayer)",
             61: "Battledome Tower (split screen)", 62: "Catacrom Graveyard (multiplayer, split screen)",
             64: "Sarathos Swamp (multiplayer, split screen)", 65: "Dark Cathedral (multiplayer, split screen)",
             66: "Temple of Shaar (multiplayer, split screen)", 67: "Valix Lighthouse (multiplayer, split screen)",
             68: "Mining Facility (multiplayer, split screen)", 70: "Torval Ruins (multiplayer, split screen)",
             71: "Tempus Station (multiplayer, split screen)", 73: "Maraxus Prison (multiplayer, split screen)",
             74: "Ghost Station (multiplayer, split screen)"},
}

# The same readers for the later games, with what differs (games.py's docstring) and, being early
# access, lenient: what does not decode yet is left out and listed.
LATER = dict(tie_ambient=False, stash=True, compact_vertex_table=True, moby_glow=False, moby_empty_slots=True,
             lenient=True)
RAC1 = Format("rac1", "Ratchet & Clank", ties=0x34, shrubs=0x3c, mobys=0x44, moby=RAC1_MOBY, tie_record=0xe0,
              tie_ambient=True, gadgets=True, lighting=True, class_names=True, packable=True,
              level_names=LEVEL_NAMES["rac1"])
RAC2 = Format("rac2", "Ratchet & Clank: Going Commando", ties=0x34, shrubs=0x40, mobys=0x4c, moby=GC_MOBY,
              tie_header=GC_TIE, moby_sound_remap=True, level_names=LEVEL_NAMES["rac2"], **LATER)
RAC3 = Format("rac3", "Ratchet & Clank: Up Your Arsenal", ties=0x34, shrubs=0x40, mobys=0x4c, moby=UYA_MOBY,
              tie_header=UYA_DL_TIE, moby_sound_remap=True, sky_short_shells=True,
              level_names=LEVEL_NAMES["rac3"], **LATER)
RAC4 = Format("rac4", "Ratchet: Deadlocked", ties=0x08, shrubs=0x14, mobys=0x30, moby=DL_MOBY,
              tie_header=UYA_DL_TIE, ratchet_sequences=False, swizzled_textures=True, skeleton_3x4=True,
              moby_animations=False, sky_short_shells=True, level_names=LEVEL_NAMES["rac4"], **LATER)
FORMATS = {f.game: f for f in (RAC1, RAC2, RAC3, RAC4)}

# The discs each game's levels are read from, as games/<game>/game.json lists its versions:
# Ratchet & Clank's PAL and NTSC-U, the others' NTSC-U. An overrides pack or an asset export names
# its game and version; a pack's files fit the levels of its own game only.
VERSIONS = {game: tuple(manifests.load(game)["versions"]) for game in FORMATS}


def known(game, version) -> bool:
    """Whether GAME's levels are read from a disc of VERSION here."""
    return version in VERSIONS.get(game, ())


def title(game: str) -> str:
    """A game's title, or its id if OpenRAC does not know it."""
    return FORMATS[game].title if game in FORMATS else str(game)


def of(level) -> Format:
    """The format of a loaded level (level.Level.game; Ratchet & Clank's when it has none)."""
    return FORMATS[getattr(level, "game", "rac1")]
