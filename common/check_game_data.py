#!/usr/bin/env python3
"""
Refuse game data in the repository.

The editions are built from each player's own discs, and nothing in this
repository may contain any game's data: no disc images, executables, archives
or other bytes taken from a disc. .gitignore keeps the usual files out, but a
forced add or a renamed file gets past it, so this check looks at the files
themselves.

  python common/check_game_data.py                 # every tracked file
  python common/check_game_data.py --staged        # the files staged for the next commit (the pre-commit hook)
  python common/check_game_data.py --range A..B    # also every file each commit in A..B adds or changes
                                                   # (any git rev-list arguments, in one string)

A file fails when its name, extension, contents or size looks like game data.
--range covers files a later commit deletes again, since history keeps them.
Exits 1 on any finding. .githooks/ runs it before each commit and push.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Disc images, executables, IOP modules, the games' archives and banks, sound and video.
EXTENSIONS = {
    ".iso", ".bin", ".img", ".cue", ".mdf", ".nrg", ".elf", ".irx", ".exe",
    ".wad", ".bnk", ".vag", ".vb", ".hd", ".bd", ".sck", ".pss", ".ipu",
    ".ps2", ".psu", ".max", ".mcd",
}
# PlayStation 2 boot executables (SCES_509.16), and the disc's system files.
NAMES = re.compile(r"^(SCES|SCUS|SLES|SLUS|SCPS|SLPS|SCKA|SCAJ)_\d{3}\.\d{2}$|^(SYSTEM\.CNF|IOPRP\d*\.IMG)$", re.I)
# Larger files need a reason: the docs are the only large files today.
MAX_SIZE = 256 * 1024
LARGE_OK = {".md", ".txt"}
# Files this check passes on purpose, with why.
ALLOWED = {
    "editions/rac1-executive/art/charge_boots.png": "the Charge Boots menu icon, drawn for the edition",
}


def git(*args: str) -> bytes:
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, check=True).stdout


def problem(path: str, data: bytes) -> str | None:
    if path in ALLOWED:
        return None
    name = path.rsplit("/", 1)[-1]
    ext = Path(name).suffix.lower()
    if NAMES.match(name):
        return "a PlayStation 2 disc file's name"
    if ext in EXTENSIONS:
        return f"a game data extension ({ext})"
    if data[:4] == b"\x7fELF":
        return "an ELF executable"
    if any(data[o:o + 5] == b"CD001" for o in (0x8001, 0x8801, 0x9001)):
        return "an ISO 9660 disc image"
    if len(data) > MAX_SIZE and ext not in LARGE_OK:
        return f"{len(data)} bytes, over the {MAX_SIZE} byte limit"
    if b"\0" in data[:8000] and ext not in {".png"}:
        return "binary content"
    return None


def tracked() -> list[tuple[str, str, bytes]]:
    paths = git("ls-files", "-z").decode().split("\0")
    return [(p, p, (ROOT / p).read_bytes()) for p in paths if p and (ROOT / p).is_file()]


def added(rng: str) -> list[tuple[str, str, bytes]]:
    out = []
    for commit in git("rev-list", *rng.split()).decode().split():
        # -z rows: ":mode mode sha sha status", then the path (two paths for a rename or copy).
        rows = git("diff-tree", "-r", "--no-commit-id", "--root", "--diff-filter=AMRC", "-z", commit).decode().split("\0")
        i = 0
        while i < len(rows) - 1:
            meta = rows[i].split()
            sha, status = meta[3], meta[4]
            i += 2 if status[0] in "RC" else 1
            path = rows[i]
            i += 1
            out.append((path, f"{path} in {commit[:12]}", git("cat-file", "blob", sha)))
    return out


def staged() -> list[tuple[str, str, bytes]]:
    paths = git("diff", "--cached", "--name-only", "--diff-filter=AMRC", "-z").decode().split("\0")
    return [(p, f"{p} (staged)", git("show", f":{p}")) for p in paths if p]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--range", help="also check the files each commit in this range adds or changes")
    ap.add_argument("--staged", action="store_true", help="check only the files staged for the next commit")
    args = ap.parse_args()
    files = staged() if args.staged else tracked() + (added(args.range) if args.range else [])
    found = 0
    for path, label, data in files:
        why = problem(path, data)
        if why:
            print(f"game data? {label}: {why}")
            found += 1
    print(f"{len(files)} files checked, {found} refused" + ("" if found else ": no game data"))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
