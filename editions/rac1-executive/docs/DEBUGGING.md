# Debugging the edition in PCSX2

The mod segment carries a debug pad and a hook trace, and `tools/pine_pad.py`
talks to PCSX2's PINE interface (Settings > Advanced > Enable PINE, TCP port
28011). Nothing here runs unless PINE is on and a script writes the words.

## The debug pad

`ee_debug_pad[4]` (address in `build/modseg/modseg.sym`): while word 0 is
nonzero, the pad hook rewrites pad 0's report before the game parses it:
word 1 the buttons in the game's layout, active high (see `BTN` in
`pine_pad.py`), word 2 the left stick `x | y << 8` (0x7F centred, 0x00 up or
left), word 3 the right stick. The game then derives held, pressed and
released edges itself, so menus work. `Pine.press("start")` holds and
releases a button; `Pine.pad(buttons, lx, ly)` holds a state.

The hook is only in each level's copy of the pad begin-frame, so the pad is
live once a level has loaded. To drive the title and menus too, patch the
executable's own copy in the built image for a debug session: the word at
`0x21812C` (`func_00217F68 + 0x1C4`, `jal func_002181F0`) to a `jal` of
`ee_pad_parse`; the hook calls the executable's parser when entered from
there. Do not ship such an image.

## The trace

`ee_debug_trace` holds the id of the hook running (0 between hooks) and
`ee_debug_last` the last hook entered: 1 twist, 2 run animation, 3 run turn,
4 pivot, 5 run planar, 6 jump press, 7 flip approach, 8 air turn, 9 jump
turn, 10 jump planar, 11 pad parse. When PCSX2 stops on an R5900 exception,
a nonzero trace names the hook it died in.

## A savestate holds the registers

`Pine._cmd(9, bytes([slot]))` saves a state; the `.p2s` is a zip with zstd
entries (`pip install zstandard`): `eeMemory.bin` is the 32 MB of RAM and
the hook's return address on the stack locates its frame.

## Two things learned the hard way

- Only the core below `0x15F000` keeps its addresses in a level. Executable
  functions above it (`fast_subtract_rotations`, `scale_game_frames`, the
  pad functions...) have a copy in every level program at the level's own
  address: call them through the catalogue's per-level places
  (`DECLARE_LVL`), never at the boot executable's address.
- A sequence slot must stay below the class's packet table offset (header
  word 0, `0x2D0` for Ratchet: 162 slots).
