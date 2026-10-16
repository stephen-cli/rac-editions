#!/usr/bin/env bash
# Executive Edition pipeline: OpenRAC build + audits, bootable executable,
# disc image. Run from anywhere in Git Bash.
#
#   bash tools/pack.sh [--quick]
#
#   --quick   in-place copy of the owner's image (same-size changes only, ~10 s)
#             -> OpenRAC/games/rac1/pal/build-sn/SCES_509.16.iso
#   default   wrench full re-layout: game bank + wrench overlay + mod/ (~3 min)
#             -> OpenRAC/build/wrench/executive-edition.iso
#
# OPENRAC and WRENCH override the sibling-directory defaults.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
OPENRAC=${OPENRAC:-$(cd "$HERE/../OpenRAC" && pwd)}
WRENCH=${WRENCH:-$(cd "$HERE/../wrench" && pwd)}
GAME_BANK=$OPENRAC/assets/wrench/rac1-pal
PAL=$OPENRAC/games/rac1/pal

echo "== OpenRAC branch: $(git -C "$OPENRAC" branch --show-current)"
cd "$PAL"
echo "== 1/3 build"
bash tools/build_sn.sh
echo "== 2/3 bootable executable"
python tools/boot_elf.py
if [ "${1:-}" = "--quick" ]; then
  echo "== 3/3 in-place image"
  python tools/iso_patch.py
  echo "-> $PAL/build-sn/SCES_509.16.iso"
  exit 0
fi
cp build-sn/SCES_509.16 "$HERE/mod/SCES_509.16"
echo "== 3/3 wrench pack"
mkdir -p "$OPENRAC/build/wrench"
"$WRENCH/bin/wrenchbuild.exe" pack "$GAME_BANK" "$WRENCH/data/overlay" "$HERE/mod" \
  -a rac -o "$OPENRAC/build/wrench/executive-edition.iso" -h release \
  | grep -vE '^Parsing|^\[ *[0-9]+%\]'
echo "-> $OPENRAC/build/wrench/executive-edition.iso"
