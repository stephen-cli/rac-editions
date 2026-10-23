#!/usr/bin/env python3
"""
The menu font's glyphs for the Reuse Seed box (src/goodies.c).

The game's three menu fonts (effect textures 1, 2 and 3: normal, small,
large) are 256 x 128 8-bit sheets sharing one palette. Codes 0x10-0x19 are
button glyphs (Cross, Circle, Triangle, Square, L1, R1, L2, R2, the sticks),
drawn as 24 x 16 cells from the row at v 0x60; codes 0x02-0x07 and 0x1A-0x1F
have empty metrics records, and the sheets' corner u 0xD0-0xFF, v 0x00-0x4F
is transparent and unused. This tool draws into that corner the D-pad's four
arms (codes 0x1A-0x1D: up, right, down, left), in the style of the game's
own D-pad prompts: a pentagon whose point faces the pad's centre (down's
points up), shaded like the face buttons, a dark base lit from the top left;
and copies of Cross, Circle and Square (0x02-0x04) centred in the shoulder
buttons' width, which all the box's symbols take. The sheets come from the
player's disc (the title screen's data), so no game data is kept in the
repository.

  python tools/gen_glyphs.py [--openrac DIR]    -> build/modseg/gen_glyphs.c
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
EDITOR = HERE.parents[2] / "common/openrac_editor"   # OpenRAC's disc readers, carried (common/deps.py)

CELL_W, CELL_H = 24, 16
BUTTON_V = 0x60                     # the button glyphs' row
ARM = 13                            # the arm's box, 13 x 13, at ARM_X, ARM_Y in the cell
ARM_X, ARM_Y = 6, 1                 # centred in the shoulder buttons' width (their ink: x 3..23)
# The face buttons' own levels: base 46, an outer rim of about 100 at the top left and 25 at the
# bottom right, an inner rim of about 75.
FACE, LIGHT, SHADE, RIM_LIT, RIM_DARK = (46, 46, 46), (75, 75, 75), (32, 32, 32), (100, 100, 100), (22, 22, 22)
# The seed box's symbols all take the shoulder buttons' advance (L1, R1, L2, R2: 25, ink x 3..23), so
# any fifteen fill the same width. code, cell in the free corner, and what it is: a D-pad arm (0 up,
# 1 right, 2 down, 3 left) or a copy of a face button's cell (its u in the button row) moved right
# by FACE_SHIFT, from ink x 2..17 to the shoulder buttons' centre. Codes 0x02-0x07 are in no string.
GLYPHS = ((0x1A, 0xD0, 0x00, "arm", 0), (0x1B, 0xE8, 0x00, "arm", 1), (0x1C, 0xD0, 0x10, "arm", 2),
          (0x1D, 0xE8, 0x10, "arm", 3), (0x02, 0xD0, 0x20, "face", 0x00), (0x03, 0xE8, 0x20, "face", 0x48),
          (0x04, 0xD0, 0x30, "face", 0x18))
ADVANCE = 25
FACE_SHIFT = 3


def arm_mask(direction):
    """The arm's pixels {(x, y)} in its box, for DIRECTION (0 up, 1 right, 2 down, 3 left). The down
    arm is a square with a roof: its point, at the top, faces the pad's centre."""
    half = ARM // 2
    down = {(x, y) for y in range(ARM) for x in range(ARM) if y > half or abs(x - half) <= y}
    last = ARM - 1
    turns = {2: lambda x, y: (x, y), 0: lambda x, y: (x, last - y),
             1: lambda x, y: (y, x), 3: lambda x, y: (last - y, x)}
    return {turns[direction](x, y) for x, y in down}


def nearest(colours, rgb):
    """The index of the opaque grey closest to RGB (the palette's other colours are the buttons' symbols)."""
    best, best_d = 0, None
    for i, (r, g, b, a) in enumerate(colours):
        if a < 0xE0 or max(r, g, b) - min(r, g, b) > 4:
            continue
        d = sum((p - q) ** 2 for p, q in zip((r, g, b), rgb))
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best


def face(sheet, width, blank, u):
    """A face button's 24 x 16 cell (Cross, Circle, Square) moved FACE_SHIFT to the right."""
    cell = [[blank] * CELL_W for _ in range(CELL_H)]
    for y in range(CELL_H):
        for x in range(CELL_W - FACE_SHIFT):
            cell[y][x + FACE_SHIFT] = sheet[(BUTTON_V + y) * width + u + x]
    return cell


def glyph(blank, colours, direction):
    """A 24 x 16 cell of the arm for DIRECTION on the sheet's transparent index BLANK, shaded like the
    face buttons: an outer rim lit at the top left and dark at the bottom right, an inner rim, a dark face. COLOURS is the palette as the GS reads
    it (formats.Texture.colours: CSM1 swaps index bits 3 and 4)."""
    cell = [[blank] * CELL_W for _ in range(CELL_H)]
    mask = arm_mask(direction)
    face, light, shade, rim_lit, rim_dark = (nearest(colours, c) for c in (FACE, LIGHT, SHADE, RIM_LIT, RIM_DARK))
    for x, y in mask:
        outside = [(dx, dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)) if (x + dx, y + dy) not in mask]
        if outside:
            colour = rim_lit if (-1, 0) in outside or (0, -1) in outside else rim_dark
        elif (x - 1, y - 1) not in mask or (x - 2, y) not in mask or (x, y - 2) not in mask:
            colour = light
        elif (x + 1, y + 1) not in mask or (x + 2, y) not in mask or (x, y + 2) not in mask:
            colour = shade
        else:
            colour = face
        cell[ARM_Y + y][ARM_X + x] = colour
    return cell


def sheets(openrac: Path):
    """{1, 2, 3: (colours, pixels, width, height)} from the title screen's data on the disc."""
    sys.path.insert(0, str(EDITOR))
    from disc import Disc
    from formats import unpack
    from formats import Texture
    from level import load_title
    iso = openrac / "games/rac1/baserom/SCES_509.16.iso"
    with Disc(iso) as disc:
        title = load_title(disc, disc.survey())
        _, table = unpack("<II", title.index, 0x58)
        bank, = unpack("<I", title.index, 0x68)
        out = {}
        for i in (1, 2, 3):
            pal, img, w, h = unpack("<4i", title.index, table + 16 * i)
            pixels = title.core[bank + img:bank + img + w * h]
            out[i] = (Texture(w, h, pixels, title.core[bank + pal:bank + pal + 1024]).colours, pixels, w, h)
        return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("-o", "--output", type=Path, default=ROOT / "build/modseg/gen_glyphs.c")
    args = ap.parse_args()
    lines = ["/* generated by tools/gen_glyphs.py from the disc's menu fonts; see src/goodies.c */",
             f"const unsigned char ee_glyph_cells[3][{len(GLYPHS)}][{CELL_H}][{CELL_W}] = {{"]
    for font, (colours, pixels, width, height) in sorted(sheets(args.openrac).items()):
        if (width, height) != (256, 128):
            raise SystemExit(f"font {font} is {width} x {height}, not the 256 x 128 sheet expected")
        for _, u, v, _, _ in GLYPHS:
            if any(pixels[(v + y) * width + u + x] != pixels[0xFF] for y in range(CELL_H) for x in range(CELL_W)):
                raise SystemExit(f"font {font}: the cell at u 0x{u:X}, v 0x{v:X} is not empty")
        lines.append("  {")
        for _, _, _, kind, which in GLYPHS:
            cell = glyph(pixels[0xFF], colours, which) if kind == "arm" else face(pixels, width, pixels[0xFF], which)
            lines.append("    {" + ", ".join("{" + ", ".join(str(p) for p in row) + "}" for row in cell) + "},")
        lines.append("  },")
    lines.append("};")
    lines.append("/* {code, u, v, advance} for each glyph */")
    lines.append(f"const unsigned char ee_glyph_places[{len(GLYPHS)}][4] = {{ "
                 + ", ".join(f"{{0x{c:X}, 0x{u:X}, 0x{v:X}, {ADVANCE}}}" for c, u, v, _, _ in GLYPHS) + " };")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
