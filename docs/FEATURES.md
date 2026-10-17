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
title, "Quick Select Pause" (0x5403) for the toggle row and "Adjust
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

## x3b: Free movement after the wrench throw — implemented (2026-10-17), awaiting owner test

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
