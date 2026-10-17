#!/usr/bin/env bash
# Executive Edition pipeline: OpenRAC build + audits, bootable executable,
# disc image. Run from anywhere in Git Bash.
#
#   bash tools/pack.sh [--quick]
#
#   --quick   in-place copy of the owner's image (same-size changes only, ~10 s);
#             the Charge Boots wear the Grindboots model
#             -> OpenRAC/games/rac1/build-sn/executive-edition.iso
#   default   wrench full re-layout (the edition's wrench fork): game bank + wrench
#             overlay + mod/ + the Charge Boots bank (R&C2's model, tools/gen_boots.py),
#             then the level-code patches computed against and written into that image
#             -> OpenRAC/build/wrench/executive-edition.iso
#
# OPENRAC, WRENCH and GAME_BANK override the defaults.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
OPENRAC=${OPENRAC:-$(cd "$HERE/../OpenRAC" && pwd)}
WRENCH=${WRENCH:-$(cd "$HERE/../wrench" && pwd)}
GAME_BANK=${GAME_BANK:-$OPENRAC/assets/wrench/rac1-pal}   # unpacked with the edition's wrench fork (stash_textures on carried items)
PAL=$OPENRAC/games/rac1
QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1

echo "== OpenRAC branch: $(git -C "$OPENRAC" branch --show-current)"
cd "$PAL"
echo "== 1/5 build"
bash tools/build_sn.sh
echo "== 2/5 strings (tools/gen_text.py), menu data (tools/gen_menu.py), hero sites (tools/gen_hero.py), R&C2 backpedal (tools/rac2_anim.py), Charge Boots item and icon (tools/gen_items.py, tools/gen_hud.py), mod segment (tools/modseg.py)"
python "$HERE/tools/gen_text.py"
python "$HERE/tools/rac2_anim.py" emit 0x14 -o "$HERE/build/modseg/gen_anim.c"
python "$HERE/tools/gen_menu.py" --openrac "$OPENRAC"
python "$HERE/tools/gen_hero.py" --openrac "$OPENRAC"
if [ "$QUICK" = 1 ]; then
  python "$HERE/tools/gen_items.py" --openrac "$OPENRAC"                        # the Grindboots model
else
  python "$HERE/tools/gen_boots.py" --openrac "$OPENRAC" --wrench "$WRENCH"    # R&C2's Charge Boots as class 2000
  python "$HERE/tools/gen_items.py" --openrac "$OPENRAC" --boots-class 2000
fi
python "$HERE/tools/gen_hud.py" --openrac "$OPENRAC"
python "$HERE/tools/modseg.py" --openrac "$OPENRAC"
echo "== 3/5 bootable executable with the mod segment"
python tools/boot_elf.py --segment "A0000:$(cygpath -w "$HERE/build/modseg/modseg.bin" 2>/dev/null || echo "$HERE/build/modseg/modseg.bin")"

if [ "$QUICK" = 1 ]; then
  echo "== 4/5 level-code patches (tools/overlay_patch.py)"
  python "$HERE/tools/overlay_patch.py" -o "$HERE/build/patches"
  echo "== 5/5 image (executable in new sectors at the end, level patches in place)"
  # An image an emulator has open cannot be overwritten: fall back to a numbered name.
  out=build-sn/executive-edition.iso; n=1
  while [ -e "$out" ] && ! ( : >> "$out" ) 2>/dev/null; do n=$((n+1)); out=build-sn/executive-edition-$n.iso; done
  python tools/iso_patch.py --patches "$HERE/build/patches/patches.tsv" -o "$out"
  echo "-> $PAL/$out"
  exit 0
fi

cp build-sn/SCES_509.16 "$HERE/mod/SCES_509.16"
echo "== 4/5 wrench pack: game bank + wrench overlay + mod/ + build/boots_bank"
mkdir -p "$OPENRAC/build/wrench"
out=$OPENRAC/build/wrench/executive-edition.iso; n=1
while [ -e "$out" ] && ! ( : >> "$out" ) 2>/dev/null; do n=$((n+1)); out=$OPENRAC/build/wrench/executive-edition-$n.iso; done
"$WRENCH/bin/wrenchbuild.exe" pack "$GAME_BANK" "$WRENCH/data/overlay" "$HERE/mod" "$HERE/build/boots_bank" \
  -a rac -o "$out" -h release \
  | grep -vE '^Parsing|^\[ *[0-9]+%\]' || true
echo "== 5/5 level-code patches against the packed image (tools/overlay_patch.py --iso), written in place"
python "$HERE/tools/overlay_patch.py" --iso "$out" -o "$HERE/build/patches-wrench"
python "$HERE/tools/apply_patches.py" "$out" "$HERE/build/patches-wrench/patches.tsv"
echo "-> $out"
