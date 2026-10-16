# Executive Edition (R&C1): HQ report

Running report from the development account, for carrying back into the HQ.
Newest entries first. Format per the handoff pack: item, new status, what
changed, decisions taken, findings that affect other items.

## 2026-10-17 (q3)

### ⏯️ Quick select pause setting (q3) — Working (owner confirmed the pause, 2026-10-17); polish round built, awaiting re-check

**What changed.** A new row "Quick Select" under Options opens an On/Off page; with it On, the world (hero, enemies, camera, particles, play-time counter) freezes while the quick-select ring is open, as in R&C2 and R&C3; music keeps playing. Off restores the original behaviour. The setting is saved with the game (an unused save flag). Applied to all 19 levels; first owner test pending.

**Decisions taken.**
- The pause uses the engine's own per-group update gate (the debug freeze facility in the frame loop) rather than a new mode, so the ring, HUD, sound and rendering behave exactly as retail while the world stops.
- The menu row reuses the game's own page and widget records (a copy of the Subtitles page with our flag), generated per level because menu data is level-local. Label text reuses the existing "Quick Select" string; a proper "Quick Select Pause" label waits on text-table tooling (shared need with q2).
- Setting stored per save file (save flag 120), default Off.

**Second owner test (2026-10-17): the pause works ("a very useful feature"); asked for the row to be named "Controls" between Load and Sound, the page "Quick Select Pause", a description "Adjust controls settings", and the Help page's Controls preview; also reported that no Options previews loaded.** Done the same day: a text-table writer now adds the edition's strings (five languages) and the image is appended to the disc with the table of contents repointed; the preview table is built from the disc's table of contents (the first copy came from the executable file, where the game fills it at boot, so it was all zeros). The first image of that round stalled at the end of the intro: the game's stream reader reads a sector past a file's end, which at the end of the disc image is a read error; appended files now get a blank-sector margin. Verified booting to the title with a clean log. Third owner check (2026-10-17): previews back and pause fine, but the new labels read "Paradox: This message does not exist" and the preview was Help Log's. Both fixed: strings are now served by a resident replacement of the game's string lookup (a level does not keep the global text table loaded, so adding to that file was not enough), and the preview is Help's slot 1 (Controls). Fourth check pending.

**First owner test (2026-10-17): row works and persists, but no pause, and selecting Quit Game crashed.** Fixed the same day: the pause hook compared the wrong HUD slot field at the executable's table address (the table is level-local), and the Options page keeps two more per-row tables (description text ids, preview-image frames) that had to grow with the new row. Rebuilt; second owner test pending. The row's description still shows Subtitles' text until new strings can be written.

**Findings affecting other items.**
- Only the resident core below 0x15F000 keeps one address; the executable's own game code and data are re-linked per level like level code. Edition code must reach everything above the core through per-level tables (now enforced by the build). This was silently right for x4 and wrong for the first q3 build.
- Text-table writer done (`tools/text_table.py`, `text/strings.tsv`): new strings for any feature are now a one-line addition. Unblocks q2 and x5 text. The hidden Options row "Video" (string 20257, "Adjust video settings") suggests a cut video-settings page: relevant to q4.
- Menu pages, widgets and rows are data records in each level program with pointers between them; adding rows or pages is now a generator step, reusable for q2 (quick select pages) and x5 (strafing cheat toggle).
- New strings need a text-table (all_text WAD) writer; needed by q2, q3's label and any new menu text.
- Mod segment builder now also resolves level-local data addresses from the disc (per-level tables), which the movement items (x3a, x3b) will need for hero state.

## 2026-10-17 (mod segment)

### 🧩 Mod segment (engineering task, unblocks q2, q3, x1, x3a, x3b, x5) — Done (RAM region confirmed; default route)

**What changed.** New code for the edition is now written in C, compiled with the game's own compiler, and carried by the boot executable as a second loadable segment at 0xA0000, below the executable, resident across all levels. Level-only functions are reached through a generated per-level address table. The executable grows past retail's size, so the disc tool now writes it in new sectors at the end of the image and moves the directory entry; PCSX2 boots it from there. The Metal Detector rule exists in both forms (code-cave shim and C in the mod segment) and the C form is wired as an alternative feature.

**Decisions taken.**
- Resident segment in the executable rather than per-level code appended to the level programs: the whole level data range is loaded into RAM as one block at level load, so growing it means relocating about 17 MB per level on disc; the resident route costs 679 appended sectors once.
- Base address 0xA0000, confirmed free on the owner's in-level savestate (Batalia): 0x84000-0x95560 is the PS2 kernel's ELF loader left from boot, 0x95561-0x10007F is all zero (437 KB), the game stages level loads at 0xFE0000. The mod segment is now the default route; x4's C version replaces the cave shim in the shipped image (owner re-test requested).

**Findings.** OpenRAC's headers (moby, hero, pad) are usable from edition C. Exe-level functions keep their addresses in every level; only `func_LNN_*` differ.

## 2026-10-17 (later)

### 💰 Metal Detector gives 10x bolts (x4) — Done (owner confirmed 2026-10-17: a spot that paid 50 pays 500 in challenge mode; circlenut bolts)

**What changed.** First real game change. In challenge mode the detector's hidden bolt spots pay out 10x: the detector spawns 50-bolt circlenut pieces instead of 5-bolt hexnuts, same number of pieces. Normal playthroughs are unchanged. Applied to all 19 level programs; the image boots.

**Decisions taken.**
- Challenge mode is detected by the save's "times completed" counter (PAL 0x15EF20; the "R n" in the Load Game list), which the challenge-mode reset increments. No new save field needed.
- Implemented as a redirected call plus a six-instruction shim in a dead code area present in every level (a linker remnant OpenRAC catalogues), not a rewrite of the function. The spawner only supports bolt values 1-5, 20 and 50, so 10x is realised as 5 -> 50.
- Ships through the in-place disc image only. The wrench route does not carry level-code patches yet (and is deprioritised, see below).

**Owner test (done).** Challenge-mode save with the Metal Detector: circlenut bolts, 500 instead of 50 at a hidden spot. Re-test of the mod-segment build requested. Image: `OpenRAC/games/rac1/pal/build-sn/executive-edition.iso` (also produced by `bash tools/pack.sh --quick` in the edition repository).

### 🏭 Build and repack pipeline — update

- **Wrench texture errors confirmed as chaoticgd/wrench#286** (its texture packer regenerates mipmaps wrongly; also visible in menus). Decision: wrench-built images are not used for releases until the packer is fixed in our fork; the in-place image is the release route for everything that keeps sizes. Wrench remains the tool for asset work and is built and ready.
- **Edition repository created (private, local): `rac1-executive-edition`** next to OpenRAC, with the mod bank, pipeline scripts, the level-code patch tool, docs and the OpenRAC patches. OpenRAC has a local `executive-edition` branch carrying the two disc tools. Owner decision 2026-10-17: no GitHub hosting for now.

**Findings affecting other items.**
- Nearly all gameplay code lives in the level programs, not the executable, and each level has its own copy at its own address; the patch tool resolves per level from OpenRAC's catalogue. Any feature needing more than about 24 bytes of new code needs a "mod segment" (a code area loaded with every level, or an overlay rebuild pipeline). This is the next engineering task and gates q2, q3, x1, x3a, x3b and x5.
- Save layout and game-state addresses: ReRAC's `docs/plan/game_state.md` (US; PAL is +0x100 for the 0x15EDxx block). Useful for q3's setting bit and x2's unlock flag.

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
Outputs: `build/wrench/executive-edition.iso` or `games/rac1/pal/build-sn/executive-edition.iso`. Boot with PCSX2 as a normal disc.

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
