# Executive Edition (R&C1): HQ report

Running report from the development account, for carrying back into the HQ.
Newest entries first. Format per the handoff pack: item, new status, what
changed, decisions taken, findings that affect other items.

## 2026-10-25 (the Randomizer: Gold Bolt spots mapped)

### 🎲 Gold Bolt spots' own rules (rz1) — Built, host tests pass; awaiting a test in game

**What changed.** Each of the 40 Gold Bolt spots now has its own rule. The game tells a planet's Gold Bolts apart by a flag (0-3) that each bolt carries in the level data; every flag is now paired with its spot in Panda291's list (matched by the bolts' positions in the level data, in Wrench). Until now each spot took the combined rule of its planet's spots, which made seeds place items later than they could.

**Decisions taken.**

- The logic version is 2: a seed code gives a different layout than under version 1, so version 1 codes are refused. Saves made under version 1 keep playing as made (the record holds the whole layout).

**Findings that affect other items.** The level data holds all 40 Gold Bolts as ordinary Gold Bolt mobies (class 1134), Batalia's two included, with the same count per planet as Panda291's list.

## 2026-10-23 (the Randomizer in play)

### 📣 Shuffled item grants and received messages (rz3) — Working in the owner's play; the owner's tenth round (the autosaves) fixed, awaiting re-test

**What changed.** In a Randomizer save every check now gives the item the seed put there, with the game's own message for it: gadgets and weapons through the game's grant routine (its banner, ammo, quick select and hand), infobots as planet unlocks (the "Infobot for Planet ... acquired" banner), Gold Bolts as a count with "Gold Bolt acquired", the Zoomerator, Raritanium and Codebot as their flags with retail's banners, the Nanotech upgrades as the health maximum with their banners, the Charge Boots as the cheat switched on. A check gives once; the save records what was collected and received. Retail saves are untouched: every hook passes through without a Randomizer record.

**Decisions taken.**

- One shared grant path (the GiveItem, UnlockPlanet and Gold Bolt hooks all end in the same receive function), as the pack proposed.
- Out-of-order safety by a "collected view": while a giver's moby updates, it sees whether its check was collected rather than whether its item is owned, so Helga still sells her check after the Swingshot arrived elsewhere, and an infobot stays when another check unlocked its planet. Consumers (locks, targets, the map) see the real inventory.
- Gold Bolt spots are matched to Panda291's spots by the game's flag (2026-10-25), so each spot has its own rule.
- Two infobot destinations in the pack's list were corrected from the game's own calls: Eudora's henchman unlocks Blarg Station, Blarg Station's ship Rilgar, Rilgar's bouncer Umbris.
- The Charge Boots are in the pool since 2026-10-24 (95 checks, 95 items): the bonus vendor entry sells the edition's item for 2,500 bolts on the seed's planet. Its icon is the Charge Boots' own; the mystery icons (rz4) are still to do.

**Findings.**

- Panda291's PS3 randomizer forces open Orxon's gates and spawns the Blarg Station bridge lowered when the O2 Mask comes early; the edition does the same since 2026-10-24 (his level flags and moby deletions, applied at the level's first moby update). Not yet tried in play.
- One Batalia Gold Bolt is awarded by another class and is not hooked yet; the Gold Bolt message does not give the count yet.
- The vendors still show the vanilla item's icon (rz4).

**Owner's second round (2026-10-23).** The owner played a seed and the shuffled grants worked; three things were wrong. (1) Every grant's message read "Paradox: this message does not exist": the game's banner texts belong to the level they are shown on, so a text for another level's item is not there. The Randomizer now shows its own banner, built from the game's words in the game's language: the item's name, "Infobot: Oltanis", "Gold Bolt 12/40". (2) The Ultra Nanotech received did not appear on the Items page (the HUD showed it): the page lists the upgrades by their flag, which the received items now set (the givers see the collected view regardless). (3) Health, as Panda291's randomizer has it: 4 orbs, one more with the Premium Nanotech, three more with the Ultra, 7 with the Ultra alone and 8 with both (retail only sells the Ultra after the Premium). Then a fourth: buying a vendor weapon whose check gave something else still equipped the vendor's weapon, unowned and with no ammo. The vendor's weapon demo on leaving (the pose with the weapon bought) equips the weapon the vendor noted at the purchase; the note is now cleared unless the reward is the vendor's own item, so the demo plays only for a weapon that landed in its original place, as the owner asked, and the hero keeps what the check gave. All four built; the first three are the owner's to confirm, the fourth is awaiting the owner's test.

**Owner's third round (2026-10-24).** After a vendor purchase whose check gave something else, the vendor's own weapon still showed for a moment (its empty ammo counter on the HUD) and then stayed in the vendor as an ammo entry. Cause: the first build marked the weapon bought in the vendor's stock list, and in the original game bought means owned, so the entry became an ammo entry and the HUD showed the "new weapon" counter. The vendor's list is now rewritten after the game builds it: a purchase entry stays while its check is not collected (even if the weapon came from elsewhere: it is the check), an ammo entry only for a weapon really owned, and ammo is added for an owned weapon whose purchase the game listed instead. The same rewrite settles the second report, a collected vendor check offered again after a reload (the owner thinks the save predated the purchase; either way the list no longer depends on the saved stock). Also asked: Helga's platform on Kerwan is left by the Swingshot she sells, so her check giving something else stranded the player; the rail lift back to the ship, which the original game starts for a player who comes back owning the Swingshot, now starts right after such a purchase. And the Metal Detector's 20x (x4) applies in a Randomizer save as in Challenge Mode, the owner's bolt grind for the RYNO and the Gold Weapons there. And the banners, which the second build reduced to the bare item name, now use the original game's own texts in the player's language, with the item in the game's orange: "You found the Trespasser!", "Infobot for Planet Oltanis acquired", "Gold Bolt acquired 12/40". Built; awaiting the owner's test.

**Owner's fourth round (2026-10-24).** Three requests. (1) The Charge Boots join the pool through the bonus vendor entry: the vendor of the seed's planet sells the edition's Charge Boots item for 2,500 bolts (fixed, as asked; the entry shows the boots' own icon for now), buying it is the 95th check, and the boots as a reward switch the cheat toggle on as before. (2) Panda291's early-O2 fixes: when the O2 Mask is owned, Orxon's Clank-section gates are forced open and Blarg Station's drawbridge spawns lowered, by his level flags and moby deletions applied at the level's first moby update. (3) The banners say how the item was come by, with the item in the game's orange: "You found the Trespasser!" for a pickup in the world (Gold Bolt spots included, as "Gold Bolt (12/40)"), "You got the Swingshot!" for a character's reward or a vendor entry, "You won a Zoomerator!" for a race prize; an infobot keeps the game's own "Infobot for Planet Oltanis acquired". Each check's kind was classified by hand (randomizer.c, `how`). Host tests pass for both pools; built; awaiting the owner's test.

**Owner's fifth round (2026-10-24).** Eight reports, one root cause behind most: the moby class was read at the wrong offset (+0xBC, the command byte, instead of `oclass` at +0xA6), so the "collected view" that makes givers judge by the check rather than the inventory never applied in any build so far. Hence the Magneboots respawning on a reload, Helga selling her check again after a death and her lift never starting, and most likely the vendor entries coming back. Fixed, and checked against a live moby over PINE. Also fixed: the freeze on the Charge Boots vendor entry (the class loader run off its directory for R&C2's class; the load is skipped, the model being resident); Al's and Helga's own "You got the Heli-Pack!" banners after a redirected check (every giver's banner call is hooked); the health HUD with seven orbs (four over three, seen in PCSX2); the pause menu's badges (the save layout tables move per level program; now per-program data). Checked in PCSX2 on a copy of the card: a check marked collected in RAM survived Save and Load from the pause menu, so saving does carry the record; the owner plays on a PS3, so the owner's own save (seed 1463998) was not available here; the reloads reported were Load Game from the title or the pause menu. The HUD's gold bolt counter reading 39/40 on a one-bolt save is noted, unexplained. Built; awaiting the owner's test.

**Owner's sixth round (2026-10-24).** Confirmed working: the seven-orb health display, the pause menu's green R, the Charge Boots vendor entry without the freeze. Fixed: the entry was sold on the seed's planet only, and now, like any weapon a vendor has once offered, every vendor sells it from then on (the record remembers the first listing); a Save and Load on Orxon as Ratchet closed Panda291's gates again, because the fixes ran once per level change and a reload is the same level, so a visit now begins at the level's init. Still reported: Gold Bolts not retained and givers staying in the world; the owner proposes the real fix, placing the shuffled items in the world (givers and vendors hand out the item the seed put there, names redacted, original prices kept, world pickups become the actual item, vendor entries show a question-mark tile and the chicken model). That is rz4 in full and a design of its own; questions put to the owner before building it. One more from the owner: after a valid Reuse Seed code the green R vanished from every Randomizer save on the New Game page, and on New Game and Load Game, until PRESS START; the title screen's card scan (a background step machine since 2026-10-24) cleared its results at its start and stalled while the save page held the card. The scan now publishes its results only when it finishes. Built; awaiting the owner's test.

**Owner's seventh round (2026-10-24).** Confirmed fixed: Gold Bolt counts, vendors remembering purchases, the Orxon gates on reload. Fixed now: NPCs and world pickups (the Magneboots) returning after a Load Game from the card but not after a death, because the Randomizer's state was read at the first gameplay frame, after the arrival, while the givers decide at their first update before it; it is read at the level's init too. The Charge Boots entry goes on sale everywhere once the player has landed on its planet (not opened its vendor), and the Persuader halves it to 1,250 bolts. The overwrite question's box on the New Game page no longer runs over the Yes/No prompt (its panel was stretched with the name box), and after a No the name box keeps its three lines (the panel's stretch now fades out towards the question). Built; awaiting the owner's test.

**Owner's eighth round (2026-10-24).** The owner confirmed the Charge Boots on sale everywhere, the Orxon gates on reload and the seven-orb display, then asked for the full design: givers and vendors present the shuffled item honestly. First step built (rz4): every vendor purchase entry is a mystery (a blue question-mark tile, "???" for the name, the Morph-o-Ray's chicken on the stand, the entry's own price), and a giver's prompts redact the item's name to "???" while its check is uncollected (Helga, Al, the Nanotech vendor, the race prizes, the infobot sellers), through msg_string's return. The givers' staying after a reload is now covered twice over (the state is read at the level's init and at the first moby update of a level). The world's pickups still wear retail's models; making them the item the seed put there is the next step, with the engine's limit noted to the owner: only two gadget models are loaded at a time (the vendor's and the menus'), so weapons and gadgets in the world will need a stand-in model; the owner chose the Gold Bolt as the stand-in. The first build of this step garbled every string of the original game on the title screen (the text hook's new return path handed msg_string's result on from the wrong register); fixed and checked in PCSX2 before the next test. Built; awaiting the owner's test.

**Owner's ninth round (2026-10-24).** The vendor's mystery entries work ("so far so good"). Found: the Magneboots pickup (and, by their code, the Bolt Grabber and Hoverboard pickups, Skid, the Grindboots scientist and the RYNO salesman) decide by the game's "has had" bytes, which GiveItem sets and the collected view did not cover; it covers them now, which should settle pickups returning after a reload. Helga returning is not explained by that (she tests the inventory, which the view covers); a precise observation is asked of the owner. Vendor polish as asked: the panel's chicken upright and smaller, the stand hidden, and a purchase entry's slogan blank instead of the original item's advert (the Charge Boots entry showed a stray text). Built; awaiting the owner's test.

**Owner's tenth round (2026-10-24).** The decisive report: a load from the title screen brought a Randomizer save back as a normal game (the original weapons and gadgets owned, the shuffled ones gone), while loads from the pause menu had seemed fine; Helga and the Orxon Magneboots offered their original item again after such a load. Found in the game's autosave, which the givers call right after giving and the ship at every planet change. It serialises the inventory the moment it is called, and inside a giver's update that is the "collected view" the Randomizer shows the givers (vanilla items owned where their check is collected): the card got the fake inventory, and the title screen's load restored it as the truth. Every autosave call in the levels' code is now hooked and the view comes off around it. Second finding, which also explains the Gold Bolts not retained (fourth round): the autosave writes the game's records and the current level's page only, never the Randomizer's page, so a save that only autosaved since its last full save came back with the record as that full save left it. The Randomizer now writes its page itself, through the game's own save sequence, whenever the record changed and the card is free. Checked in PCSX2 in halves (the hook collecting through the ship's autosave; the page written and read back from the card across a title-screen load); the whole flow in one run is the owner's test. Known narrow window: the page is written on a gameplay frame, so a quit during the cutscene right after a check loses that change. Also seen on the test card: the galactic map lists Pokitaru twice; whether a real save shows it too is asked of the owner. The vendors' mystery tile is square-cornered in the same build, as asked. The owner then hit a freeze opening the Novalis vendor in a new game (eleventh round): the frozen game, read over PINE, had a normal list with the cursor on the Pyrocitor's mystery entry and the chicken stand scaled to 0; the stand is now hidden by moving it out of view instead, and the owner confirmed the vendor opens again (2026-10-24).

**Owner's twelfth round (2026-10-24).** Reloads now keep the save intact, from the title screen too (confirmed by the owner). Still wrong: the givers came back on a revisit of their planet. Cause: a level's load runs every character's update once in a separate pass before the regular update loop, and the givers decide to stay or go there, outside the Randomizer's view. That pass is now hooked too; verified in PCSX2 both ways on Orxon's Magneboots (collected: gone on arrival; uncollected: there). Vendor: the question-mark tile now fills its frame like the item icons, and the panel's chicken is placed from the panel's centre instead of the original item's offset. The hologram chicken's tilt is still open. Owner's follow-up: "everything is working great now"; asked for the panel chicken a little higher and the hologram chicken upright. Both done in the next build (the hologram is re-posed every frame from the original item's tilt; that per-frame placement is hooked and the tilt dropped, the spin kept). The owner then saw some hologram chickens off-centre and asked for the tile to match the other icons: the hologram's two per-weapon offsets (its spot in the beam and the pivot of its spin) are now skipped for the chicken, and the tile uses the weapon icons' own blue gradient. Awaiting the owner's look.

**Verification.** Host tests pass (both pools, 300-2,000 seeds). In PCSX2 on a Novalis save with a record injected: the record was accepted, every moby update ran through the new dispatcher for two minutes of play without a crash, bolts collected normally. No giver came within range, so no shuffled grant has been seen in game yet: the next step is playing a seed from Veldin into Novalis (plumber, mayor, vendor, Gold Bolts).

## 2026-10-23 (the Randomizer: logic, seed codes and saves)

### 🎲 Randomizer logic and seed code (rz1) — Built and tested on a PC; awaiting owner test in game

**What changed.** The randomizer's logic exists and is in the build: the 94 checks and 94 items of Panda291's R&C1 Archipelago world (95 and 95 with the Charge Boots), each check's rule as a bit test, a seeded generator, the fill, a full-reachability sweep that re-rolls a seed that fails it, and the nine-press seed code with its check press.

**Decisions taken.**

- The fill is Archipelago's own assumed fill (the method Panda291's rules were tested with) rather than the forward fill the pack proposed: progression items are placed one at a time into checks reachable with the items still to be placed, so nothing ends up behind its own lock. The sweep and the re-roll stay as proposed.
- Seed code: digits 0-10 are Up, Right, Down, Left, Cross, Circle, Square, L1, L2, R1, R2; the first eight presses carry seed × 64 + weapon mode × 16 + version in base 11, the ninth makes the weighted sum a multiple of 11. Logic version 2 (since 2026-10-25, the Gold Bolt spots' own rules). Only the vanilla starting weapon mode is implemented; a code with another mode or version is refused.
- The bonus vendor check's planet and price (2,500 or 5,000) come from the seed; its only rule is the planet's infobot.

**Verification.** On a PC, 2,000 seeds per pool: every layout is completable from the start and deterministic, every item placed exactly once, at most 3 fills needed; every single wrong press and every neighbour swap in a code is caught. Requirements ticked: check and item tables, rules as bitmask tests, the fill with sweep and re-roll, the seeded generator, the seed code with version and check press, the two reserved weapon-mode bits.

### 🎁 Goodies menu and seed screens (rz2) — New Game, Reuse Seed, the save type, its badge and its Seed line working in PCSX2; awaiting owner test

**What changed.** The Randomizer's New Game creates a Randomizer save: a normal save whose seed, logic version, options and the item of every check sit in hidden save space (the twentieth level page of the save file, 2 KB the original game saves and loads without reading). Reuse Seed takes exactly nine presses, refuses a bad code (the box clears) and opens the same New Game with the code's seed. On the New Game and Load Game lists a Randomizer save shows a green version of Challenge Mode's "R". The box with the planet picture is stretched down and shows "Seed" and the nine buttons under the picture (in two lines of five and four where nine do not fit).

**Decisions taken.**

- The record keeps the whole layout, not just the seed, so a later logic version still plays an old save as it was made.
- The green R is the game's own R badge recoloured (a new palette), drawn where the game draws Challenge Mode's.

**Findings.**

- The pause menu's Save and Load pages in a level show the badge since 2026-10-24 (the owner's request; the slots are read again when the card's summaries change), not the Seed line.
- Starting Challenge Mode from a Randomizer save loses the record (the carry-over resets that page).
- The vendors' mystery icons (rz4) are not built; the shuffled rewards (rz3) and the Charge Boots as an item (rz5, the bonus vendor entry) are, see the later entry.

**Owner's first round (2026-10-23).** Fixed: after a visit to Load Game with a Randomizer save on the card, the Load Game row came back stretched into Options (the box's stretch now grows in over the page's animation instead of applying to its first frame, where the row rests). Done as asked: Reuse Seed waits for Cross once nine symbols are in, with the prompt box offering Confirm and Exit; a refused code empties the box and shows "Invalid seed code" in red until the next press starts a new code; the entry box is narrowed to the nine symbols. All four seen in PCSX2.

**Verification.** In PCSX2 on a copy of a card with Challenge Mode saves: Goodies > Randomizer > New Game created a Randomizer save (over an existing save, through the game's own overwrite question) and the game started; after power-on, Load Game and New Game showed the green R on that slot and the three-line box with "Seed" and its nine buttons; Reuse Seed refused a code with two presses swapped and, with the right code, made a second save with exactly the same layout; loading the save brought its data back from the card. Requirements ticked: New Game flow, Reuse Seed entry screen, the save type and icon, the Seed line, the button glyphs, the hidden save data with the slot still valid for the original (the original game reading it is still to be tried). Not yet tried: real hardware, a 16:9 title screen, the original game loading such a save.

## 2026-10-23 (Strafing: L2 and R2 outside play)

### 🎯 Strafing on L2/R2 (x5) — L2 and R2 work again outside play; fixed, checked in PCSX2

**What was wrong.** With Strafing on, L2 and R2 were taken from the game everywhere, not just while playing. Anything else that used them stopped working:

- skipping a cutscene, which needs all four shoulder buttons held;
- fast-forwarding the credits;
- entering item codes in the pause menu, which needs L1 and R2 held;
- fast-scrolling text with L2;
- the hoverboard's L2 and R2 tricks.

**What changed.** Strafing now takes L2 and R2 only in normal play off the hoverboard. In cutscenes, the credits, the pause menu, at vendors and on the hoverboard they work as in the original game. It applies to both control layouts. As Clank and Giant Clank in the Classic layout, L2 and R2 still do nothing while Strafing is on, as before.

**Verification.** Tested in PCSX2 on a Challenge Mode save on Veldin, Strafing on, Classic layout. In the pause menu, L1+R2 and all four shoulder buttons now reach the game. In play, R2 and L2 still strafe. Not yet tried in the game: an actual cutscene skip, the credits, an item code or a hoverboard race.

## 2026-10-23 (the Future control layout)

### 🎮 Control layout: Future — Implemented and checked in PCSX2; awaiting test

**What changed.** The Controls page's Control layout row now does something when set to Future (Classic, the default, is the original game's buttons and stays exactly as before):

- **Crouch on R2**, and everything built on crouch with it: the crouch jumps, Comet-Strike, Power Slam, the ledge drop and the Hydro-Pack's jets.
- **Double-tap R2** for the Thruster-Pack's hover and the Charge Boots' dash.
- **R1 fires the weapon or uses the gadget in hand**, exactly as Circle still does. Pressing both counts as one press: nothing fires twice.
- **Strafing on L2 alone** (when Strafing is on). L2 also steers the Thruster-Pack's hover.
- **L1 and R1 still turn the quick-select pages** with the ring open.
- **Button prompts follow:** weapon prompts show R1, crouch and Hydro-Pack prompts R2 (German "R1-Taste" too), the Thruster-Pack and ledge prompts name the one button Future uses, and the Charge Boots' description says R2. Gadget prompts keep Circle.

**Decisions taken.**

- Giant Clank, the hoverboard races, the pause menu, cutscenes and vendors keep the original buttons. The hoverboard's tricks use all four shoulder buttons, and the cutscene skip and the item-code entry need L2 and R2 as in the original game.
- The game cannot tell a weapon prompt from a gadget prompt, so the prompts are listed one by one by text id: 7 weapon prompts, 20 crouch and jets prompts, 3 prompts naming two buttons. The ship, Giant Clank and hoverboard rocket prompts keep Circle.
- Turret cannons, the Visibomb and the Hologuise also answer R1, as the game treats it as Circle.

**Findings.**

- The HelpDesk's voiced lines still say the original buttons, and the Help Desk's controller diagrams have the original labels drawn in. Alternative English voice lines are planned as a separate task.

**Verification.**

- **On a PC.** The new code ran against the game's text in all five languages and against simulated controller input. Classic leaves every string and button as in the original game. Future changes exactly the listed prompts and gives the original text back when switched back to Classic.
- **In PCSX2, on a real save** (Veldin's second visit, Thruster-Pack worn, Strafing on):
  - **Classic** is unchanged: R1 crouches and its double tap hovers, Circle fires, L2 and R2 strafe, and the prompts show the original buttons.
  - **Future:** R2 crouches and its double tap hovers; R1 fires the Blaster and doesn't crouch; L2 strafes and R2 doesn't. Circle and R1 pressed together or overlapping register as one press.
  - **Prompts:** the Moves help page reads "Crouch with R2" and "To drop down hit R2", and the Help Log's messages change too. Switching back to Classic in the pause menu restores the original text.
- **Not yet run in the game:** the Hydro-Pack, the Charge Boots, Clank, Giant Clank, the hoverboard races, and switching the setting through its menu row (the tests set it directly in memory).

## 2026-10-23 (quick select pages in saves)

### 📑 Quick select pages (q2) — Saves load on page 1, in retail too; working

**What was wrong.** A save made with page 2 or 3 of the quick select active kept the original game's ring on that page for good: the original game knows only one ring, and the save gave it whichever page was active.

**What changed.** Every save now stores page 1 as the ring the original game reads, with pages 2 and 3 kept in the edition's spare save bytes. Loading a save starts on page 1, in the edition and in the original game. During play the ring still reopens on the last page used, across levels too. A save already made on page 2 or 3 loads on that page until it is saved again.

**Verification.** Tested in PCSX2 on a Challenge Mode save in Batalia that was on page 2: after saving, the ring stayed on page 2; the save file holds page 1 in the original game's ring with pages 2 and 3 intact; loading it opened the ring on page 1. Not yet run: loading the new save on the original game.

## 2026-10-22 (16:9: the weapon reticles)

### 📺 Proper widescreen (q4) — Weapon reticles keep their shape in 16:9; working

**What was wrong.** In 16:9 the weapon reticles, such as the Blaster's red aim ring and green target ring, were a third wider than tall.

**What changed.** The reticles keep their round shape in 16:9 and stay where they were on screen. This covers every weapon that uses the game's reticle drawing, in every level.

**Verification.** Measured in PCSX2 on Quartu with the Blaster in first-person aim: the reticle is 0.75 as wide in 16:9 as in 4:3 (138 against 183 pixels), the same height, in the same place. Also tested in game on enemies: the green target ring and the third-person aim ring.

## 2026-10-22 (L2 and R2 as Clank)

### 🎯 Strafing on L2/R2 (x5) — L2 and R2 do nothing as Clank; working

**What was wrong.** With Strafing on, holding L2 or R2 while playing as Clank switched on the strafe camera, though Clank can't strafe.

**What changed.** As Clank (and Giant Clank), L2 and R2 now do nothing at all when Strafing is on. With Strafing off they still work as in the original game, as another look button and another crouch button.

**Verification.** Tested as Clank with Strafing on: holding L2 or R2 leaves the camera and Clank alone. Not tested as Giant Clank.

## 2026-10-22 (the countdown timers drawn cleanly)

### 📺 Proper widescreen (q4) — Countdowns drawn after the HUD; working

**What changed.** The countdowns on Blarg Station, Quartu and Drek's Fleet are drawn after the HUD instead of among the world's 3D effects, so they keep a solid backing and clear numbers wherever Ratchet is, including swimming on the water's surface. This applies in 4:3 and 16:9; in 16:9 they keep their corrected shape.

**Decisions taken.** A deliberate improvement on the original game, which draws the countdowns with the 3D effects' blending and depth test.

**Findings.** OpenRAC's latest master needs its generated disassembly refreshed (OpenRAC's `tools/setup_asm.sh`) before the edition builds against it.

**Verification.** Tested on Quartu: on the platform, turning the camera, and swimming on the surface.

## 2026-10-21 (the countdown timers on Quartu)

### 📺 Proper widescreen (q4) — Quartu's countdown confirmed in 16:9; its rendering quirks are the original game's

**What changed.** Nothing: a check.

**Findings.** The timer keeps its shape in 16:9 on Quartu, but its numbers turn faint and see-through while Ratchet swims on the water's surface, and its black backing vanishes in some camera directions. That is how the original game draws it: the timer is drawn in the middle of the world's 3D effects and picks up their settings (the water's blending, the depth test). With the timer switched back to the original game's own drawing code, the backing is missing and the numbers are see-through just the same.

## 2026-10-21 (16:9: the title cards, logo and PRESS START, owner's second report)

### 📺 Proper widescreen (q4) — Title cards, logo and PRESS START fixed; confirmed by the owner (all reported issues)

**What changed.**
- **The story's title cards** now keep their retail shape in 16:9, with black at the sides. The previous build's fix did nothing to them, so the owner's report was right.
- **The title screen's logo and PRESS START** keep their retail size and place in 16:9.

**Findings.**
- The previous entry's verification was flawed: it measured the cards only in 16:9, with nothing to compare against, and didn't check the logo at all.
- The cards weren't being adjusted because their strips span the whole screen, and the edition deliberately leaves full-screen layers (fades, bars) alone. The cards are now an exception.
- The logo and PRESS START are pictures drawn by the title screen's own frame, which the earlier changes didn't reach.

**Verification.** In PCSX2, each element was captured with 16:9 off and on and measured. A correct 16:9 picture is exactly three quarters as wide in the frame, and the logo, PRESS START, two title cards and the main menu buttons all measured 0.75. The first card is the same size on screen as in the owner's retail screenshot. The title card entering Novalis is right too, so the cards between planets work as well.

## 2026-10-21 (16:9: the title screen itself, the intro movies, the title cards)

### 📺 Proper widescreen (q4) — Title screen and intro movies in 16:9; the title cards' fix here did not work (see the entry above)

**What changed.**
- **The title screen turns 16:9** as soon as 16:9 is switched on in Options → Video, the same way the game does: a wider view of the scene, with menus and text keeping their shape.
- **The intro movies after New Game** now have black bars at the sides, like every other movie.
- **The story's title cards** ("Kyzil Plateau, Planet Veldin", "Chairman Drek's flagship", "Approaching Planet Veldin..." and the ones between planets) keep their original shape with black bars at the sides, instead of stretching across the screen.

**Decisions taken.** This reverses the earlier choice to keep the title screen in 4:3, at the owner's request.

**Findings.**
- The title screen has its own copy of most of the game's code, so the 16:9 work now covers it the same way it covers every planet.
- The title cards are pictures, not text, drawn by their own routine before the story's movies; nothing had been adjusting them.

**Verification.** In PCSX2 on a copy of the owner's memory card (deleted afterwards; PCSX2's settings restored): switching 16:9 on widened the title screen at once, with its menus in proportion. A New Game showed the first title card and the next one centred and in proportion, with the intro movie between them pillarboxed. Not checked: the title cards between planets later in the game, which use the same code.

**Owner test.** With 16:9 on, look over the title screen, its menus and the logo, then start a New Game and watch the intro's title cards and movies. Later, check a title card between planets (for example arriving at Novalis).

## 2026-10-21 (16:9 on the title screen, owner's request)

### 📺 Proper widescreen (q4) — 16:9 row added to the title screen's Video page; working per the owner

**What changed.** The title screen's Options → Video page has a second row, **16:9 (widescreen)**, under the 50Hz/60Hz setting. It is the same setting as the in-game Camera page's row. Turning it on there and starting a New Game starts the game in 16:9. The title screen itself stays 4:3, as decided before.

**Decisions taken.** It behaves like the volumes: loading a save uses that save's own 16:9 setting, and starting a new game keeps the one set on the title screen.

**Findings.** The title screen couldn't show the edition's own text until now (its text lookup wasn't hooked); it can now, which the new row's label needed.

**Verification.** In PCSX2 on a copy of the owner's memory card (deleted afterwards; PCSX2's settings restored): both rows show on the Video page, the new row toggles, and a New Game started on the copy kept 16:9 on, with Veldin's opening cutscene in the edition's 16:9 framing. Not run: "Continue without saving", which goes through the same reset.

**Owner test.** Turn 16:9 on from the title screen, start a New Game (or Continue without saving), and check the game is in 16:9. The Video page's box is snug with two rows; say if you want it taller.

## 2026-10-21 (Movie volume: owner's first round)

### 🎬 Movie volume — Working per the owner (prerendered and in-level cutscenes, title screen)

**What changed.**
- **In-level cutscenes** (the ones Goodies → In-Level Movies replays) now follow Movie volume too: their speech, music and effects, and the world sounds under them. When the scene ends, everything is back on Sound effects.
- **The title screen's Options → Sound** has the Movie volume row and orange bar too, after power-on and after quitting a level. The title screen's attract movie follows it as well.

**Findings.**
- In-level cutscenes aren't movies. They run in the game engine, and their sound is one recorded track per scene, played on the same channel the game uses for the HelpDesk voice, the vendor and NPC dialogue. So that channel follows Movie volume only while a cutscene is playing, and those voices stay on Sound effects.
- A Movie volume set on the title screen behaves like the other volumes there: loading a save brings back the save's own setting, and starting a new game keeps it.

**Verification.** In PCSX2 on the owner's Veldin save (nothing saved to the card), reading the sound chip's volume table each time: on the title screen the new row moved only the movie level. After loading, the save's own Movie volume applied. During a replayed in-level cutscene every channel followed Movie volume, and after the scene they were back on Sound effects. Not heard: the volumes were read from memory, not listened to.

**Owner test.** Set Movie volume and Sound effects far apart, then replay a scene from Goodies → In-Level Movies, and check normal play afterwards (HelpDesk voice and vendor speech should still follow Sound effects). Then try the slider on the title screen.

## 2026-10-21 (owner's extra item: Movie volume)

### 🎬 Movie volume — Implemented and verified in PCSX2, awaiting owner test

**What changed.** The Sound page (Options → Sound) has a fourth row, **Movie volume**, between Music volume and the stereo/mono toggle, with an orange bar. It sets the volume of in-level movies, prerendered cutscenes and the Goodies movies, which the original game ties to the Sound effects volume. Sound effects no longer affect movies.

**Decisions taken.**
- **Orange**, the owner's pick of four candidates previewed beside the original green and blue bars. It is the green bar's own texture with an orange palette, so it has the same white centre line.
- **Saved with the game**, like the other volumes. Until it is set, it matches the save's Sound effects volume, so movies start exactly as loud as before. Original-game saves still load in the edition, and edition saves in the original game.
- The title screen's attract movie still follows Sound effects (the title screen is a separate program the edition leaves alone).

**Findings.**
- Movies play their sound in an audio group of their own that nothing else uses. That is why a separate volume works cleanly: only three places in the game set that group's level, and they now use the movie volume.
- The original game applies a save's volumes when loading from the title screen, and keeps the current ones when starting a new game.

**Verification.** In PCSX2 on the owner's Veldin save (nothing saved to the card): the page shows the four rows with the orange bar, and holding right on Movie volume raises only that bar. The sound chip's own volume table showed the movie group at the movie volume, not the sound effects one. Not yet tested: hearing a movie at the new volume, and saving then reloading the setting.

**Owner test.** Turn Movie volume down and Sound effects up (or the other way round), then play a movie from Goodies → In-Level Movies (or reach a prerendered cutscene). Save, reload the save from the title screen, and check the slider kept its place.

## 2026-10-21 (owner's extra item: Oltanis's lightning)

### ⛈️ Oltanis keeps its lightning — Fixed; working per the owner

**What was wrong.** A bug in the original game. Oltanis's lightning strikes and thunder are run by the same object that plays the arrival scene the first time you land. That object waits for the arrival scene before it starts the lightning. Once your save records the scene as seen, it waits forever. So the lightning only ever ran on the first visit; after dying, loading a save or coming back later, the sky stayed quiet.

**What changed.** When the arrival scene has already been seen, the object now skips straight to the lightning. The first visit is unchanged: the scene plays, then the strikes start. Two instructions in Oltanis's code (new feature `oltanis_lightning`, in the default build); no other planet is touched.

**Verification.** The patch applies to the original disc's code and is in the new image (`build/iso/executive-edition.iso`, full `pack.sh` build). The owner confirmed it working in game.

## 2026-10-20 (q4: owner's report on title-screen loads and the warship timer)

### 📺 Proper widescreen (q4) — Three fixes confirmed by the owner

**What changed.**
- **The Trespasser's screen** (the ring puzzle) keeps its circles round in 16:9.
- **The flight to the planet after loading from the title screen** is now 16:9 (wider view, planet name out at the left edge), as it already was when loading in game. The title screen and its menus stay 4:3.
- **The Nebula G34 warship's self-destruct timer** keeps its proportions in 16:9. The timers of the same kind on Quartu and Drek's Fleet get the same fix.

**Findings.**
- **The title screen is two programs.** After power-on it runs from the boot executable; after quitting a level it is reloaded from a separate frontend program on the disc, with the same code at other addresses. The flight after a title-screen load is drawn by whichever of them is running, and the edition had patched neither. Both are now patched.
- **The timers are drawn outside the HUD**, like the Trespasser's rings, which is why the HUD's 16:9 handling missed them.

**Verification.** Tested and working (owner): the Trespasser's screen, the self-destruct countdown, and the flight after loading a 16:9 save from the title screen. Not reported separately: the second title-screen route (after quitting a level), and the Quartu and Drek's Fleet timers.

## 2026-10-20 (the build after OpenRAC's master merge)

### 🏭 Build and repack pipeline — Working again on the merged OpenRAC

**What changed.** `pack.sh` builds from scratch again on this Windows machine with the latest OpenRAC master merged in.

**Findings.**
- **Master's build needs a newer `make`** than the 3.77 bundled with the toolchain. `pack.sh` now uses the GNU make 4.4.1 already on this machine.
- **Two objects are now assembled by original Linux assemblers.** One comes from a release archive OpenRAC documents and was checked against its published hash. Both run through WSL with a small helper in the edition repo.
- **Master itself had a source error** in one game file, left by a partial sync from rac1-decomp. It is fixed to match rac1-decomp on our OpenRAC branch and is included in the edition's OpenRAC patch.

**Verification.** A full build from scratch: all 1,222 decompiled functions byte-identical to retail, image check passed; the edition image built from it loads the Kalebo III save in PCSX2 with the GADGETRON sign.

## 2026-10-19 (owner's extra item: the GADGETRON sign)

### 🏢 Kalebo III's sign reads GADGETRON — Done (owner confirmed, near and far)

**What changed.** The rotating ring on Kalebo III's main building reads GADGETRON, as in the American release, instead of GALACTRON.

**Decisions taken.**
- **Built from the PAL disc only.** The PAL disc has no GADGETRON ring to switch back on (unlike the Commercials), and asking players for a second disc was ruled out. The six shared letters are the PAL letters moved to where the American sign has them, the second G is a copy of the first, and the D and E are built from the L's own parts, with the D's curve made new. The few measurements the PAL disc can't supply were taken from the American sign and kept as numbers.
- **Packed with wrench's builder**, through the edition's fork, matching retail's conventions.

**Findings.**
- **The first builds blanked the top of the screen and stopped drawing frames on Kalebo III.** The cause was a bug in the edition's wrench fork, not the new letters: generated colour data was padded with the wrong value. Found by testing the sign piece by piece in the emulator, and fixed in the fork. The Charge Boots model goes through the same code and now matches retail too.
- **Merging OpenRAC's master broke step 1 of the build on this machine**; fixed the same night (next entry).

**Verification.** In PCSX2 on the owner's Kalebo III save: the whole word as the ring turns, the sky intact, full frame rate; close up and far away (owner).

## 2026-10-19 (q4: second round; the hoverboard race freeze)

### 🥾 Charge Boots (x2) — Hoverboard race freeze fixed; the boots come off for races; verified by the owner

**What changed.**
- **The Kalebo III race no longer freezes** at the first Devastator ammo pickup. The freeze happened in 4:3 and 16:9 alike.
- **The Charge Boots come off while Ratchet is on the hoverboard** and go back on as soon as he is off it and the race weapon has left his hand.
- **The Gadgets page shows the Charge Boots right after quitting a race**, not the Devastator.

**Findings.**
- **The "unused" item the Charge Boots were built on is the hoverboard races' Devastator.** Picking up race ammo puts that item in Ratchet's hand, and the game then loaded the boots' model as a gun and stopped. Found from a saved state of the frozen game.
- **There is no free item slot.** The only other empty-looking one is Clank on Ratchet's back; one test build used it and turned Clank into a boot.
- **So the item has two identities.** It holds the race weapon during races and the Charge Boots everywhere else, switched at moments the game can't trip over. Putting the boots back on too early, while the race weapon was still in hand, made a second Ratchet appear and launched him across the level; that is fixed.

**Verification.** In PCSX2 on Kalebo III (owner): races with and without the boots on, finishing and quitting, the Devastator pickup, the Gadgets page after quitting, the boots back on after a race.

### 🖥️ Proper widescreen (q4) — Owner's second round fixed; every part of the request verified by the owner

**What changed** (each of the owner's notes):
- **Hoverboard race HUD:** Lap and Place sit at the 16:9 edges, the bottom bar reaches both sides, and the time stays whole in the middle.
- **Flying to a planet:** the planet's name keeps its shape and sits at the left edge.
- **Pause menu 3D models** (Weapons, Gadgets, Quick Select): they have their proper width again.

**Findings.**
- **The race HUD draws its whole bottom bar from one place**, so it is stretched to the edges rather than moved like the other HUD elements.
- **Pause menu panels are drawn into small pictures first**, and the models in them were narrowed twice: once by the wide camera and once by the squeeze that keeps panels in shape. The models are now drawn with the 4:3 camera.

**Verification.** In PCSX2 (owner): the race HUD, the pause menu models, the planet name. Measured in the session: a menu model's width matches 4:3 to within 4%.

**Also confirmed by the owner:** boss health bars and the Visibomb's camera view, the last two items not yet seen. Every part of the original request has now been seen in game.

## 2026-10-19 (q4: owner's first round of 16:9 notes)

### 🖥️ Proper widescreen (q4) — Owner's first round fixed; awaiting owner re-test

**What changed** (each of the owner's notes):
- **Objects popping in at the sides:** fixed. The game's culling was still using the 4:3 field of view. It now uses the wide one, so everything in the 16:9 picture is drawn.
- **Fighter jet HUD:** the radar, health and missiles go to the 16:9 corners without stretching. The "Quit?" prompt keeps its shape (confirmed working by the owner).
- **Credits:** the pictures are shown at 4:3 with black bars at the sides, and the name tags keep their shape.
- **Quick select:** L1 sits beside the ring again, like R1.
- **Ammo display:** its dark backing covers the whole count again, like the bolt counter's.
- **Toggling 16:9 in the menu:** the menu redraws immediately, with no garbage panels at the sides when turning it off.
- **Ship landings and take-offs** (owner's request): no pillarbox any more; they show the full 16:9 picture like play. Other in-level movies keep the original framing with side bars. Built; not yet seen in game.
- **Side meters** (Suck Cannon, O2 mask, Morph-o-Ray): they now hide a little further out, so they no longer peek in at the right edge for a few frames (Suck Cannon confirmed by the owner).

**Findings.**
- **The culling reads the camera's field of view directly:** the game's culling code takes it from the view record every frame. Any change to the view has to stay in place, not be restored afterwards.
- **The ammo display sizes its backing from the element's own position:** a slip in the original game that only shows when the element moves. HUD elements are now moved after they are drawn, which suits all of them.
- **The credits copy whole pictures straight to the screen:** they can't be drawn narrower in place, so in 16:9 each picture is staged in the depth buffer's memory and drawn from there.
- **The jet's HUD is drawn by the ship, not by the HUD system:** Pokitaru, Gemlik and Drek's fleet are handled one by one.

**Verification.** In PCSX2 at 16:9:
- **Pop-in:** the culling field stays wide every frame (panning on Orxon).
- **HUD:** the ammo backing, quick select's L1, and the Camera page toggle in both directions.
- **Pokitaru jet:** its HUD compared against 4:3, and its "Quit?" prompt (owner).
- **Suck Cannon meter:** hides cleanly off the right edge (owner).
- **Credits:** pictures and name tags.

**Not yet watched:** boss health bars, the Visibomb's camera view, the hoverboard races.

## 2026-10-19 (q4: proper widescreen)

### 🖥️ Proper widescreen (q4) — Implemented; verified in PCSX2, awaiting owner test

**What changed.** A new row on the pause menu's Camera page: "16:9 (widescreen)", on or off, saved with the game (off in every existing save). Turned on, the game draws for a television, or PCSX2, set to 16:9:
- **Gameplay shows more to the sides.** Nothing is cut off at the top or bottom, and nothing is zoomed. Culling widens with the view; no pop-in seen.
- **The HUD moves to the 16:9 corners without stretching.** Nanotech stays top centre, the weapon and ammo go top left, the bolts top right, and the O2 gauge to the right edge. The quick-select ring goes halfway out (the owner left this to us).
- **Pause menu, vendor and map keep their shape and place**, with more of the level visible around them. Panels that slide off screen now slide all the way past the 16:9 edges.
- **In-level movies and arrival flights keep their original framing**, with thin black bars at the sides. Letterboxed scenes fill the screen's height instead of showing the top and bottom bars.
- **Prerendered movies play at 4:3 with black bars at the sides**, as on a 4:3 television.
- **The title screen and main menu stay 4:3.**

**Decisions taken.**
- **Frontend stays 4:3** (owner's choice from three options); 16:9 starts when a save with the setting on is loaded.
- **In-level cutscenes fill the height** (owner's choice). Retail letterboxes them to about 3:2, not 16:9, so they show exactly the original picture, scaled to the screen's height, with thin bars at the sides.
- **The camera departs from Going Commando on purpose.** Going Commando's own 16:9 crops the top and bottom, which is what the owner asked to avoid.
- **Setting byte:** `gCheats[10]`, next to Quick Select Pause's `gCheats[11]`. Saves stay compatible with the original game both ways.

**Findings.**
- **The pause menu's text is laid out per panel at the panel's on-screen size.** At 16:9 width the Camera page's labels ran into their values, so each panel is laid out at its 4:3 size and then drawn narrower.
- **The vendor's item monitor draws the item model with the main camera's transform**, so its monitors are drawn with the 4:3 view.
- **The HUD's 13 places carry their own anchor flags.** Any element shown in a right-anchored place (the side meters) moves to the edge with no per-element work, and slides in and out exactly as before.

**Verification.** In PCSX2 at 16:9, on a copy of the owner's card, nothing saved:
- Novalis and Pokitaru gameplay, the HUD, the quick-select ring.
- The Novalis arrival, and a letterboxed scene (forced).
- The pause menu: Options, Camera with the new row, Sound, Load, Goodies, Cinematics, In-Level Movies, Weapons, the map, and their open, close and page animations.
- The vendor.
- A prerendered cinematic, an in-level movie with subtitles, the O2 gauge underwater.
- The setting switches live, and 4:3 is unchanged with it off.

**Not yet seen.** The Suck Cannon meter, the ship HUD, boss health bars, the Visibomb's camera view, the hoverboard race HUD. They use the same HUD places, so they should follow, but nobody has watched them yet.

**Owner test.**
- **Setup:** set PCSX2's aspect ratio (or the TV) to 16:9, load a save, turn the row on in Options → Camera.
- **What to look at:** a boss fight, a ship mission, the Suck Cannon, and any cutscene.
- **Then off:** turn it off and check that everything looks exactly as before.

## 2026-10-18 (x4: Metal Detector against the Blackwater City farm)

### 💰 Metal Detector gives 20x bolts (x4) — Raised from 10x; builds, not yet run in-game, awaiting owner test

**Goal (owner).** Make a Challenge Mode run that collects every Metal Detector stash a better way to 400,000 bolts than the Blackwater City crate farm: 150,000 for the RYNO and 250,000 for the ten Gold Weapons (prices read from the game: Devastator and Tesla Claw 60,000, Pyrocitor 30,000, Bomb Glove, Blaster and Morph-o-Ray 20,000, Suck Cannon, Mine Glove, Glove of Doom and Decoy Glove 10,000).

**What changed.** In Challenge Mode every 5 bolts of a stash now comes out as two 50-bolt circlenuts instead of one: 20x the original, so a 50-bolt stash pays 1,000. Normal playthroughs are unchanged.

**Decisions taken.**
- 20x (owner's choice, from 20x, 25x and 30x). The 358 stashes pay 349,200 in a Challenge Mode run; the remaining ~51,000 of the 400,000 comes from the run's enemies and crates (not measured). For comparison, forum figures for the farm are 58,000-100,000 bolts an hour, so 4-7 hours for 400,000.
- Only 50-bolt pieces, two per step, rather than odd mixes.

**Findings.**
- 358 stashes over 18 levels (none in the Veldin tutorial), 17,460 bolts in the original game. 347 hold 50; ten on Kalebo III and one on Veldin 2 hold 10.
- The stashes cannot be farmed: the game saves how much of each stash was dug up, so a stash stays empty for the rest of the run. Starting Challenge Mode does not carry that record over, so every stash refills once per Challenge Mode run.
- A whole stash pays out in a single frame once Ratchet stands on it; all the time goes into finding the stashes. Shortening the search (a larger pickup radius, a map marker) would raise the bolts per hour without raising the payout.
- When the game runs out of room for bolt pieces, the value goes straight to the bolt count, so no bolts are lost however many pieces a stash throws.
- Correction to an earlier note: the two extra numbers on some stashes are the area Ratchet must stand in for the stash to register, not a mission lock; all 358 can be collected.

**Verification.** The mod segment compiles with the change, and its code was checked in disassembly: two calls to the level's bolt spawner with 50 in Challenge Mode, the original single call otherwise. No image was built and nothing was run in PCSX2 (another session's emulator was running).

**Owner test.** Challenge Mode save (slots 3-5), a stash not yet dug: twice the circlenuts of the 10x build, bolt count up 1,000 for a 50-bolt stash. Then a normal save: hexnuts worth 5.

## 2026-10-18 (owner's extra item: the health display's lower backing)

### ❤️ Health display with Premium/Ultra Nanotech — Fixed; working per the owner

**What was wrong.** A bug in the original game, in every level: with 5 or 8 nanotech the health display draws its orbs in two rows, 32 pixels apart, but moves the second row's dark backing down by only 16. The lower backing started halfway up the top row and the lower orbs hung below it.

**What changed.** The lower backing now moves down the full 32 pixels, so each row of orbs sits centred on its own backing. One instruction per level (new feature `health_backing`, in the default build); the game's stored value is left alone. Nothing else changes: the top row, the 4-orb display, and the left-right placement were already right.

**Verification.** Rebuilt with `pack.sh --quick`. On the owner's Challenge Mode Novalis save (slot 1 of Mcd003, Ultra Nanotech), compared in PCSX2 by switching the instruction between the original and the fix while the game ran: the original overlaps the two backings, the fix gives two separate backings, each under its row. Then, at the owner's request, max nanotech set to 5 and to 4 in RAM on Veldin 2 (restored to 8 after, nothing saved): with 5 the original leaves the lower orbs' bottom halves off their backing and the fix puts the backing behind the whole lower row; with 4 there is one row and one backing, the same with or without the patch. The owner confirmed it fixed.

**Findings.** L3 shows the health display on demand (the game's own per-frame check), which is handy for testing any HUD change.

## 2026-10-18 (q2: three quick-select pages)

### 📑 Quick select pages (q2) — Ghost rings removed (owner's call); R1 prompt kept moved in, awaiting owner test

**What changed.** At the owner's word the ghost rings are gone: it was all too busy. The quick-select ring is back at its original place, as before the ghosts. The L1 and R1 prompts are where they were after the owner's R1 note: L1 just left of the ring, and R1 moved in to the same small gap on the right. The ghost code is kept in git history (commits d0b0ce9, 0ff040b) if the idea ever comes back.

**Verification.** In PCSX2, on a copy of the owner's memory card: the ring is at its retail place with no ghosts, the L1/R1 prompts sit evenly beside it, and R1/L1 still turn the pages.

### 📑 Quick select pages (q2) — Ghost icons in their real places; narrower ghosts (owner's report), verified in PCSX2, awaiting owner test

**What was wrong.** The ghost icons were a quarter turn out of place: what sits East on a page showed at North, SE at NE, South at East, and so on. The ring's draw code turns its slot angles by an extra 90° that the ghost code had missed.

**What changed.**
- The ghost icons now sit where they really are on their page. Checked by turning a page in PCSX2: the ring that comes in matches the ghost it came from, icon for icon.
- At the owner's request the ghosts are narrower (60% of the ring's width instead of 80%, height unchanged). The whole group now moves about 35 game pixels away from the screen edge instead of 55. Narrower still started to crowd the R1 prompt against the ghost.

**Owner test.** With weapons on pages 2 and 3, hold Triangle and compare each ghost with the page that comes in when you press R1 or L1.

### 📑 Quick select pages (q2) — R1 prompt moved in; ghost rings added (stretch goal), tuned in PCSX2, awaiting owner test

**What changed.**
- **R1 prompt (owner's note):** R1 sat further from the ring than L1. It now leaves the same small gap to the ring as L1.
- **Ghost rings:** while the quick-select ring is open, faint, smaller copies of the neighbouring pages sit behind it: the next page (R1) on the right, the previous page (L1) on the left. Each ghost shows the five icons on its far side and hides the three that would sit under the ring. The L1 and R1 prompts sit in the gap between the ring and each ghost. A page with nothing on it shows no ghost. The ghosts fade in and out with the ring and update as pages turn.

**Decisions taken.**
- The retail ring sits close to the screen's left edge, where the left ghost doesn't fit, so the ring moves right just enough while it is open. It shows on screen about 55 game pixels (about a tenth of the width) further right than before.
- Layout settled on screen: ghost centres 100 pixels from the ring's centre, ghost circles 0.8 of the ring's size, ghost icons 26 pixels (the ring's are 32), at about a third of the ring's brightness. A tighter layout looked scattered; a larger one pushed the ring too far right.

**Verification.** On a copy of the owner's memory card (Challenge Mode Novalis, slot 1), with weapons put on pages 2 and 3 in RAM for the test:
- Both ghosts draw on screen, and the left one clears the screen edge.
- Turning pages with R1 moves the right ghost's page to the main ring and the old page to the left ghost.
- An empty neighbour shows no ghost.
- Equipping works in every direction with the ring moved.

The owner's card was not changed (checksum matches).

**Owner test.** Fill pages 2 and 3 in the pause menu, then hold Triangle in game and judge the ghosts: spacing, size, faintness, and whether the ring's new position works. Each of these is one number to change.

### 📑 Quick select pages (q2) — Fix: opening the ring froze Batalia (owner's report); fixed and verified in PCSX2, awaiting owner test

**What was wrong.** On the owner's Batalia Challenge Mode save, opening quick select froze the game. The edition's ring code worked out which level was running from where the level keeps its HUD data, but several levels keep it at the same address. In Batalia it therefore ran another level's ring code. Novalis happened to be unaffected, so the earlier test missed it.

**What changed.** The ring code now identifies the level from the call that runs it, as every other edition hook does. No other edition hook has this flaw (checked).

**Verification.** On a copy of the owner's memory card:
- Batalia, slot 4 (Challenge Mode): the ring opens with its L1/R1 prompts, R1 and L1 turn pages, and the stick plus release equips (Bomb Glove). The game keeps running.
- Novalis, slot 1, loaded from Batalia's pause menu: the ring and its pages work as before.

**Owner test.** On the Batalia save, open quick select, turn the pages, and equip something.

### 📑 Quick select pages (q2) — Implemented and verified in PCSX2; awaiting owner test

**What changed.** The quick-select ring has three pages. With the ring open (Triangle held), R1 goes to the next page and L1 to the previous one, in a loop (1 → 2 → 3 → 1 with R1, the other way with L1). L1 and R1 button prompts sit left and right of the ring. The ring reopens on the page you last used. In the pause menu's Quick Select tab, Square switches between the three rings, the ring's centre reads "Page 1", "Page 2" or "Page 3", and the button box lists "[X] Equip", "[square] Next ring", "[triangle] Exit". Applied to all 19 levels.

**Decisions taken.**
- Pages are saved inside the existing save file, in 24 bytes of a saved statistics block that no game code reads or writes. No new save block, so the save file's size and layout are unchanged. Page 1's eight slots stay where the retail game keeps its ring.
- Backwards compatibility: the retail game loads an edition save normally, shows the page that was active when it was saved, and carries the other two pages along untouched. The edition loads retail saves with pages 2 and 3 empty.
- The ring reopens on the last page used (owner's choice). The same weapon may sit on more than one page (owner's choice); within one page the game's own rule (one slot per weapon) still applies.
- A newly acquired weapon takes the first empty slot of the active page as in retail; if that page is full, the next page, then the previous one (owner's choice).
- While the ring is open, L1 and R1 don't reach Ratchet, so turning a page doesn't also crouch.
- The pause prompt reads "Next ring" instead of "Switch rings": the retail prompt box is a fixed width and clipped the longer label to "Switch rin" (owner's choice). The prompt box is shared with the Weapons and Gadgets tabs, so Quick Select gets its own copy; Weapons and Gadgets still read "Equip / Exit".
- Challenge Mode's carry-over keeps all three pages, as it keeps the ring.

**Verification.** On the owner's Novalis save (slot 1), on a copy of the memory card; the owner's card was not touched:
- R1/L1 cycle the pages in game, and the stored pages rotate correctly.
- Square cycles Page 1/2/3 in the pause tab. Weapons equipped onto page 2 there showed up in the in-game ring, which reopened on page 2, and equipping from page 2 worked.
- Saved with three different pages, wiped them in RAM (test tool only), and reloaded the save: all three pages and the page number came back.

Not yet tested:
- Loading an edition save in the retail game (the owner's test; the retail image can't be driven by script).
- The overflow of a new weapon to the next page.
- The Challenge Mode carry-over.

**Owner test.**
1. Hold Triangle: tap R1 and L1 and watch the pages change; equip something from page 2.
2. In Pause > Quick Select, press Square to reach page 3 and put a weapon there with Cross.
3. Save, reload, and check all three pages.
4. Optionally load that save on the original disc: the ring should show the page that was active.

**Stretch goal (ghost rings).** Drafted, not built yet. Planned after the owner confirms the core feature: faint, shrunken previews of the neighbouring pages left and right, showing their five outer icons.

## 2026-10-18 (x5: Going Commando's landing jump back; guns held up on the backpedal)

### 🎯 Strafing on L2/R2 (x5) — Landing press back to Going Commando's plain jump (owner's call), verified in PCSX2

**What changed.** The previous entry's change is undone at the owner's word, since Going Commando doesn't do it. Pressing Cross in the last few frames before a strafe side flip lands gives the plain jump again, which can double jump, as in Going Commando. Pressing Cross after landing still gives the next flip. Kept from that change: the flip reads its direction from the stick, Going Commando's way.

**Verification.** On the owner's Novalis save, with the trigger and the stick held right, Cross 1 to 5 frames before a side flip lands gives the plain jump on landing. A strafe jump with the stick only partly pushed, and a jump without the trigger, are unchanged.

### 🎯 Strafing on L2/R2 (x5) — Guns held up when strafing backwards, as in Going Commando (owner's report); verified in PCSX2, awaiting owner test

**What was wrong.** Strafing backwards, Ratchet swung every weapon at his side like a glove, so the Blaster, Pyrocitor, Devastator, RYNO and the other hand-held guns dangled.

**What Going Commando does.** Both games lay a separate arm animation over Ratchet's arms while a gun is held: the right arm for one-handed guns like the Blaster, both arms for two-handed ones like the Pyrocitor and the Devastator. Gloves and the wrench get none. The arm animation follows whatever move Ratchet's body makes, but only for moves that have an arm animation; for any other move the arm just follows the body. Going Commando added an arm animation for its backward step. R&C1 has none, so the edition's backward step (taken from Going Commando) had no gun arm.

**Fix.** The edition now carries Going Commando's own two backward-step arm animations (one per arm), taken from its disc like the backward step itself, and the game uses them while a gun is held. Gloves and the wrench swing as before.

**Findings.** Compared frame by frame, Going Commando's gun-arm animations are R&C1's for every other move, so the backward step was the only one missing. R&C1's data had room for one more arm animation, so nothing else had to move.

**Verification.** In PCSX2 on the owner's Novalis save, strafing backwards: with the Bomb Glove the arm swings as before. With the Blaster the right arm plays Going Commando's backward-step arm, the gun held up and forward, and firing works while backpedalling. With the Pyrocitor and the Devastator both arms hold the weapon up. Running forward is unchanged, a wrench swing with a gun out still swings normally, and pausing or visiting the Weapons screen mid-backpedal causes no problems.

**Owner test.** Strafe backwards (L2/R2, stick back) with the Blaster, Pyrocitor, Devastator and RYNO, and with the Bomb Glove or the wrench for comparison. Also fire while backpedalling, and swing the wrench with a gun out.

## 2026-10-18 (x5: a quick press at the landing flips)

### 🎯 Strafing on L2/R2 (x5) — Quick follow-up side flips chain (owner's request), verified in PCSX2; awaiting owner test

**What changed.** Holding the strafe trigger with the stick to the side, pressing Cross the moment Ratchet lands from a side flip now gives the next side flip. A slightly early press still counts, as it does for any jump. Back flips chain the same way. Plain strafe jumps are unchanged: jumping with the stick centred or forward, then steering sideways in the air, is still a plain jump.

**Decisions taken.**
- This is a deliberate difference from Going Commando, at the owner's request. There, a press in the last frames before landing gives a plain jump. Everything else comes from Going Commando's own code: the same rule decides when a strafe jump becomes a flip (stick pushed most of the way, not forward), and the same way of reading the stick's direction.
- The flip reads the stick itself rather than Ratchet's travel direction, as Going Commando does. The travel direction is frozen during a flip, so the earlier version only judged the direction correctly when Ratchet was running.

**Verification.** Built and tested in PCSX2 on the owner's Novalis save. With the trigger and the stick held right, a press 5, 3 or 1 frames before landing starts the next right flip straight from the landing, and it flies a full flip. Left and back flips chain the same way. Unchanged: a press just after landing still flips; a gentle stick still gives a plain hop; forward strafe jumps are plain jumps; the backward walk still plays; without the trigger, a press at landing is the game's normal running jump. Owner test: strafe sideways and chain side flips with quick presses, including slightly early ones.

## 2026-10-18 (x5: the quick second strafe flip)

### 🎯 Strafing on L2/R2 (x5) — Quick follow-up flip fixed (owner's report), verified in PCSX2; awaiting owner test

**What was wrong.** A quick second side flip while strafing often came out as a fast plain side jump that could take a double jump.

**What Going Commando does (measured in R&C2 itself).** On the owner's Oozla save, with the controller driven from a script, a strafe side flip lasts about a second. Pressing Cross after Ratchet lands gives another flip. Pressing Cross in the last 8 frames before he lands gives a plain jump the moment he touches down, and that jump can double jump. That is the game's "jump as soon as you land" buffer, which has no strafe rule in either game. So the plain jump and its double jump are Going Commando's behaviour too. The difference was speed: Going Commando's buffered jump leaves at about the flip's speed (5 to 6 units a second), and the edition's left at 10.4.

**Fix.** The strafe flip kept Ratchet's ground speed in the wrong speed field. That value kept climbing toward running speed during the flip, so whatever followed the landing started at about twice the speed. The flip now keeps the same field Going Commando uses, the speed Ratchet is actually travelling.

**Decisions taken.**
- The buffered landing jump is kept as both games have it. Making a late press flip instead would differ from Going Commando; it can be done if wanted.

**Verification.** Built and tested in PCSX2 on the owner's Batalia save. A strafe side flip with Cross pressed 5 frames before landing, and again 2 frames before: Ratchet now goes into the plain jump at the flip's own speed, building to normal running speed (was twice that), as in Going Commando (5 to 5.7 units a second here, 5 to 6 there). He can double jump out of it, as in Going Commando. Cross right after landing still gives the next side flip. A plain landing no longer surges forward for a frame. Owner test: strafe sideways and chain side flips. Pressing Cross just after each landing should flip every time. Pressing slightly early gives a normal-speed side hop, as in Going Commando.

**Findings.**
- Going Commando can now be driven from a script in the emulator (its controller input redirected to a buffer the script writes), so its own behaviour can be measured rather than inferred from code. That's how these numbers were taken.
- One test double jump stalled in mid-air. That's retail R&C1, not the strafe: over some platform objects (the game found one under that spot on the Batalia bridge) the game deliberately cuts a double jump's sideways drift. Over plain ground it keeps its speed, strafing or not.

## 2026-10-18 (x3b: jump out of the throw)

### 🔧 Free movement during the wrench throw (x3b) — Run cancel and jump cancel working (owner)

**Owner test.** Passed: the owner confirmed the jump cancel works (2026-10-18).

**What changed.** After a Comet-Strike (crouch + Square), pressing Cross once the wrench has left his hand now jumps Ratchet out of the throw, as in Going Commando. It works from animation frame 35, right after the release at 33, and a press up to about 7 frames early still counts. Holding R1 when pressing Cross gives the crouch jumps as usual. Applied to all 19 levels.

**Findings.** R&C1 already had this jump in its code, but its Comet-Strike data only allowed it in the last 4 frames of the 91-frame throw, so in practice it never happened. Going Commando changed exactly two things: the data opens the jump at frame 35, and the press window is a fixed 8 frames because R&C1's formula gives nothing that early. The edition makes the same two changes and nothing else.

**Decisions taken.**
- The rest is left to the game's own code: no jump while locked on, the R1 crouch jumps and the jump itself. This also matches Going Commando.
- The 8-frame window is scaled to PAL's 50 Hz the way the game scales its other jump windows (7 frames).

**Verification.** Rebuilt; the new sites were found and checked in all 19 levels (the record holds 83/87/91 in every level before the patch), and the hook disassembly was checked. Owner test: crouch + Square, then press Cross right after the wrench leaves. Ratchet should jump at once, and the wrench should still fly back to him and catch in the air or after landing. Also try R1 + Cross mid-throw (crouch jump), Cross while locked on (no jump, as before), and a normal Comet-Strike without Cross (unchanged). The stick-to-run cancel should still work.

## 2026-10-18 (x2: the Charge Boots dash, and the boots' ankles)

### 🥾 Charge Boots (x2) — Cheats page switch now updates the Gadgets screen at once; "Cheat Enabled" banner (owner's reports); verified in PCSX2, awaiting owner test

**What was wrong.** Switching the Charge Boots on or off on the Cheats page didn't change the Gadgets screen until you unpaused and paused again. The unlock also showed the edition's own "Charge Boots unlocked" message instead of the game's "Cheat Enabled".

**What changed.**
- The switch takes effect at once. In the same pause, the boots appear on or disappear from the Gadgets screen. If Ratchet is wearing them when they're switched off, they come off when you unpause.
- The unlock shows "Cheat Enabled", like every other cheat, and the edition's own message is gone.

**Verification.** In PCSX2 on the Challenge Mode save (Batalia), without memory changes:
- The sequence unlocked the boots, and "Cheat Enabled" showed.
- All in one pause: the boots were equipped on the Gadgets screen. Switched off, the Gadgets screen showed their slot empty and the preview Ratchet without boots. Switched on, they were back.
- Worn in the game, then switched off from the pause menu: gone from the Gadgets screen at once, and off Ratchet's feet after unpausing.

Owner test: on a Challenge Mode save with the boots unlocked, pause and switch the Charge Boots on the Cheats page. Then go straight to Gadgets. Try it off and on, with the boots worn and not worn.

**Findings.** The pause menu keeps its own record of what's equipped and hands it to Ratchet when it closes. Switching the boots off clears them from that record too; otherwise closing the menu would put them back on for a moment.

### 🥾 Charge Boots (x2) — On the Cheats page, and Challenge Mode only (owner's requests); verified in PCSX2, awaiting owner test

**What changed.** Once unlocked, the Charge Boots get their own row on the Goodies → Cheats page, "Charge Boots on/off", alongside the game's cheats. Switching it off takes the boots away, and takes them off Ratchet if he's wearing them. Switching it on gives them back. The unlock itself stays permanent: the row stays listed either way, and entering the move sequence again does nothing. Applied to all 19 levels. Saves that already unlocked the boots on the previous build get the row, with the boots on.

**Challenge Mode only.** The game's cheat detector, which this cheat uses, ignores every move unless the save is in Challenge Mode, so the sequence can't unlock the boots in a normal game, and nothing in the edition gets around that. It only looked possible because the previous round's test switched the game's Challenge Mode flag on in the emulator's memory, on a normal save. This round was tested without that: on a normal save the moves aren't even recorded, and on a real Challenge Mode save the sequence unlocks the boots.

**Decisions taken.**
- Only the page switches the boots off; the sequence can't.
- The row uses the boots' name and the game's own "on"/"off" text, so it shows in all five languages.

**Verification.** In PCSX2 on this build:
- Normal save (Novalis, slot 1): the six moves do nothing, and the boots stay locked.
- Challenge Mode save (Batalia, slot 4): the sequence, entered by hand in the emulator window, unlocked the boots. Switching them off on the page then removed them.
- The page shows "Charge Boots on". Cross switches it to "off", and back in the game the boots are gone.

Switching back on wasn't exercised separately; it runs the same code that gives the boots at the unlock.

Owner test: on a Challenge Mode save, enter the sequence (crouch for a second, back flip, back flip, Comet-Strike, Hyper-Strike, Hyper-Strike), then open Goodies → Cheats. Switch the row off and on, checking the Items menu each time. On a normal save the sequence should do nothing.

**Findings.**
- Each cheat has two saved flags: on/off, and "seen", which lists it on the page. The boots use their seen flag as the permanent unlock and their on/off flag as the switch.
- Goodies is missing from the pause menu until the game is in Challenge Mode.

### 🥾 Charge Boots (x2) — Locked behind a new cheat; pause freeze and Veldin 1 fixed (owner's reports); verified in PCSX2, awaiting owner test

**What was wrong.** Pausing in the middle of a charge froze the game once it resumed. The charge still never started on Veldin's first visit.

**What changed.**
- **Pause freeze.** The pause menu borrows the top of Ratchet's animation list and clears it on closing, which removed the charge animation (and the strafe's backward step) while Ratchet was still playing it. The edition now puts them back at the start of every frame. Tested: charge, pause, resume; the charge carries on.
- **Veldin 1.** The charge had kept one condition from the Thruster-Pack's code: Clank must be on Ratchet's back, which he isn't on Veldin 1. The boots don't need Clank, so the condition is gone. Tested on Veldin 1 itself.
- **The cheat.** The Charge Boots are now locked behind a new cheat, entered like R&C1's own: perform a sequence of moves after beating the game. The sequence is **crouch for a second, back flip, back flip, Comet-Strike, Hyper-Strike, Hyper-Strike**. The unlock is permanent for that save and shows "Charge Boots unlocked"; repeating the sequence does nothing. Saves made with earlier builds lose the boots until the cheat is entered. Tested with the game's beaten flag set in the emulator: locked on load, the six moves unlock them, the banner shows, and the boots can be worn and used.

**Decisions taken.**
- The owner chose post-game only (like the game's other cheats) and a permanent unlock.
- The sequence was designed for this (the owner left it open). It uses only moves available everywhere, and it can't be confused with an existing cheat.
- The boots' menu description now says how to charge, in place of the old placeholder text.

**Findings.** R&C1 has space for 12 cheats and uses 7; the new one takes the ninth place. The pause-menu clearing also affected the strafe's backward step, which is fixed by the same change; pausing mid-backpedal was tested too and doesn't hang.

### 🥾 Charge Boots (x2) — Charge works in every level, and is no weapon (owner's reports); implemented, awaiting owner test

**What was wrong.** The charge did nothing on Veldin's first visit. The cause was wider than Veldin. The charge borrowed Going Commando's internal number for its movement state, and that number is free in Batalia (where it was tested), but in 11 of the 19 levels R&C1 uses it for a state of its own, a kind of stun. There the game's own code fought the charge: Veldin 1, Aridia, Eudora, Rilgar, Umbris, Gaspar, Orxon, Oltanis Orbit, Quartu and both Veldin 2 levels. Separately, the charge damaged enemies.

**What changed.** The charge now uses a state number that no level uses, checked across all 19 level programs and the main executable. Contact follows Going Commando, whose charge only ever hits crates, plus the owner's rule: crates still break; running into an enemy bonks Ratchet and ends the charge, leaving the enemy unhurt; anything else is left alone.

**Decisions taken.**
- "Enemy" means the game's own creature class, which covers the 98 enemy types the game's targeting knows across the levels. "Crate" means the game's crate class (bolt, nanotech, metal, explosive and ammo crates).
- The fix compiles cleanly, and the static checks covered every level. The in-game test is left to the owner: two Claude sessions' builds overlapped and produced a mixed image that crashed on loading, so the test waits for a clean build.

**Findings.** Going Commando's charge never damaged enemies. Its hit goes only to its crate classes, so this rule matches it.

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
