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
  `DECLARE_LVL(name)` / `LVL(level, name, type)`, a 19-entry table the build
  emits from the catalogue, indexed by the **running level**; and
  level-local data through `LVL_DATA(level, name, type)`, a table the build
  reads from the disc (`tools/modseg.py` `LEVEL_DATA`: a function and the
  offsets of a lui/addiu pair that addresses it). The build refuses a direct
  reference above 0x15F000.
- **The running level is not the game's current-level variable**
  (`D_0015EE84`). Loading a save from the pause menu writes the save's
  globals, that variable included, while the old level program still runs
  its last frames; hooks that indexed the tables with it then called the new
  level's addresses inside the old program (R5900 "jump to unaligned
  address" on every second load, owner 2026-10-17; the first load, from the
  frontend, has no hooks running). Since then a hook learns its level from
  its own call site: `src/hooks.s` trampolines (the `jal` targets) pass the
  return address to the C hook, and `ee_level_of_call` (src/level.c)
  matches it against the hooked function's 19 addresses plus the site's
  offset (`EE_SITE_*` in src/modseg.h, which `tools/overlay_patch.py` reads
  too). msg_string's entry patch, a plain `j`, gets `addiu $at, $zero,
  LEVEL` in its delay slot instead, read by `src/text_hook.s`.
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

## x4: Metal Detector gives 20x bolts in challenge mode — 10x owner-confirmed 2026-10-17; raised to 20x 2026-10-18, awaiting owner test

**Goal (owner, 2026-10-18).** Make a Challenge Mode run that collects every
hidden spot a better way to the 400,000 bolts of the RYNO (150,000) and the
ten Gold Weapons (250,000 together) than the Blackwater City crate farm
(forum figures 58,000-100,000 bolts an hour, so 4-7 hours for 400,000).

**The spots.** 358 spots of class 605 over 18 levels (none in the Veldin
tutorial), 17,460 bolts in retail: 347 hold 50, 11 hold 10 (ten on
Kalebo III, one on Veldin 2). Counted from the unpacked gameplay instances
(`OpenRAC/assets/wrench/rac1-pal`). Each spot's pvars `+0x10`/`+0x14` are a
path and a volume (-1 for none) that the hero must be inside for the spot to
register (`func_L01_002F4290`), not a mission lock. At 10x the run pays
174,600; at 20x 349,200, the rest of the 400,000 coming from the run's
enemies and crates (not measured).

**Not farmable.** Each 5-bolt step taken from a spot is counted in a nibble
of the table at `0x14C010` (16 bytes per level, one nibble per spot in
placement order; `func_L00_002D42D8` increments it), which is a per-level
save block (id 3008, 0x10 bytes, saved for all 20 level slots). The spot's
init subtracts it from the count, so a drained spot stays drained for the
run. The challenge-mode reset `func_00227DB0` keeps 0x50 bytes at
`0x14BFC0`, which ends exactly at `0x14C010`, and restores everything else
from the new-game save, so the spots refill once per challenge-mode run.

**Mechanism in retail.** Hidden bolt spots are mobys of class 605 (wrench
underlay: "Metal Detector", the placed spot; class 585 is the held gadget's
own moby). The held detector's update function `func_L00_002D4398`
(class 585) tracks the nearest spot (`hero + 0x2040`), and when within 1.0
drains the spot's bolt count (`spot->pvars + 0x18`) in steps of 5, the whole
spot in one frame, each step
spawning one bolt through `func_L00_002A86D8(spot, pos, dir, 0x13, value=5, 0)`.
That spawner maps the value to a bolt class: 5 hexnut (14), 20 starnut
(15), 50 circlenut (16), any other value a plain bolt (13). Flags 2|0x10
give each bolt a random throw of +-30 degrees; with no free moby it adds the
value to `gBolts` instead, so no bolt is lost to the moby limit. The PS3
multiplayer mod implements the same feature by multiplying
the spot's count (10x as many spawns), which would overflow the 4-bit
per-spot counter above.

**20x (mod segment, `src/metal_detector.c`, the default
`metal_detector_10x_modseg`).** In a challenge-mode run each step spawns two
circlenuts instead of one hexnut: 100 per step, exactly 20x. The two get
independent random throws from the same arguments. The cave shim below
(feature `metal_detector_10x`) stays at 10x, for reference.

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

**Test plan (owner).** Challenge-mode save with the Metal Detector: find an
undrained hidden spot, observe twice as many circlenut bolts as before and
20x the bolt gain (a 50-bolt spot pays 1,000). Then a normal save:
unchanged hexnut bolts worth 5. Levels to cover: any two.

## q3: Quick select pause setting — working (owner, 2026-10-17); labels, row position and preview fixed, awaiting re-check

**Setting.** `EE_SETTING_QS_PAUSE` in src/modseg.h = `0x15EEBB`, `gCheats[11]`:
the last byte of the twelve cheat flags at `D_0015EEB0`, saved whole as the
save's chunk 7 (the save block table at `0x1A05C0` in level 0 lists every
saved range as `{address, size, chunk id}`). Retail addresses cheats 0-7
only, by constant or through the cheat list ids {1, 3, 0, 7, 4, 6, 2};
bytes 8-11 are touched by nothing but the whole-block copies, in all 19
levels, so the byte is 0 in every retail save. Off by default, kept per save
file. The first choice, `gSpecialItems[120]` (`0x13D508`), was wrong: that
array is indexed by the item ids of level pickups and the owner's oldest
saves had the byte set; builds before 2026-10-17 wrote the setting there,
so a save where it was switched On with those builds carries item 120 set.

**Pausing.** The mode-0 frame (`func_L00_00299250`) runs each world-update
group only if its bit is set in the gate word at `*(update_flags + 8)`:
1 hero, 2 mobys and tick counter, 4 particles, 8 camera (0x10 is the debug
single-step mode, 0x200 on the pad struct steps). HUD update, help update,
sound and rendering run regardless. The frame's `HudUpdate` call
(`func_L00_0023B610`, at `func_L00_00299250 + 0x838`) is redirected to
`ee_hud_update_hook` (src/quick_select_pause.c), which calls HudUpdate and
then, with the setting on, HUD slot 3's update callback equal to
`QuickSelectUpdate` (`func_L00_00238530`) and Triangle held (pad
`D_0013CA40 + 0x1C0`, bit 0x10 in the game's button layout) for more than
4 consecutive frames, clears the four bits; the frame Triangle is released
it restores them. Music keeps playing, play time stops, the ring animates
and takes input as before. Releasing ends the pause at once rather than
after the ring's 8-frame fade-out (owner's second-round feedback,
2026-10-17).

**Timings, from Going Commando (owner's request, 2026-10-17).** Read from
SCUS_972.68's program and two owner savestates (ring open; just after a hot
swap). R&C2 does not open its quick select on the press: a trigger state
machine in its level programs (`func_L00_002FC058`; state at gp-0x5C40,
countdown at gp+0x3278) counts frames while Triangle is held and opens the
ring, an overlay game state in which the world does not run, once the hold
reaches **4 frames** (gp-0x5C3C = 4, static); a shorter tap never shows it.
Its hero (level code at 0x2C367C in Oozla's program) does the hot swap: a
press loads a **20-frame** tap timer (hero + 0x24F8), a release inside it is
a tap, the first tap arms a **20-frame** window (hero + 0x24FA) and a second
tap inside it swaps to the previous weapon (hero + 0x2538 = 1) and cancels
the trigger. R&C1's hero already has the same two 20-frame timers (hero +
0x22D4 and + 0x22D6, `func_1FEED0(0x14)` for the PAL frame conversion), so
only the hold threshold was adopted: `GRACE_FRAMES = 4`. The ring itself
still appears on the press as in retail R&C1; delaying it to the 4th frame
as R&C2 does would be a patch to the hero's opener and was not asked for.

**The missed release.** The hero reads Triangle's release as the latched
edge at pad + 0x1B8, rebuilt every frame by the pad update as ~held & the
previous frame's raw buttons (+ 0x1BC, copied from + 0x1B0 by
`func_00217F68` at the start of each frame; `src/game/pad.c` in OpenRAC).
A hero frozen on the release frame never sees it, so for holds of 5 to 20
frames the double tap would die. On that frame the hook now puts Triangle
back into + 0x1B0, and the next frame's pad update derives the edge again:
the hero sees the release one frame late with its tap timer frozen where
the pause left it. Holds longer than `TAP_FRAMES` (20) are not replayed, so a
long hold never arms the window, as in retail. The gate struct is level-local
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
disc's table of contents, with the Help page's "Controls" preview at our
row (`help_ss[1]`: the Help rows are Help Log, Controls, Moves, Weapons,
Gadgets), emits per-level description arrays, and patches both widget
pointers. The row sits after Load (index 3).

**Text.** The edition's strings (`text/strings.tsv`, ids 0x5402.., five
languages with English fallback) are served from the resident segment.
`tools/gen_text.py` emits them as tables; every level's copy of the game's
`msg_string` (`func_001FE540`) has its first two words replaced by a jump
to `ee_msg_string_hook` (`src/text_hook.s`), which answers ids in the
edition's range from the tables (`src/text.c`) and for any other id
re-executes the two replaced prologue instructions and continues at the
level's msg_string + 8, so the game's own lookup and data are untouched.
This is needed because a level does not keep the global text table: in a
gameplay savestate the text system is empty (count 0), the pause menu
loads text on demand, and an enlarged global table (`tools/text_table.py`,
kept for the frontend) left the new ids unresolved in level menus
("Paradox: This message does not exist"). A first hook that re-implemented
msg_string in C read the entries base at the executable's address of that
variable (0x15F780); the level copies reach it gp-relative with a
per-level offset, so every game string came back null: the lesson is that
even gp-relative data is not at one address across levels, and the safe
pattern for a hook is to re-enter the original function. q3 uses
"Controls" (0x5402) for the row, "Controls Options" (0x5405) for the page
title, "Quick Select pause" (0x5403) for the toggle row and "Adjust
controls settings" (0x5404) for the description. Accented letters are flattened to ASCII until the font's code
page is mapped.

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

## Goodies: Commercials — implemented (2026-10-17), awaiting owner test

**What the NTSC release has.** Under Goodies, after "'Making Of' Video", a
row "Commercials" (string 0x4FCC, description 0x4FD8 "Watch the hilarious
television commercials...") opening a page with three rows: "Decoy Glove
ad", "Devastator ad", "Morpharay ad" (0x508D-0x508F, button hint 0x5091
"Play Ad"), each a movie action (list action 10) with movie numbers 1, 2
and 0; 'Making Of' is the same action with number 3. The row is locked
(action 2, description 0x4FDB "You have to buy all Gold Weapons...") until
all ten Gold Weapons are bought, exactly like 'Making Of'.

**What the PAL disc has.** Everything: the row in the Goodies item table
(`0x1B6C80` in level 0, row 8), the page (`0x1B7C70`, kind 0x2C, list,
title, preview and button-hint widgets), the strings in every level's text
block (checked in a Veldin savestate), the four preview images (table of
contents group `commercials`, 0xE98) and the movies: the table of contents
`mpeg` group holds the Goodies movies at slots 70-74 with their PAL encodes
at 75-79, chosen by the region flag like the intro movies, so no data had to
come from the NTSC disc. The NTSC image was used only to compare.

**Why PAL hides it.** The Goodies unlock routine (`func_00228160`, an
executable function copied into every level; level 0's copy at `0x285F68`)
sets rows 5-8's actions and description ids from the skill-point and
Gold-Weapon counts in both releases; PAL's copy alone ends with
`if (D_0015EF90) rows[8].label = 0` (`sh $zero, 0x60($v0)` at +0xE8).
`D_0015EF90` is the region flag the boot code derives from SYSTEM.CNF
(`VMODE` not starting with N), 1 on every PAL console, so the list ends at
'Making Of'.

**The patch.** `tools/overlay_patch.py` feature `commercials`: that one
store becomes a no-op in all 19 levels (19 words). Nothing else changes; the
unlock, description switch and movie playback are retail code.

**Owner test to run.** With all ten Gold Weapons on the save: Goodies shows
"Commercials" after "'Making Of' Video"; the page lists the three ads with
previews; each plays (PAL encodes, 50 Hz) and returns to the game. On a save
without the Gold Weapons the row shows locked with the Gold Weapons message.

## x3b: Free movement after the wrench throw — run cancel and jump cancel working (owner, 2026-10-18)

**What R&C1 does.** The Comet-Strike (crouch + Square; the game's Moves
help names it so) is hero state 0x15 (the NTSC decomp calls it
`HERO_STATE_CROUCH_SWING`). Its movement block, case 0x15 of the per-state
movement switch (`func_L00_002180F8`, level 0 at `0x21B588`; NTSC
`00233de0.c`), sets the target speed to 0 every frame and eases the ground
speed to it with `speed_step(37, 28)`; its state update (NTSC `00242930.c`,
case 21) leaves the state only past the attack record's end frame, to IDLE.
The wrench is released at animation frame 33 (`FUN_L00_00211870(33.0f)`).
Ratchet stands through the whole throw and catch.

**What R&C2 does.** Read from SCUS_972.68's Oozla program: the crouch throw
is state 0x15 there too, and the Circle throw with the wrench equipped is
state 0x83; both share one movement block (`0x2BAE08`) that is R&C1's to the
constant, including `speed_step(37, 28)` and the release at 33, and one
entry (`0x2C0A48`, control mode 6). The difference is the state update
(`0x2C45B0`): from animation frame 34, with the left stick past 0.9, it
leaves the throw for RUN (`set_state(2)` with a 13-frame blend), and at the
record's end frame it picks RUN over IDLE when the stick is past 0.8. So
the throw is cancelled into a run as soon as the wrench has left the hand;
the wrench returns to wherever he is.

**The port.** `src/comet_strike.c`: the throw block's `speed_step` call
(level 0: `0x21B900`) goes to `ee_comet_speed_step`, which calls the game's
`speed_step` and then, in state 0x15 with no lock-on target (hero + 0xA9C),
animation frame > 34 and stick > 0.9, calls `hero_set_state(RUN, 1)` (level
0: `func_L00_00222B80`), whose entry picks the run animation and blend. The
hero's movement switch and hero_set_state are built differently in groups
of levels (the catalogue's level-0 names cover six), so `tools/gen_hero.py`
finds the call site, `speed_step` and `hero_set_state` in each level's own
program: the state table with 0x83 code entries whose state 0x15 block
calls `speed_step` (`func_L00_00212740`, shared by all 19), and the function
most often called as `f(state, 0|1)` that stores to hero + 0x2084. It writes
the three 19-entry tables the hook indexes by its own call site. The hero is core data at `0x13F450`
(state + 0x2084, animation frame + 0xAA8, stick magnitude + 0x229C). Not
ported: R&C2's Circle-with-wrench throw (state 0x83), which R&C1 has no
input for; and R&C2's auto-aim on the moving throw.

**The jump cancel (owner's request, 2026-10-18).** R&C1 already has one:
the state update's block for states 0x13-0x15 (NTSC `00242930.c` case
19-21; level 0 `0x22B938`) jumps when, past the attack record's f14 frame
and with no lock-on target, Cross was pressed within
`(int(frame) - fC) * 2` frames (`pad_pressed_within`, `func_L00_00267BA8`):
`hero_set_state(JUMP, 1)`, or with R1 held the crouch jumps
(`func_L00_00229E30`). The Comet-Strike is attack record 3 (table at
`0x17BD28` in level 0, 0x2C-byte records, level data), whose fC, f14 and
f18 are 83, 87 and 91 in every level, so the jump only opens 4 frames
before the throw ends. R&C2's block (`0x2C45B0`) is the same with two
differences, read from Oozla: its record 3 (`0x25EC90`, set for state 0x15
at `0x2C0D94`) has f14 = 35, and for state 0x15 the window is a fixed 8
frames (`movz` at `0x2C4604`; `(frame - 83) * 2` would be negative that
early). Its other records match R&C1's.

The port makes the same two changes. `tools/overlay_patch.py` feature
`comet_strike_jump` writes 35 into record 3's f14 (one data word per
level) and sends the block's `pad_pressed_within` call (level 0:
`0x22BB18`) to `ee_comet_jump_window` (`src/comet_strike.c`), which in
state 0x15 asks for R&C2's 8 frames through the level's
`scale_game_frames` (7 at 50 Hz, like the game's other jump windows) and
leaves states 0x13 and 0x14 their own windows. The lock-on test, the R1
crouch jumps and the jump itself stay the game's code. `tools/gen_hero.py`
finds the call (of the 0x83-entry state tables, the one whose entries
0x13-0x15 are a single block that indexes the attack records and calls
`pad_pressed_within` with Cross) and the record table (the block's
lui/addiu after `addiu $a0, 0x2C`), and checks record 3 is 83/87/91.
Record 3's f14 has no other reader: the block is its only use.

## x5: Strafing on L2 and R2 — first increment (movement) implemented (2026-10-17), awaiting owner test

**What R&C2 does.** Read from SCUS_972.68's Oozla program. Its hero input
pass (`0x2B40A0`) reads L2 or R2 held (pad + 0x1A0 & 3) and marks the hero
strafing (hero + 0x1454 = 1; a locked target replaces the 1). Its RUN
movement (`0x2BBCDC`) then, instead of turning toward the stick, calls its
strafe facing routine (`0x2B3F30`): the stick heading is saved (hero +
0x1458), the move heading (hero + 0x1A0) is set to the camera yaw (camera
+ 0x158), the hero turns toward it (`turn(0, 0.037, 0.25, 0.20)`), and the
planar velocity is later set along the saved heading
(`set_planar_vel(hero + 0x1458)`, `0x2BC274`). Ratchet faces where the camera
looks and slides where the stick points. Both triggers together is the
lock-on strafe, which the edition does not take over.

**What R&C1 has.** The RUN movement block (state 2; NTSC `00233de0.c` case
2; PAL level 0 `0x21C6D8`) already has every piece: `stick_target`
(`func_L00_00211F80`) turns the stick into the move heading (hero + 0x180)
relative to the camera yaw (`0x166ED8`, lit data, the same address in all 19
levels); `turn_to` (`func_L00_002124E8`) or, with the stick hard over, the
pivot turn (`func_L00_002125F0`) turns the facing toward it; and
`set_planar_vel` (`func_L00_00212790`) sets the velocity along the facing
(99999) or, in the drift case, along the heading.

**The port (this increment).** `src/strafe.c`, sites from `tools/gen_hero.py`
(the RUN block's calls in order: turn_to, pivot, turn_to, speed_step,
set_planar_vel twice):
- The pad begin-frame's parse call (`func_00217F68 + 0x1C4`) goes to a hook
  that, for pad 0, records L2/R2 held and clears bits 0x3 from held,
  pressed, released, raw and their copies (+0x1A0 .. +0x1C8, +0x348). L2
  and R2 only mirror L1 (look) and R1 (crouch) in R&C1, so this frees the
  triggers without touching any button test in the hero.
- The two `turn_to` calls and the pivot call of the RUN block go to hooks
  that, with a trigger held in state RUN, save the stick heading, set the
  heading to the camera yaw and turn toward it with R&C2's rates
  (`turn_to(0, fss*0.037, fss*0.25, ft*12.2)`); otherwise they call the
  original.
- Both `set_planar_vel` calls go to a hook that, after a strafe turn, puts
  the stick heading back and sets the velocity along it.

**Not yet.** Strafe animations (R&C1's Ratchet has 134 sequences and no
side-step or backpedal cycles; the run cycle plays whatever the direction),
the standing strafe stance (R&C2 turns to face the camera on L2 even with
the stick centred), R&C2's speed ramp and slower backpedal, and the camera
(R&C1's follow camera may try to swing behind the movement; R&C2's strafe
camera holds). These are the next increments.

## x5: Strafing — second increment: the camera, and the hover kept as it was (2026-10-17), awaiting owner test

**Owner's test of the first increment.** Facing and movement right; the
camera "follows Ratchet in the direction he's moving" and makes the
strafe hard to control. Asked for Going Commando's strafe camera.

**What R&C2 does.** Its hero camera pass (Oozla program, `0x4224D0`; the
strafe branch at `0x425F70`), when the hero is strafing with a trigger
held and not in states 0x7E, 0x81 or 0x82, sets four things on the active
follow camera through the camera's own setters: auto-turn off (camera
state + 0x10 = 0, `0x427D58`), a slide started (+ 0x136 = 1, `0x427E38`),
the lateral spring to stiffness 0.04 and damping 0.2 (`0x427D80`), and the
three stiffness timers (+ 0x12, + 0x14, + 0x16) cleared; then it skips the
normal stiffness logic for the frame. The follow camera's auto-turn is what
swings the eye behind the direction of travel; with it off the camera
keeps its yaw and only the right stick turns it, which is the R&C2 strafe
feel.

**What R&C1 has.** The very same block, with the same setters and the
same numbers, in the follow camera's per-frame overrides
(`func_L00_002E74B0`; NTSC decomp `follow_camera_apply_overrides`):
`FUN_L00_002e8568(0)` (auto-turn off, state + 0x10), `follow_camera_start_slide()`
(+ 0x116 = 1), `FUN_L00_002e8590(0.04f, 0.2f)` (lateral spring), `h12 = h14 =
h16 = 0`, `return`. R&C1's camera state is R&C2's minus 0x20 from + 0x20 on;
the setters refuse while the record's + 0x86 is set in both games. R&C1
gates the block on `hero state == 0x81 && pad raw held & 3`: state 0x81 is
the **thruster pack's hover**, whose movement block (NTSC `00233de0.c`
case 0x81) reads L2/R2 itself to turn toward the camera yaw, R&C1's own
primitive strafe. The auto-turn is switched back on by the camera's
approach pass (`func_L00_002E72E8`) at the end of every frame, so the hold
lasts exactly as long as the condition does.

**The port.** `tools/overlay_patch.py` feature `strafe_camera`: in each
level's own build of `func_L00_002E74B0` it finds the test sequence
(`addiu $a0, $zero, 0x81; lw $v1, 0x2084(hero); bne; lw; lui/lw pad + 0x1A0;
andi 3; beqz`), turns the `bne` into a `nop` and points the `lui`/`lw` at
the mod segment's `ee_strafe_held` (the pad hook's L2 | R2 of pad 0). The
block then runs whenever a trigger is held: in the hover exactly as in
retail, and now while strafing on foot. No other word of the camera code
changes; the stiffness logic and the right-stick turn are retail's.

**The hover, kept as it was (owner's request).** The first increment's pad
hook hid L2/R2 from the whole game, which also took them from the hover's
own `held & 3` test and from this camera branch, so hovering had lost its
retail strafe. The hook now records the triggers and returns without
clearing them while the hero's state is 0x81; the hover sees retail input
and runs retail code. (Entering the hover is R1 as before; R2's mirror of
R1 was given up with the first increment, by design.)

**Verification.** Rebuilt; the three patched words checked by disassembly
in levels 0, 7, 13 and 18 (nop; `lui $v1, 0xA; lw $v0, 0x35F4($v1)` =
`ee_strafe_held`), the pattern found exactly once in all 19 levels at
build time; the pad hook's disassembly shows the flag stored and the
scrub skipped when hero + 0x2084 == 0x81. Owner test: strafe sideways and
back on foot, the camera should hold its direction and only turn with the
right stick; release, the follow camera should resume at once; hover with
the thruster pack and hold L2/R2, which should behave as in retail.

**Not yet.** Strafe animations, the standing strafe stance, the slower
backpedal, strafe jumps and flips (next increments).

## x5: Strafing — third increment: jumps and flips (2026-10-17), awaiting owner test

**What R&C2 does.** Read from SCUS_972.68. Its jump-press handler
(`0x2C2420`, called from the idle, run and landing updates) is R&C1's
`func_L00_00229348` line for line (X pressed within a window; with crouch
held and a fresh press, the high jump; otherwise a plain jump unless the
caller wants only the crouch jumps) plus one rule: strafing with the stick
past 0.9, when the stick's direction class (`0x2BEA58`) is not forward, it
enters state 0xB. State 0xB is the flip family in both games: R&C1's entry
(NTSC `hero_set_state.c`, case 0xB) classifies the stick against the
facing (`func_L00_00222580`: 2 forward, 3 back, 0 and 1 the sides) and
plays animation 0x1C + class, which are the side flips (0x1C, 0x1D, with
the mirror option swapping them) and the back flip (0x1F), and the flip
travels along the move heading at the take-off speed. In R&C2 the same
ids are 0x1C + class with 0x1E added for the forward class, which the
strafe never uses. In the air, R&C2's jump block (`0x2BD34C`) turns toward
the heading (the camera yaw while strafing) and sets the planar velocity
along the stick heading (`0x2BD4C4`), and its air control (`0x2B7360`, at
`0x2B7920`) turns toward the camera yaw every frame while strafing. R&C2
also makes a jump from the run a plain jump rather than a run-jump while
strafing (`0x2C4A08`).

**The port.** Hooks found per level by `tools/gen_hero.py`:
- The four calls of `func_L00_00229348` go to `ee_jump_press`
  (`src/strafe.c`): the game's handler is called for the crouch jumps
  only; then, if X was pressed (re-read with `pad_pressed_within`, a pure
  query), strafing with the stick past 0.9 and a class other than forward
  enters state 0xB, R&C1's own flips; otherwise the plain jump as before.
  So a sideways strafe jump is a side flip and a backward one the back
  flip, with R&C1's animations.
- The jump block's first `turn_to` and `set_planar_vel` (the first five
  frames of a jump) and the air control's plain-jump `turn_to` (the one
  whose rate is hero + 0x414) go to hooks that, while strafing, turn
  toward the camera yaw and move along the stick heading, as the run.
- Not ported: the plain-jump-instead-of-run-jump rule. R&C1 picks the
  run-jump only with the heading within 60 degrees of the facing at speed,
  so it still appears when strafing straight ahead at a run; its call is
  not found by pattern in the level programs. Harmless.

**Standing stance and backpedal, dropped.** Reading R&C2's code found
neither: its `stick_target` keeps the facing as the heading with the stick
centred in both games (no turn to the camera while idle), and no speed
scaling by direction exists; the only strafe speed rule is a five-frame
penalty on a quick left-right reversal (`0x2B3F30`). The strafe animations
remain the open item.

**Verification.** Rebuilt; the four trampolines and the two C hooks
checked in disassembly (register moves; stick at hero + 0x229C, 0.9, Cross,
states 0xB and 7, class 2, the crouch-only flag); the seven patched sites
checked against the trampolines in levels 0, 7 and 18 and found by pattern
in all 19 at build time. Owner test: strafe sideways and press X (side
flip), strafe back and press X (back flip), strafe forward and press X
(plain jump or run-jump), hold a trigger through a jump (Ratchet keeps
facing the camera and drifts along the stick); crouch jumps and the hover
unchanged.

## x5: Strafing — fourth increment: R&C2's flips, the run twist, a backpedal (2026-10-17), awaiting owner test

**Owner's test of the third increment.** Camera and movement good. The
strafe flips felt stiff and "side-flips tend to trigger forward corkscrew
flips"; R&C1's non-strafe movement must not change. Go ahead with the
twist; a backward-stepping cycle is wanted too (R&C2 has one, R&C1 not).

**The flips, compared.** Both Ratchets carry the same four flip sequences
(slots 0x1C-0x1F, 33 frames each; R&C2's are the same data in its packed
frame format, read from the R&C2 class in a RAM dump, `0x1202F80`, 157
sequences). From the pelvis rotation in R&C1's data: 0x1C and 0x1D are the
two side flips (a 335-degree roll with a twist, the "corkscrew"), 0x1E and
0x1F the two somersaults. So the look is R&C2's own; what differed was the
motion and the choice:
- R&C2's flip air control (`0x2B75AC`) is R&C1's (`FUN_L01_00234b40`, the
  state 0xB branch) plus one strafe path (`0x2B760C`): the horizontal
  speed (hero + 0x458) starts at frame_time * 5 instead of 0 and follows
  the stick, approached at frame_time^2 * 37 toward max(stick * frame_time
  * 5, frame_time * 2); the ground speed is kept at the current horizontal
  speed; the "drift back toward the take-off yaw" term is dropped
  (take-off speed + 0x454 = 0). R&C1's flip only gains speed after the
  apex, hence the stiffness. Ported: the two `approach_value(&hero +
  0x458)` calls of the air control go to `ee_flip_approach`, which takes
  R&C2's path while a strafe flip is in the air; the jump-press hook seeds
  the speed at take-off when the stick is past 0.3 (R&C2's condition).
- R&C2 classifies the flip from the stick relative to the camera (its raw
  stick classes); R&C1's entry classifies against the facing, which lags
  the camera while strafing, so a sideways stick near a camera turn could
  read as a different class. The hook now classifies against the camera
  yaw and sets the flip animation itself (0x1C left, 0x1D right, 0x1F
  back; the mirror option swaps the sides as the game does) and the class
  the air control reads (hero + 0x450). Crouch flips outside the strafe
  are untouched.

**The run twist.** R&C2 has no strafe run cycles. Its run block calls a
helper (`0x2B8078`) that, while strafing with the stick past 0.15, not in
a flip and not backpedalling, writes the angle between the stick heading
and the facing into the hero's per-joint ease table: hips + angle (table
+ 0x68), torso - angle (+ 0x118), spring rates 0.037/0.27 and 0.025/0.27
(+ 0xA4/+ 0xA8, + 0x154/+ 0x158), skipping the normal lean. R&C1's lean
helper (`func_L00_00215A90`; NTSC `FUN_L00_00215340`) writes the same
table (NTSC `D_L00_0017A680`; PAL level 0 `0x17A780`, per level from the
rate setter `func_L00_0020A858`) from the turn rate: records of 0xB0
bytes, lean at + 0x60 and + 0x118, rates at record + 0x44/+ 0x48, the same
layout. The helper's six calls in the movement switch go to `ee_twist`,
which writes the table R&C2's way while strafing and calls the helper
otherwise. The legs follow the movement, the torso the camera, over
R&C1's own walk and run cycles.

**The backpedal.** R&C2 plays a 30-frame backward step (its slot 0x14; in
R&C1 that slot is the 14-frame stop) when the stick class is "back" past
0.15 (`0x2C2560`), at speed max(ground speed * 25, 0.7) capped at 2.2, and
returns to the walk/run cycle with a 7-frame blend. R&C2's cycle is in the
packed format the game decodes at run time, so carrying it over needs
that format decoded, a separate job. Until then the edition builds a
reversed copy of R&C1's run cycle (slot 4, 23 frames) when first needed:
once loaded a sequence is a 0x1C-byte header and a table of frame
pointers (`update_moby_animation_state` reads `seqs[seq]->frames[frame]`),
so the copy is the header with the pointers in reverse order and no
triggers, installed in the free slot 200 of the hero's class (134 of 256
used; `set_hero_animation` and the state updater do not check the count).
The run animation updater's call in the RUN update goes to `ee_run_anim`,
which plays and paces the backpedal R&C2's way and hands back to the
updater, with R&C2's blend, when the stick leaves the back sector.

**Verification.** Rebuilt; the ease table's address checked in the
generated level tables (levels 0, 7: `0x17A780`; 18: `0x17B100`); the
three trampolines and the hooks checked in disassembly (R&C2's constants,
the table offsets, slot 200, moby + 0x53, the speeds and blends); the nine
new patched sites checked against the trampolines in levels 0, 7 and 18
and found by pattern in all 19 at build time. Owner test: strafe sideways
and back and watch the hips/torso twist; strafe back (backward step, run
cycle reversed for now); side and back flips from a strafe should travel
with the stick and feel like R&C2's; crouch flips, the normal run and the
hover unchanged.

## x5: fourth increment, fix (2026-10-17): the backpedal slot and the flip blend helper

**Owner's test.** Loaded and ran fine; holding R2 and moving crashed with
"Jump to unmapped recLUT page (PC 0x70030c00)".

**Cause 1.** The backpedal sequence was installed in slot 200 of the hero's
class, but the class keeps only 162 slot entries: its mesh packet table
starts at + 0x2D0 (header word 0), right after slot 161. Writing slot 200
put a pointer into the packet table and the renderer followed it. Now
slot 140 (134 used; 134..161 are zero padding in every level checked:
0, 5, 18), with a guard: the install is skipped when the slot would reach
the packet table or is not empty, and the hook then leaves the game's
cycles alone.

**Cause 2 (latent).** The flip's animation blend called
`scale_game_frames` at `0x1FEED0`, a mid-function address copied from a
level call site; the PAL function is `func_001F98C0` (NTSC
`FUN_001f96f8`). Fixed; it would have crashed the first strafe flip.

**Verification.** Rebuilt; hooks checked in disassembly (slot 0x8C, the
packet-table guard, the helper's address) and the nine sites in levels 0,
7, 18.

## x5: Going Commando's backward step, converted (2026-10-17), awaiting owner test

**Owner's request.** The real R&C2 cycle, not a stand-in.

**Where R&C2 keeps it.** Like R&C1, each level archive's core index points
(+ 0x78) at a 256-slot table of core offsets for Ratchet's sequences, with
157 slots used; the armor WAD holds only his mesh. A sequence is the same
0x1C-byte header and frame-offset table as R&C1's, but every frame offset
carries 0xF in its top nibble and the frames are packed; the engine
expands a frame at animation time into the layout R&C1 stores on disc.
The expander is a hand-written routine at `0x308B20` in SCUS_972.68,
transcribed in `tools/rac2_anim.py`:
- after the triggers, the sequence's packed data: a u32 of three
  bit-fields giving the frame parts' offsets, a base pose of three s16 per
  joint, the scale and translation record counts and their base records;
- a frame: u16 1/rate, u16 (header word + 4), a bitstream of 10-bit deltas
  (three per 32-bit word, low bits first), s16 replacements, s8 deltas, one
  w sign bit per joint, then the record flags and values;
- per joint, x/y/z start from the base pose and the base's low two bits
  choose: keep, add s8 * 4, add 10-bit * 4, or replace; w is rebuilt as
  sqrt(1 - |xyz|^2) in 1/32768 with the sign bit, clamped to 0x7FFF;
- each record component starts from its base record and two flag bits
  choose keep, add s8 * 4, or replace with 16 bits.

**Proof.** `python tools/rac2_anim.py validate` decodes R&C2's run, walk
and jump sequences (slots 4, 3, 6) and compares them with R&C1's own, which
both discs carry: rates and header fields identical; quaternion components
within 6/32768, the packed format's 2-bit quantization (deltas are
multiples of 4); every R&C1 scale and translation record present with the
same values, R&C2 adding rest-pose records R&C1 leaves implicit.

**In the edition.** `tools/pack.sh` runs `rac2_anim.py emit 0x14`, which
reads the owner's R&C2 disc (`games/rac2/ntsc/baserom/SCUS_972.68.iso`)
and writes `build/modseg/gen_anim.c`: the 30 frames (1264 bytes each) and
the sequence header with their addresses, 37 KB in the mod segment (now 56
KB at 0xA0000). The run animation hook installs the header in slot 140 of
the hero's class and plays it as before (R&C2's pacing and blends). The
reversed run cycle is gone.

**Verification.** Rebuilt; the segment's 30 frames compared byte for byte
with the converter's output; the header's counts and the 16-byte-aligned
frame addresses checked; the install's store (slot 140) checked in
disassembly; the nine strafe sites unchanged. Owner test: strafe back and
watch the backward step; forward and sideways unchanged.

## x5: the strafe crash, found and fixed (2026-10-17)

**Owner's report (twice).** Everything ran until a trigger was held while
moving: "Jump to unmapped recLUT page (PC: 0x70030c00)". The slot fix of
the previous entry did not change it.

**Reproduced.** With PCSX2's PINE interface and the mod segment's new
debug pad (`docs/DEBUGGING.md`), the save was loaded and R2 + stick held
by script: the same exception on the first strafing frame, and the hook
trace said the twist hook was running.

**Cause.** The twist, the stick classes and the flip blend called three
executable helpers at their boot-executable addresses (`0x1FA790`
fast_subtract_rotations, `0x1FA850` fast_difference_between_rotations,
`0x1F98C0` scale_game_frames). Only the core below `0x15F000` is resident
in R&C1; above it every level program is linked afresh, and each level
carries its own copy of these helpers at its own address (the catalogue
lists them: `func_001FA790`, `func_001FA850`, `func_001F9850`, 19 places
each). In a level the boot addresses hold level data, so the first call
executed data and jumped into nowhere. The slot write of the previous fix
was real but not this crash.

**Fix.** The three helpers go through the level tables (`DECLARE_LVL`),
like every other level function the edition calls. Checked in the
emulator with the scripted scenarios: strafing in all four directions,
strafe side flip, strafe jumps, plain crouch flip, no exception; the twist
records ease to +90/-90 degrees in a sideways strafe and back to 0 in a
plain run; the backpedal plays slot 140 at R&C2's pacing and hands back to
the run cycle on release.

**Kept.** `tools/pine_pad.py`, `tools/pine_scenarios.py` and the debug pad
and trace in the mod segment (inert unless written), with
`docs/DEBUGGING.md`.

## x5: no foot slide when a strafe ends (2026-10-17), awaiting owner test

**Owner's request.** Everything works; the stopping foot slide Ratchet does
at high speed should not happen while strafing.

**What R&C1 does.** The run update, when the stick is released past
frame_time * 2.7 of speed (NTSC `00242930.c`, the RUN case), enters STOP
one of two ways: `hero_set_state(STOP, 1)` at high speed with a steady
stick history (the entry plays the skid, animation 6 or 5 from frame 4),
else `hero_set_state(STOP, 0)` followed by animation 0x14 from frame 7.
Both then decelerate in STOP until the ground speed reaches 0 and only
then hand to IDLE, which is the slide itself.

**The port.** Both calls are found per level by `tools/gen_hero.py` (the
speed constants 4.4 and 0.8, the `bc1f` to the shared tail and the
fall-through call) and go to `ee_stop`. While a trigger is held the hook
enters STOP without the entry animation, zeroes the ground, target and
current speeds and the planar velocity, so the next STOP update goes to
IDLE at once, and shows the short stop pose over the game's 9-frame
blend. Without a trigger both stops are the game's. The high-speed call
is a shared tail that other paths reach with other states; the hook
passes those through unchanged. A first cut hooked only the high-speed
entry and kept the deceleration; the owner saw the slide continue.

**Verification.** Rebuilt; the hook's stores (the five speed fields, the
animation, frame and blend) checked in disassembly; both sites checked in
levels 0, 7 and 18 against the trampoline and found once per level at
build time. Owner test: strafe at speed and release the stick with the
trigger held: Ratchet should stop on the spot; release both and he should
skid as before.

## x2: Charge Boots — first step (2026-10-17): the item, its icon and its menu cell, awaiting owner test

The reward and the gold model come later; this step makes "Charge Boots" a
gadget the player owns and can equip from the Gadgets page. Equipping it
attaches the Grindboots model and grants nothing else.

**Item 36.** The game's item records (0x4C bytes each: name and description
text ids, equip slot, attach bone, moby class, icon, 37 of them) are part
of every level's data record. Record 36 is an unused copy of the Devastator
in retail: no grid shows it and no vendor sells it, and the loops that walk
the table run to 37. The per-item byte arrays the id indexes (owned flags
at 0x13D5C8, gold-weapon flags at 0x13E620) hold 40 entries (the
challenge-mode reset copies them whole, 0x28 bytes), so id 36 is in range.
`tools/gen_items.py` finds the table in each level (icons 0xEA60 + id at
record +0x38) and rewrites record 36 per level as the Grindboots record
(slot 1, class 0xC3 on both feet, same bone) with name 0x5406, description
0x5407, "works on" 0x5408 (text/strings.tsv) and icon 0xEA84. The boots
grid of the Gadgets page (Grindboots, Magneboots: a 1x2 grid widget, cells
{icon, frame, kind, id, stream}) gets a third cell: the generator emits a
three-cell copy per level into the mod segment and `tools/overlay_patch.py`
(feature `charge_boots`) points the widget's cell pointer (+0x48) at it and
sets its column count (+0x44) to 3; the grid draw spaces columns from the
count. The equip handler (`update_menu_grid_selection` in the NTSC decomp)
writes the id into the slot the record names, and the gadget loader
attaches the record's class, so nothing else needs to know the id.

**Owned.** Until the Challenge Mode reward exists, `ee_charge_boots_frame`
(src/charge_boots.c), called from the HUD update hook, sets the owned flag
every frame. The flag is saved with the game like any item's.

**Icon 0xEA84.** HUD icons live in the HUD header (table of contents 0x14F8,
3 sectors): an icon table {id, frames, first frame}, frame references
{palette page, image page} and page tables {source address, GS block, log2
size}. Page addresses are bank offsets until `LinkHudBank` adds the bank's
address to the pages in that bank's index range (header +0x14 / +0x34 hold
the ends); pages past the last range are never relinked or uploaded with a
bank, and `GetFrameTex` fetches a page from its address on first use. So
the icon's two 32x32 frames (black, white while equipped; every gadget
icon's two frames share palette pages 5 and 6, tile colour included) are 2
KB in the mod segment (`ee_icon_charge_boots`, build/modseg/gen_icons.c),
and `tools/gen_hud.py header` writes build/hud_header.bin: the retail
header plus one icon row, two frame references and two image pages at the
segment's address. The patcher writes it over the header's sectors, so no
HUD bank is recompressed. `tools/gen_hud.py emit` converts
art/charge_boots.png (32x32 RGB) to the frames with the Grindboots icon's
palette entries, the white frame by the same black-to-white entry mapping
the Grindboots frames show. The hud header is re-read from the disc at each
level load, so the pages' GS allocations start clean like every other page's.

**Pipeline.** pack.sh runs gen_items.py and `gen_hud.py emit` before the
mod segment and `gen_hud.py header` after it (the header needs the pixels'
address). In-place route only; the wrench route does not carry the header.

**Test plan (owner).** Pause, Gadgets: a third boots cell with the new icon,
black; equip it: the icon turns white, Ratchet wears the Grindboots model;
the name and description read "Charge Boots". Grind rails need not work
with it. Check Magneboots and Grindboots still equip, and that a save made
with it equipped loads.

## x5: the strafe stop, measured and finished (2026-10-17)

**Owner's report.** Still a skid when stopping while strafing.

**Measured in the emulator** (the PINE harness, sampling state, animation,
speed, the momentum vector at hero + 0x150 and the moby position every
frame or two):
- Trigger kept while the stick is released: the previous build already
  stopped dead (STOP, animation 0x14 from frame 7, speeds 0, position
  frozen at the release point).
- Trigger and stick released together, which is how one stops in play:
  the retail skid (STOP, animation 6, momentum decaying over half a
  second, about 1.3 units of slide), because the trigger reads as released
  a frame or two before the game decides the stop.

**Fix.** The pad hook keeps a six-frame grace after the trigger is
released and the stop hook honours it. Two further details from the
measurements: the slide is carried by the momentum vector at hero +
0x150 that set_state copies from the last displacement and the idle/stop
movement block adds to the velocity each frame (FUN_L00_00216710), so
the hook zeroes it too; and both STOP entries of the run update are
hooked (the high-speed skid and the low-speed stop).

**Verified in the emulator after the fix.** Both released together: STOP
with the short pose, speed and momentum 0, position frozen. Trigger kept:
the same. Plain run stop without a trigger: the retail skid, unchanged.

## x2: Charge Boots — second round (2026-10-17): the icon, the preview, the equip and the box, awaiting owner test

Owner's first test of the build above: a narrow Foot Items box, a broken
preview, a missing-icon placeholder, and the boots never equipping. Found
and fixed:

- **The icon came from the wrong file.** Every level's data archive carries
  its own copy of the HUD header and banks (`wrench`'s level_data_wad.cpp;
  the archive's header at +0x20 lists them), and the level loader builds the
  HUD arena from that copy, not from the global file at table of contents
  0x14F8 (that one serves the frontend). The copies differ (some levels carry
  extra icons) and most have no room to grow, so the disc copies are left
  alone: the loader's header copy (`func_002032D0` + 0xB8, `jal` FastMemCopy)
  is redirected to `ee_hud_header_copy` (src/hud_icons.c), which copies the
  retail header and rebuilds the four tables in a resident buffer with the
  icon added, then points the header's offsets at them.
- **Then the icon showed other textures.** GetFrameTex's GS allocations are
  transient: `SetupGifPaging(0)` (func_001F4630) clears the GS block of every
  image page below the last bank range's end before each menu draw. Pages
  past the ranges (the first attempt) kept a stale block and drew whatever
  had been uploaded there since: other icons, font glyphs. The two pages now
  sit at the end of bank 1's range (the later banks' pages, frame references
  and range ends shift by two), and since `LinkHudBank(1, bank)` adds the
  bank's address to every page in that range, the loader's call to it
  (+0x190) is redirected to `ee_hud_link_bank`, which links as retail does
  and then points the two pages at the pixels in the segment.
- **The preview spawned inside the camera.** The item preview's placement
  table (`D_001E0708`, `ItemPreviewPlacement[36]`, 8 floats each) ends at
  item 35; item 36 read the words of the next table as its position. A
  37-entry copy (`ee_preview_placements`, item 36 = the Grindboots entry)
  lives in the segment and the lui/addiu pairs in `func_0021F238` (+552,
  +560, +576, +584, +604) and `func_0021F6A0` (+80, +88, +108, +116, +140)
  are pointed at it in every level.
- **The boots never equipped.** Item id 36 is the hero's "temporary item"
  sentinel: `hero_switch_item` (NTSC decomp) moves a pending id into
  `hero.selected_item` but writes the persistent equipped array
  (`D_00141760`, per slot, saved) only when the id is not 0x24, and the item
  mobys are created from that array. Every other use of the sentinel is about
  slot 0, so `ee_charge_boots_frame` sets the boots slot's equipped id
  whenever the hero has the Charge Boots pending or selected; retail's
  switch overwrites it for any other choice.
- **The box.** The Gadgets page record (kind 4) pairs its 14 widgets with 14
  sequences of the menu panel class 0x472, one panel moby per widget
  (`moby_anims[i]` / `screens[i]`, NTSC menu_system.h); the box's size comes
  from the sequence (the grid draws its columns from the panel's width,
  pvars +0x40). The boots grid is widget 10 with sequence 0x26 (1.43 wide);
  the Head Items grid's sequence 0x25 is 2.04 wide. The page word is patched
  per level to 0x25 (gen_items.py `panel`). Whether the sequence also fixes
  the box's position is what the owner's test will show; if the box moves
  onto the Head Items box, a copy of sequence 0x25 moved down has to be
  installed in the class at level load, the way the backpedal was.

Also found on the way: `tools/pine_pad.py` could never load its symbol file
(a `/` bound tighter than `.read_text()`); fixed.

## x2: third round (2026-10-17): the menu texture regression, and the box in its place, awaiting owner test

- **Every pause-menu texture broke** in the second-round build. The HUD
  header's image page range ends are 8 words at +0x34; the loop shifting
  them ran over 16 and so incremented the words after them: the bank sizes
  and the "bank present" flags at +0x54.. (unk54..unk64 in OpenRAC's
  loaders.c), which made the loader treat the two empty banks as present.
  Fixed to the 8 real entries.
- **The Foot Items box** with the Head Items sequence sat on the Head Items
  box: the sequence carries the place as well as the size (the 5-joint panel
  class 0x472: a root and four corners; each sequence slides the box in over
  7 frames, the last frame being the resting box). `tools/gen_items.py`
  now builds `ee_panel_seq`, the Head Items sequence with every frame's
  root translation moved down by the difference between the two boxes'
  final heights (-3442 packed units, 3.36), as a sequence header plus
  frame addresses in the mod segment, and `src/charge_boots.c` installs it
  in slot 219 of the class (219 retail sequences; the table has room before
  the packet table at +0x28AD0) once per level, through the class tables
  (id -> slot byte at `D_001B3E40`, slot -> data at `D_001B3580`; both
  level-local, `LEVEL_DATA` entries from `func_00226720` and
  `func_00205270`) or through a panel moby while the menu is open. The
  Gadgets page word is patched to 219.

Pipeline note: `pack.sh --quick` numbers its output when the previous
image is open in an emulator, so the newest `executive-edition*.iso` by
time is the current build.

## x2: fourth round (2026-10-17): the texture regression's real cause

The 8-entry loop was a bug but not the one breaking every icon: the
bank-link hook addressed the HUD arena at the executable's `D_0019A4E8`,
and that variable is above the resident core, so in a level that address
is unrelated memory, which the hook then "linked" by adding the bank's
address to whatever it found there (the level-local copy is at 0x17E5D8 in
Veldin). The attract demo looked fine because it runs the frontend's
unpatched copy of the loader. The hook now goes through a trampoline that
passes its return address, learns the level from the call site and reads
the arena from a `LEVEL_DATA` table built from the loader's own lui/addiu
(`hud_arena`). Checked live: the arena, the rebuilt tables with icon
0xEA84 at the end, and the two pages' sources inside the segment.

## x2: Going Commando's Charge Boots model, and the wrench route made whole (2026-10-17)

**The model.** R&C2 names its items in a weapon table in the boot program
(`g_weaponTable`, 0xE0-byte records): record 54 has the "Charge Boots" text
(0xA9F), the foot slot and moby class 0xE70 (3696) on both feet, the same
shape as R&C1's Grindboots record. wrench's unpack of the owner's R&C2 disc
holds that class (5 joints, 3 sequences, 18 mesh and 5 metal packets, one
128x128 texture, scale 0.1458, like the Grindboots). `mobyconvert` in the
edition's wrench fork rewrites it in R&C1's format; R&C1 vertex tables end
with per-vertex colour multipliers that R&C2's lack, so the fork's writer now
generates neutral ones (0x80, the only value on the R&C1 disc). The result
decodes with OpenRAC's independent R&C1 moby decoder (1,591 skinned
vertices). `tools/gen_boots.py` builds `build/boots_bank`, a wrench asset
bank with the class as id 2000 (R&C1 indexes ids in a 0x800-entry table and
uses up to 1973) and a reference to it in every level's class collection;
the fullest level (Oltanis Orbit) goes from 220 to 221 of the engine's 224
class slots, gadget entries included. Item 36's record then names class 2000
(`gen_items.py --boots-class 2000`). Nothing from either disc is committed.

**wrench's mipmaps (chaoticgd/wrench#286), fixed in the fork.** Measured on
PAL Aridia: a mipped R&C1 texture (types 3 and 4; type 1 has none) stores a
half-size image right after its pixels, which the game reads as mip level 1,
and a quarter-size image in GS RAM, both box-filtered (within 1.9 and 2.7
RGB units of the block averages). Upstream wrench wrote no half image, so
mip 1 was the next texture in the pool (98.7 from the average), and point
sampled the quarter (22.7). The fork writes both, filtered (1.7 and 2.0).

**The wrench route carries the edition.** `tools/pack.sh` without `--quick`
now packs the game bank, wrench's overlay, `mod/` and the boots bank with the
fork, then runs `overlay_patch.py --iso` against the packed image (level
locations read through the table of contents and level headers, which works
on wrench's layout where the editor's disc reader does not) and writes the
sectors in place with `tools/apply_patches.py`. The Options page previews,
which are streamed by sector, are refreshed at runtime from the tables the
game fills at boot from the table of contents (`EE_OPTIONS_SS`, `EE_HELP_SS`),
so each image streams its own. `--quick` stays as it was, with the
Grindboots model.

**Status at the end of 2026-10-17, tested in PCSX2 on the owner's Hoven save
(wrench route).** The edition boots and loads; the Charge Boots icon and the
R&C2 model show in the Gadgets page. But every item Ratchet carries or wears
(Clank, the Heli-pack, the weapons in the menus, the boots, the infobot)
draws with corrupt garbage instead of its texture, as in upstream wrench
builds (the "texture errors in menus" of #286). Measured and ruled out:
- the retail rule for those items' textures is type 1, no mips (33 textures
  in Hoven, 39 classes over the 19 levels, never mixed with other types);
  the fork now unpacks them as `stash_textures` and packs them as type 1
  without mips (bank `assets/wrench/rac1-pal-fork`, `GAME_BANK` in pack.sh),
  which matches retail but does not fix the corruption;
- texture pixel offsets in the core are now identical to retail's;
- the GS RAM upload is consistent (the uploader's running cursor equals
  every entry's stored address, in retail and in the wrench image), so the
  palettes are where the texture entries say.
Still unknown: how the engine keeps those items' textures (the per-frame
moby texture streaming is not decompiled to C yet). The quick route is
unaffected and still wears the Grindboots model.

## x2: the R&C2 Charge Boots model in game, without wrench (2026-10-18) — working, verified in PCSX2

`tools/pack.sh --quick` now builds the image the edition ships: the owner's
disc copied, the level code patched in place, and the Charge Boots class
spliced into every level's data archive by `tools/splice_boots.py`, which
appends the 19 new archives to the image and points each level header at its
copy. Nothing else on the disc moves, so the wrench route's carried-item
texture corruption does not arise. Verified in PCSX2: on the owner's Hoven
and Aridia saves the level loads, Clank, the Heli-pack and every menu icon
draw correctly, the Charge Boots cell previews R&C2's model with its own
texture, and once equipped (boots slot 36) Ratchet wears R&C2's Charge Boots.

What the game requires, found by changing one thing at a time on Hoven
(every failure hung the load at stage 7, waiting for the level's sound bank):

| Change | Result |
|---|---|
| retail archive moved to the end of the image, header repointed | loads |
| sections re-laid out at 0x100 alignment | loads |
| core data recompressed with wrench (3.5% smaller than retail) | loads |
| class blob and texture inserted below +0x7C, offsets shifted | loads |
| palette and GS RAM list entry added, texture entry added (tables moved) | loads |
| moby class table moved to the end of the index, even unchanged | hangs |
| class blob placed after the gadget blobs (past +0x7C) | hangs |
| moby class table grown in place, later tables moved down | loads, boots work |

So: the moby class table must stay right after the core index header; the
class blobs must sit in the resident part of the core, below +0x7C (wrench's
scene_view_size); the texture and upload tables may move. Retail packs its
core data about 3.5% looser than wrench's compressor, which leaves room for
the additions with every archive still smaller than retail's.

## x2: the Charge Boots posed from Ratchet's leg (2026-10-18) — fixed, verified in PCSX2

**The owner's report.** Worn, R&C2's Charge
Boots tipped with Ratchet's shin and their soles went through the floor,
where the Magneboots and Grindboots stay flat. Two causes, both needed:

- *The game poses only two boots.* A worn boot is two item mobys (one per
  foot) placed at Ratchet's knee each frame. The item updater
  (`func_L00_0020FC18`) asks three predicates of each slot's item, and the
  boots one (`func_L00_0020DBD8`: `item - 0x1C < 2`, the Magneboots or the
  Grindboots) sends each boot moby through the pose builder
  (`func_L00_00209940`), which writes Ratchet's leg (his class's joint
  lists 2 and 3: knee, ankle, heel, ball, toe) into a per-boot frame
  buffer the moby then plays (`0x17D100`/`0x17D200` in level 8, the moby's
  frame pointers at + 0x68/+ 0x6C). Item 36 failed the test, so the
  Charge Boots played their class's own static frame (all bind pose) and
  were drawn rigid on the knee whatever their skin: measured in PCSX2,
  the boot mobys' frame pointers pointed into the class, the Magneboots'
  at the buffers; and a test image binding the whole boot to the toe
  joint looked exactly the same. The predicate's `slot_item` call
  (`func_L00_0020DBD8 + 8`) now goes to `ee_boots_pose_item_hook`
  (`src/charge_boots.c`), which answers 36 as the Magneboots for that
  test only (the predicate has no other caller).
- *Their skin hangs the shoe on the knee.* The boot classes' five joints
  are those five leg joints. R&C1's Magneboots and Grindboots, and R&C2's
  own Magneboots, put the shoe on joint 1 (the ankle) and only the cuff
  on joint 0 (the knee); R&C2's Charge Boots hang the whole boot, sole
  included, on joint 0 (R&C2 shows them only during a charge). `mobyconvert
  --rebind 0 1 -950 -1250` (the edition's wrench fork, `tools/gen_boots.py`)
  moves the skin below the ankle to joint 1: vertices on joint 0 alone
  below z -1100 (boot units; the knee is the origin, the ankle at -1335,
  the ground at -1905) load joint 1 (one more matrix transfer per packet,
  into a VU0 address no vertex uses), the knee/ankle two-way blends are
  re-weighted from all knee at -950 to all ankle at -1250, about where
  the Magneboots blend, and the metal (shine) packets, which name their
  joints directly, get the same. Vertex order (the VU1 slots) is kept.

Verified in PCSX2 (Batalia, the owner's save): with both, the Charge
Boots' frame pointers point at the leg buffers like the Magneboots', and
side views of idle and crouch show the soles flat on the floor with the
cuff following the shin, as the Magneboots do.

## x2: the Charge Boots dash, Going Commando's, on R1's double tap (2026-10-18)

**What R&C2 does.** Read from SCUS_972.68's Oozla program. The Charge Boots
are hero state 0x7B. Its input handler (`0x2C1A70`) ends with the very
block R&C1's ends with for the Thruster-Pack (NTSC decomp
`hero_set_state.c`, the end of `FUN_L01_00240ed8`; level 1 PAL `0x241F50`):
when nothing changed state this frame, R1 pressed and held (pad + 0x1A0 /
+ 0x1A4, bit 8) within 30 frames of the previous press, in control mode 0,
1 or 12, or 2 or 4 on the ground, enters the state. R&C1 tests the
Thruster-Pack on the back (`slot_item(3) == 3`) and enters its hover, 0x81;
R&C2 tests the Charge Boots owned and worn (`0x1A7B2E`, `0x1A739C == 0x36`)
and enters 0x7B (`0x2C23D0`), skipping the two look states. The dash
itself took the Thruster-Pack's kit: the hover spring at ground + 0.17, the
looping sound in hero sound slot 4, the same entry fields.

- **Entry** (set_state case, `0x2BF200`): control mode 0x1C (the dash's
  own; no action reads it), fidget timer 0x68, velocity = this frame's
  displacement, animation 0x8B with an immediate blend. R&C2 also puts its
  Charge Boots item in the foot slot for the dash only.
- **Movement** (`0x2BC338`): loop sound 0xD; target speed 0.1917 per frame
  (11.5 u/s), 0.5 (30 u/s) for the first 60 frames with a tenth of the
  steering; steering = smoothed stick x (0.17 per frame) x 0.02036 x stick
  magnitude past 0.2; speed_step 1/36 up, 1/240 down; set_planar_vel along
  the facing; pos.z sprung to ground + 0.17 (0.04, 0.3, 0.125); the
  mobies in a 0.57 sphere three frames ahead of the body centre collected
  without a hit (`0x2D68B8`, mask 0), and a hit of 1 (flags 0x10000,
  pushed along seven frames of travel, `0x312330`) sent only to those
  `0x315368` accepts: classes 500 to 540, R&C2's crates. The charge breaks
  crates and harms nothing else; a lean from the turn in the hero's ease
  table (whole body roll, upper body twist).
- **Contact, in the edition** (the owner's rule, 2026-10-18): crates break
  as in R&C2, and an enemy ends the dash in a bonk, unhurt. R&C1's crates
  are class type 9 (class + 0x46: bolt, nanotech, metal, explosive and ammo
  crates, a breakable pipe and box), its enemies class type 5 (the
  creatures the game's targeting knows: 98 classes across the levels).
  `charge_contact` collects the sphere without a hit (the result list, a
  null-terminated list of moby pointers, `collect_list` per level from
  `tools/gen_charge.py`); any enemy in it sets the wall bonk (0x7A, a
  state of every level); otherwise, with a crate in it, it collects again
  with R&C2's hit while every other moby there has its hittable flag
  (moby + 0x34, bit 0x4000, which the hit pass tests) cleared for the call.
  The first port sent the hit to everything hittable in the sphere, which
  hurt enemies.
- **Update** (`0x2C4BD8`): R1 let go after 61 frames, or the surface flag
  hero + 0x348, ends it in STOP with the skid (animation 6 from frame 4)
  and STOP's jump/crouch tests held off 30 frames (R1 is still down); a
  wall (the collision's wall flag and the frame's movement under 3/4 of
  the velocity) gives the wall bonk, 0x77 (R&C1's 0x7A: same animation
  0x29, same 9 u/s rebound). Every frame two sprites at each ankle
  (Ratchet's joint lists 22 and 23, his ankle chains; both games' Ratchet
  carry the same 111-joint skeleton and lists), blue `20 80 FF 40` and cyan
  `00 FF FF 20`: the boots' jets.
- **Camera** (the hero camera pass, `0x4256E0`, the block at `0x425ED0`):
  every frame of the dash, lateral spring 0.04/0.2 (`0x427D80`), turn
  toward Ratchet's facing (his moby's matrix row 0) with strength pi/4 and
  no limit (`0x428110`), distance 3 at rate 0.003 (`0x427C18(0, ..)`) and
  the flag at camera + 0x246 (`0x427E10`). Measured in R&C2 (the owner's
  Oozla state, the dash forced and held, Ratchet turned 180 degrees
  mid-dash): the camera is 168 degrees off his facing, then 97 after 0.2 s,
  29 after 0.4 s and behind him after 0.6 s.

**The port** (`src/charge_dash.c`, `src/charge_hooks.s`). The dash is R&C1
hero state 0x48. R&C2's number, 0x7B, was the first choice, free in
Batalia, but it is a state of R&C1's own in 11 level builds (0, 2, 4, 5,
7, 9, 10, 13, 15, 17, 18): `hero_set_state` and the movement function have
its case there (in level 0 a stun: control mode 0x19, animation 0x71,
Ratchet stopped) and 13 levels enter it, so the dash failed in those
levels (the owner's report, Veldin 1). 0x48 is the state that no level's
state switch handles (the 130- and 131-entry switches of `hero_set_state`,
the movement function and the update, read with each switch's own default
from its bound check, all 19 levels), that no code enters
(`hero_set_state(0x48, ..)` nowhere) and that no code compares the hero
state with (levels and boot executable); `hero_set_state` takes it with
bookkeeping only. Three calls per level are hooked, found by
`tools/gen_charge.py` in each level's own program:

- the input handler's `slot_item(3)` (the one whose block enters 0x81 and
  reloads the window with `scale(0x1E)`; 13 places test the pack in level 0)
  goes to `ee_charge_trigger_hook`. Without the Charge Boots worn
  (`D_00141760[1] != 36`) it is the game's call. With them it runs R&C2's
  block and answers "no Thruster-Pack", so the R1 double tap is the boots'
  even with the pack on the back (the owner's rule: the boots win);
- the one call of the per-state movement function (the function holding
  the 0x83-entry movement table) goes to `ee_charge_move_hook`, which, in
  the dash's state, runs R&C2's update and movement in R&C1's terms and
  then the game's function (a no-op for 0x48 but for its bookkeeping);
- the follow camera overrides' (`func_L00_002E74B0`) one call of
  `collect_mobies_in_sphere`, made every frame just before the overrides'
  Thruster-Pack block (R&C2's charge block sits next to its own), goes to
  `ee_charge_camera_hook`: the game's call, then, during the dash, R&C2's
  camera block. R&C1's follow camera has R&C2's helpers, the same code
  with the camera's fields 0x20 to 0x30 lower: `func_L00_002E9A40`
  (lateral spring), `func_L00_002E9DC8` (turn toward a direction; its
  inner routine `func_L00_002E9B60` is R&C2's `0x427EA0`),
  `func_L00_002E9900` (distance) and `func_L00_002E9AD0` (+ 0x226). The
  trampoline passes the return address in `$8`, the fifth integer
  argument, so the call's four arguments and its float pass through.

R&C1 has every primitive under its own name: `stick_target`, `speed_step`,
`set_planar_vel` (the full velocity, slope included, as R&C2's),
the spring the Thruster-Pack hovers with (`func_L00_0025C918`),
`hero_loop_sound`, `make_hit_descriptor` + `collect_mobies_in_sphere` (the
hero's slams use them), `build_moby_bone_transform` and particle type 27's
spawner (`func_L00_0026EBC0`). R&C2's per-frame
constants become R&C1's frame_time units, as R&C1 writes its own (speeds x
60 x frame_time, accelerations x 3600 x frame_time², frame counts through
the level's `scale_game_frames`, the block's own call). Field map: R&C2
hero + 0x1B0/0x1B4 = R&C1 target/ground speed + 0x190/0x194, + 0x1B8 =
state timer + 0x198, + 0xF0/0x110 = velocity/displacement + 0xE0/0x100,
+ 0xE0 = body centre + 0xD0, + 0x287 = wall flag + 0x257, + 0x318 = ground
z + 0x2D8, + 0x340 = grounded frames + 0x300, + 0x348 = + 0x308, + 0x1E0 =
STOP's hold-off + 0x1C4, + 0x1FB0 = stick x + 0x1D20, + 0x24B8 = stick
magnitude + 0x229C.

Where R&C1 lacks a piece: the loop sound is the Thruster-Pack's jet
(Ratchet's sound 0x12; R&C2's 0xD is not on the R&C1 disc). The jets are
R&C1's own particles, the engine R&C2's grew from (OpenRAC's
`docs/engine/systems/PARTICLES.md`):
R&C2's are its particle type 0x4A (spawner `0x331FC8`, update `0x332130`,
records at `0x1A7CA0`/`0x1A7CB0`), an additive sprite with particle
texture 12, a faint grey cloud. The spawner keeps it as an offset in its
owner's frame (the moby's matrix rows at + 0xC0, position at + 0x10) with
a velocity in that frame, (-1/12, 0, 0) a frame: backwards, along -x, the
facing being row 0. Each frame the update damps the velocity by 2% and
adds it to the offset, places the sprite from Ratchet's frame (so the jets
blow out behind the heels and turn with him), turns it 1/256, grows it
from 70 000 to 170 000 (blue) and 45 000 to 135 000 (cyan) in 1/210 000
unit, and moves its colour from birth to death (blue `20 80 FF 40` to
`00 60 CF 00`, cyan `00 FF FF 20` to `00 80 00 00`) over 25 and 15
frames. R&C1 has the texture: index 12 is the first frame of its cloud
types 4, 11, 13, 18, 23 and 28 in every level, pixel for pixel R&C2's
(R&C2 kept the slot). Its type 27 (spawner `func_L00_0026EBC0`, update
`func_L00_0026ECA0`, both in every level) is the same kind of sprite,
additive, fading its alpha linearly as R&C2's does, but moving by a fixed
step a frame and keeping its size, colour and angle. The dash spawns type
27 and gives it texture 12 from type 4's frame list (the frame-list table
found per level by `tools/gen_charge.py`, `part_frames`, from the type 27
spawner's own load); `jets_age` does the rest of R&C2's update every frame
on the sprites the dash spawned (128 tracked, each checked to be the same
record still: type 27, alive, its lifetime and last colour). R&C2 places
the sprites after the hero has moved; the dash runs before, so each
record's step is Ratchet's last one and type 27's update adds it. R&C2's
update rides only while the game-mode word `0x1A8F40` is 0 (otherwise it
moves the sprite in the world); it reads 4 in the owner's Oozla state at
rest and 0 during a dash, and measured live mid-dash R&C2's sprites sit
0.60 to 1.6 behind the ankles at + 0.46 height, + 0.10 / - 0.08 aside,
and keep those offsets through a 90-degree turn: rigid, as the port's
(0.63 to 2.0, the same height and sides). A first
port drew the jets with `spawn_glow_particle` (`func_L00_002ADBB0`), which
spawns a 3D moby (class 112, random orientation), not a sprite: it read as
blue bubbles. The animation is R&C2's own: sequence 0x8B
(15 frames) expanded by `tools/rac2_anim.py` into
`build/modseg/gen_anim_charge.c` and installed in slot 141 of Ratchet's
class, as the strafe's backpedal is in 140.

**Verified in PCSX2** (Batalia, the owner's save, which wears the Charge
Boots and the Thruster-Pack): the double tap enters the dash (state 0x7B
at the time, 0x48 since) in control mode 0x1C with animation slot 141; ground speed reaches 0.6 per PAL frame (30
u/s) and settles toward 0.23 (11.5 u/s) after the first second; the
movement step caps the actual displacement at the collision radius minus
0.02 (0.43 per frame, 21.5 u/s at 50 Hz), as R&C2's does at 60 Hz;
steering is a tenth for the first second, then about 1.2 rad/s at full
stick; letting go of R1 skids to STOP; running into a railing bonks
(0x7A); with the Grindboots worn instead, the double tap enters the
Thruster-Pack hover as in retail. Not tested in game: hitting an enemy
with the charge.

## x2: the Charge Boots cheat, the pause freeze and Veldin 1 (2026-10-18) — verified in PCSX2, awaiting owner test

**The pause freeze (owner's report).** Pausing mid-dash froze the game when
it resumed. Ratchet's class keeps 162 sequence slots; slots 134 and up are
the pause menu's: it raises the class's count (class + 0xC) from 134 to 162
while open and empties them when it closes (measured: count 134, 162, 134;
slots 131-161 zero after). The dash's sequence (slot 141) and the strafe's
backpedal (slot 140) live there, so Ratchet resumed playing an empty slot
and the boots' pose (the item updater's `func_L00_00209940`, NTSC decomp
`pack_pose`) waited forever in the animation evaluator (`func_L00_00255B90`
spinning on scratchpad `0x700035F0`; found from a savestate's registers, PC
`0x26ABA8`, and the stack's return addresses). Every one of Ratchet's ids
0-133 is used by some level (the levels' `ratchet_seqs`), so no lower slot
is free. The mode-0 frame's call of `MobyFreeSlotBookkeeping`
(`func_L00_00299250 + 0x6D4`, every level, just before the moby pass and the
hero update) now goes to `ee_frame_start_hook`, which puts slot 141 or 140
back whenever Ratchet's moby plays it (+ 0x52/+ 0x53) and it is empty.
Verified: charge, pause, unpause; the dash goes on. Pausing mid-backpedal
(tested by the strafe work's session on the same image, twice, once through
the Weapons page with its animated Ratchet): no hang, the count 134, 162,
134, and the backpedal playing from slot 140 after the resume.

**Veldin 1 (owner's report).** The dash never started on Veldin's first
visit. The edition's trigger kept a test of R&C1's Thruster-Pack block,
hero + 0x22D8 == 0; that halfword is the game's "no Clank on the back" (read
by `HeroItemsCreate` and the Clank jumps, written by no level code, 1 on
Veldin 1 before Clank), and the boots need no Clank. The test is gone.
Verified on Veldin 1 (loaded from the owner's save through the game's own
level request, `func_0022F4A0`'s globals): the double tap dashes with the
flag still 1.

**The cheat (owner's decision, 2026-10-18: post-game only, a permanent
unlock).** The Charge Boots are locked until the move sequence below is
done after the game has been beaten, then owned for good in that save. It
uses the game's own cheat detector (`func_L00_00207418`; NTSC decomp
`FUN_L00_00206e00`), which only runs when the game has been beaten
(0x15EF20, the challenge count, or 0x15EFA0). It records a move id 15 ticks
into a move (60 for a held crouch or glide) in a 16-entry ring at hero +
0x21C4, compares the ring's tail with 12 zero-terminated sequences of 16
bytes (a per-level table in the level's data, `cheat_table` from
`tools/gen_charge.py`), and on a match toggles `gCheats[i]` (0x15EEB0 + i,
saved with the file), sets 0x15EEC0 + i and shows the cheat banner. Retail
fills entries 0-4, 6 and 7 (entry 0, `1 2 3 3 4 5 4 6`, is Big Head
Ratchet: side flip left, side flip right, back flip twice, Comet-Strike,
double jump, Comet-Strike, Hyper-Strike; ids 1/2 side flips (swapped in the
mirrored mode), 3 back flip, 4 Comet-Strike, 5 double jump, 6 Hyper-Strike,
7 crouch held a second, 8 a second of glide, 9 wall jump, 10 long jump,
12-14 the wrench combo's hits). The edition writes entry 8: **crouch for a
second, back flip, back flip, Comet-Strike, Hyper-Strike, Hyper-Strike**
(`7 3 3 4 6 6`; no retail sequence ends it or is ended by it).
`gCheats[8]` (`EE_CHEAT_CHARGE_BOOTS`, 0x15EEB8) is the unlock; byte 11 stays
the quick select pause setting. Each frame `ee_charge_boots_frame` owns the
boots when it is set, and otherwise takes them away (not owned, the
equipped slot cleared, and 0x26, `hero_switch_item`'s "no item", made
pending if Ratchet wears them: saves from earlier builds own and wear
them). Once set, `ee_charge_cheat_frame` blanks entry 8 of the loaded
level's table at the start of every frame, so the sequence cannot switch
the cheat off again, and the detector's `ShowBanner` call (`banner_site`)
goes to `ee_charge_banner_hook`, which shows the edition's text 0x5409,
"Charge Boots unlocked", for the unlock instead of the generic cheat-on
banner. Verified (the owner's Novalis save, the detector's gate set in RAM
as a beaten game sets it): locked on load (item gone, boots taken off);
the six moves recorded as `7 3 3 4 6 6`; then `gCheats[8]` 1, the boots
owned, entry 8 blanked and the banner reading "Charge Boots unlocked". A
back flip by pad needs the direction pressed with Cross while crouched
(`HeroCrouchJumps` wants a fresh direction press); the stick held alone
gives a high jump.

## x2: the Charge Boots on the Cheats page; Challenge Mode only (2026-10-18) — verified in PCSX2, awaiting owner test

**Owner's requests.** Once unlocked, the Charge Boots should be listed on the
Goodies Cheats page and switch on and off there. The cheat must not unlock
outside Challenge Mode (the previous round's test had written the gate's
flag into RAM on a save that is not in Challenge Mode).

**The page.** Goodies > Cheats is a toggle list (level 0's widget at
`0x1B8098`). Its "enter" callback (widget + 8) is the row builder
`func_0021B138` (NTSC decomp `rebuild_configured_text_label_list`), which
writes a row {name, &gCheats[id], "on", "off", 0} for each seen cheat of its
id list {1, 3, 0, 7, 4, 6, 2} into the widget's list (widget + 0x34, 13 rows
of 0x14 bytes) and ends it with a zero name; Cross flips the byte the row
points at. The page has no other per-row table (its other widgets are the
title and the button prompts). Each level's callback slot (one data word,
`cheats_enter` from `tools/gen_charge.py`, which checks it is the only word
holding the builder's address) now holds a stub `ee_cheats_enter_NN`
(src/charge_hooks.s; a callback reached through a pointer has no call site
to name the level, so each stub passes its own), and `ee_cheats_enter_c`
runs the builder and then, once the cheat has been seen, appends {0x5406
"Charge Boots", &gCheats[8], "on", "off", 0}. 19 words.

**Toggle and unlock split.** `gCheats[8]` (0x15EEB8) is now the switch:
`ee_charge_boots_frame` owns the boots while it is 1 and takes them away
while it is 0 (as when locked). The unlock is the detector's own seen byte
for entry 8, 0x15EEC8, saved with the file like the retail cheats': it lists
the row, keeps entry 8 of the loaded level's sequence table blanked (so only
the page switches the boots off) and picks the "Charge Boots unlocked"
banner. Saves that unlocked the boots on the previous build have both bytes
set, so they get the row with the boots on.

**Challenge Mode only.** The detector returns before recording any move
unless 0x15EFA0 or 0x15EF20 is set (NTSC decomp `if (D_0015EEA0 == 0 &&
D_0015EE20 == 0) return;`, PAL + 0x100). 0x15EF20 is the completion count,
the "R 1" badge on the Load Game list; 0x15EFA0 is set when a beaten game
continues into Challenge Mode (NTSC `process_global_state_flags`). Both are
Challenge Mode, so the game's own gate already confines the cheat to it and
the edition adds none; nothing in the edition writes either flag.

**Verified in PCSX2 (this build, no RAM writes unless said).**
- Slot 1, Novalis, not Challenge Mode (both flags 0): the six moves leave
  the detector's ring empty; the boots stay locked.
- Slot 4, Batalia, Challenge Mode (0x15EF20 = 1): the sequence entered by
  hand in the emulator window (the scripted attempt's ring index was reset
  mid-sequence, cause not found) left the ring reading `7 3 3 4 6 6` and the
  boots unlocked (gCheats[8] 1, seen 1, owned); then switched off on the
  page: gCheats[8] 0, the boots no longer owned.
- The page itself (Novalis with 0x15EFA0 written in RAM, the earlier run
  of this build): the row reads "Charge Boots ... on"; Cross shows "off",
  gCheats[8] 0, and back in the game the boots are no longer owned.
  Switching back on was not separately exercised; it is the same frame code
  that gives the boots on the unlock.

## x2: the Cheats page toggle shows at once; the retail banner (2026-10-18) — verified in PCSX2, awaiting owner test

**Owner's reports.** Switching the Charge Boots on the Cheats page did not
change the Gadgets page until the game had been unpaused and paused again.
And the unlock should show "Cheat Enabled" like every other cheat.

**The Gadgets page.** The grid reads the owned flags as it draws (a kind-0
cell is drawn only while 0x13D5C8[id] is set; `draw_menu_item_grid` in the
NTSC decomp) and marks and equips from the menu's own equipped array (menu
state + 0x30, NTSC `menu_system.equipped`), which the menu hands to the hero
when it closes: equipped in the menu, the boots read 36 there while the
hero's array at 0x141760 still held 28. The edition synced the boots with
`gCheats[8]` only from the HUD update hook, which does not run while
paused. Now the Cheats widget's "update" callback (widget + 0,
`func_00221E60`, the option list's update, which flips a row's flag on
Cross; gen_charge's `cheats_update_slot`, checked to hold it in every
level) goes through a per-level stub `ee_cheats_update_NN` as well, and
`ee_cheats_update_c` runs it and then `sync_ownership`, the gameplay
frame's sync, which now also clears the menu's boots slot when it holds the
Charge Boots and they are switched off. 19 words.

**The banner.** The detector's ShowBanner hook and text 0x5409 are gone (19
words fewer); the detector shows its own 0x4FBE, "Cheat Enabled", as for
every cheat.

**Verified in PCSX2 (slot 4, Batalia, Challenge Mode, no RAM writes).**
- The sequence unlocked the boots; the banner read "Cheat Enabled".
- In one pause: Gadgets showed the third boots cell; equipped there (menu
  slot 36); Cheats, off; Gadgets: the cell empty, the preview without
  boots, menu slot 0; Cheats, on; Gadgets: the cell back.
- Worn in the game (hero boots 36), paused, switched off; Gadgets: the cell
  empty at once; unpaused: the boots slot 0, nothing pending.
- At that save's start two back flips reset the detector's ring before the
  Comet-Strike (twice, by script; cause not looked into); walked forward
  first, the sequence registers.

## x5: the quick second strafe flip (2026-10-18), awaiting owner test

**Owner's report.** A quick follow-up side flip while strafing often came
out as a boosted plain side jump, which (unlike a flip) takes a double jump.

**What R&C2 does, measured in R&C2.** The owner's Oozla savestate,
driven through PINE: the pad parse's report pointer (Oozla `0x324F60`,
`daddu $a1, $sp, $zero`) redirected to a buffer at `0xF0000` (free in the
state), so the game parses a report the script writes, R2 and the stick
held. A strafe side flip is state 0xB; it lands after 57 frames and is in
RUN the next frame.
- Cross after the landing: another flip (the RUN update calls the
  jump-press handler `0x2C2420` with crouch_jumps_only, and its strafe rule
  flips).
- Cross in the last 8 frames before the landing (pressed at flip frames 49
  and 52): a plain jump (state 7) on landing. The jump family's landing
  branch (`0x2C6A04`: not on slip ground, hold-off `+ 0x1E0` clear, Cross
  within 8 frames, air frames under 4, then RUN_JUMP when fast and facing
  the heading, else JUMP) has no strafe test, in R&C2 as in R&C1 (NTSC
  `00242930.c`, the jump-family case, `hero.unk418 != 0`). Cross again 12
  frames into that jump is a double jump (state 0xE).
- Cross 12 frames before the landing: dropped (out of the window).
- Speeds, per frame at 60 Hz: flip 0.083 (5 u/s); the plain jump 0.083
  rising to 0.099 (the run speed); the double jump 0.083.

**What the edition did.** The same states at the same moments: the flip
lands after 43 PAL frames, Cross after the landing flips again, Cross 5
frames before it jumps (state 7) and Cross again double jumps. But the
plain jump left at 0.208 per PAL frame (10.4 u/s, against R&C2's 5 to 6)
and the RUN after a landing started with a one-frame spike to 0.218.

**Cause.** R&C2's strafe flip path keeps the ground speed at `hero +
0x174` (`0x2B765C`), which is R&C1's `motion + 0x164`: R&C2's jump-family
entry takes the take-off speed from it (`0x2C0714`: `+ 0x4D8 = + 0x174`
when above 1.5/60) as R&C1's does (NTSC `hero_set_state.c`: `unk454 =
motion.unk164` when above frame_time * 1.5), and R&C1's own air states
write `ground_speed = motion.unk164` (NTSC `00233de0.c`, states 10 and
0x11). The port copied `motion + 0x160` (speed_xy), which eases toward the
run target all through the flip: frame by frame it rose from 0.15 to 0.22
by the landing, while the flip travelled at 0.10. The landing jump (and
the RUN after a landing) start from the ground speed.

**Fix.** `ee_flip_approach_c` (`src/strafe.c`) copies `motion + 0x164`.
The landing-jump rule stays the game's: a press in the last frames of a
flip is a plain jump in R&C2 too.

**Verified in PCSX2** (the owner's Batalia save, a clean `pack.sh --quick`
build, the mod segment's debug pad over PINE; the hook in RAM reads `hero +
0x164`): a strafe flip right lands after 42 frames into RUN at 0.10 rising
to 0.114 (was a spike to 0.218); Cross at flip frame 38 (5 before the
landing) and at frame 41: a plain jump (state 7) at 0.10 rising to 0.114,
the run speed (was 0.208), and Cross 12 frames into it a double jump
(state 0xE); Cross on the first RUN frame after the landing: the next flip
(state 0xB), as before. Per second that is close to R&C2's: its plain
jump goes from 5 to 6 u/s, the edition's from 5 to 5.7.

**Not changed (retail R&C1).** A double jump over a moby floor (hero +
0x30A, set by the ground probe when the ground below is a moby: it found
one under that trial's landing spot on the Batalia bridge) has its air
target capped at frame_time (0.02 a frame) by air control (`air_accel`,
level 1 `func_L01_00234970`: state 0xE with + 0x30A set); the same double
jump over plain ground keeps 0.07, strafing or not. One trial's double
jump fell to 0.02 for that reason alone (measured: + 0x30A = 1 there, 0
where it kept its speed).

## x5: a press at the landing flips while strafing (2026-10-18) — reverted the same day (next entry)

**Owner's request.** With the trigger and the stick held to the side,
Cross pressed the moment a side flip lands gave a plain side jump; it
should be the next flip. Plain strafe jumps (taken with the stick centred
or forward, steered sideways in the air) stay as they are.

**R&C2.** The plain jump there is R&C2's own (measured, see the previous
x5 entry): its jump family's landing branch (`0x2C6A04`) has no strafe
rule, so this is a deliberate change of the owner's. Everything the change
uses is R&C2's: the flip rule of its jump-press handler (`0x2C2498`:
strafing, stick past 0.9, any class but forward, state 0xB) and its strafe
stick classes. While strafing, R&C2's stick routine (Oozla
`0x2B4534`-`0x2B45D4`) classifies `fast_arc_tan(x, y)` of the raw stick
vector (hero + 0x1FB0; R&C1's + 0x1D20) against pi/2 (back), 0 (right) and
pi (left), 45 degrees either side, into hero + 0x1457, which its flip
classifier returns while strafing (`0x2BEA78`); the same pass sets its
backpedal flag (+ 0x1456) for class 3 past 0.15, the edition's existing
rule.

**The port** (`src/strafe.c`). The landing jump's two requests (JUMP and
RUN_JUMP; level 0 `0x22FA70`-`0x22FB34`) reach `hero_set_state` through the
shared tail the strafe stop already hooks (`stop_site`, level 0
`0x23246C`, all 19 levels from `tools/gen_hero.py`), so `ee_stop_c` takes
them: a JUMP or RUN_JUMP request from a landed (hero + 0x418) jump-family
state goes through the jump-press hook's flip rule first. The flip entry is
one helper for both paths. R&C1's flip entry already handles this case: from
an airborne control mode (4) it starts the animation at frame 1 with a
1-frame wind-up (`unk420 = scale_game_frames(1)`, NTSC `hero_set_state.c`)
instead of 4, the way its own crouch flips chain on landing. The stick
class now comes from the stick vector as R&C2's does; the first port
classified the move heading, which R&C1's flips freeze at their take-off
heading (`move_yaw = unk43C`), so it only held in RUN.

**Verified in PCSX2** (the owner's slot 1, Novalis; flips there land at
frame 47): with R2 and the stick held right, Cross at flip frames 42, 44
and 46 (5, 3 and 1 frames before the landing) starts the next right flip
(animation 0x1D, class 1) on the landing frame, which launches on its
frame 2 (vertical speed 0.245) and flies a full flip; left (0x1C, class 0)
and back (0x1F, class 3) the same. Unchanged: Cross after the landing
flips (from RUN); a strafe jump with the stick at 0.54 and a press before
its landing gives the plain landing jump (state 7); forward strafe jumps
are plain jumps; the backpedal plays (slot 140); without the trigger the
landing jump is the game's (RUN_JUMP, state 9).

## x5: the landing press back to R&C2's plain jump (2026-10-18), verified in PCSX2

**Owner's word.** "Lets have it back to how Going Commando had it": the
previous entry departs from R&C2, which the owner had remembered otherwise.
`ee_stop_c` (`src/strafe.c`) no longer takes the jump family's landing
requests; JUMP and RUN_JUMP from a landing reach `hero_set_state` as in
retail R&C1, which is R&C2's behaviour too (its landing branch, `0x2C6A04`,
has no strafe rule; measured there: a plain jump that can double jump).
Kept from the previous entry, because they are R&C2's: the flip's stick
class from the stick vector (Oozla `0x2B4534`, `0x2BEA78`), which the
jump-press hook uses for every strafe flip, and its one flip-entry helper.

**Verified in PCSX2** (the owner's slot 1, Novalis; a right flip's last
frame in state 0xB is 46): with R2 and the stick held right, Cross at flip
frames 42 to 46 gives the plain landing jump (state 7, animation 7) on the
landing, then the run, each sample read twice to rule out a torn read. With
the stick at 0.54, and without the trigger, the landing jumps are the
game's (states 7 and 9). The jump-press hook, which flips a press after the
landing, is untouched.

## x5: R&C2's gun arm over the backpedal (2026-10-18) — verified in PCSX2, awaiting owner test

**Owner's report.** Running backwards while strafing, Ratchet's arm
animates every held item like a glove or the wrench; R&C2 holds a trigger
weapon (Blaster, Pyrocitor, Devastator, RYNO...) differently there.

**What both games do.** While a trigger weapon is held, a per-frame
gun-arm manager (R&C1: NTSC `FUN_L00_0020dd20`, level 0
`func_L00_0020E3B8`, shared by all 19 levels; R&C2 Oozla `0x2AF820`) keeps
a layer over each arm the weapon takes (hero + 0xD00 the right arm,
+ 0xD04 the left; R&C2 + 0xE90 and + 0xE94). The weapon type (hero +
0x20AB) decides: 1 takes the right arm, 2 both, 0 (the gloves and the
wrench) none. A layer (`func_L00_00250060` on the arm's joint set)
animates in the arm's own class (moby classes 1 and 2: 20 joints, 23
sequences; layer + 0x18) and every frame plays the sequence Ratchet's body
plays, at the body's frame, through a mapper that is the identity
(`func_L00_0020DC50`, 0xFF meaning the moby's + 0xA5; R&C2 `0x2AEA88`).
The arm classes hold the gun-held arm for the body's first 23 sequences,
so the manager enables a layer channel only for a sequence below 0x17
(`sltiu v0, v0, 0x17`, twice). A disabled channel reads Ratchet's own
class at the mapped index, so the arm follows the body.

**R&C2, measured.** On the owner's Oozla state, the backpedal frozen at
frame 8, with a gun and with a glove: the same body sequence (0x14) and
frames; with the gun the layer holds the forearm up and the gun forward,
with the glove the arm swings. Compared frame by frame, R&C2's arm classes
match R&C1's in every sequence but two: 0x14, a 30-frame backpedal arm
where R&C1's is the 14-frame stop arm (R&C2's body 0x14 is the backpedal,
R&C1's the stop), and a 24th sequence (0x17, 50 frames). So R&C2 added the
backpedal arm, and its identity mapper lines it up with the body.

**The edition's gap.** Its backpedal is R&C2's body sequence installed in
Ratchet's class slot 140 (fourth increment), above 0x17: the channel was
off and every weapon swung like a glove.

**The port.**
- `tools/rac2_anim.py emit-arm 0x14` (run by `tools/pack.sh`) reads
  sequence 0x14 of R&C2's arm classes 1 and 2 from its level 1 archive
  (index + 0x18: the class count and offset, 32-byte entries) and writes
  them as `ee_gunarm_seq_1` and `ee_gunarm_seq_2`
  (`build/modseg/gen_anim_arm.c`; 30 frames each, 5,280 and 6,240 bytes).
  R&C2 stores them unpacked in R&C1's frame layout, so they go in as they
  are.
- `src/gun_arm.c` installs them, on demand, as the arm classes' sequence
  0x17 (the table has room: entries 0x17 to 0x19 are free before the class
  data at + 0xB0), and its mapper sends body slot 140 there. Body sequence
  0x17 has to keep its channel off: it is the wrench combo's first hit
  (state 0x13 plays 0x17 + hit; NTSC `hero_set_state.c`). Mapped as itself
  it would now enable the backpedal arm, so it maps to an alias slot of
  Ratchet's class (142) that holds sequence 0x17's own pointer, and the
  disabled channel reads the swing from there. Every install checks the
  slot first and leaves one that holds something else. The pause menu's
  screens write some of Ratchet's slots 134-161 and zero the range on
  close; the alias goes back in when next needed. The manager compares the
  body's sequence, through an identity call, with the layer's mapped one;
  that call maps the same way.
- `tools/overlay_patch.py` (feature `strafe`) sends the manager's identity
  call (+ 0x29C) and its two mapper calls (+ 0x3E0, + 0x4F0) to
  `ee_arm_ident` and `ee_arm_map`, and makes its two tests (+ 0x478,
  + 0x5B8) `sltiu v0, v0, 0x18`. `tools/gen_hero.py` places the manager
  and the mapper from the catalogue in each level and checks every site's
  retail word, and for the calls the delay slot's load of the sequence,
  before naming it. Level 0: the identity call `0x20E654`, the mapper calls
  `0x20E798` and `0x20E8A8`, the tests `0x20E830` and `0x20E970`.

**Verified in PCSX2** (the owner's slot 1, Novalis; R2 with the stick held
back):
- Bomb Glove (type 0): no layer; the arm swings as before.
- Blaster (type 1): the right arm's layer plays sequence 0x17 (ours) with
  the channel on, at the body's frame (3 to 29 through the cycle). On
  screen the Blaster is held up and forward through the backpedal, where it
  swung at the hip before. Firing while backpedalling keeps the layer on
  that arm and the shots fly; a standing shot (body 56) is the game's.
- Pyrocitor and Devastator (type 2): both layers, each with its own
  class's arm; both weapons held up in both hands.
- Running forward (sequence 4), the stop (0x14) and the idle keep their
  own arm sequences.
- A wrench swing with a gun out (body 0x17): on the swing's first frame
  the layer maps to the alias (142) with the channel off, then the game
  frees the layers as the wrench comes out, as in retail.
- The pause menu mid-backpedal, and the Weapons screen (which wrote slot
  140 and zeroed 134-161 on close): the backpedal arm and the alias came
  back on their next use; no crash.

## q2: Quick select pages — verified in PCSX2 (2026-10-18), awaiting owner test

**What the player gets.** The quick-select ring has three pages. With the
ring open (Triangle held), R1 shows the next page and L1 the previous, in a
loop: page 1, R1, page 2, R1, page 3, R1, page 1; L1 goes the other way. L1
and R1 glyphs sit left and right of the ring. The ring reopens on the page
it was left on, and that page is what the pause menu's Quick Select tab
shows first. On that tab, Square switches between the three rings ("Page 1",
"Page 2", "Page 3" in the ring's centre) and the button box reads "[X]
Equip", "[square] Next ring", "[triangle] Exit" (the owner's "Switch
rings" does not fit the retail box, which clipped it to "Switch rin";
the owner chose a shorter label). A weapon may sit on
more than one page; within a page the game's own rule (one slot per item)
still applies. A newly acquired weapon takes the first empty slot of the
active page as in retail; when that page is full it goes to the next page,
then the previous. L1 and R1 do not reach the hero while the ring is open,
so a page flip is not also a crouch or a look.

**The game's ring.** HUD slot 3, shown by the hero's input handler
(`func_L00_0022A340` in level 0; `HeroStateTransitions` in others) with
`HudShow(3, 0, QuickSelectInit, QuickSelectUpdate, QuickSelectDraw, 0, 0)`
(`func_L00_00238478`, `func_L00_00238530`, `func_L00_00238C30`). Init
copies the eight saved item words `D_00141FA0` (save block 0xD, 0x20 bytes)
into the level's eight slot records (level data, `0x17DBC0` in level 0,
0x1C each: +0 icon id from the item table's record +0x38, +8..+0x14 the
d-pad neighbours, +0x18 item id) and sets the count `0x15FB48` = 8 and the
set pointer `0x15FB78` -> `gp-0x74E8`, a table of slot arrays indexed by
`gp-0x74F4` (0; Giant Clank's ring, `func_L00_002394A8`/`00239510`, has
its own four-slot array right after). Update: a 30-frame wait until the
stick is centred or the d-pad is pressed, then the stick (> 0.9 deflection,
angle -> slot, 0.2 hysteresis) or the d-pad neighbours pick slot +0x74; an
empty slot is skipped in the direction of travel; on Triangle's release the
slot's item becomes the hero's pending item (hero + 0x20B8; 0x18 sets hero
+ 0x1FF5 instead), the use statistics at `0x141948 + 0xA0` are bumped, and
the slot fades out over 8 frames (+0x70). Draw: a four-quadrant darkening
disc (icon 0xE934) over the 210x200 box whose origin `func_L00_00236400`
derives from the slot's anchor flags, the icons (helper
`func_L00_0023BAB8`: centred, natural size 32) on a circle of radius 74 x
1.05 about (origin + 105, + 100), the highlight box 0xE99C pulsing with
`0x15F6B0` mod 30, and the selected item's name (split at '-' or ' ' into
two lines) and ammo in the centre. Its constants are gp-relative level
data at two different offsets across the levels (`-0x74F4` in 17 levels,
`-0x74E4` in 5 and 16), which the edition does not read: the slot records
and the item table come from Init's own lui/addiu pairs
(`tools/modseg.py` LEVEL_DATA `qs_slots`, `item_table`). The pad's
unmasked words are what the ring reads: held `D_0013CA40 + 0x1C0`, pressed
`+0x1C4`; L1 is 0x4, R1 0x8, Square 0x80 (NTSC decomp `pad_state.h`).

**The pause tab.** The pause menu's tabs are Weapons, Gadgets, Quick
Select, Items, Help, Options (rows of the list at level 0 `0x1B2990`, 24
bytes apart). The Quick Select page (`0x1CF538` in the executable's data,
kind 5) has ten widgets; the ring's is `{update func_0021D7A0, draw
func_00221B58 (draw_pause_ring, NTSC decomp), enter func_0021D9C8, leave
func_0021DA60}`. Enter copies the eight words into the widget state (+0x30)
and puts the highlight (+0x50) on the first empty slot; leave copies the
state back; update steps the highlight with R1/L1, and on Cross takes the
item grid's item (the focused grid's `+0x3C` index into its `+0x48` rows
of 10 bytes, id at +6), removes it from any slot it already had, puts it in
the highlighted slot and advances. Draw puts the icons on a circle of
radius min(w, h)/2 - 40 about the widget's centre (+0x20/+0x24), with the
L1 and R1 glyphs (`lit.rodata 0x1603D8`/`0x1603E0`: codes 0x14, 0x15) at
the top corners. The button box is the widget `{draw func_0021C1B0, +0x30
0x4002, +0x34 -> rows}`, rows of 12 bytes `{s16 string id, ...}` ending in
0: `{0x4EEC "[X] Equip", 0x4EE0 "[triangle] Exit"}`. Font control codes:
0x10 Cross, 0x11 Circle, 0x12 Triangle, 0x13 Square, 0x14 L1, 0x15 R1,
0x18/0x19 the sticks (from the game's own strings).

**Where the pages are saved.** Retail code reads the eight words whole, as
item ids, so they cannot carry more (an edition save with packed bytes
would send a retail game's ring reading item records far past the table).
The save file is a header with the two block tables' total sizes
(`func_00209CE8` rejects any other) and id-tagged blocks (`D_001A05C0`, 47
global blocks of `{address, size, id}`; `D_001A08C0`, 11 per-level blocks
saved 20 times; `func_0020BAD8` sizes a table, `func_0020BD70` unpacks by
id), so a new block changes the file size and the retail game would refuse
the save. Instead the two stored pages and the page number live in the
saved statistics block `D_00141948` (save block 0x11, 0x120 bytes): a fixed
table of event counters `{u16 count (capped 0xFFFE), u16 time, u32 level
bits}` that nothing displays, every reader and writer of which (696
references across the executable and the 19 level programs, all by constant
offset) stays below `0x141A4D`. Its last three records, `0x141A50`-
`0x141A67`, hold the next page (8 bytes), the previous page (8) and the page
number (`EE_QS_PAGE_NEXT`, `EE_QS_PAGE_PREV`, `EE_QS_PAGE_INDEX` in
src/modseg.h). The active page stays in the eight words. A retail game
loads such a save whole, shows the active page, and keeps and re-saves the
other two untouched; the edition loads retail saves with the two pages
empty. The new-game reset zeroes both blocks. The challenge-mode carry-over
(`func_00227DB0`: copies the ring and the items to the scratchpad, calls the
reset `func_00209DC0`, copies them back validated) resets the statistics,
so its reset call (`+ 0x140`, `EE_SITE_QS_CARRY`) goes through
`ee_qs_carry`, which keeps the 24 bytes across it. Items no longer owned
leave the stored pages whenever they are about to be shown.

**Code** (`src/quick_select_pages.c`, `src/qs_hooks.s`, `tools/gen_qs.py`,
feature `quick_select_pages` in `tools/overlay_patch.py`; all 19 levels):
- The HudShow call's update and draw arguments (the `lui`/`addiu` pairs
  into $7 and $8; `gen_qs.py` finds the one site per level by the three
  callbacks' addresses and the call) load `ee_qs_update` and `ee_qs_draw`.
  They learn the level from their return address (`src/qs_hooks.s`):
  HudUpdate (`func_L00_0023B610`) and HudDraw (`func_001FFFB8`) call the
  slot's callbacks from one `jalr` each, at + 0xFC and + 0x5C in all 19
  levels. Update: with Triangle held, R1 or L1 pressed rotates the
  pages (active <- next <- previous <- active, or the reverse) and refills
  the slot records as Init does; then the game's update runs. Draw: the
  game's draw, then the two glyphs at the box's left and right edges, the
  ring's own fade on their alpha. q3's "ring open" test now compares slot
  3's update callback with `ee_qs_update` (`ee_qs_ring_open`), and the pad
  hook (src/strafe.c) clears L1 and R1 from the masked words while it is.
- The pause tab's ring widget record (found in each level's data by its
  four callbacks) runs the level's stubs `ee_qs_pause_update_NN` /
  `ee_qs_pause_draw_NN` (a callback reached through data has no call site to
  name the level). The update wrapper flips the page on Square, writing the
  state back to the words as leave would and reloading it as enter does,
  with the slot-step sound (`func_0022ED80(1, 0x11, moby)`); the draw
  wrapper prints "Page N" (strings 0x540A-0x540C) centred after the game's
  draw. The button box widget is shared with the Weapons and Gadgets pages
  (each page record lists it), so the Quick Select page's entry for it, the
  one right after the ring widget's, goes to a per-level copy
  (`build/modseg/gen_qs.c`, `ee_qs_prompt_widget_NN`) whose row pointer is
  `ee_qs_prompts` (three rows; string 0x5409 "[square] Next ring"). A first
  build patched the shared widget's row pointer and Weapons showed "Next
  ring" too.
- In the acquisition function (`func_L00_002618D8`), the six words after
  the search for an empty slot (`+ 0x1EC`: the index test and the store into
  the active page) become a call to `ee_qs_autoslot`, which stores there or,
  with the page full, in the first empty slot of the next page, then the
  previous.

**Fix (2026-10-18): the ring froze Batalia.** The owner's Challenge Mode
save in Batalia froze when the ring was opened. The first build named the
level from the slot record's address, matched against the 19 levels' HUD
slot tables, but several levels put that table at the same address
(`0x17E4D0` in levels 6 and 8; also 0/7, 1/10/12/16, 2/15, 4/9, 11/18),
so in Batalia (level 8) the hooks took level 6 and called level 6's
QuickSelectUpdate and Draw addresses inside Batalia's program: slot 3 was
registered but never faded in, and the game hung. Novalis (level 1, the
first of its group) happened to work. The hooks now take the return
address, like the edition's other hooks; every hooked function's address
is unique per level (only two catalogue functions share one, neither
used). Verified on a copy of the owner's card: Batalia slot 4 (Challenge
Mode), the ring fades in with its L1/R1 prompts, R1/L1 turn pages, the
stick and release equip the Bomb Glove, the game runs on; then Novalis
slot 1 loaded from Batalia's pause menu, the ring and both pages as before.

**Verified in PCSX2** (the owner's slot 1, Novalis, a retail save, on a copy
of the memory card; the owner's card untouched):
- Loaded: page 1 = the save's eight weapons, pages 2 and 3 empty.
- Ring held: R1, R1 went to pages 2 and 3 (empty rings), L1, L1 back to
  page 1; the stored bytes rotated as designed each time. L1/R1 glyphs
  beside the box (the L1 glyph was clipped at the screen's left edge;
  now kept on screen).
- Pause > Quick Select: "Page 1" in the ring's centre; Square gave Page
  2, 3, 1; the prompt box listed the three rows. Bomb Glove and Pyrocitor
  equipped on page 2 while still on page 1.
- Back in game the ring opened on page 2 with those two; the stick to
  the Pyrocitor and release equipped it (0x141760 = 16).
- Bomb Glove put on page 3; saved to slot 1; the 24 bytes and the active
  page cleared in RAM over PINE (test only); slot 1 loaded: all three
  pages and the page number came back from the card.
Not yet run: a retail game loading an edition save (no scripted input on
the retail image; the owner's test), the new-weapon overflow to the next
page (needs a fresh weapon with a full page), Challenge Mode carry-over.

**Ghost rings (owner's stretch goal, 2026-10-18): built, then removed.**
Faint, shrunken copies of the neighbouring pages beside the ring (the next
page right, the previous left, five far-side icons each) were built and
tuned in PCSX2 (commits d0b0ce9, 0ff040b; there in git history if wanted
again). Two things learned: QuickSelectDraw puts slot i at i x 45 degrees -
180 + 90 (FastAddRots(a, pi/2), `func_001FA748`), so slot 0 is at the top
and the slots run clockwise; and the left ghost needs the ring moved right,
as the retail ring sits near the screen's left edge. The owner found the
result too busy and asked for the ring as it was, at its retail place.
**R1 prompt.** The plain ring keeps the owner's earlier note: R1 sits at box
x + 201, the same small gap (about 3 pixels) to the right-hand icon as the
clamped L1 leaves on the left (R1 had sat about 13 pixels out).

## Health display: the lower backing with Premium/Ultra Nanotech — fixed (2026-10-18); working per the owner

**Retail's bug.** HudHealthDraw (`func_L00_0023A1A0`, every level; decompiled
in OpenRAC's `src/overlays/shared/hud_00235960.c`) takes the orb positions
from a table chosen by the orb count (`gMaxNanotech`, `0x15EEA0`): 4 orbs in
one row (`D_0015F8D8`), 5 as 3 + 2 (`D_0017E560`) or 8 as 4 + 4
(`D_0017E578`), the second row at dy = 32 in both two-row tables. The backing
bar (icon 0x7580: caps and a stretched middle) is 32 px tall and drawn at
the row's y + `D_0015F8F0` (-16), centred on the orbs; the second bar is
offset from the first by `D_0015F8EC`, which is 16. So the second bar sat
16 px too high, half behind the first, and the lower orbs hung below it.
All 19 levels hold the same values (`D_0015F8EC` is `gp-0x7414`, or
`gp-0x7404` in levels 5 and 16); nothing else reads it.

**The patch.** `tools/overlay_patch.py` feature `health_backing` (default):
the delay-slot load of that offset (`lw $2, D_0015F8EC($gp)` at function +
0x2DC, after `beqz $19` that skips the second bar for 4 orbs) becomes
`addiu $2, $zero, 32`. One word per level, checked against the branch and
the load before patching.

**Verified in PCSX2** (owner's Mcd003 slot 1, Challenge Mode Novalis, max
nanotech 8; the display shown with L3, which the frame-start code
`func_L00_00299250` turns into HudShowHealth): with the retail word written
back over PINE the two bars overlap and the lower orbs sit half off theirs;
with the patch each bar is centred on its row. Then on Veldin 2 (level
18) with max nanotech and health set to 5 and to 4 over PINE (restored to
8, nothing saved): 5 orbs, retail leaves the lower orbs half off their bar
and the patch puts the bar behind the whole lower row; 4 orbs, one row and
one bar, identical either way (the second bar is skipped). The owner
confirmed the fix.

## Item preview: no flash of an unowned item on the Gadgets page — fixed (2026-10-18), awaiting owner test

**The bug.** The owner reported it: on the Gadgets page, moving the cursor
from an empty cell to an owned item showed the empty cell's item model for
one frame before the new preview loaded. It happened in Head Items, Foot
Items and when moving up or down between rows, never inside Hand Items or
Packs. Retail has the same flaw; the owner probably owns every hand item
and pack, so those rows never had an empty cell to leave. The preview
binding (`func_0021F238`; NTSC decomp `update_item_preview_binding`)
builds a preview moby for whatever cell is under the cursor, owned or not.
The draw (`func_0021F610`; NTSC `draw_available_item_preview_mobys`) hides
it by testing the owned flag (`D_0013D5C8`) of the cell under the cursor,
not of the moby's own item. The binding runs before the cursor moves in a
frame, so on the frame the cursor reaches an owned cell the old moby is
still there. The draw passes the owned test and shows the unowned item.
The binding replaces the moby on the next frame. The Weapons page uses the
same two routines.

**The patch.** `tools/overlay_patch.py` feature `item_preview` (default)
rewrites `func_0021F610` from +0x44 to +0x78 in all 19 levels (14 words,
same size, checked word for word against retail before patching; the
prologue, the cursor-cell lookup and the epilogue stay). The cursor test is
kept, so leaving an owned item for an unowned one is still hidden at once.
The primary moby then goes through `ee_preview_shown` (`src/item_preview.c`),
which returns it only when its own item (moby pvars + 0xC, which the binding
sets) is owned. Otherwise the draw returns 0, as retail does for an unowned
cell: nothing drawn and no blit. The secondary moby (two-part previews) is
drawn as before, only after the primary.

## q4: 16:9 (widescreen) — implemented (2026-10-19), verified in PCSX2, awaiting owner test

**Setting.** `EE_SETTING_WIDESCREEN` in src/modseg.h = `0x15EEBA`,
`gCheats[10]` (saved with the file, 0 in every retail save; see q3 for why
bytes 8-11 are free). A fourth row of the pause menu's Camera page,
"16:9 (widescreen)" (string 0x540D), on/off. The Camera page's list widget
(page +0x40) holds entries of six words `{label, value byte*, names[4]}` and
shows `names[*value]`, Cross cycling the value; `tools/gen_menu.py` copies
each level's three retail entries into the mod segment with the new one
appended (`{0x540D, &gCheats[10], "off", "on"}`) and the widget's +0x34 is
pointed at the copy. Rows are spaced `height / (rows + 1)`, so four fit the
retail panel. The frontend (title, main menu) is not patched and stays 4:3
(owner's choice); a save with the setting on comes up in 16:9.

**How 16:9 works on a PS2.** Anamorphically: the game still draws its
512-pixel-wide frame and the television (or PCSX2 at 16:9) shows it a third
wider. So the 3D view must show a third more to the sides, and every 2D
element must be drawn 3/4 as wide about the centre. Everything below is in
`src/widescreen.c` and `src/ws_hooks.s`, patched by the `widescreen` feature
of `tools/overlay_patch.py` (default build).

**The view (Hor+).** UpdateViewContext (`func_001F3140`) builds every
projection, frustum and culling constant from the view record's horizontal
field of view (+0xB0, 0.63 in play, 0.414 in some cinematics) and a vertical
one derived from it (+0xB4 = +0xB0 x 0.756 at 50 Hz, 0.775 at 60 Hz). Each
level's copy starts with `j ee_view_hook` / `addiu $at, $zero, LEVEL`, and
its read of +0xB0 for the vertical value (+0x68) reads +0xBC instead. For the
full-screen view (screen size = the resolution table's, `0x1519D0`; a render
to a texture has its own size and is left alone) the hook hands the function
+0xB0 x 4/3 with the unwidened value in +0xBC, and puts +0xB0 back after, so
nothing else ever sees the widened value and repeated calls cannot compound.
Going Commando's own 16:9 (`SCUS_972.68` 0x27ABC0) multiplies the vertical
value by 0.75 instead, which crops the top and bottom: departed from on
purpose (owner's request). Occlusion is per camera position, so the wider
view causes no pop-in (none seen).

**2D.** The HUD and text routines append DMA-tagged GIF packets (CNT tags,
VIF DIRECT, PACKED or register-list GIF) to the VIF1 chain through its
cursor (`D_00161000`, level-local; x in 12.4 around 2048). A bracketed draw
records the cursor before and after, and `chain_squeeze` walks what it wrote:
every XYZ2/XYZF2/XYZ3/XYZF3 x (packed, register list or A+D) goes to
`2048 + (x - 2048) x 3/4`, scissors likewise; a GIF tag whose vertices span
the whole screen (fades, tints) is left alone; REF'd data (shared) is never
touched. Bracketed: DrawWorld's HudDraw, draw_help and subtitles
(`func_001F3D78` + 0x4C0/0x4C8/0x4FC), the pause renderer's panel fills,
self-drawing panels, content quads and last pass, the vendor's monitor
quads and its HudDraw (`ee_ws_bracket`, which keeps stack arguments where
the callee expects them).

**The HUD.** The 13 HUD slots (level-local, 0x90 each) have fixed places and
anchor flags (+0x60: 1 top, 2 bottom, 4 left, 8 right; x at +0x50): 0
weapon/ammo top-left, 1 health top-centre, 2 bolts top-right, 3 quick select
left, 4 right-middle, 5-7 bottom row, 8-11 right, 12 centred (x 142). Around
HudDraw, left/right-anchored slots move out by a third of the half width
(85), so after the squeeze they sit as far from the 16:9 edges as from the
4:3 ones (the native port's rule); the quick-select ring moves half that.
Elements slide in and out from their anchored edge by their width + 20, so
the 4:3 edge maps exactly onto the 16:9 edge: the O2 gauge and the other
side meters leave and enter the screen as in retail.

**The pause menu.** Its panels are 14 mobys of class 0x472 in front of the
menu camera (the view keeps their shape). Each page plays a sequence per
panel that moves its root joint between a resting place (|y| <= 11983 on the
disc's data) and a parked one off to the side (|y| >= 15056; the 4:3 edge is
at 16904 at the panels' depth, the 16:9 edge at 22538), so in 16:9 parked
panels showed at the sides and slides stopped short. The renderer's panel
draw (`func_L00_002781D8` + 0xFC) draws from copies of the two key frames
with the root's y remapped: unchanged to 12500, stretched to the 16:9 edge up
to the 4:3 edge, moved out by the difference beyond (one copy pair per panel:
the walker queues frames and evaluates them later). The corners projected for
the panel's rectangle (+0x1EC) move by the same remap. That rectangle is
given back as the 4:3 picture would have it: the renderer lays each panel's
content out in a texture of the rectangle's size, and at 16:9 width the
Camera page's labels ran into their values. The fill and content quad are
then squeezed.

**The vendor.** The six monitor screens (VendorDrawScreens, `func_0023AA38`,
called at `func_L00_002A1540` + 0x3C4) are drawn with the 4:3 view and frame
transform (`func_001F2608`), both rebuilt widened after: the item monitor
draws its moby straight into the monitor's texture with the frame's
transform, which with the wide view put the item a third of the way to the
right. Their quads are squeezed; the other monitors' projections
(`func_L00_002A0F60` + 0x188) get the 4:3 rectangle like the pause panels.

**In-level movies.** Game mode 2 (cinematics) and 6 (a level's arrival and
departure) keep the original framing: the horizontal field drops back to the
original and black pillars cover the rest (DrawWorld's DoGifPaging after the
HUD block, + 0x520, draws them, over the world and under the fades). The
letterbox (bars growing to 24 lines over 24 frames, `D_0015F548`) is not
drawn in 16:9: the view zooms instead until the picture between the bars
fills the height (H / (H - 48)), with pillars for the original width; the
bars' growth carries the change (owner's choice: fill the height, nothing
beyond the original framing).

**Prerendered movies.** The player (Sony's ezmpeg design) uploads each
frame to a texture and draws it with a fixed packet in the core
(`0x151EF0`: TEST/TEX1/TEX0, then 16 sprite strips 32 pixels wide), REF'd
from the end of setImageTag's (`func_0023C5E0`) tag list. The level's call
(`func_0023E298` + 0xF8) goes to `ee_ws_movie_tag`, which points that REF at
a copy with the strips squeezed and two black sprites over the sides: 4:3
with bars, as on a 4:3 television. The frontend's movies are untouched.

**Verified in PCSX2** (16:9 aspect, a copy of the owner's card, nothing
saved): Novalis and Pokitaru gameplay (Hor+, HUD corners, quick-select ring,
weapon/ammo top-left, bolts top-right, health top-centre), the Novalis
arrival (pillarboxed), a letterboxed cutscene forced through the letterbox
request (zoomed to the height, pillarboxed), the pause menu (main, Options,
Camera with the new row, Sound, Load, Goodies, Cinematics, In-Level Movies,
Weapons, map) with its open, close and page transitions, the vendor
(monitors, item model, prices), a prerendered cinematic (4:3, bars), an
in-level movie with subtitles, the O2 gauge underwater. The setting
switches live from the Camera page; 4:3 is unchanged with it off.

**Not seen yet.** The Suck Cannon meter, the ship HUD, boss health bars
(they use the same slots: right-anchored slots go to the edge, slot 12 is
centred), the Visibomb's camera view, the hoverboard races' HUD.

### q4, owner's first round (2026-10-19, commit cce4e1a)

- **Pop-in at the sides.** The first build put the field of view back after
  UpdateViewContext ran. But TfragProc, TieProc, ShrubProc and MobyProc load
  the view record's +0xA0..+0xBF themselves every frame for their side
  planes, so they culled at the 4:3 edges. The widened value now stays in
  the record. On the next call the hook recognises its own value by its
  bits (it keeps the bits it wrote and the game's value they stand for), so
  the widening never compounds. A view built in 4:3 (setting off, a render
  to a texture, the vendor's monitors) gets the game's value back.
- **The HUD's anchored elements move after they draw.** HudDraw calls
  each slot's draw callback (+0x18) through a wrapper that notes the
  stretch of the chain the slot wrote. The squeeze then adds the edge
  offset to those vertices. Moving the slot's x before the draw (the first
  build) broke the ammo display: its draw (`func_L00_0023B140`) passes
  x + width as its backing's width, a slip in the original that is
  harmless at x = 20. The edge offset is 64 + 2 pixels after the squeeze.
  The parked meters (the Suck Cannon, O2 and Morph-o-Ray gauges) sat with
  their left column on the screen's last pixel and showed for a few
  frames. The quick-select ring (the edition's own draw) still moves by
  its slot's x, half the way out, and its L1 glyph's clamp moves with it
  (`ee_ws_ring_shift`).
- **Toggling the setting in the menu.** The pause menu does not rebuild
  the view while it is open. The view is now rebuilt (UpdateViewContext,
  then the frame transform) when the setting differs from the one the
  full-screen view was built with.
- **The fighter jet's HUD** (Pokitaru `func_L11_00313218`, Gemlik
  `func_L13_002BAA68`, Drek's fleet `func_L17_002ED498`). It is a draw
  callback the ship registers every frame, not a HUD slot. Its
  registration's address is pointed at a bracket stub, and each of its
  draws is moved by where it sits (left third out left, right third out
  right, the middle squeezed in place).
- **Paging pairs.** SetupGifPaging leaves a hole that DoGifPaging fills
  with a NEXT out to the texture uploads, which NEXT back. The chain
  walker stopped at the first NEXT, so a draw with its own paging pair
  (the jet's "Quit?" prompt, `func_001FBE80`, called from game mode 4's
  frame at `func_L00_001F9248` + 0x60) stayed stretched. The packets are
  laid out in order in the buffer, so the walk now goes on.
- **The credits** (game mode 7, `func_L00_001F92E8`). The frame copies a
  full-screen picture straight into the draw buffer with an IMAGE transfer
  (`func_001F7680`, + 0x24). A copy cannot be narrowed, so in 16:9 the
  picture is copied into the Z buffer's memory instead (byte 0x1E0000;
  nothing in this mode draws in 3D). It is drawn from there as a texture
  into the middle 3/4 of the frame, with Z writes masked, the Z test set
  to always and black bars at the sides. The Z memory is then zeroed with
  an IMAGE transfer that REFs a 16 KB block of zeros 56 times. The name
  tags (`func_L00_001FCC50`, + 0xC8) are squeezed. The vertical stripes
  between slides are the credits' own transition.

**Verified in PCSX2 (owner and session):** the cull field stays at 0.84
(Orxon pan), the ammo backing, quick select's L1, toggling in the Camera
page both ways, the Pokitaru jet's HUD against 4:3 and its "Quit?" prompt
(owner), the credits, the Suck Cannon meter's slide-out (owner). **Not
watched yet:** boss health bars, the Visibomb's view, the hoverboard
races.

**Ship landings and take-offs stay widescreen (2026-10-19, owner's request).** A level's arrival and departure flights (game mode 6) are no longer
pillarboxed: they get the Hor+ view like play. Cinematics (game mode 2) and
letterboxed scenes keep the original framing with pillars.

### q4, owner's second round (2026-10-19, commit db4a9ac)

- **Hoverboard races** (Rilgar, Kalebo III). The bottom bar's slot
  (`func_L05_00266D10`, slot 5: the bar, Lap, Place) draws at fixed places
  across the whole screen, so moving it whole put Place on top of the
  time. It is stretched instead (`HUD_STRETCH`): a draw narrower than half
  the screen goes to its half's edge; the bar's wide pairs of vertices
  (more than 64 pixels apart) move each end by its own half, so the bar
  reaches both edges. The time (`func_L05_002670A0`, slot 7, anchored
  right) is squeezed in place: moved whole, its minutes and seconds were
  pieces in different halves and parted.
- **The flight to a planet.** The transition (`func_002305A0`, space.c in
  the decomp) runs from the leaving level's program; its planet and stars
  get the 16:9 view like any frame. The planet's name is a 256 x 32
  picture 32 pixels from the left (`func_0022FD20`), bracketed at + 0x140
  and squeezed out to the left edge like the HUD's left elements (walker
  mode `CHAIN_LEFT`).
- **The pause menu's item previews** (Weapons, Gadgets, Quick Select) were
  three quarters as wide as in 4:3. Each panel draws into a render target
  (`func_001F7A50` at `func_L00_002781D8` + 0x458, which builds a view of
  the target's size with a field of 1.0; the panel at + 0x49C), drawn back
  as a quad that the edition squeezes. Mobys are projected through the
  frame's camera block (`func_001F2608`: the camera times the view's
  projection, `D_00187040` and its copy at view + 0x280), which the frame
  built from the widened main view: narrowed by the view and again by the
  squeeze. Before the target is set up the block is now built from the
  main view as 4:3 (the view record up to + 0x280 and the screen record
  are put back as they were), and after the panel has drawn it is built
  from the widened view again. A first attempt rebuilt the view inside the
  target and changed nothing: the target's own view was already the same
  in 4:3 and 16:9 (both records captured mid-pass).

**Verified in PCSX2:** the race HUD (owner); the previews (the bomb-glove
ammo model 130 px wide on a 16:9 output, against 135 expected from 4:3 and
105 before; owner); the planet name (owner); boss health bars and the
Visibomb's view (owner). Every item of the original request has now been
seen in game.

## x2: the hoverboard races' Devastator is item record 36 (2026-10-19) — fixed, verified by the owner

**Owner's report.** In the Kalebo III hoverboard race the game froze at the
first Devastator ammo pickup, in 4:3 and 16:9 alike.

**Cause.** Record 36 is not unused. The races' ammo pickup (class 0x1D6;
NTSC decomp `ui/help/00237488.c`) puts item 36 in Ratchet's hand
(hero + 0x20B8) and adds to its ammo (`weapon_ammo_counts[36]`); the hero
then loads the record's class as the gun (`func_00205270`, the hand
gadget loader: hero code at `func_L00_0020F118` + 0xB0 even tests for 0x24).
With the Charge Boots' class (2000) in the record, the loader found no such
gun among the level's 21 (`0x1B0340`, count at `0x1600C8`), unpacked from
`D_001CBEA0[21]` = 0 and the game ran into the retail halt routine
`func_L00_0024EC20` (two `teq $zero, $zero`). Read from a PCSX2 state of
the frozen game: Cause 0x34 (trap), EPC `0x24A260`, the thread's ra
(kernel save area) in `func_00205270`, $s2 = 0x7D0. Retail also clears a
hand item 0x24 at the pause entry (`func_00219C70`), and the item switch
treats 0x24 as its temporary item.

**No free record.** Record 0 is "nothing" and record 1 is Clank on the back
(the back slot's "no pack": no name, no icon); a test build that moved the
boots to 1 turned Clank into a boot. Relocating the 37-record table would
mean about 47 code sites per level.

**The swap** (`ee_race_item_frame`, at the start of every gameplay frame,
before the hero update; `tools/gen_items.py` writes record 36's address and
both versions per level). Record 36 holds retail's words while Ratchet is
on the hoverboard (control mode 0x16) or item 36 is in his hand, and the
Charge Boots' otherwise.

- **Worn boots read the same record.** With retail's words in it they were
  attached as the Devastator is, near Ratchet's hand (owner). On the board
  the boots are taken off (pending "no item", 0x26), and the record
  switches only once they are off, unless a pickup has put item 36 in the
  hand. They go back on once Ratchet is in normal control, item 36 is out
  of his hand and the record holds the boots' words again. Putting them
  back the frame he left the board (the race's Devastator still in his
  hand, the record still retail's) loaded a second Devastator into the
  hand gadget's buffer: a second Ratchet appeared and threw the real one
  across the level (owner). A one-second idle wait fixed that but felt
  clumsy (owner); the record test is what makes it safe.
- **The pause menu.** Quitting a race can open the pause menu before any
  gameplay frame has run off the board, and the Gadgets page showed the
  Devastator as the Charge Boots (owner). The mode-0 frame's two calls of
  the pause entry (`func_L00_00299250` + 0x36C, + 0x434) go to
  `ee_pause_entry_c`, which puts the boots' words back first, unless item
  36 is in the hand.
- A boots slot holding 1 (a save from the Clank build) is turned back into
  the boots.

**Verified in PCSX2 (owner, Kalebo III):** races with and without the boots
worn, finishing and quitting, the Devastator pickup, the Gadgets page right
after quitting, the boots back on after a race. Session: records 1 and 36
read back as Clank's and the boots' off the board; the pause hook patched
at both sites.

## Controls page: the Strafing toggle, and "Quick Select pause" (2026-10-19), page and box confirmed by the owner

**What changed.** The Controls page (Options → Controls) has two toggle rows
now: **Strafing** (new, id 0x540E) above **Quick Select pause** (0x5403,
relabelled from "Quick Select Pause" at the owner's request). Movement
comes before items, so Strafing goes first. Strafing is **off by default**
(owner's request): until a player turns it on, L2 and R2 work as in retail
(they mirror L1 look and R1 crouch).

**Setting.** `EE_SETTING_STRAFE` in src/modseg.h = `0x15EEB9`, `gCheats[9]`:
the last free one of the cheat bytes retail never touches (8 Charge Boots,
10 16:9, 11 quick select pause). It is saved with the file, 0 in every
retail save and new game, and no earlier edition build wrote it, so
existing saves come up with Strafing off.

**Menu.** `tools/gen_menu.py` writes the page's toggle entries as two rows,
`{label, &flag, on, off, 0}` each, plus a zero row. The toggle list widget
(update `func_L00_0027FA68` in level 0) walks entries until the zero label;
retail's Help page (kind 0x13, entries 0x4F9F/0x4FA0 on 0x15EF1C/D) is a
two-row page on the same widget, with the same panel sequences as
Subtitles' one-row page, so the panel does not size itself by row count.
Our page keeps the vibration template's wider panels (sequences 118-123),
but its list panel (123) ends one row high, half-height 971 against
Subtitles' 1441, and the owner's first test showed the two rows filling it
to the edges. Retail uses 123 for the cut vibration page only, so
`ee_controls_panel` (src/quick_select_pause.c) raises the last key frame's
corners (joints 1-4, z) to 1441 in the level's class data the first time
a pause panel draws (called from the panel draw hook in src/widescreen.c,
in 4:3 too). The opening frames before it already reach 1440-1518, so
the box now settles the way Subtitles' does.

**Gate.** `ee_pad_parse_c` (src/strafe.c) returns straight after the game's
parse (and the q2 ring's L1/R1 masking) while the setting is off: L2 and R2
stay in the pad, and `ee_strafe_held`, `strafe_grace` and `strafe_flip`
are zeroed. Every strafe hook keys on `ee_strafe_held` or `strafe_grace`,
the camera patch (`strafe_camera`) included, so they all fall back to the
game's own code.

**Verification.** `bash tools/pack.sh --quick` built; the generated entries
are right in all 19 levels. Not run in PCSX2 (owner's machine had been in
use); owner test: the Controls page shows both rows without clipping, each
toggles on its own, strafing works only with its row on, L2/R2 crouch and
look with it off, and the setting survives a save and load.
