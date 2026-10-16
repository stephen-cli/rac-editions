# Ratchet & Clank: Executive Edition (PAL)

A definitive-style edition of Ratchet & Clank (2002, PS2, SCES_509.16) within
PS2 hardware limits, built on the [OpenRAC](https://github.com/OpenRAC/OpenRAC)
decompilation and shipped as a PS2 disc image made from the player's own disc.
No game data is distributed: this repository holds the mod bank, the
pipeline, the design notes and the game code changes as patches.

## Layout

```
mod/        wrench mod asset bank (gameinfo.txt, build.asset); the rebuilt executable is generated into it
tools/      pipeline scripts: pack.sh (build -> bootable executable -> disc image), export-patches.sh
patches/    the game code changes, as `git format-patch` output of OpenRAC's `executive-edition` branch
docs/       design notes, HQ report
```

## Working copy layout this repository expects

```
<repos>/OpenRAC                 OpenRAC monorepo, branch `executive-edition` (the code changes live there)
<repos>/wrench                  wrench, built into wrench/bin (MinGW, see docs/SETUP.md)
<repos>/rac1-executive-edition  this repository
```

OpenRAC's `assets/wrench/rac1-pal` is the pristine wrench unpack of the disc
and `games/rac1/pal/baserom/` holds the disc image and boot executable; both
are ignored by git there.

## Pipeline

```sh
bash tools/pack.sh --quick   # OpenRAC build + audits -> bootable SCES_509.16 -> in-place disc image (same-size changes)
bash tools/pack.sh           # same, then a wrench full re-layout pack (needed once sizes or assets change)
bash tools/export-patches.sh # refresh patches/ from OpenRAC's executive-edition branch
```

Outputs: `OpenRAC/games/rac1/pal/build-sn/SCES_509.16.iso` (in-place) or
`OpenRAC/build/wrench/executive-edition.iso` (wrench). Boot either in PCSX2 as a
normal disc.

Known limitation: wrench's texture packer regenerates mipmaps incorrectly
(chaoticgd/wrench#286), so wrench-built images show texture errors at
distance and in menus. Use the in-place image for anything that does not
change sizes or assets until that is fixed in our wrench fork.

## Rules

Same as OpenRAC's: your own disc is the evidence; no Sony SDK source, samples
or headers; no leaked material; never commit disc images, executables,
extracted data or build output.
