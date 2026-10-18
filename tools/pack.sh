#!/usr/bin/env bash
# Executive Edition pipeline: OpenRAC build + audits, bootable executable,
# disc image. Run from anywhere in Git Bash.
#
#   bash tools/pack.sh [--quick]
#
#   --quick   copy of the owner's image with the level code patched in place, and
#             the Charge Boots class (R&C2's model) spliced into every level's data
#             archive, appended at the end of the image (tools/splice_boots.py)
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
python "$HERE/tools/rac2_anim.py" emit-arm 0x14 -o "$HERE/build/modseg/gen_anim_arm.c"   # R&C2's gun arms over the backpedal (src/gun_arm.c)
python "$HERE/tools/gen_menu.py" --openrac "$OPENRAC"
python "$HERE/tools/gen_hero.py" --openrac "$OPENRAC"
python "$HERE/tools/gen_boots.py" --openrac "$OPENRAC" --wrench "$WRENCH"    # R&C2's Charge Boots as class 2000
python "$HERE/tools/gen_items.py" --openrac "$OPENRAC" --boots-class 2000
python "$HERE/tools/gen_charge.py" --openrac "$OPENRAC"                       # the Charge Boots dash's sites (src/charge_dash.c)
python "$HERE/tools/gen_qs.py" --openrac "$OPENRAC"                           # the quick-select pages' sites (src/quick_select_pages.c)
python "$HERE/tools/rac2_anim.py" emit 0x8B --name ee_charge_seq -o "$HERE/build/modseg/gen_anim_charge.c"   # R&C2's dash animation
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
  echo "== 5b/5 the Charge Boots class spliced into every level's data (tools/splice_boots.py)"
  python "$HERE/tools/splice_boots.py" "$out" --openrac "$OPENRAC"
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
