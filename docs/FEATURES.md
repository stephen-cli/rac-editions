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
