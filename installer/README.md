# Installer

The tool players run to make an edition from their own disc images. The
installer core (`installer/core`, C11) makes them: `rac-installer --serve`
speaks the protocol below, for the desktop builder (`rac-editions-web`, a
separate MIT repository that runs it as a separate program), and the same
code is to run an on-console PS3 builder. Each edition's `tools/install.py`
is the reference implementation: the core makes the same images byte for
byte.

- **Inputs are the player's disc images.** The installer identifies each
  image by its hash and refuses ones it does not know.
- **Discs decide the features.** A feature that needs another game's data is
  built only when that game's disc is given, and is otherwise left out
  entirely, never replaced by a stand-in. In the Executive Edition the Charge
  Boots and strafing need Going Commando (NTSC-U, SCUS-97268).
- **No game data in a release.** What ships is the editions' own compiled
  code, with the bytes it takes from a disc cut out, and lists of the words
  it changes. Data taken from a disc is made from the player's disc while
  the installer runs; the input image's hash stands for the retail bytes the
  changes replace.
- **No development setup for players.** The decompilations rebuild the games'
  executables byte-identical to retail, so the installer starts from the
  player's own executable and needs no compiler or decompilation build.
- **One tool for every edition**, set up by itself as far as possible.
- **Options the player picks.** Challenge Mode's economy, "skill" (the
  default: drops as in a first playthrough, and a bolt multiplier for kills
  without a hit) or "classic" (retail's doubled drops), and the features a
  player may leave out (the Executive Edition's Charge Boots, strafing and
  Quick Select pages). Today `tools/install.py --economy` and `--without`.
- **A payload per release.** An edition's `tools/release.py` writes what a
  release ships (`tools/payload.py`); `tools/install.py` makes the image from
  that payload and the player's discs.

## The protocol

The builder runs the installer as a separate program and talks to it over
its standard input and output: one JSON object per line, UTF-8, each way.
Version 1, the first the installer speaks.

**Messages to the installer** are commands: `cmd` names the command and
`id` is any number or string the builder chooses; every event about that
command carries the same `id`. For example
`{"id": 1, "cmd": "identify", "path": "D:\\Games\\rac1.iso"}`.

| Command | Fields | Does |
|---|---|---|
| `editions` | | lists the editions this installer has payloads for |
| `identify` | `path` | hashes a disc image and says which disc it is |
| `plan` | `edition`, `discs`, `options`, `output` | checks a build without making it: what is in, what is left out and why, the space needed, and with `output`, whether the image can be written there |
| `build` | as `plan` | makes the image |
| `cancel` | `of` (the `id` of a running `build` or `identify`) | stops it; a new image is deleted, an image updated in place is restored |

`build` and `identify` run side by side, so two discs are checked at once;
the other commands answer in order. The installer keeps each disc image's
SHA-256 between runs, by its path, size and modification time, so a disc
checked when it was added is not hashed again for the build: its `verify`
stage is one `progress` event with `done` equal to `total`. The new image's
final check is always a full hash.

- `discs`: `{"rac1": "<path>", "rac2": "<path>"}`, each optional but the
  edition's base disc.
- `options`: `{"economy": "skill", "without": ["quick_select_pages"]}`.
- `output`: `{"mode": "new", "path": "<path>"}`, or `{"mode": "in_place"}`,
  which updates the base disc's image and first writes `<image>.restore`
  beside it (the original bytes of every sector it changes and the image's
  original length).

**Messages from the installer** are events.

| Event | Fields | When |
|---|---|---|
| `ready` | `protocol` (1), `installer` (its version) | once, at start, before any command is read |
| `progress` | `id`, `stage`, `done`, `total` | while a command runs; `stage` is `hash`, `verify`, `convert`, `patch` or `write`: `hash`, `verify` and `write` count bytes, `convert` counts the mod segment's holes and the executable's steps, `patch` counts shares of the time each step takes (about 100 in all) |
| `log` | `id`, `text` | a line for the builder's Details view |
| `result` | `id`, `value` | the command finished; `value` as below |
| `error` | `id`, `code`, `message` | the command failed; nothing else follows for it |

Results:
- `editions`: `[{"id": "rac1-executive", "name": "Executive Edition", "game":
  "Ratchet & Clank", "version": "<payload version>", "discs": {"rac1":
  {"name": "Ratchet & Clank", "region": "PAL", "required": true}, "rac2":
  {"name": "Going Commando", "region": "USA", "required": false}},
  "options": {"economy": ["skill", "classic"], "switches": [{"id":
  "charge_boots", "needs": "rac2"}, {"id": "strafing", "needs": "rac2"},
  {"id": "quick_select_pages", "needs": null}]}}]`
- `identify`: `{"disc": "rac1" | "rac2" | null, "game": "rac1" | "rac2" |
  null, "name", "region", "sha256", "known": true | false}`. `disc` is set
  only for an image the edition is built from; `game` and `name` (as the
  edition's `editions` entry names that disc) for any version of one of the
  games, so for an image of the right game in the wrong version the builder
  can say "This is the US version". An image that is no PS2 disc has every
  field null and `known: false`.
- `plan`: `{"in": ["charge_boots", ...], "left_out": [{"id": "strafing",
  "because": "no_rac2" | "chosen"}], "bytes_needed": n}`. With `output`,
  `not_writable` for a folder, one of the discs, a file in use or read-only,
  or a folder that cannot be made or written, and `no_space` for a drive
  with less than `bytes_needed` free.
- `build`: `{"path", "sha256"}`.

Error codes, which the builder turns into words: `unknown_disc`,
`wrong_version`, `image_incomplete` (shorter than the disc), `missing_disc`,
`no_space`, `not_writable`, `cancelled`, `internal` (with the details in
`message`).
