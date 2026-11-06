"""
The Executive Edition's release payload: what tools/release.py writes and
tools/install.py reads to make the edition's image from a player's discs.

A release carries the edition's own work and no bytes of either game. What
the edition takes from the games is made from the player's own discs while
the installer runs:

  edition.json   the edition, its version (the rac-editions commit), the
                 SHA-256 of each disc it is built from, and the mod
                 segment's load address
  modseg.bin     the mod segment (tools/modseg.py), with its holes zeroed:
                 the tables made from a disc's bytes
  holes.json     each hole: its symbol, offset in the segment, size, the
                 disc it comes from and how to make it (HOLES below)
  exe.tsv        the boot executable's words the edition changes (the boot
                 frontend's hooks, tools/gen_frontend.py patch-elf): file
                 offset, new word, after the mod segment is added
  overlay.tsv    the level programs' and frontbin's words the edition
                 changes (tools/overlay_patch.py): feature, image byte
                 offset, new word
  helpdesk.json  the edition's HelpDesk lines as cut lists of the disc's
                 own lines (audio/helpdesk.json, tools/gen_helpdesk.py)
  switches.json  per feature a player may leave out (SWITCHES below), the
                 segment bytes a build without it writes over the build's
                 (offset, hex): without strafing, the Controls page's rows
                 without the Strafing row

edition.json's features_word is ee_features' offset in the segment
(src/features.c): the installer writes there the bits of the features in.

The holes, by the disc their bytes come from:
  rac2   Going Commando's animations in R&C1's layout (common/rac2_anim.py):
         Ratchet's sequences 0x14 (the backpedal) and 0x8B (the Charge
         Boots' dash), the gun-arm classes' sequence 0x14; each a frame blob
         and the sequence's seven header words
  rac1   the seed glyphs' cells, copied from the title screen's fonts and
         drawn over (tools/gen_glyphs.py); two HUD palettes recoloured from
         retail's (tools/gen_hud.py); the Randomizer banners' words from the
         text table (tools/gen_randomizer.py), replayed from a recipe of
         (language, text id)
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
COMMON = HERE.parents[2] / "common"

# What the builder shows for the edition and its discs (edition.json, the protocol's editions command).
EDITION = {
    "name": "Executive Edition",
    "game": "Ratchet & Clank",
    "disc_names": {
        "rac1": {"name": "Ratchet & Clank", "region": "PAL", "required": True},
        "rac2": {"name": "Going Commando", "region": "USA", "required": False},
    },
}

# The builder's choice of Challenge Mode economy: the features each leaves out. "skill", the default, is the bolt
# multiplier (x1 base); "classic" keeps retail's doubling and limiter (the limiter's 301-351 fix is in both, and the
# Metal Detector pays 10x in both).
ECONOMIES = {
    "skill": (),
    "classic": ("bolt_multiplier",),
}

# The features a player may leave out (the builder's Edition settings), all in by default. Each names the
# overlay features (tools/overlay_patch.py) whose words it owns, the holes only it reads, and the disc it is built
# from; the Charge Boots and strafing are left out entirely without Going Commando. "helpdesk" is the uses of
# audio/helpdesk.json whose lines it owns. ee_features (src/features.c) tells the mod segment which are in.
SWITCHES = {
    "charge_boots": {"bit": 0x1, "disc": "rac2", "features": ("charge_boots",),
                     "holes": ("ee_charge_seq_frames", "ee_charge_seq"), "helpdesk": ()},
    "strafing": {"bit": 0x2, "disc": "rac2", "features": ("strafe", "strafe_camera"),
                 "holes": ("ee_backpedal_seq_frames", "ee_backpedal_seq", "ee_gunarm_seq_1_frames",
                           "ee_gunarm_seq_1", "ee_gunarm_seq_2_frames", "ee_gunarm_seq_2"),
                 "helpdesk": ("strafe", "strafe_future")},
    "quick_select_pages": {"bit": 0x4, "disc": "rac1", "features": ("quick_select_pages",), "holes": (),
                           "helpdesk": ()},
}

HEAD = 0x1C          # a loaded sequence's header: the first seven words of its table

# symbol, disc, fill: how its bytes are made
HOLES = (
    ("ee_backpedal_seq_frames", "rac2", {"kind": "ratchet_sequence", "slot": 0x14, "part": "frames"}),
    ("ee_backpedal_seq", "rac2", {"kind": "ratchet_sequence", "slot": 0x14, "part": "head"}),
    ("ee_charge_seq_frames", "rac2", {"kind": "ratchet_sequence", "slot": 0x8B, "part": "frames"}),
    ("ee_charge_seq", "rac2", {"kind": "ratchet_sequence", "slot": 0x8B, "part": "head"}),
    ("ee_gunarm_seq_1_frames", "rac2", {"kind": "arm_sequence", "class": 1, "slot": 0x14, "part": "frames"}),
    ("ee_gunarm_seq_1", "rac2", {"kind": "arm_sequence", "class": 1, "slot": 0x14, "part": "head"}),
    ("ee_gunarm_seq_2_frames", "rac2", {"kind": "arm_sequence", "class": 2, "slot": 0x14, "part": "frames"}),
    ("ee_gunarm_seq_2", "rac2", {"kind": "arm_sequence", "class": 2, "slot": 0x14, "part": "head"}),
    ("ee_glyph_cells", "rac1", {"kind": "glyphs"}),
    ("ee_palette_movie_bar", "rac1", {"kind": "hud_palette"}),
    ("ee_palette_randomizer_badge", "rac1", {"kind": "hud_palette"}),
    ("ee_rz_words", "rac1", {"kind": "words"}),
)


class Filler:
    """Makes the holes' bytes from the discs, each source read once."""

    def __init__(self, discs: dict):
        self.discs = discs
        self.cache = {}
        sys.path.insert(0, str(COMMON))
        sys.path.insert(0, str(HERE))

    def once(self, key, make):
        if key not in self.cache:
            self.cache[key] = make()
        return self.cache[key]

    def fill(self, symbol: str, fill: dict) -> bytes:
        kind = fill["kind"]
        if kind in ("ratchet_sequence", "arm_sequence"):
            import rac2_anim
            if kind == "ratchet_sequence":
                head, blob, _ = self.once((kind, fill["slot"]),
                                          lambda: rac2_anim.ratchet_sequence(self.discs["rac2"], fill["slot"]))
            else:
                head, blob, _ = self.once((kind, fill["class"], fill["slot"]), lambda: rac2_anim.arm_sequence(
                    self.discs["rac2"], fill["class"], fill["slot"]))
            return blob if fill["part"] == "frames" else head
        if kind == "glyphs":
            import gen_glyphs
            return self.once(kind, lambda: gen_glyphs.holes(self.discs["rac1"]))[symbol]
        if kind == "hud_palette":
            import gen_hud
            return self.once(kind, lambda: gen_hud.holes(self.discs["rac1"]))[symbol]
        if kind == "words":
            import gen_randomizer
            return gen_randomizer.words_blob(self.discs["rac1"], fill["recipe"])
        raise SystemExit(f"unknown hole kind {kind}")
