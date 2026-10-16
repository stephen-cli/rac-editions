# Executive Edition (R&C1): HQ report

Running report from the development account, for carrying back into the HQ.
Newest entries first. Format per the handoff pack: item, new status, what
changed, decisions taken, findings that affect other items.

## 2026-10-17

### 🏭 Build and repack pipeline (OpenRAC) — Done (pipeline proven end to end)

**Sub-items**
- [x] Rebuild a modified executable — OpenRAC `games/rac1/pal`, native Windows build, audit clean (1,157 of 1,158 functions byte-exact, layout and whole-image checks pass).
- [x] Repack it into a bootable ISO — two routes, both working:
  - in-place (same-size changes): `tools/iso_patch.py`, about 10 seconds;
  - wrench (full re-layout, needed for anything that changes sizes or assets): `wrenchbuild pack <game bank> <wrench overlay> <mod bank> -a rac -o out.iso -h release`, about 2.5 minutes.
- [x] Check it runs in an emulator — both ISOs boot in PCSX2 through the normal disc path. The wrench-built ISO was taken into Veldin (Kyzil Plateau) via Load Game: level geometry, textures and HUD render correctly, no sign of the texture glitches the old community notes warned about.
- [x] Choose the target disc version and confirm it is supported — **PAL (SCES_509.16)**, the owner's ripped disc; passes OpenRAC's checksum identification and is in wrench's release table.

**Recommended follow-up before calling the task closed for good:** one full playthrough of a wrench-built ISO (owner, with a controller), since wrench rebuilds the disc layout rather than copying it.

**How the pipeline runs now** (Git Bash, development machine):
```
bash assets/wrench/mods/executive-edition/pack.sh          # build → bootable ELF → wrench ISO
bash assets/wrench/mods/executive-edition/pack.sh --quick  # build → bootable ELF → in-place ISO
```
Outputs: `build/wrench/executive-edition.iso` or `games/rac1/pal/build-sn/SCES_509.16.iso`. Boot with PCSX2 as a normal disc.

**What changed (2026-10-16 → 17)**
- OpenRAC set up: disc verified, toolchain mirrors cloned, disassembly and all 19 level overlays regenerated, executable built with the SN ProDG toolchain.
- The three libgcc functions that only match with the Linux build of Sony's 2.9 compiler are now compiled through WSL (launcher + script kept under `toolchains/wsl-launcher/`), which is what makes the audit clean on Windows.
- Local tools added in `games/rac1/pal/tools/` (uncommitted): `boot_elf.py` (bootable executable from the linked image, which alone has the wrong entry point and no VU0 microcode), `iso_patch.py` (in-place disc image; also applies the OpenRAC editor's `patches.tsv` level edits).
- wrench built from source with a portable MinGW-w64 GCC 16.2 plus CMake and Ninja from pip (no admin): all tools, including the level editor, launcher and save editor.
- The PAL disc unpacked into a wrench asset bank (`assets/wrench/rac1-pal`, 4.8 GB, 3 minutes). Wrench also unpacks the 19 level code overlays as ELF files, which will matter for level-code features.
- A first mod bank exists: `assets/wrench/mods/executive-edition/` (gameinfo.txt, build.asset overriding `boot_elf`, the rebuilt executable, `pack.sh`).

**Decisions taken**
- Target PAL first. The USA-only final boss music fix (x6) stays a separate NTSC item.
- Repack tooling in two layers: OpenRAC-native in-place patching for the executable and for the OpenRAC editor's level edits (the PAL disc addresses everything by absolute sector and the editor sizes its output to fit), and wrench for everything that changes sizes or touches assets.
- The unmodified build must stay byte-identical to retail so edition regressions are visible.
- Edition changes live in a wrench **mod bank** layered over the pristine game bank, never by editing the unpacked game bank.

**Findings affecting other items**
- The build mirrors retail's memory layout exactly. Any feature that adds code will outgrow its function's slot, so the edition needs its own linker layout (a "mod segment") before the first code-adding feature lands. Affects every q*/x* task; plan it as the first engineering task now that the pipeline is done.
- Wrench's maintainer does not accept AI-generated contributions upstream; edition changes to wrench itself must live in a fork.
- Packing with wrench requires mounting its overlay bank (`wrench/data/overlay`), or pvar type parsing fails on `mobylink`.
- A wrench-built ISO is about 37 MB smaller than the original: wrench re-lays out the disc, so it is not byte-comparable to the retail image; validation is by play, not by checksum.
- The Executive Edition should get its own repository (mod bank + scripts + later the C changes as patches or a fork of rac1-decomp). Proposed, not yet created: owner to decide name and hosting.

**External facts re-checked**
- rac1-decomp upstream is ahead of the OpenRAC import (upstream at commit 8a6b2d30 "feat/to-90", OpenRAC synced to e1ee2ee3 of 2026-10-14).
