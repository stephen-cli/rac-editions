#!/usr/bin/env python3
"""
Write tools/overlay_patch.py's sector files into an image in place.

The patch route (pack.sh's default) copies the retail image and patches the copy with OpenRAC's
tools/iso_patch.py. The wrench route builds its own image, so the level-code
patches, computed against that image (overlay_patch.py --iso), are written
straight into it: patches.tsv lists `first sector<TAB>sectors<TAB>file`.

  python tools/apply_patches.py IMAGE build/patches-wrench/patches.tsv
"""
import sys
from pathlib import Path

SECTOR = 2048


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    image, listing = Path(sys.argv[1]), Path(sys.argv[2])
    size = image.stat().st_size
    total = 0
    with image.open("r+b") as f:
        for line in listing.read_text().splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            first, count, name = line.split("\t")[:3]
            first, count = int(first), int(count)
            blob = (listing.parent / name).read_bytes()
            if len(blob) != count * SECTOR or (first + count) * SECTOR > size:
                raise SystemExit(f"{name}: {len(blob)} bytes for {count} sectors at {first} do not fit the image")
            f.seek(first * SECTOR)
            old = f.read(len(blob))
            changed = sum(1 for a, b in zip(old, blob) if a != b)
            f.seek(first * SECTOR)
            f.write(blob)
            total += changed
    print(f"{image.name}: {total} byte(s) changed by {listing}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
