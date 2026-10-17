#!/usr/bin/env python3
"""
The Charge Boots icon: pixels for the resident segment, and a HUD header
that knows them.

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

  emit    writes build/modseg/gen_icons.c, the 2 KB of pixels, 16-aligned,
          for the resident segment (tools/modseg.py picks gen_*.c up);
  header  writes build/hud_header.bin: the retail header plus icon 0xEA84
          with two frames whose image pages sit past the last bank's range
          and point at the pixels' resident address (build/modseg/modseg.sym),
          so no bank is recompressed; tools/overlay_patch.py writes the file
          over the header's sectors.

  python tools/gen_hud.py emit|header [--openrac DIR]
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
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
        sys.path.insert(0, str(openrac / "editor"))
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
        csm1 = lambda i: i ^ 0x18 if ((i >> 3) & 1) != ((i >> 4) & 1) else i
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


def emit(args) -> int:
    frame0, frame1, _ = convert(args.openrac, args.png)
    data = frame0 + frame1
    lines = ["/* generated by tools/gen_hud.py from art/charge_boots.png: icon 0xEA84's two 32x32 frames",
             "   (black; white while equipped), 8-bit indices into HUD palette pages 5 and 6. See src/charge_boots.c. */",
             f"const unsigned char {SYMBOL}[{len(data)}] __attribute__((aligned(16))) = {{"]
    for i in range(0, len(data), 32):
        lines.append("  " + ", ".join(str(b) for b in data[i:i + 32]) + ",")
    lines.append("};")
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "gen_icons.c").write_text("\n".join(lines) + "\n")
    print(f"wrote {args.out / 'gen_icons.c'} ({len(data)} bytes of pixels)")
    return 0


def header(args) -> int:
    sys.path.insert(0, str(HERE))
    from overlay_patch import modseg_symbol
    address = modseg_symbol(SYMBOL)
    if address & 0xF or address >= 0x80000000:
        raise SystemExit(f"{SYMBOL} at 0x{address:X}: the pixels must be 16-byte aligned, below 0x80000000")
    hdr = HudHeader(args.openrac)
    if any(i[0] == ICON_ID for i in hdr.icons):
        raise SystemExit(f"icon 0x{ICON_ID:X} already exists in the header")
    n_frames, n_img = len(hdr.frames), len(hdr.img_pages)
    icons = hdr.icons + [[ICON_ID, 2, n_frames, 0, 0]]
    frames = hdr.frames + [[PALETTE_BLACK, n_img], [PALETTE_WHITE, n_img + 1]]
    img_pages = hdr.img_pages + [[address, 0, 5, 5], [address + SIZE * SIZE, 0, 5, 5]]   # past every bank's range
    o_icons = hdr.o_icons
    o_frames = o_icons + 8 * (len(icons) + 1)
    o_pal = o_frames + 4 * len(frames)
    o_img = o_pal + 8 * len(hdr.pal_pages)
    end = o_img + 8 * len(img_pages)
    if end > len(hdr.raw):
        raise SystemExit("the HUD header's tables do not fit its sectors")
    out = bytearray(hdr.raw)
    struct.pack_into("<2H", out, 0, len(icons) + 1, len(frames))
    struct.pack_into("<4I", out, 4, o_icons, o_frames, o_pal, o_img)
    out[o_icons:] = bytes(len(out) - o_icons)
    for i, row in enumerate(icons):
        struct.pack_into("<HHHBB", out, o_icons + 8 * i, *row)
    struct.pack_into("<H", out, o_icons + 8 * len(icons), 0xFFFF)
    for i, f in enumerate(frames):
        struct.pack_into("<hh", out, o_frames + 4 * i, *f)
    for i, p in enumerate(hdr.pal_pages):
        struct.pack_into("<IHBB", out, o_pal + 8 * i, *p)
    for i, p in enumerate(img_pages):
        struct.pack_into("<IHBB", out, o_img + 8 * i, *p)
    (ROOT / "build").mkdir(exist_ok=True)
    (ROOT / "build/hud_header.bin").write_bytes(bytes(out))
    print(f"wrote build/hud_header.bin: icon 0x{ICON_ID:X}, frames {n_frames}-{n_frames + 1}, "
          f"image pages {n_img}-{n_img + 1} at 0x{address:X}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("step", choices=["emit", "header"])
    ap.add_argument("--openrac", type=Path, default=ROOT.parent / "OpenRAC")
    ap.add_argument("--png", type=Path, default=ROOT / "art/charge_boots.png")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/modseg")
    args = ap.parse_args()
    return emit(args) if args.step == "emit" else header(args)


if __name__ == "__main__":
    sys.exit(main())
