# Installer (planned)

The tool players run to make an edition from their own disc images. Not
written yet; these are the decisions so far.

- **Inputs are the player's disc images.** The installer identifies each
  image by its hash and refuses ones it does not know.
- **Discs decide the features.** A feature that needs another game's data is
  built only when that game's disc is given, and is otherwise left out
  entirely, never replaced by a stand-in. In the Executive Edition the Charge
  Boots and strafing need Going Commando (NTSC-U, SCUS-97268).
- **No game data in a release.** What ships is the editions' own compiled
  code and lists of changes checked against the retail bytes they replace.
  Data taken from another game is converted from the player's disc while
  the installer runs.
- **No development setup for players.** The decompilations rebuild the games'
  executables byte-identical to retail, so the installer starts from the
  player's own executable and needs no compiler or decompilation build.
- **One tool for every edition**, set up by itself as far as possible.
