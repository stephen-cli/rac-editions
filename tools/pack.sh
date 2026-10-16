#!/usr/bin/env bash
# Executive Edition pipeline: OpenRAC build + audits, bootable executable,
# disc image. Run from anywhere in Git Bash.
#
#   bash tools/pack.sh [--quick]
#
#   --quick   in-place copy of the owner's image (same-size changes only, ~10 s)
#             -> OpenRAC/games/rac1/pal/build-sn/executive-edition.iso
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
echo "== 2/5 strings (tools/gen_text.py), menu data (tools/gen_menu.py), hero sites (tools/gen_hero.py), mod segment (tools/modseg.py)"
python "$HERE/tools/gen_text.py"
python "$HERE/tools/gen_menu.py" --openrac "$OPENRAC"
python "$HERE/tools/gen_hero.py" --openrac "$OPENRAC"
python "$HERE/tools/modseg.py" --openrac "$OPENRAC"
echo "== 3/5 bootable executable with the mod segment"
python tools/boot_elf.py --segment "A0000:$(cygpath -w "$HERE/build/modseg/modseg.bin" 2>/dev/null || echo "$HERE/build/modseg/modseg.bin")"
echo "== 4/5 level-code patches (tools/overlay_patch.py)"
python "$HERE/tools/overlay_patch.py" -o "$HERE/build/patches"
if [ "${1:-}" = "--quick" ]; then
  echo "== 5/5 image (executable in new sectors at the end, level patches in place)"
  # An image an emulator has open cannot be overwritten: fall back to a numbered name.
  out=build-sn/executive-edition.iso; n=1
  while [ -e "$out" ] && ! ( : >> "$out" ) 2>/dev/null; do n=$((n+1)); out=build-sn/executive-edition-$n.iso; done
  python tools/iso_patch.py --patches "$HERE/build/patches/patches.tsv" -o "$out"
  echo "-> $PAL/$out"
  exit 0
fi
cp build-sn/SCES_509.16 "$HERE/mod/SCES_509.16"
echo "== 5/5 wrench pack (NOTE: level-code patches and the text table are not applied on this route yet)"
mkdir -p "$OPENRAC/build/wrench"
"$WRENCH/bin/wrenchbuild.exe" pack "$GAME_BANK" "$WRENCH/data/overlay" "$HERE/mod" \
  -a rac -o "$OPENRAC/build/wrench/executive-edition.iso" -h release \
  | grep -vE '^Parsing|^\[ *[0-9]+%\]'
echo "-> $OPENRAC/build/wrench/executive-edition.iso"
