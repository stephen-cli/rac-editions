#!/usr/bin/env bash
# Executive Edition pipeline: OpenRAC build + audits, bootable executable,
# disc image. Run from anywhere in Git Bash.
#
#   bash editions/rac1-executive/tools/pack.sh [--wrench]
#
#   default   the patch route: a copy of the owner's image: the executable in new sectors at the end, the
#             level code patched in place, the Charge Boots class (R&C2's model)
#             spliced into every level's data archive in place (tools/splice_boots.py),
#             and Kalebo III's GADGETRON sign spliced into its archive (tools/splice_sign.py)
#             -> editions/rac1-executive/build/iso/executive-edition.iso
#   --wrench  experimental: wrench full re-layout (the edition's wrench fork): game bank +
#             wrench overlay + mod/ + the Charge Boots bank (R&C2's model, tools/gen_boots.py)
#             + the sign bank (tools/gen_sign.py),
#             then the level-code patches computed against and written into that image
#             -> editions/rac1-executive/build/iso/executive-edition-wrench.iso
#             Not a stable build: wrench's texture packer regenerates mipmaps wrongly
#             (chaoticgd/wrench#286), so textures look wrong at a distance and in menus.
#
# OPENRAC, WRENCH and GAME_BANK override the defaults.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
COMMON=$(cd "$HERE/../../common" && pwd)                      # tools shared by the editions
OPENRAC=${OPENRAC:-$(cd "$HERE/../../../OpenRAC" && pwd)}
WRENCH=${WRENCH:-$(cd "$HERE/../../../wrench" && pwd)}
GAME_BANK=${GAME_BANK:-$OPENRAC/assets/wrench/rac1-pal}   # unpacked with the edition's wrench fork (stash_textures on carried items)
PAL=$OPENRAC/games/rac1
ROUTE=patch
case "${1:-}" in
  "") ;;
  --wrench) ROUTE=wrench ;;
  *) echo "usage: pack.sh [--wrench]" >&2; exit 2 ;;
esac

echo "== OpenRAC branch: $(git -C "$OPENRAC" branch --show-current)"
# OpenRAC and wrench against the commits deps.json pins, and the carried
# OpenRAC modules (common/openrac_editor) against the pinned commit: reported, never fatal.
python "$COMMON/deps.py" check --openrac "$OPENRAC" --wrench "$WRENCH" || echo "!! common/openrac_editor differs from the pinned OpenRAC: python common/deps.py vendor"
cd "$PAL"
# Native Windows (Git Bash): OpenRAC's Makefile.sn needs a GNU make newer than the
# toolchain's 3.77 ($(if), since the 2026-10-19 master merge), and two objects are
# assembled by Linux assemblers (tools/gas210.py, tools/dtls29_as.py), run through
# WSL with common/wsl_linux_run.sh. Set only when unset and present.
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    TCS=$OPENRAC/toolchains
    if [ -z "${MAKE_SN:-}" ] && [ -x "$TCS/mingw64-dl/mingw64/bin/mingw32-make.exe" ]; then
      export MAKE_SN="$TCS/mingw64-dl/mingw64/bin/mingw32-make.exe"
    fi
    if command -v wsl >/dev/null 2>&1; then
      W=$(MSYS_NO_PATHCONV=1 wsl -d Ubuntu -e wslpath -a "$(cygpath -w "$TCS")" | tr -d '\r')
      R="wsl -d Ubuntu -e sh $(MSYS_NO_PATHCONV=1 wsl -d Ubuntu -e wslpath -a "$(cygpath -w "$COMMON/wsl_linux_run.sh")" | tr -d '\r')"
      [ -n "${EE_GAS210:-}" ] || export EE_GAS210="$R $W/sn-prodg-24/local/sce/ee/gcc/bin/ee-as"
      [ -n "${EE_DTLS29_AS:-}" ] || [ ! -f "$TCS/ee-as-dtls13010/as.bin" ] ||
        export EE_DTLS29_AS="$R $W/ee-as-dtls13010/lib/ld-2.3.6.so --library-path $W/ee-as-dtls13010/lib $W/ee-as-dtls13010/as.bin"
    fi ;;
esac
echo "== 1/5 build"
bash tools/build_sn.sh
echo "== 2/5 strings (tools/gen_text.py), menu data (tools/gen_menu.py), hero sites (tools/gen_hero.py), R&C2 backpedal (common/rac2_anim.py), Charge Boots item and icon (tools/gen_items.py, tools/gen_hud.py), mod segment (tools/modseg.py)"
python "$HERE/tools/gen_text.py"
python "$COMMON/rac2_anim.py" --openrac "$OPENRAC" emit 0x14 -o "$HERE/build/modseg/gen_anim.c"
python "$COMMON/rac2_anim.py" --openrac "$OPENRAC" emit-arm 0x14 -o "$HERE/build/modseg/gen_anim_arm.c"   # R&C2's gun arms over the backpedal (src/gun_arm.c)
python "$HERE/tools/gen_menu.py" --openrac "$OPENRAC"
python "$HERE/tools/gen_hero.py" --openrac "$OPENRAC"
python "$HERE/tools/gen_boots.py" --openrac "$OPENRAC" --wrench "$WRENCH"    # R&C2's Charge Boots as class 2000
python "$HERE/tools/gen_sign.py" --openrac "$OPENRAC" --wrench "$WRENCH"     # Kalebo III's sign reads GADGETRON (class 1891 rebuilt from the PAL sign)
python "$HERE/tools/gen_items.py" --openrac "$OPENRAC" --boots-class 2000
python "$HERE/tools/gen_charge.py" --openrac "$OPENRAC"                       # the Charge Boots dash's sites (src/charge_dash.c)
python "$HERE/tools/gen_qs.py" --openrac "$OPENRAC"                           # the quick-select pages' sites (src/quick_select_pages.c)
python "$COMMON/rac2_anim.py" --openrac "$OPENRAC" emit 0x8B --name ee_charge_seq -o "$HERE/build/modseg/gen_anim_charge.c"   # R&C2's dash animation
python "$HERE/tools/gen_hud.py" --openrac "$OPENRAC"
python "$HERE/tools/gen_frontend.py" --openrac "$OPENRAC" emit                # the frontends' flight in 16:9 (src/widescreen.c FE_BOOT/FE_BIN)
python "$HERE/tools/modseg.py" --openrac "$OPENRAC"
echo "== 3/5 bootable executable with the mod segment"
python tools/boot_elf.py --segment "A0000:$(cygpath -w "$HERE/build/modseg/modseg.bin" 2>/dev/null || echo "$HERE/build/modseg/modseg.bin")"
python "$HERE/tools/gen_frontend.py" --openrac "$OPENRAC" patch-elf build-sn/SCES_509.16   # the boot frontend's flight hooks (frontbin's: overlay_patch.py)

if [ "$ROUTE" = patch ]; then
  echo "== 4/5 level-code patches (tools/overlay_patch.py)"
  python "$HERE/tools/overlay_patch.py" -o "$HERE/build/patches"
  echo "== 5/5 image (executable in new sectors at the end, level patches in place)"
  # An image an emulator has open cannot be overwritten: fall back to a numbered name.
  mkdir -p "$HERE/build/iso"
  out=$HERE/build/iso/executive-edition.iso; n=1
  while [ -e "$out" ] && ! ( : >> "$out" ) 2>/dev/null; do n=$((n+1)); out=$HERE/build/iso/executive-edition-$n.iso; done
  python tools/iso_patch.py --patches "$HERE/build/patches/patches.tsv" -o "$out"
  echo "== 5b/5 the Charge Boots class spliced into every level's data (tools/splice_boots.py)"
  python "$HERE/tools/splice_boots.py" "$out" --openrac "$OPENRAC"
  echo "== 5c/5 Kalebo III's GADGETRON sign spliced into its data (tools/splice_sign.py)"
  python "$HERE/tools/splice_sign.py" "$out" --openrac "$OPENRAC"
  echo "-> $out"
  exit 0
fi

cp build-sn/SCES_509.16 "$HERE/mod/SCES_509.16"
echo "== 4/5 wrench pack: game bank + wrench overlay + mod/ + build/boots_bank + build/sign_bank"
mkdir -p "$HERE/build/iso"
out=$HERE/build/iso/executive-edition-wrench.iso; n=1
while [ -e "$out" ] && ! ( : >> "$out" ) 2>/dev/null; do n=$((n+1)); out=$HERE/build/iso/executive-edition-wrench-$n.iso; done
"$WRENCH/bin/wrenchbuild.exe" pack "$GAME_BANK" "$WRENCH/data/overlay" "$HERE/mod" "$HERE/build/boots_bank" "$HERE/build/sign_bank" \
  -a rac -o "$out" -h release \
  | grep -vE '^Parsing|^\[ *[0-9]+%\]' || true
echo "== 5/5 level-code patches against the packed image (tools/overlay_patch.py --iso), written in place"
python "$HERE/tools/overlay_patch.py" --iso "$out" -o "$HERE/build/patches-wrench"
python "$COMMON/apply_patches.py" "$out" "$HERE/build/patches-wrench/patches.tsv"
echo "-> $out"
