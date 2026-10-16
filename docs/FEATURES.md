# Features and how each is implemented

Addresses are PAL (SCES_509.16) unless marked. Level-code functions are
named as in OpenRAC's catalogue (`func_LNN_XXXXXXXX`: level NN's copy at
address X; the shared code sits at a different address in every level and
`tools/overlay_patch.py` resolves each per level).

## How code changes reach the game

The boot executable's own gameplay text (from 0x15F000) is replaced by every
level program, so gameplay code is patched in the level programs, which are
stored raw on the disc inside each level's data range. For now a feature is a
set of word patches plus, when more than a word is needed, a shim placed in a
**code cave**: one of the linker remnants OpenRAC catalogues
(`config/overlays/linker_remnants.txt`), dead words retail's linker left of
stripped functions. `func_L00_001F3DF0` is a 44-byte remnant present in all
19 levels and is the first cave in use (24 of its 44 bytes taken).

Resident game state the shims read is in the executable's core and never
moves: the save's global chunks live at 0x13D388.. and 0x15ED84.. (ReRAC's
`docs/plan/game_state.md`, US addresses; this block is +0x100 on PAL).

Limits of word patches: a patched function cannot grow and the remnants
hold only a few words. Code of any size goes in the **mod segment**
(below). The wrench repack route does not carry these patches yet (they
would have to be applied to the unpacked overlay ELFs in the mod bank); use
the in-place image.

## The mod segment (resident code in C)

`tools/modseg.py` compiles `src/*.c` with the game's own compiler (SN GCC
2.95.3, `-O2 -G0`), links it at **0x000A0000** against the game's symbols,
and writes a flat image that OpenRAC's `tools/boot_elf.py --segment` carries
in the boot executable as a second loadable segment. The console's ELF
loader places it in RAM at boot; level programs replace memory from 0x15F000
up and never touch it. The executable grows past its retail size, so
`tools/iso_patch.py` writes it in new sectors at the end of the image and
moves the directory record (verified booting in PCSX2 from sector 2058000).

- Only the resident core, below 0x15F000 (core text, data, bss, lit), is at
  one address: `D_XXXXXXXX` there can be used directly (bolts, current
  level, completes, the save's flags...). Everything above it, the
  executable's own game text and data included, is re-linked into every
  level program at level-specific addresses (the catalogue's "exe" kind
  lists 19 places too: `func_001FFCB0` is at 0x236298 in Veldin, 0x24B8A8
  in Novalis). So any function above the core is reached through
  `DECLARE_LVL(name)` / `LVL(name, type)`, a 19-entry table the build emits
  from the catalogue, indexed by the current level (`D_0015EE84`); and
  level-local data through `LVL_DATA(name, type)`, a table the build reads
  from the disc (`tools/modseg.py` `LEVEL_DATA`: a function and the offsets
  of a lui/addiu pair that addresses it). The build refuses a direct
  reference above 0x15F000.
- Exported functions (`modseg.sym`) are hook targets for
  `tools/overlay_patch.py`: redirect a call site in level code with `jal`.
- Region, confirmed 2026-10-17 on a PCSX2 savestate taken in Batalia
  (tools/ramscan.py): 0x84000-0x95560 holds the PS2 kernel's ELF loader
  (left over from boot, never touched by the game), 0x95561-0x10007F is all
  zero, and the disassembly references nothing in 0x80000-0xFFFFF. So the
  segment has 0xA0000-0xFFFFF (384 KB). The game's level-load staging buffer
  is at 0xFE0000 (whole level data range), the frontend's near 0x1FF7FF0;
  both in the upper half of RAM.
- The mod segment is the default route; the cave shim of x4 remains as
  feature `metal_detector_10x` for reference.

## x4: Metal Detector gives 10x bolts in challenge mode — implemented, awaiting owner test

**Mechanism in retail.** Hidden bolt spots are mobys of class 605 (wrench
underlay: "Metal Detector", the placed spot; class 585 is the held gadget's
own moby). The held detector's update function `func_L00_002D4398`
(class 585) tracks the nearest spot (`hero + 0x2040`), and when within 1.0
drains the spot's bolt count (`spot->pvars + 0x18`) in steps of 5, each step
spawning one bolt through `func_L00_002A86D8(spot, pos, dir, 0x13, value=5, 0)`.
That spawner maps the value to a bolt class: 1-4 plain bolt (class 13),
5 hexnut (14), 20 starnut (15), 50 circlenut (16); other values spawn
nothing. The PS3 multiplayer mod implements the same feature by multiplying
the spot's count (10x as many spawns).

**Challenge mode.** `D_0015EF20` (s32, save global chunk 2, "times
completed") is incremented by the challenge-mode reset `func_00227DB0`
(US 0x226B08) and is 0 on a first playthrough, so `completes > 0` means a
challenge-mode run. It is what the Load Game list shows as "R n".

**Patch.** The `jal func_L00_002A86D8` at `func_L00_002D4398 + 0x638` is
redirected to the shim in the cave:

```
lui   $at, %hi(0x15EF20)
lw    $at, %lo(0x15EF20)($at)   # completes
addiu $t2, $zero, 50
movn  $a4, $t2, $at             # value = 50 if completes != 0 (else the caller's 5)
j     func_L00_002A86D8         # tail call, $ra still points into the detector
nop
```

Same number of spawns, each worth 50 instead of 5: exactly 10x, with the
circlenut model. 133 words over 38 sectors across the 19 levels. The shim's
encoding was checked against the SN assembler's output.

**Test plan (owner).** Challenge-mode save with the Metal Detector: find a
hidden spot, observe circlenut bolts and 10x the bolt gain. Then a normal
save: unchanged hexnut bolts worth 5. Levels to cover: any two.

## q3: Quick select pause setting — working (owner, 2026-10-17); labels, row position and preview fixed, awaiting re-check

**Setting.** Saved global flag 120 (`0x13D508`; the save's chunk 5 holds 128
flag bytes from `0x13D490`, indices 118-126 unused by retail). Off by
default, kept per save file.

**Pausing.** The mode-0 frame (`func_L00_00299250`) runs each world-update
group only if its bit is set in the gate word at `*(update_flags + 8)`:
1 hero, 2 mobys and tick counter, 4 particles, 8 camera (0x10 is the debug
single-step mode, 0x200 on the pad struct steps). HUD update, help update,
sound and rendering run regardless. The frame's `HudUpdate` call
(`func_L00_0023B610`, at `func_L00_00299250 + 0x838`) is redirected to
`ee_hud_update_hook` (src/quick_select_pause.c), which calls HudUpdate and
then, with the setting on and HUD slot 3's update callback equal to
`QuickSelectUpdate` (`func_L00_00238530`), clears the four bits; when the
ring closes it restores them. Music keeps playing, play time stops, the
ring animates and takes input as before. The gate struct is level-local
(`0x16C158` in level 0, elsewhere in others): `tools/modseg.py`'s
`LEVEL_DATA` table reads its address from each level's copy of the frame
function (the lui/addiu pair at +0x4/+0x69C).

**Menu.** The Options list gains a row "Quick Select" before Quit Game,
opening a copy of the Subtitles page (kind 0x1B) whose single toggle entry
is `{label, &flag, on 20314, off 20315}` with our flag. Pages, widgets and
rows are level-local records with pointers between them, so
`tools/gen_menu.py` finds them in each level's data record (the Options rows
by their label ids 0x4F1E..0x4F26, the list widget by its item pointer, the
Subtitles page from the Subtitles row) and generates one copy per level
into the mod segment; the patch points each level's list widget item
pointer (`widget + 0x34`) at its copy. Two more tables are indexed by the
row cursor and had to grow with the rows (the first build crashed on Quit
Game, which read past them): the Options page's description widget
(`+0x34` -> description string ids 20273..20281, one per row, sentinel 0)
and its image widget (`+0x30` -> a table of `{first sector, sectors}` per
row). The image widget (`func_00220DF0`) streams each preview straight
from the disc with `start_audio_stream_read(buffer, sector, sectors)` and,
with its flag 0x20, WAD-decompresses it in place; the core table at
0x138140 is filled at boot from the table of contents' `options_ss` group
(the one before it, 0x138108, from `help_ss`, is the Help page's), so the
executable file holds zeros there, which is why a copy taken from the file
showed no previews at all. The generator now takes the pairs from the
disc's table of contents, with the Help page's "Controls" preview
(`help_ss[0]`) at our row, emits per-level description arrays, and patches
both widget pointers. The row sits after Load (index 3).

**Text.** `tools/text_table.py` rebuilds the game's text table with the
edition's strings from `text/strings.tsv` (ids 0x5402.., five languages,
English fallback) and the pipeline appends the larger file to the image
with the table-of-contents entry (byte 0x1528 of the table of contents at
sector 1500) pointed at it. q3 uses "Controls" (0x5402) for the row,
"Control Options" (0x5405) for the page title, "Quick Select Pause"
(0x5403) for the toggle and "Adjust controls settings" (0x5404) for the
description.

**HUD slots.** `HudShow(slot | flags, a, init, update, draw, b, c)` queues
a slot record (13 records of 0x90 at a level-local table: 0x17DD50 in
Veldin); `apply_pending` copies init/update/draw to record +0x10/+0x14/+0x18
and `HudUpdate` calls +0x14. The first build compared +0x8 at the
executable's table address and never saw the ring. Record layouts (from ReRAC's
menus.md, checked against the PAL data): page 0x88 = `s32 seq[14]`, parent,
kind, widget pointers from +0x40; widget 0x40 with update/draw/enter/leave
first and type data at +0x30 (toggle list: +0x34 entries, +0x38 cursor);
title widget 0x58 with style at +0x30 and label id at +0x34; list item 12
bytes `{s16 label, s16 action, u32 arg, s16 sublabel, s16 hl_timer}`, action
3 = go to page `arg`, sentinel label 0; toggle entry 0x14 bytes.

**Text table format** (`all_text`, table-of-contents group at sector
18200, 630,784 bytes, uncompressed): eight u32 offsets to sub-tables
(0 English, 1 unused, 2 French, 3 German, 4 Spanish, 5 Italian, 6 shared,
7 unused); each sub-table `{u32 count, u32 size, entry[count]}` then its
strings, an entry being `{u32 offset from the sub-table start, u32 id,
-1, 0}`, entries not sorted by id, ids up to 0x5401 in retail. The hidden
Options rows are 20257 "Video" (description 20276 "Adjust video settings",
of interest to q4) and 20261 "Vibration".

**Open points.** The page reuses the Subtitles page's kind 0x1B. The
French, German, Spanish and Italian strings are first drafts.

**Test plan (owner).** In a level: Start, Options, "Quick Select", set On;
open the ring (hold triangle): enemies, camera and particles frozen, ring
animates, selection works; release: play resumes, timer did not advance.
Set Off: original behaviour. Save and reload: setting persists.
