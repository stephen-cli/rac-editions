# Ratchet & Clank: Executive Edition (PAL)

A definitive-style edition of Ratchet & Clank (2002, PS2) within PS2
hardware limits, built on the [OpenRAC](https://github.com/OpenRAC/OpenRAC)
decompilation. You build it yourself from your own discs: this repository
holds only the edition's code, tools and notes, never game data.

## What's in it

### New features

- **Charge Boots.** Going Commando's Charge Boots, model and all. Double-tap
  R1 and keep holding it to charge: a fast dash with blue jets that smashes
  crates and bonks off enemies and walls. Unlock them in Challenge Mode with
  a new cheat: crouch for a second, back flip, back flip, Comet-Strike,
  Hyper-Strike, Hyper-Strike. Switch them on or off in Goodies → Cheats.
- **Strafing on L2 and R2.** Hold either trigger and Ratchet keeps facing
  where the camera looks, as in Going Commando. He side-flips, strafe-jumps,
  backpedals with his gun held up and stops on the spot. L1 and R1 still
  look and crouch. Switch it on in Options → Controls (off by default, so L2
  and R2 work as in the original game).
- **Three quick-select pages.** With the ring open, R1 and L1 turn between
  three pages of eight slots. In the pause menu's Quick Select tab, Square
  changes the page. Your saves still load in the original game.
- **Quick Select pause.** A new row on the Controls page in Options. Turned on, the game
  stops while the quick-select ring is open, as in the sequels. Off by
  default.
- **Movie volume.** A new orange slider on the Sound page (Options → Sound)
  for the sound of movies and prerendered cutscenes, which the original
  game ties to the Sound effects volume. Saved with your game; until you
  set it, it matches your Sound effects volume.
- **16:9 widescreen.** A new row on the Camera page (Options → Camera).
  Turned on, the game fills a widescreen TV (set your TV, or PCSX2's aspect
  ratio, to 16:9): you see more to the sides, nothing is cut off at the top
  or bottom, and the HUD sits in the corners without being stretched.
  Menus and the vendor keep their shape. Cutscenes keep their original
  framing, with black bars at the sides. Saved with your game; off by
  default. The title screen and main menu stay 4:3.
- **Moving out of the Comet-Strike.** Once the wrench has left Ratchet's
  hand, push the stick to run or press X to jump, as in Going Commando.
- **A richer Metal Detector in Challenge Mode.** Buried bolt stashes pay out
  far more in Challenge Mode, so hunting them down is a real way to save up
  for the RYNO and the Gold Weapons, rather than farming bolts.
- **Commercials.** The three TV ads from the American release, in Goodies.
  They unlock with all ten Gold Weapons, along with the 'Making Of' video.
- **GADGETRON.** Kalebo III's rotating sign reads GADGETRON, as in the
  American release, instead of the PAL release's GALACTRON.

### Fixes to the original game

- **Health display.** With 5 or 8 health orbs (Premium or Ultra Nanotech),
  the lower row's dark backing sat halfway under the top row. Each row now
  sits on its own backing.
- **Gadgets page preview.** Moving the cursor from an empty slot to an item
  no longer flashes the empty slot's item for a frame.
- **Oltanis's lightning.** The lightning strikes and thunder over Oltanis
  stopped for good after the first visit's arrival scene: after dying,
  loading a save or coming back later, the sky stayed quiet. They now run
  whenever you are on the planet.

Some of these are still being tested. [docs/FEATURES.md](docs/FEATURES.md)
has every change in full detail, with its status.

## What you need

**Discs** (your own rips, as `.iso`):

| Disc | Serial | Used for |
|---|---|---|
| Ratchet & Clank, PAL | SCES-50916 | the game itself |
| Ratchet & Clank: Going Commando, NTSC-U | SCUS-97268 | the Charge Boots model and Ratchet's strafe animations, taken from R&C2 |

**Software** (Windows is the tested platform):

| Dependency | Why |
|---|---|
| [Git for Windows](https://git-scm.com/download/win) (Git Bash) | all commands below run in Git Bash |
| [Python](https://www.python.org/) 3.10 or newer | every pipeline step |
| The edition's fork of [OpenRAC](https://github.com/OpenRAC/OpenRAC): [stephen-cli/OpenRAC](https://github.com/stephen-cli/OpenRAC), branch `executive-edition` | builds the game's executable from source |
| The SN ProDG toolchain mirrors: [SN-Systems-ProDG_for_PS2_3.01](https://github.com/AngheloAlf/SN-Systems-ProDG_for_PS2_3.01) and [sce_ps2_sdk_24](https://github.com/AngheloAlf/sce_ps2_sdk_24) | the PS2 compiler, assembler and linker the game was built with |
| The edition's fork of [wrench](https://github.com/chaoticgd/wrench): [stephen-cli/wrench](https://github.com/stephen-cli/wrench), branch `executive-edition` | unpacks the discs, converts the R&C2 boots to R&C1's format, compresses level data |
| [CMake](https://cmake.org/), [Ninja](https://ninja-build.org/) and a C++20 compiler ([MinGW-w64 GCC](https://winlibs.com/)) | to build wrench |
| [PCSX2](https://pcsx2.net/) (optional) | to play the result |

## Build the edition, step by step

Put the three repositories side by side, in a short path (the PS2 toolchain's
`make` fails on long paths):

```
C:\rac\OpenRAC
C:\rac\wrench
C:\rac\rac-editions
```

**1. Get the code.**

```sh
cd /c/rac
git clone -c core.autocrlf=false -b executive-edition https://github.com/stephen-cli/OpenRAC.git OpenRAC
git clone -c core.autocrlf=false -b executive-edition https://github.com/stephen-cli/wrench.git wrench
git clone https://github.com/stephen-cli/rac-editions.git rac-editions
python rac-editions/common/deps.py check   # both at the commits rac-editions/deps.json pins?
```

`core.autocrlf=false` matters: Windows line endings break OpenRAC's linker
script. If `check` names a different commit, check it out:
`git -C OpenRAC checkout <commit>` (or `wrench`).

**2. Give OpenRAC your discs and the toolchain.**

```sh
cp "/path/to/Ratchet & Clank (PAL).iso" OpenRAC/baserom/
mkdir -p OpenRAC/games/rac2/ntsc/baserom
cp "/path/to/Going Commando (NTSC-U).iso" OpenRAC/games/rac2/ntsc/baserom/SCUS_972.68.iso
git clone https://github.com/AngheloAlf/SN-Systems-ProDG_for_PS2_3.01 OpenRAC/toolchains/sn-prodg-3.01
git clone https://github.com/AngheloAlf/sce_ps2_sdk_24 OpenRAC/toolchains/sn-prodg-24
cd OpenRAC
python tools/openrac.py discs              # should name your PAL disc
python tools/openrac.py setup rac1/pal     # places the disc, its executable and the toolchain (a symbolic link: turn on Windows Developer Mode)
cd games/rac1
pip install -r requirements.txt
bash tools/setup_asm.sh                    # disassembles your executable
python tools/overlays.py dump              # extracts each level's code from your disc
cd /c/rac
```

**3. Build wrench and unpack both discs with it.**

```sh
git -C wrench submodule update --init --recursive
cmake -S wrench -B wrench/bin -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build wrench/bin
wrench/bin/wrenchbuild.exe unpack OpenRAC/games/rac1/baserom/SCES_509.16.iso -o OpenRAC/assets/wrench/rac1-pal
wrench/bin/wrenchbuild.exe unpack OpenRAC/games/rac2/ntsc/baserom/SCUS_972.68.iso -o OpenRAC/assets/wrench/rac2-ntsc
```

**4. Build the edition.**

```sh
cd rac-editions
bash editions/rac1-executive/tools/pack.sh
```

This takes about five minutes. It compiles the game from source, checks it
against your disc, builds the edition's code, and writes the disc image:

```
rac-editions/editions/rac1-executive/build/iso/executive-edition.iso
```

If PCSX2 has that image open, the build writes `executive-edition-2.iso`
(and so on) beside it instead.

**5. Play.** Open `executive-edition.iso` in PCSX2 like any disc (you need
your own PS2 BIOS), or burn it to a DVD for a real PS2.

## Two ways to build the image

`pack.sh` (the default) is the stable one. It takes a copy of your disc and
changes it in place: the new executable goes in new sectors at the end of
the image, each level's code is patched where it is, and the Charge Boots
and Kalebo III's sign are spliced into the levels' data where it sits. The
rest of the disc stays byte-for-byte yours.

`pack.sh --wrench` rebuilds the whole disc with wrench instead, writing
`executive-edition-wrench.iso`. It is experimental and has not made a
playable build since 2026-10-17: wrench's texture packer regenerates mipmaps
wrongly (chaoticgd/wrench#286), so textures look wrong at a distance and in
menus.

## Layout

This edition's folder (`editions/rac1-executive/`) in the rac-editions repository;
tools shared with the other editions are in the repository's `common/`.

```
src/        the edition's C, compiled with the game's compiler into a resident "mod segment"
tools/      the pipeline: pack.sh (build -> bootable executable -> level-code patches -> disc image),
            overlay_patch.py (per-level patches), modseg.py (builds src/), splice_boots.py,
            gen_*.py (per-level data), gen_frontend.py (the title screen's programs),
            export-patches.sh (refreshes patches/)
patches/    what the edition adds to OpenRAC (two disc tools), as one diff
mod/        wrench mod bank, used only by the full wrench rebuild
text/       the edition's new strings, in five languages
docs/       FEATURES.md (each feature and how it is patched), HQ_REPORT.md, DEBUGGING.md
```

## Rules

Same as OpenRAC's: your own disc is the evidence; no Sony SDK source,
samples or headers; no leaked material; never commit disc images,
executables, extracted data or build output.
