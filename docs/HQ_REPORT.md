# Executive Edition (R&C1): HQ report

Running report from the development account, for carrying back into the HQ.
Newest entries first. Format per the handoff pack: item, new status, what
changed, decisions taken, findings that affect other items.

## 2026-10-18 (x2: the Charge Boots dash, and the boots' ankles)

### 🥾 Charge Boots (x2) — Going Commando's charge camera ported (owner's report), verified in PCSX2; awaiting owner test

**What was wrong.** When Ratchet charged back toward the camera, R&C1's camera stayed where it was, so he often bonked into it. In Going Commando the camera swings round behind him at once.

**What changed.** The charge now uses Going Commando's camera: every frame of a charge it turns toward Ratchet's facing, pulls in to Going Commando's closer distance, and uses its sideways settling. R&C1's camera has the same building blocks as R&C2's, so these are Going Commando's exact settings. Measured with a U-turn mid-charge: R&C2's camera is behind Ratchet after 0.6 s, and the edition's after about 0.7 s, following the same curve. The unchanged R&C1 camera had not moved at all after 0.3 s, and Ratchet bonked.

**Findings.** The owner felt Going Commando's jets were more "flowy", like a tail. Measured in R&C2 itself (the owner's Oozla state, the charge forced), its jet sprites ride on Ratchet exactly as the edition's do: the same distances behind the ankles, the same height, and they swing with him in a turn. No jet code was missed. The likely difference was the camera: with Going Commando's camera behind him, the plume streams back toward the viewer instead of swinging sideways.

### 🥾 Charge Boots (x2) — Jets redone with Going Commando's own effect (owner's reports), verified in PCSX2; awaiting owner test

**What was wrong.** The first jets used an R&C1 "glow" that turns out to be a small 3D sphere rather than a particle, so they read as blue bubbles. The owner asked whether R&C1 had something closer to Going Commando's blue jets. An intermediate build then drew the plume straight down; the owner pointed out it should trail back toward the camera. That came from misreading one value in R&C2's code.

**What changed.** The jets are now Going Commando's own effect, rebuilt from its code. R&C2 draws them with the particle engine it inherited from R&C1, so R&C1 has the same pieces: the same faint cloud texture (the same image in the same slot, in every level) and an additive sprite type. Everything else uses R&C2's numbers: two sprites a frame at each ankle, one blue and one cyan, blown backwards out of the heels and turning with Ratchet, growing and darkening as they fade. Measured in the emulator, the trail runs from the ankle to about two units behind, at ankle height.

**Decisions taken.**
- The edition repeats R&C2's per-sprite update every frame on the sprites it spawns, because R&C1's nearest particle type has no growth, no colour change and does not move with Ratchet.

**Findings.** Going Commando kept R&C1's particle texture slots: texture 12 is the same cloud in both games. That helps if other R&C2 effects are ported later.

### 🥾 Charge Boots (x2) — Going Commando's dash on R1's double tap implemented and verified in PCSX2; awaiting owner test

**What changed.** With the Charge Boots on, double-tapping R1 (and holding it) charges as in Going Commando: R&C2's own animation, its speed curve (a fast first second, then about twice running speed while R1 is held), its weak-then-normal steering and lean, the hover just above the ground, enemies hit along the way, a wall bonk, and a skid to a stop when R1 is let go. The boots show blue jets at the ankles. With the Thruster-Pack also on the back, the double tap belongs to the boots, as the owner asked; with other boots on, it is the Thruster-Pack's hover as before. Applied to all 19 levels.

**Findings.** Going Commando built the Charge Boots out of R&C1's Thruster-Pack: the same double-tap test at the end of the same input routine, the same hover spring and sound slot. R&C1 has every building block under its own name, so the port is R&C2's charge written in R&C1's terms rather than new physics. R&C1's movement step caps a frame's travel at Ratchet's collision radius, as R&C2's does, so at PAL's 50 Hz the burst tops out at 21.5 units a second where NTSC reaches 25.8.

**Decisions taken.**
- The charge sound is the Thruster-Pack's jet loop: R&C2's charge sound is not on the R&C1 disc.
- The jets are R&C1 glow sprites (R&C2's particle type does not exist in R&C1), sized against Going Commando itself: the owner's R&C2 savestate on Oozla, with the charge forced from the emulator.
- R&C2's charge camera tweaks are not ported; the follow camera keeps up.

### 🥾 Charge Boots (x2) — Worn boots now sit flat (owner's report fixed), verified in PCSX2

**What was wrong.** The Charge Boots tipped with Ratchet's shins and their soles went through the floor. The game poses worn boots from Ratchet's leg (knee, ankle, heel, ball, toe) only for two items, the Magneboots and the Grindboots; every other item is drawn rigid on the knee. A first fix re-skinned the boot to follow the ankle, and the owner rightly reported no change: the skin never mattered while the game skipped the pose.

**Fix.** The pose test now accepts the Charge Boots, and their skin follows the ankle below the ankle as the other boots' does. Side views of idle and crouch in the emulator show the soles flat on the floor, the cuff following the shin, as with the Magneboots.

## 2026-10-17 (x5, fourth increment: flips, twist, backpedal)

### 🎯 Strafing on L2/R2 (x5) — Strafe stop measured and finished

**What was wrong.** The previous build did stop Ratchet dead, but only while the trigger was still held; letting go of trigger and stick together (the natural motion) still skidded, because the trigger reads as released a frame or two before the game decides the stop. Found by measuring the stop frame by frame in the emulator rather than by inspection.

**Fix.** A six-frame grace after the trigger is released, and the stop also clears the momentum vector that actually carries the slide. Verified in the emulator: released together or trigger kept, Ratchet freezes at the release point; a plain run stop keeps its skid.

**Note.** The working tree also holds another session's in-progress Charge Boots work; this commit touches only the strafe files, and the image built from the tree includes that work as it stands.

### 🎯 Strafing on L2/R2 (x5) — Working; stop-on-the-spot while strafing added, awaiting owner test

**Owner's verdict.** "It works wonderfully." One request: no high-speed foot slide when a strafe ends.

**What changed.** Letting the stick go while a trigger is held now stops Ratchet where he stands instead of sliding to a halt; the normal run keeps its skid. A first cut only swapped the skid animation and still let him slide, which the owner spotted in play; the stop now also kills the speed, so the game's own stop state hands straight to idle.

### 🎯 Strafing on L2/R2 (x5) — Crash found by reproduction and fixed; awaiting owner test

**What happened.** The strafe crash survived the first fix, so I reproduced it myself: PCSX2's scripting interface plus a debug pad in the edition let a script load a save and hold R2 with the stick, and the edition's new hook trace named the hook that died. The cause was three engine helpers the strafe called at the addresses they have in the boot executable; in R&C1 only memory below a fixed line stays put inside a level, and each level keeps its own copy of those helpers elsewhere, so the calls landed in level data. They now go through the same per-level tables as everything else.

**Verified in the emulator.** Strafing in all directions, strafe side flips, strafe jumps and a plain crouch flip run without an exception; the twist and the converted backward step are visibly in effect in memory. Your PCSX2 settings were put back exactly as they were.

**Kept for next time.** The scripting harness and a short debugging guide are in the repo, so the next strafe-class bug gets reproduced in minutes rather than by round trips.

### 🎯 Strafing on L2/R2 (x5) — Going Commando's backward step converted and in; awaiting owner test

**What changed (same day, after the crash fix).** The stand-in is gone: strafing backward now plays Going Commando's own 30-frame backward step. R&C2 stores Ratchet's animations in a packed per-frame format its engine unpacks on the fly; the edition now carries a transcription of that unpacker (`tools/rac2_anim.py`), proven by decoding R&C2's run, walk and jump cycles and matching them against R&C1's copies of the same animations to within the format's own quantization. The build reads the cycle from the owner's R&C2 disc and converts it into R&C1's frame layout; nothing is hand-authored.

**Decisions taken.** The converter is general (any R&C2 Ratchet sequence can now be brought over the same way, 37 KB per 30 frames in the resident segment), which also opens R&C2's other hero animations should later items want them.

**Verification.** Rebuilt; converted frames compared byte for byte in the built segment; install checked in disassembly. Owner test: the backward step while strafing back; the rest as before.

### 🎯 Strafing on L2/R2 (x5) — R&C2's flip motion, the run twist and a stand-in backpedal; crash fixed, awaiting owner re-test

**Fix (same day).** The owner's first run of this build crashed as soon as a trigger was held while moving. The stand-in backpedal was placed in an animation slot beyond the space the hero's class actually reserves and overwrote the start of its mesh data; it now uses a slot inside that space, with a guard that skips the install if there is no room. A second, not yet triggered, wrong helper address in the flip's animation blend was fixed at the same time.


**What changed.** Three things the owner's test asked for. Strafe flips now move as in Going Commando: they travel with the stick from take-off instead of hanging in the air, and left/right/back are read against the camera, so a sideways flip is a side flip. Strafing on foot now twists Ratchet the R&C2 way: legs toward the movement, torso toward the camera, over R&C1's own walk and run cycles. Strafing backward plays a backward step.

**Decisions taken.**
- Both games have the very same four flip animations (checked against the R&C2 data), so nothing was imported for the flips; the difference was motion and classification, both ported. Crouch flips outside the strafe keep R&C1's behaviour.
- R&C2 has no separate strafe run cycles, only this twist; porting it reuses R&C1's own joint-ease table, no animation data involved.
- The backward step is a stand-in: R&C1's run cycle played in reverse, built at run time. R&C2's real 30-frame cycle is stored in a packed frame format the engine decodes on the fly; bringing it over means decoding that format, a separate piece of work I can take on next if the stand-in is not good enough.

**Verification.** Rebuilt; table addresses, hooks and all patched sites checked (details in FEATURES.md). Owner test: twist while strafing sideways, the backward step, side/back flips travelling with the stick; crouch flips, normal run and hover unchanged.

## 2026-10-17 (x5, third increment: jumps and flips)

### 🎯 Strafing on L2/R2 (x5) — Strafe jumps and flips implemented, awaiting owner test

**What changed.** Jumping while strafing now behaves as in Going Commando: sideways gives a side flip, backward a back flip, forward a normal jump, and through any jump Ratchet keeps facing the camera and drifts along the stick. The flips are R&C1's own crouch flips (its state 0xB and animations), which is exactly what R&C2 reuses; no new animation was needed for them.

**Decisions taken.**
- Two items from the earlier plan were dropped after reading R&C2's code: there is no "turn to face the camera while standing still" and no slower backpedal in Going Commando. Noted in FEATURES.md so they are not chased again.
- One small divergence kept: a forward strafe jump at full run speed still gives R&C1's run-jump animation; R&C2 forces a plain jump there. Not worth a fragile patch.

**Verification.** Rebuilt; hooks and patched sites checked by disassembly in three levels and found by pattern in all 19 at build time. Owner test: side and back flips from a strafe, forward strafe jump, facing held through a jump, crouch jumps and the thruster hover unchanged.

**Next.** The strafe animations (R&C2's cycles carried into each level's Ratchet), the last open piece of x5.

## 2026-10-17 (x5, second increment: the camera)

### 🎯 Strafing on L2/R2 (x5) — Camera hold implemented, hover protected; awaiting owner test

**What changed.** While a trigger is held, the follow camera no longer swings behind the direction Ratchet moves: it keeps its direction and only the right stick turns it, as in Going Commando. Released, the normal camera is back the same frame. R&C1's camera code turned out to carry R&C2's strafe camera already, used by the thruster pack's hover (its own primitive strafe); the edition reuses that very block for strafing on foot, so no camera behaviour is new code.

**Decisions taken.**
- The owner flagged the thruster pack's hover: it must keep its retail logic and controls. It does, and this build also repairs it: the first increment's trigger remap had hidden L2/R2 from the hover too, so hovering had lost its retail strafe; the triggers now stay visible to the game while hovering.
- Only the condition of the existing camera block changes (three words per level); the camera's stiffness logic and right-stick turning stay retail.

**Verification.** Rebuilt; patched words checked by disassembly in four levels and the pattern found once in every level at build time; the pad hook checked for the hover exception. Owner test: strafe sideways and back on foot (camera should hold); release (camera should follow again at once); hover with L2/R2 held (as in retail).

**Next.** Strafe jumps and flips, the standing stance and the slower backpedal, then the animations (R&C2's cycles carried over into each level's Ratchet).

## 2026-10-17 (x5, first increment)

### 🎯 Strafing on L2/R2 (x5) — Movement implemented, awaiting owner test; animations and camera to follow

**What changed.** Holding L2 or R2 now strafes: Ratchet faces where the camera looks and moves in the direction of the stick, as in Going Commando, which this copies rule for rule from its hero code (camera yaw as the facing, the stick heading as the velocity, R&C2's turn rates). L2 and R2 no longer act as look and crouch; L1 and R1 keep those. Applied to all 19 levels.

**Decisions taken.**
- The triggers are freed at the pad layer: the game never sees L2/R2 any more, which avoids touching the dozens of button tests in the hero code where they mirrored L1/R1.
- Holding both triggers strafes like one; R&C2's lock-on strafe is not ported, as asked.
- Delivered as an increment: movement first. Strafe animations (R&C1's Ratchet has no side-step or backpedal cycles; the run cycle plays for now), the standing strafe stance, the slower backpedal and the camera hold come next; the animation step needs R&C2's cycles carried over, which hinges on the two Ratchet skeletons matching.

**First owner test (2026-10-17): every input dead after loading a save, Ratchet running on his own.** Cause: the pad parse routine takes three arguments (pad, raw report, length) and the hook's trampoline put the return address into the second, so the game parsed its buttons and stick from garbage every frame. Fixed the same day (return address in the fourth register, all three arguments passed through); the hook's prologue checked in disassembly. Re-test pending.

**Second owner test (2026-10-17): movement feels right, but Ratchet faced a fixed world direction while strafing, R2 still gave crouch-gated pack jumps, and R&C1's camera fights the strafe (it follows the direction of movement).** Two fixed the same day: the camera yaw is executable data that moves per level (level 0's address was used everywhere; now read per level from the game's own stick code), and R2 survived in the pad's 30-frame pressed history that the pack-jump checks scan (now scrubbed with the live fields). The camera is the next increment: Going Commando switches its camera into a strafe mode (a flag on the camera object plus two follow rates, 0.04 and 0.2) that holds behind the hero's facing; R&C1's follow camera has to be found and given the same rule. Re-test of facing and jumps pending.

**Verification.** Rebuilt; the hooks' disassembly checked (pad fields cleared, camera yaw, frame globals and rates); all 12 patched sites per level checked against retail. Owner test: hold L2 or R2 and push the stick sideways and back; Ratchet should keep facing the camera direction and slide; release to run normally; L1 look and R1 crouch unchanged; watch how the camera behaves while strafing sideways, which decides the next step.

## 2026-10-17 (x3b)

### 🔧 Free movement during the wrench throw (x3b) — Implemented, awaiting owner test

**What changed.** After a Comet-Strike (crouch + Square), Ratchet can run again as soon as the wrench has left his hand, instead of standing through the whole throw and catch. Pushing the stick past 0.9 from animation frame 34 cancels the throw into a run; the wrench returns to him wherever he is. Applied to all 19 levels.

**Decisions taken.**
- Ported the rule exactly as Going Commando has it. Both games share the same throw movement code to the constant (same deceleration, same release frame 33); R&C2 only adds the cancel-into-run in its state update, so that is all the edition adds.
- Not ported: R&C2's second way to throw, Circle with the wrench equipped (its state 0x83), and the auto-aim that goes with it. R&C1 has no input for it; it can be added later as its own item if wanted.

**Verification.** Rebuilt; hook disassembly and all patched words checked. Owner test: crouch + Square, push the stick during the throw, Ratchet should break into a run right after the wrench leaves; without the stick he finishes the throw as before; the wrench still comes back and still picks up bolts.

**Findings affecting other items.** The hero's per-state movement switch, state update and state setter are now mapped in both games (R&C1 PAL level 0: movement `func_L00_002180F8`, setter `func_L00_00222B80`, state at hero + 0x2084; R&C2 Oozla: 0x2BA208, 0x2BEB90, hero + 0x2294) with the NTSC decomp's C for R&C1. x3a (turning) and x5 (strafing) start from the same places.

## 2026-10-17 (q3 round 3: Going Commando's timings)

### ⏯️ Quick select pause setting (q3) — R&C2's timings adopted, awaiting owner re-test

**What changed.** The owner asked for Going Commando's quick select and hot-swap timings instead of the edition's guesses. Read from the R&C2 program and the two savestates the owner made: R&C2 opens its quick select, and stops the world, only once Triangle has been held for 4 frames; a shorter tap never shows the ring. Its hot swap counts a release within 20 frames of the press as a tap and swaps on a second tap within 20 frames of the first. R&C1's own hot swap already uses those two 20-frame timers, so the edition adopted the one number that differed: the world now stops after 4 held frames instead of 10. The missed-release problem that made the double tap unreliable with a short grace is handled directly: when a hold of up to 20 frames ends while the world is stopped, the release is fed back to the pad state so the hero sees it one frame later, exactly as it would have.

**Decisions taken.** The ring still appears on the press as in retail R&C1; R&C2 also delays the ring's appearance to the 4th frame, which would need a patch to R&C1's hero code and was not requested. Long holds are not replayed, so releasing after browsing the ring never arms a swap window.

**Verification.** Rebuilt; hook disassembly and all patched words checked. Owner re-test: double tap at normal speed and slowly (taps up to a third of a second), hold and release, the feel of the pause starting about a twelfth of a second into a hold.

**Findings affecting other items.** R&C2's quick select is an overlay game state, not a HUD item; its world pause is the state change itself. The R&C2 level programs carry the hero and gameplay code, as R&C1's do; OpenRAC's `tools/levels.py dump` and `config/levels/functions.tsv` map them, which x1 (Challenge Mode Plus) and x5 (strafing) may need when cross-referencing the sequels.

## 2026-10-17 (q3 round 2)

### ⏯️ Quick select pause setting (q3) — Fixed three owner findings, awaiting re-test

**What changed.**
- Double tap (swap to the previous weapon) works again with the setting On: the world now keeps running for the first 10 frames of a hold, so a tap never pauses. The double tap is handled by the hero's own update, which the pause freezes, and it reads Triangle's release as a one-frame edge; frozen on that frame it never saw the release.
- Releasing Triangle resumes the game immediately; the ring's short fade-out plays over a running world instead of a paused one.
- The setting moved to a byte retail never writes (the unused tail of the cheat flags block, saved with the game), so old saves read Off. The previous byte was a level pickup flag in the special-items array, which is why very old saves (Mc003) showed On.

**Decisions taken.** Grace period of 10 frames (200 ms at 50 Hz) rather than a fixed fraction of the ring's own 8-frame fade-in, so it stays above a tap's length; one constant in the source. The byte's address is defined once (src/modseg.h) and the menu generator reads it from there.

**Note for the owner.** Saves where the setting was switched On with builds before this one carry a 1 in special item 120 (the old byte). What pickup that id denotes is not identified; it is harmless for the setting (now elsewhere) and only matters if that item's flag means something in those saves. Mc001's blocks 4, and any other save switched On earlier, are the ones affected.

**Verification.** Rebuilt and the hook disassembly reviewed; all patched words checked. Owner re-test: double tap with the setting On and Off; hold, release, feel of the fade; load each old save and confirm the setting reads Off; Controls Options toggle still persists after a save and reload.

## 2026-10-17 (load crash)

### 🛠️ Crash loading a second save from the pause menu — Fixed, awaiting owner re-test

**What changed.** Owner report: load a save from the main menu, then a different save from the pause menu, and the game stops with an R5900 "jump to unaligned address" (not with every pair of saves). Cause found in the edition's resident code, present since the first mod-segment build (x4, q3): the hooks found out which level program is running from the game's current-level variable, and loading a save from the pause menu overwrites the save's globals, that variable included, while the old level's program still runs its last frames; a hook then jumped into the old program at the new level's addresses. Pairs of saves on the same level did not crash, which matches the report. The hooks now identify the level from their own call site (the return address, which differs per level program), never from game state; the string hook gets the level id from its patch. Every hook changed; nothing visible changes in play.

**Decisions taken.** Removed the current-level variable from the edition's shared header altogether so no future hook can repeat this; the call-site offsets are defined once (src/modseg.h) and the patch tool reads them from there.

**Verification.** Rebuilt; all 152 patched words checked against retail and against the new hook addresses in every level; the generated code reviewed in disassembly. Not run in-game here (owner's emulator was in use); owner re-test: main-menu load, then pause-menu load of a save on another level, several times, plus Metal Detector 10x, quick select pause, Controls page text and Commercials as regression checks.

## 2026-10-17 (Commercials)

### 📺 Goodies: Commercials (new item, owner's request) — Implemented, awaiting owner test

**What changed.** The Goodies menu gets the NTSC release's "Commercials" row after "'Making Of' Video": a page with the three television ads (Decoy Glove, Devastator, Morph-o-ray), each playable, with previews. It unlocks with all ten Gold Weapons, together with 'Making Of', and shows locked with the Gold Weapons message before that. Applied to all 19 levels.

**Decisions taken.**
- No data was imported from the NTSC disc. The PAL disc already carries the row, the page, the strings, the preview images and the three movies in PAL encodes (the game keeps a PAL alternate next to every Goodies movie). The PAL build hides the row with one extra instruction in the Goodies unlock routine, run when the console's region flag is set; the edition turns that instruction into a no-op (one word per level). The NTSC image served only to confirm that the two routines differ in nothing else.
- Left the row's unlock, wording and movie playback entirely to the retail code, so the behaviour is exactly the NTSC one.

**Findings affecting other items.**
- The PAL program has a region flag (`D_0015EF90`, from SYSTEM.CNF's VMODE line) that gates PAL-only differences in several places. Other NTSC-only content may be a single check away; worth a sweep when x6 (USA boss music) is looked at.
- The table of contents keeps NTSC/PAL pairs for movies and some other groups; a feature that adds or swaps a movie must set both halves.

## 2026-10-17 (q3)

### ⏯️ Quick select pause setting (q3) — Working (owner confirmed the pause, 2026-10-17); polish round built, awaiting re-check

**What changed.** A new row "Quick Select" under Options opens an On/Off page; with it On, the world (hero, enemies, camera, particles, play-time counter) freezes while the quick-select ring is open, as in R&C2 and R&C3; music keeps playing. Off restores the original behaviour. The setting is saved with the game (an unused save flag). Applied to all 19 levels; first owner test pending.

**Decisions taken.**
- The pause uses the engine's own per-group update gate (the debug freeze facility in the frame loop) rather than a new mode, so the ring, HUD, sound and rendering behave exactly as retail while the world stops.
- The menu row reuses the game's own page and widget records (a copy of the Subtitles page with our flag), generated per level because menu data is level-local. Label text reuses the existing "Quick Select" string; a proper "Quick Select Pause" label waits on text-table tooling (shared need with q2).
- Setting stored per save file (save flag 120), default Off.

**Second owner test (2026-10-17): the pause works ("a very useful feature"); asked for the row to be named "Controls" between Load and Sound, the page "Quick Select Pause", a description "Adjust controls settings", and the Help page's Controls preview; also reported that no Options previews loaded.** Done the same day: a text-table writer now adds the edition's strings (five languages) and the image is appended to the disc with the table of contents repointed; the preview table is built from the disc's table of contents (the first copy came from the executable file, where the game fills it at boot, so it was all zeros). The first image of that round stalled at the end of the intro: the game's stream reader reads a sector past a file's end, which at the end of the disc image is a read error; appended files now get a blank-sector margin. Verified booting to the title with a clean log. Third owner check (2026-10-17): previews back and pause fine, but the new labels read "Paradox: This message does not exist" and the preview was Help Log's. Both fixed: strings are now served by a resident replacement of the game's string lookup (a level does not keep the global text table loaded, so adding to that file was not enough), and the preview is Help's slot 1 (Controls). Fourth check (2026-10-17): that build blanked every game string in the pause menu (the replacement lookup read the text base at the wrong per-level address) and "Quick Select Pause" overflowed the toggle box. Fixed: the hook now re-enters the game's own msg_string for game ids; and the fifth check (2026-10-17) passed except the wording: owner wants "Controls Options" / "Quick Select Pause" with a wider box. Done without code: the page now uses the cut Vibration page's frame sequences, whose panel fits a twenty-character row. Sixth check pending.

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
