#!/usr/bin/env python3
"""
Read the edition's HelpDesk lines' cut lists from their DaVinci Resolve timelines.

Every new HelpDesk line is made from the game's own English lines: pieces of
them cut and joined in DaVinci Resolve. The repository keeps no audio, only
each line's cut list (audio/helpdesk.json), and tools/gen_helpdesk.py builds
the lines from the player's own disc. This updates the cut lists from the
timelines Resolve exports (File > Export > Timeline, .drt).

A .drt is a zip whose SeqContainer/*.xml holds the timeline. Its audio tracks
list clips (Sm2TiAudioClip: the source file, Start and Duration on the
timeline, In in the source) and transitions between them (Sm2TiTransition:
Start and Duration, centred on the cut, "Cross Fade 0DB" = a linear
crossfade). Times are frames of 1/24 s: a whole number, then optionally "|"
and a little-endian double with the fraction. The timeline starts at
01:00:00:00, frame 86400; the cut list counts from the first clip. A source
file is one of OpenRAC's decoded HelpDesk lines, named NNN_name.wav for slot
NNN; the cut list keeps the slot and the name.

Each entry of audio/helpdesk.json names its timeline ("project"); this
rewrites the entries' clips and leaves the rest. A new line is added to the
file by hand (its slot, its name, its use: "future" with the retail slot it
"replaces", or "strafe" / "strafe_future" for the strafing help, and its
project), then read in here.

  python tools/helpdesk_recipes.py TIMELINES_DIR [--recipes audio/helpdesk.json]
"""
import argparse
import json
import re
import struct
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
TIMELINE_START = 86400      # 01:00:00:00 at 24 frames a second
FRAME_RATE = 24.0
CROSSFADE = "Cross Fade 0DB"
DECIMALS = 6                # a millionth of a frame: under 0.002 samples at 44.1 kHz


def frames(text: str) -> float:
    """'86452|000000000000b43f' -> 86452 + the double after the bar."""
    if not text:
        return 0.0
    whole, _, fraction = text.partition("|")
    value = float(whole)
    if fraction:
        value += struct.unpack("<d", bytes.fromhex(fraction))[0]
    return value


def field(block: str, name: str) -> str:
    match = re.search(rf"<{name}>([^<]*)</{name}>|<{name}/>", block)
    return (match.group(1) or "") if match else ""


def read_timeline(path: Path) -> list[dict]:
    """The clips of the one audio track that has any, with their crossfades as fade_in/fade_out halves."""
    archive = zipfile.ZipFile(path)
    xml = "".join(archive.read(n).decode("utf-8") for n in archive.namelist() if n.startswith("SeqContainer/"))
    tracks = [t for t in re.findall(r"<Sm2TiTrack .*?</Sm2TiTrack>", xml, re.S)
              if field(t, "Type") == "1" and "<Sm2TiAudioClip" in t]
    if len(tracks) != 1:
        raise SystemExit(f"{path.name}: {len(tracks)} audio tracks with clips; the cut list needs exactly one")
    clips, fades = [], []
    for kind, item in re.findall(r"<(Sm2TiAudioClip|Sm2TiTransition) .*?>(.*?)</\1>", tracks[0], re.S):
        start, length = frames(field(item, "Start")), frames(field(item, "Duration"))
        if kind == "Sm2TiTransition":
            if field(item, "PrettyType") != CROSSFADE or field(item, "AlignmentType") != "2":
                raise SystemExit(f"{path.name}: a {field(item, 'PrettyType')!r} transition; only centred "
                                 f"{CROSSFADE!r} is supported")
            fades.append((start + length / 2, length / 2))
            continue
        rate = struct.unpack("<d", bytes.fromhex(field(item, "MediaFrameRate")[:16]))[0]
        if rate != FRAME_RATE:
            raise SystemExit(f"{path.name}: a clip at {rate} frames a second; the cut lists use {FRAME_RATE}")
        source = Path(field(item, "MediaFilePath").replace("\\", "/")).stem
        match = re.match(r"(\d+)_(.+)$", source)
        if not match:
            raise SystemExit(f"{path.name}: source {source!r} is not one of the game's lines (NNN_name.wav)")
        clips.append({"line": int(match.group(1)), "source": match.group(2), "at": start,
                      "from": frames(field(item, "In")), "length": length, "fade_in": 0.0, "fade_out": 0.0})
    clips.sort(key=lambda c: c["at"])
    for centre, half in fades:
        before = min(clips, key=lambda c: abs(c["at"] + c["length"] - centre))
        after = min(clips, key=lambda c: abs(c["at"] - centre))
        if abs(before["at"] + before["length"] - centre) > 1e-3 or abs(after["at"] - centre) > 1e-3:
            raise SystemExit(f"{path.name}: a crossfade at frame {centre - TIMELINE_START:.3f} is not on a cut")
        before["fade_out"] = after["fade_in"] = half
    origin = clips[0]["at"]
    for clip in clips:
        clip["at"] -= origin
        for key in ("at", "from", "length", "fade_in", "fade_out"):
            clip[key] = round(clip[key], DECIMALS) + 0.0
    return clips


def dump(recipes: dict) -> str:
    """The cut lists as JSON with one clip per line, so a change reads as a change to that clip."""
    lines = ["{"]
    for key, value in recipes.items():
        if key != "lines":
            lines.append(f"  {json.dumps(key)}: {json.dumps(value)},")
    lines.append('  "lines": [')
    for n, entry in enumerate(recipes["lines"]):
        head = {k: v for k, v in entry.items() if k != "clips"}
        lines.append("    {" + json.dumps(head)[1:-1] + ', "clips": [')
        for m, clip in enumerate(entry["clips"]):
            lines.append("      " + json.dumps(clip) + ("," if m < len(entry["clips"]) - 1 else ""))
        lines.append("    ]}" + ("," if n < len(recipes["lines"]) - 1 else ""))
    lines += ["  ]", "}", ""]
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("timelines", type=Path, help="the folder of .drt exports")
    ap.add_argument("--recipes", type=Path, default=ROOT / "audio/helpdesk.json")
    args = ap.parse_args()

    recipes = json.loads(args.recipes.read_text(encoding="utf-8"))
    for entry in recipes["lines"]:
        path = args.timelines / (entry["project"] + ".drt")
        if not path.is_file():
            raise SystemExit(f"slot {entry['slot']}: no {path}")
        entry["clips"] = read_timeline(path)
        end = max(c["at"] + c["length"] for c in entry["clips"])
        print(f"slot {entry['slot']}: {path.name}, {len(entry['clips'])} clips, {end / FRAME_RATE:.3f} s")
    args.recipes.write_text(dump(recipes), encoding="utf-8")
    print(f"wrote {args.recipes}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
