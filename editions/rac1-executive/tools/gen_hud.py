#!/usr/bin/env python3
"""
The edition's HUD icons: the Charge Boots icon's pixels, and the Sound
page's orange Movie volume bar's palette, for the resident segment.

HUD icons are entries of the HUD header (table of contents group 0x14F8, 3
sectors; OpenRAC's games/rac1/docs/ASSETS.md): an icon table {id, frame
count, first frame}, frame references {palette page, image page}, and page
tables {source address, GS block, log2 width, log2 height} whose source
addresses are offsets into the HUD banks until LinkHudBank adds a bank's
address to the pages in that bank's index range (header +0x14 and +0x34
hold the ranges' ends). Each gadget icon is 32x32, 8-bit, two frames: the
black glyph (palette page 5) and the white one shown while equipped
(palette page 6); every frame of every icon shares those two palettes, the
tile colour included.

This converts art/charge_boots.png (32x32) to the two frames the way the
Grindboots icon is drawn (its black frame's palette entries, and the white
frame by the same black-to-white entry mapping), and:

writes build/modseg/gen_icons.c, the 2 KB of pixels, 16-aligned, for the
resident segment (tools/modseg.py picks gen_*.c up). src/hud_icons.c adds
the icon (0xEA84, two frames, palette pages 5 and 6) to the HUD header's
tables as each level loads: every level's data archive carries its own
copy of the header, which is why the disc copies are not patched.

The Movie volume bar (src/movie_volume.c) is the slider icon's green bar
(0xE99E frame 8) under a palette of its own: the bar's palette page with
every entry the bar's pixels use turned orange. The green bar is a white
centre line fading to pure green (0, 212, 30) at its edges, the red channel
measuring the fade, so each entry keeps its place in the fade and moves to
the same place between BAR_ORANGE and BAR_HIGHLIGHT. Alpha bytes and the
entries the bar does not use stay as retail has them. Written to
gen_icons.c as ee_palette_movie_bar; src/hud_icons.c adds the page and the
icon (EE_ICON_MOVIE_BAR in src/modseg.h).

The Randomizer save badge (src/goodies.c) is the Load Game list's Challenge
Mode "R" (0xE99E frame 4) under a palette of its own too: the badge's
palette page with its gold entries turned green at the same brightness.
Written as ee_palette_randomizer_badge; src/hud_icons.c adds the page and
the icon (EE_ICON_RANDOMIZER).

  python tools/gen_hud.py [--openrac DIR]
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
EDITOR = HERE.parents[2] / "common/openrac_editor"   # OpenRAC's disc readers, carried (common/deps.py)
ROOT = HERE.parent
ICON_ID = 0xEA84                 # ICON_BASE + item 36 (tools/gen_items.py)
ICON_GRINDBOOTS = 0xEA7D
PALETTE_BLACK, PALETTE_WHITE = 5, 6
SYMBOL = "ee_icon_charge_boots"
SIZE = 32


def png_decode(data: bytes):
    """A non-interlaced 8-bit RGB, RGBA, grey or indexed PNG -> (w, h, [(r, g, b)])."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("not a PNG")
    pos, idat, plte = 8, b"", None
    while pos < len(data):
        n, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">2I5B", body)
        elif kind == b"PLTE":
            plte = body
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    if depth != 8 or interlace:
        raise SystemExit("only 8-bit, non-interlaced PNGs are supported")
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append(bytes(line))
        prev = line
    px = []
    for line in rows:
        for x in range(w):
            p = line[x * bpp:(x + 1) * bpp]
            if ctype == 2 or ctype == 6:
                px.append((p[0], p[1], p[2]))
            elif ctype == 3:
                px.append(tuple(plte[p[0] * 3:p[0] * 3 + 3]))
            else:
                px.append((p[0], p[0], p[0]))
    return w, h, px


class HudHeader:
    """The retail HUD header's tables, and the pixels of a frame."""

    def __init__(self, openrac: Path):
        sys.path.insert(0, str(EDITOR))
        from formats import wad
        g = openrac / "assets/wrench/rac1-pal/globals"
        self.raw = (g / "hud_header.bin").read_bytes()
        d = self.raw
        self.counts = struct.unpack_from("<2H", d, 0)
        o_icons, o_frames, o_pal, o_img = struct.unpack_from("<4I", d, 4)
        self.pal_ends = list(struct.unpack_from("<8i", d, 0x14))
        self.img_ends = list(struct.unpack_from("<8i", d, 0x34))
        self.icons = []
        o = o_icons
        while struct.unpack_from("<H", d, o)[0] != 0xFFFF:
            self.icons.append(list(struct.unpack_from("<HHHBB", d, o)))
            o += 8
        self.o_icons, self.o_frames, self.o_pal, self.o_img = o_icons, o_frames, o_pal, o_img
        self.frames = [list(struct.unpack_from("<hh", d, o_frames + 4 * i)) for i in range((o_pal - o_frames) // 4)]
        self.pal_pages = [list(struct.unpack_from("<IHBB", d, o_pal + 8 * i)) for i in range((o_img - o_pal) // 8)]
        self.img_pages = [list(struct.unpack_from("<IHBB", d, o_img + 8 * i)) for i in range(self.img_ends[4])]
        self.banks = {}
        self.wad = wad
        self.globals = g

    def bank(self, b: int) -> bytes:
        if b not in self.banks:
            self.banks[b] = self.wad((self.globals / f"hud_banks/{b}.bin").read_bytes())
        return self.banks[b]

    def page(self, index: int, pages, ends, size):
        addr, _, wl, hl = pages[index]
        b = next(b for b in range(5) if index < ends[b])
        off = addr & 0x7FFFFFFF
        return self.bank(b)[off:off + size(wl, hl)], 1 << wl, 1 << hl

    def frame_pixels(self, frame: int):
        pal_i, img_i = self.frames[frame]
        pix, w, h = self.page(img_i, self.img_pages, self.img_ends, lambda wl, hl: 1 << (wl + hl))
        return pix, w, h, pal_i

    def palette(self, index: int):
        raw, _, _ = self.page(index, self.pal_pages, self.pal_ends, lambda wl, hl: 1024)
        def csm1(i):
            return i ^ 0x18 if ((i >> 3) & 1) != ((i >> 4) & 1) else i
        return [tuple(raw[csm1(i) * 4:csm1(i) * 4 + 3]) for i in range(256)]

    def icon(self, icon_id: int):
        return next(i for i in self.icons if i[0] == icon_id)


def convert(openrac: Path, png: Path):
    """The two frames of the new icon, as palette indices."""
    hdr = HudHeader(openrac)
    w, h, px = png_decode(png.read_bytes())
    if (w, h) != (SIZE, SIZE):
        raise SystemExit(f"{png}: {w}x{h}, the icon must be {SIZE}x{SIZE}")
    _, _, start, _, _ = hdr.icon(ICON_GRINDBOOTS)
    black, _, _, pal_black = hdr.frame_pixels(start)
    white, _, _, pal_white = hdr.frame_pixels(start + 1)
    if (pal_black, pal_white) != (PALETTE_BLACK, PALETTE_WHITE):
        raise SystemExit("the Grindboots frames do not use palette pages 5 and 6")
    colours = hdr.palette(PALETTE_BLACK)
    # The entries the Grindboots black frame uses (tile reds and glyph blacks), and
    # for each the entry the white frame has at the same pixels.
    pairs = {}
    for b, wv in zip(black, white):
        pairs.setdefault(b, {}).setdefault(wv, 0)
        pairs[b][wv] += 1
    to_white = {b: max(m, key=m.get) for b, m in pairs.items()}
    used = sorted(pairs)
    frame0 = bytearray()
    for r, g, b in px:
        best = min(used, key=lambda i: (colours[i][0] - r) ** 2 + (colours[i][1] - g) ** 2 + (colours[i][2] - b) ** 2)
        frame0.append(best)
    frame1 = bytes(to_white[i] for i in frame0)
    return bytes(frame0), frame1, hdr


ICON_SLIDER, SLIDER_GREEN_BAR = 0xE99E, 8
BAR_GREEN_WHITE = 224            # the green bar's red channel at its white centre (0 at its green edges)
BAR_ORANGE = (236, 112, 0)       # the owner's choice of the four previewed (2026-10-21)
BAR_HIGHLIGHT = (234, 228, 214)  # the centre line, warm as the green and blue bars' are tinted
PALETTE_SYMBOL = "ee_palette_movie_bar"


def movie_bar_palette(hdr: HudHeader) -> bytes:
    """The green bar's palette page, raw (CSM1 order), with the bar's entries orange."""
    _, count, start, _, _ = hdr.icon(ICON_SLIDER)
    if count <= SLIDER_GREEN_BAR:
        raise SystemExit(f"icon 0x{ICON_SLIDER:X} has {count} frames, not the green bar")
    pixels, _, _, pal = hdr.frame_pixels(start + SLIDER_GREEN_BAR)
    raw, _, _ = hdr.page(pal, hdr.pal_pages, hdr.pal_ends, lambda wl, hl: 1024)
    out = bytearray(raw)
    def csm1(i):
        return i ^ 0x18 if ((i >> 3) & 1) != ((i >> 4) & 1) else i
    for i in set(pixels):
        at = csm1(i) * 4
        r, g, b = out[at:at + 3]
        if (r, g, b) == (0, 0, 0):
            continue                       # the black margin above and below the bar
        if g < max(r, b):
            raise SystemExit(f"palette {pal} entry {i} ({r}, {g}, {b}) is not the green bar's")
        k = min(r / BAR_GREEN_WHITE, 1.0)
        out[at:at + 3] = bytes(round(c * (1 - k) + h * k) for c, h in zip(BAR_ORANGE, BAR_HIGHLIGHT))
    return bytes(out)


ICON_BADGE_FRAME = 4                     # the Challenge Mode "R" badge of the Load Game list (16 x 16)
BADGE_GREEN = (46, 196, 62)              # the gold's place in the hue circle, moved to green
BADGE_SYMBOL = "ee_palette_randomizer_badge"


def header_define(name: str) -> int:
    import re
    m = re.search(rf"#define\s+{name}\s+(0x[0-9A-Fa-f]+|\d+)", (ROOT / "src/modseg.h").read_text())
    if not m:
        raise SystemExit(f"{name} is not defined in src/modseg.h")
    return int(m.group(1), 0)


def randomizer_badge_palette(hdr: HudHeader) -> bytes:
    """The R badge's palette page with its gold entries green (src/goodies.c: the Randomizer
    save's badge). The badge is a gold disc, white at its highlight, with a dark grey R: each
    gold entry (red well above blue) keeps its brightness and goes to the green of the same
    brightness; the greys and the black stay. The edition's icon id must be one retail lacks."""
    icon_id = header_define("EE_ICON_RANDOMIZER")
    if any(i[0] == icon_id for i in hdr.icons):
        raise SystemExit(f"icon 0x{icon_id:X} exists in retail's HUD header; pick another EE_ICON_RANDOMIZER")
    _, count, start, _, _ = hdr.icon(ICON_SLIDER)
    if count <= ICON_BADGE_FRAME:
        raise SystemExit(f"icon 0x{ICON_SLIDER:X} has {count} frames, not the R badge")
    pixels, w, h, pal = hdr.frame_pixels(start + ICON_BADGE_FRAME)
    if (w, h) != (16, 16):
        raise SystemExit(f"the R badge is {w}x{h}, not 16x16")
    raw, _, _ = hdr.page(pal, hdr.pal_pages, hdr.pal_ends, lambda wl, hl: 1024)
    out = bytearray(raw)
    def csm1(i):
        return i ^ 0x18 if ((i >> 3) & 1) != ((i >> 4) & 1) else i
    for i in set(pixels):
        at = csm1(i) * 4
        r, g, b = out[at:at + 3]
        if r < b + 40 or r < 100:
            continue                       # black, the R's greys, the rim's shadow
        lum = (299 * r + 587 * g + 114 * b) / 1000
        k = min(lum / 255, 1.0)
        # the green at this brightness: dark greens toward black, bright ones toward white
        green = tuple(round(c * k * 1.15) if lum < 180 else round(c + (255 - c) * (lum - 180) / 75)
                      for c in BADGE_GREEN)
        out[at:at + 3] = bytes(min(255, max(0, v)) for v in green)
    return bytes(out)


MYSTERY_SYMBOL, MYSTERY_PALETTE_SYMBOL = "ee_icon_mystery", "ee_palette_mystery"
# the weapon icons' own background (measured on their frames): a diagonal blue gradient, light at
# the top left, deep at the bottom right, no rim: the tile looks like its neighbours on the strip
TILE_CORNERS = ((129, 135, 242), (113, 118, 225), (81, 86, 185), (64, 68, 164))   # TL, TR, BL, BR
GLYPH = (236, 236, 240)              # the question mark, the font's near white
TILE_RADIUS = 0                      # square corners, as the weapon tiles'


def mystery_tile() -> tuple[bytes, bytes]:
    """The vendor's mystery tile (src/randomizer_play.c: a purchase entry's icon): a square blue
    tile with a bold question mark, drawn here with 4x4 supersampling, as one 32x32 8-bit frame
    with a palette page of its own (RGBA in the game's CSM1 order, alpha 0x80 opaque, 0 clear)."""
    import math
    n = 4
    size = SIZE * n
    cov = [[None] * size for _ in range(size)]      # None clear, else (r, g, b)

    def in_tile(x, y):
        lo, hi, r = 0, SIZE * n, TILE_RADIUS * n          # edge to edge, as the item icons fill their frame
        if not (lo <= x < hi and lo <= y < hi):
            return 0
        if r == 0:
            edge = x < lo + 1.2 * n or x >= hi - 1.2 * n or y < lo + 1.2 * n or y >= hi - 1.2 * n
            return 1 if edge else 2
        cx = lo + r if x < lo + r else hi - r if x >= hi - r else x
        cy = lo + r if y < lo + r else hi - r if y >= hi - r else y
        if (cx, cy) == (x, y):
            return 2
        d = math.hypot(x - cx, y - cy)
        return 0 if d > r else 1 if d > r - 1.2 * n else 2

    def in_glyph(x, y):
        # the question mark in 32-pixel units, centred on x 16: an arc from the left, over the
        # top and down the right side; a stem from the arc's foot to the middle; a dot below
        px, py = x / n, y / n
        thick = 3.2
        acx, acy, ar = 16.0, 11.5, 5.2
        d = math.hypot(px - acx, py - acy)
        ang = math.degrees(math.atan2(acy - py, px - acx))      # 0 right, 90 up, 180 left
        on_arc = abs(d - ar) <= thick / 2 and (ang >= -75 or ang <= -170)
        stem = 16.0 - thick / 2 <= px <= 16.0 + thick / 2 and acy + 2.0 <= py <= 20.5
        dot = math.hypot(px - 16.0, py - 24.5) <= 1.9
        return on_arc or stem or dot

    for y in range(size):
        for x in range(size):
            t = in_tile(x, y)
            if t == 0:
                continue
            if in_glyph(x, y):
                cov[y][x] = GLYPH
                continue
            u, v = x / (size - 1), y / (size - 1)
            tl, tr, bl, br = TILE_CORNERS
            cov[y][x] = tuple(round(tl[i] * (1 - u) * (1 - v) + tr[i] * u * (1 - v)
                                    + bl[i] * (1 - u) * v + br[i] * u * v) for i in range(3))
    pixels = []
    for y in range(SIZE):
        for x in range(SIZE):
            samples = [cov[y * n + j][x * n + i] for j in range(n) for i in range(n)]
            opaque = [s for s in samples if s is not None]
            if len(opaque) < n * n // 2:
                pixels.append(None)
                continue
            r = sum(s[0] for s in opaque) // len(opaque)
            g = sum(s[1] for s in opaque) // len(opaque)
            b = sum(s[2] for s in opaque) // len(opaque)
            pixels.append((r // 8 * 8, g // 8 * 8, b // 8 * 8))
    colours = sorted({p for p in pixels if p is not None})
    if len(colours) > 255:
        raise SystemExit(f"the mystery tile has {len(colours)} colours, over 255")
    index = {c: i + 1 for i, c in enumerate(colours)}

    def csm1(i):
        return i ^ 0x18 if ((i >> 3) & 1) != ((i >> 4) & 1) else i

    palette = bytearray(1024)
    for c, i in index.items():
        palette[csm1(i) * 4:csm1(i) * 4 + 4] = bytes((c[0], c[1], c[2], 0x80))
    frame = bytes(0 if p is None else index[p] for p in pixels)
    return frame, bytes(palette)


def c_bytes(symbol: str, data: bytes) -> list[str]:
    lines = [f"const unsigned char {symbol}[{len(data)}] __attribute__((aligned(16))) = {{"]
    for i in range(0, len(data), 32):
        lines.append("  " + ", ".join(str(b) for b in data[i:i + 32]) + ",")
    return lines + ["};"]


def emit(args) -> int:
    frame0, frame1, hdr = convert(args.openrac, args.png)
    data = frame0 + frame1
    palette = movie_bar_palette(hdr)
    badge = randomizer_badge_palette(hdr)
    tile, tile_palette = mystery_tile()
    lines = ["/* generated by tools/gen_hud.py from art/charge_boots.png: icon 0xEA84's two 32x32 frames",
             "   (black; white while equipped), 8-bit indices into HUD palette pages 5 and 6. "
             "See src/charge_boots.c. */",
             *c_bytes(SYMBOL, data),
             "/* the Movie volume bar's palette: the green bar's palette page with its entries orange "
             "(src/hud_icons.c) */",
             *c_bytes(PALETTE_SYMBOL, palette),
             "/* the Randomizer badge's palette: the R badge's palette page with its gold entries green "
             "(src/hud_icons.c) */",
             *c_bytes(BADGE_SYMBOL, badge),
             "/* the vendor's mystery tile (EE_ICON_MYSTERY): one 32x32 frame and its palette page "
             "(src/hud_icons.c) */",
             *c_bytes(MYSTERY_SYMBOL, tile),
             *c_bytes(MYSTERY_PALETTE_SYMBOL, tile_palette)]
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "gen_icons.c").write_text("\n".join(lines) + "\n")
    print(f"wrote {args.out / 'gen_icons.c'} "
          f"({len(data)} bytes of pixels, {len(palette) + len(badge)} bytes of palettes)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("--png", type=Path, default=ROOT / "art/charge_boots.png")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/modseg")
    args = ap.parse_args()
    return emit(args)


if __name__ == "__main__":
    sys.exit(main())
