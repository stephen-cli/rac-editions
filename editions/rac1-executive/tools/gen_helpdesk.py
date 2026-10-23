#!/usr/bin/env python3
"""
Build the edition's HelpDesk lines from the game's own lines on the player's disc.

audio/helpdesk.json holds each new line as a cut list of the game's English
HelpDesk lines (tools/helpdesk_recipes.py reads it from the DaVinci Resolve
timelines the lines were made in); no audio is kept in the repository. This
reads the lines the cut lists name from the PAL disc, decodes them, joins the
pieces with their crossfades, encodes each new line as a VAG in the format of
the game's own and writes:

  build/helpdesk/NNN_name.vag          one per line, for tools/splice_helpdesk.py
  build/helpdesk/preview/NNN_name.wav  the same decoded, as the game plays it
  build/modseg/gen_helpdesk.c          the slots for src/helpdesk_voices.c and
                                       src/strafe_help.c

The disc's lines. The table of contents (sector 1500) has the help-audio group
at byte 0x1AB8: a word per line, the first sector of its VAG file, 150 lines
per language in language order, English first. A VAG file is a 0x30-byte
header ("VAGp", big-endian version 0x20, data size at 0x0C and sample rate,
44,100, at 0x10, the line's name at 0x20) and SPU-ADPCM frames of 16 bytes:
a byte of filter (high nibble) and shift (low nibble), a flag byte and 28
4-bit samples. Each sample is the nibble, sign-extended and shifted up by
12 - shift, plus (f0 * s1 + f1 * s2 + 32) >> 6 with the filter's pair from
{(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)}, clamped to 16 bits; the
first frame is silent, and the last audio frame carries flag 1 before an end
frame with flag 7.

Joining. Times are frames of 1/24 s (1837.5 samples). A clip plays its source
from "from" for "length", placed at "at"; a crossfade between two clips is
linear and centred on the cut, so the clip before keeps playing for fade_out
and the clip after starts fade_in early, each ramping between full and
silent. The levels are the game's own: clips are not scaled. The line then
fades in over 2 ms and out over 12 ms, so a cut through a word does not
click.

Encoding. Each frame of 28 samples takes the filter and shift whose decoded
samples come closest to the line (squared error), decoded as the game will,
so errors do not build up; the search tries, for each filter, the shift the
frame's range needs and its two neighbours, and digital silence is written as
retail writes it (filter 0, shift 12). The results are cached in
build/helpdesk/cache by a hash of the cut list, its source lines and this
encoder's version, so a build after the first takes a moment.

  python tools/gen_helpdesk.py [--openrac DIR] [--iso SCES_509.16.iso]
"""
import argparse
import hashlib
import json
import struct
import sys
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SECTOR = 2048
TOC_LBA = 1500
HELP_AUDIO = 0x1AB8
LINES_PER_LANGUAGE = 150
RATE = 44100
FRAME = RATE / 24.0
VAG_HEADER = 0x30
FADE_IN, FADE_OUT = RATE * 2 // 1000, RATE * 12 // 1000
FILTERS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))
ENCODER_VERSION = 1     # change when the rendering or the encoder changes: invalidates the cache
USES = ("future", "strafe", "strafe_future")


def clamp16(v: int) -> int:
    return -32768 if v < -32768 else 32767 if v > 32767 else v


def decode(vag: bytes) -> list[int]:
    """The audio frames of a VAG (not its silent first frame or its end frame) as 16-bit samples."""
    size, = struct.unpack_from(">I", vag, 12)
    frames = size // 16
    out = []
    s1 = s2 = 0
    for n in range(frames):
        at = VAG_HEADER + 16 * n
        shift, filt = vag[at] & 15, vag[at] >> 4
        f0, f1 = FILTERS[filt] if filt < 5 else (0, 0)
        for k in range(28):
            nibble = (vag[at + 2 + k // 2] >> (4 * (k & 1))) & 15
            value = ((nibble ^ 8) - 8) << 12 >> shift
            value = clamp16(value + ((f0 * s1 + f1 * s2 + 32) >> 6))
            s2, s1 = s1, value
            out.append(value)
    return out[28:(frames - 1) * 28]


def encode_frame(x: list[int], s1: int, s2: int):
    """The best (filter, shift, nibbles, s1, s2) for 28 samples after decoded state s1, s2."""
    if not any(x):
        return 0, 12, [0] * 28, 0, 0
    best = None
    for filt, (f0, f1) in enumerate(FILTERS):
        # The shift the frame needs: residuals of the line's own samples against the filter.
        a, b, peak = s1, s2, 0
        for v in x:
            d = v - ((f0 * a + f1 * b + 32) >> 6)
            peak = max(peak, d if d >= 0 else -d)
            b, a = a, v
        fit = 12                            # the finest shift whose steps still reach the peak
        while fit > 0 and peak > 7 << (12 - fit):
            fit -= 1
        for shift in (fit - 1, fit, fit + 1):
            if not 0 <= shift <= 12:
                continue
            step = 1 << (12 - shift)
            half = step >> 1
            a, b, err, nibbles = s1, s2, 0, []
            for v in x:
                p = (f0 * a + f1 * b + 32) >> 6
                d = v - p
                q = (d + half) // step if d >= 0 else -((half - d) // step)
                q = -8 if q < -8 else 7 if q > 7 else q
                r = clamp16(q * step + p)
                err += (v - r) * (v - r)
                if best is not None and err >= best[0]:
                    break
                nibbles.append(q & 15)
                b, a = a, r
            else:
                if best is None or err < best[0]:
                    best = (err, filt, shift, nibbles, a, b)
    _, filt, shift, nibbles, a, b = best
    return filt, shift, nibbles, a, b


def encode(samples: list[int], name: str) -> bytes:
    """A VAG file in the format of the game's lines."""
    blocks = (len(samples) + 27) // 28
    padded = samples + [0] * (blocks * 28 - len(samples))
    body = bytearray(16)                    # the silent first frame
    s1 = s2 = 0
    head = 0
    for n in range(blocks):
        filt, shift, nibbles, s1, s2 = encode_frame(padded[28 * n:28 * n + 28], s1, s2)
        head = filt << 4 | shift
        frame = bytearray(16)
        frame[0] = head
        frame[1] = 1 if n == blocks - 1 else 0
        for k in range(0, 28, 2):
            frame[2 + k // 2] = nibbles[k] | nibbles[k + 1] << 4
        body += frame
    body += bytes([head, 7]) + bytes(14)     # the end frame repeats the last frame's first byte
    header = b"VAGp" + struct.pack(">3I", 0x20, 0, len(body)) + struct.pack(">I", RATE) + bytes(12)
    header += name.encode("ascii")[:16].ljust(16, b"\0")
    data = header + bytes(body)
    return data + bytes(-len(data) % SECTOR)


def render(clips: list[dict], sources: dict[int, list[int]]) -> list[int]:
    end = max(c["at"] + c["length"] for c in clips)
    mix = [0.0] * int(round(end * FRAME))
    for c in clips:
        src = sources[c["line"]]
        first, last = c["at"] - c["fade_in"], c["at"] + c["length"] + c["fade_out"]
        for i in range(max(0, int(first * FRAME)), min(len(mix), int(last * FRAME) + 1)):
            t = i / FRAME
            if t < first or t >= last:
                continue
            s = int(round((t - c["at"] + c["from"]) * FRAME))
            if not 0 <= s < len(src):
                continue
            gain = 1.0
            if c["fade_in"] and t < c["at"] + c["fade_in"]:
                gain = (t - first) / (2 * c["fade_in"])
            if c["fade_out"] and t > c["at"] + c["length"] - c["fade_out"]:
                gain *= (last - t) / (2 * c["fade_out"])
            mix[i] += gain * src[s]
    for i in range(min(FADE_IN, len(mix))):
        mix[i] *= i / FADE_IN
    for i in range(min(FADE_OUT, len(mix))):
        mix[len(mix) - 1 - i] *= i / FADE_OUT
    return [clamp16(int(round(v))) for v in mix]


def disc_lines(iso: Path, wanted: set[int]) -> dict[int, bytes]:
    """The English lines' VAG files from the disc, by slot."""
    out = {}
    with iso.open("rb") as f:
        f.seek(TOC_LBA * SECTOR + HELP_AUDIO)
        table = struct.unpack(f"<{LINES_PER_LANGUAGE}I", f.read(4 * LINES_PER_LANGUAGE))
        for slot in sorted(wanted):
            if not table[slot]:
                raise SystemExit(f"the disc has no English line {slot}")
            f.seek(table[slot] * SECTOR)
            head = f.read(VAG_HEADER)
            if head[:4] != b"VAGp":
                raise SystemExit(f"line {slot}: no VAG at sector {table[slot]}")
            size, = struct.unpack_from(">I", head, 12)
            out[slot] = head + f.read(size)
    return out


def write_wav(path: Path, samples: list[int]) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(struct.pack(f"<{len(samples)}h", *samples))


def emit_c(lines: list[dict], path: Path) -> None:
    pairs = [(e["replaces"], e["slot"], e["name"]) for e in lines if e["use"] == "future"]
    strafe = {e["use"]: e["slot"] for e in lines if e["use"] != "future"}
    if set(strafe) != {"strafe", "strafe_future"}:
        raise SystemExit("audio/helpdesk.json needs one line for each of 'strafe' and 'strafe_future'")
    out = ["/* generated by tools/gen_helpdesk.py from audio/helpdesk.json */",
           "/* The Future layout's lines: {retail slot, the new line's slot} (src/helpdesk_voices.c). */",
           f"const short ee_helpdesk_future[{len(pairs)}][2] = {{"]
    out += [f"    {{ {old:3d}, {new} }},   /* {name} */" for old, new, name in pairs]
    out += ["};", f"const int ee_helpdesk_future_count = {len(pairs)};",
            "/* The strafing help's line in Classic and in Future (src/strafe_help.c). */",
            f"const short ee_helpdesk_strafe[2] = {{ {strafe['strafe']}, {strafe['strafe_future']} }};", ""]
    path.write_text("\n".join(out))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parents[2] / "OpenRAC")
    ap.add_argument("--iso", type=Path, help="the PAL disc image (default: OpenRAC's games/rac1/baserom)")
    ap.add_argument("--recipes", type=Path, default=ROOT / "audio/helpdesk.json")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/helpdesk")
    ap.add_argument("--c-out", type=Path, default=ROOT / "build/modseg/gen_helpdesk.c")
    args = ap.parse_args()

    recipes = json.loads(args.recipes.read_text(encoding="utf-8"))
    lines = recipes["lines"]
    for e in lines:
        if e["use"] not in USES or (e["use"] == "future") != (e.get("replaces") is not None):
            raise SystemExit(f"slot {e['slot']}: use {e['use']!r} with replaces {e.get('replaces')!r}")
    iso = args.iso or args.openrac / "games/rac1/baserom/SCES_509.16.iso"
    vags = disc_lines(iso, {c["line"] for e in lines for c in e["clips"]})
    for e in lines:
        for c in e["clips"]:
            name = vags[c["line"]][0x20:0x30].rstrip(b"\0").decode("ascii")
            if name != c["source"][:16]:
                raise SystemExit(f"slot {e['slot']}: line {c['line']} on the disc is {name!r}, "
                                 f"the cut list expects {c['source']!r}")
    cache = args.out / "cache"
    preview = args.out / "preview"
    cache.mkdir(parents=True, exist_ok=True)
    preview.mkdir(parents=True, exist_ok=True)
    for old in args.out.glob("*.vag"):
        old.unlink()
    decoded = {}
    for e in lines:
        key = hashlib.sha256(json.dumps([ENCODER_VERSION, e["name"], e["clips"]], sort_keys=True).encode()
                             + b"".join(vags[s] for s in sorted({c["line"] for c in e["clips"]}))).hexdigest()
        cached = cache / f"{key[:32]}.vag"
        stem = f"{e['slot']}_{e['name']}"
        if not cached.is_file():
            for s in {c["line"] for c in e["clips"]}:
                if s not in decoded:
                    decoded[s] = decode(vags[s])
            samples = render(e["clips"], decoded)
            cached.write_bytes(encode(samples, e["name"]))
            write_wav(preview / f"{stem}.wav", decode(cached.read_bytes()))
            print(f"slot {e['slot']}: {e['name']}, {len(samples) / RATE:.3f} s, built")
        else:
            print(f"slot {e['slot']}: {e['name']}, cached")
        (args.out / f"{stem}.vag").write_bytes(cached.read_bytes())
    args.c_out.parent.mkdir(parents=True, exist_ok=True)
    emit_c(lines, args.c_out)
    print(f"wrote {len(lines)} lines to {args.out} and {args.c_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
