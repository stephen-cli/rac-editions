# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
The games' manifests: games/<game>/game.json, read one way.

  from openrac_common import games
  rac1 = games.load("rac1")                     # one manifest
  for game, name, version in games.versions():  # every version of every game, in game order
      print(game["id"], name, version["serial"], games.version_dir(game, name))

Written from tools/openrac.py's vdir, games and versions, the most complete of the five readers
there were. tools/openrac.py, extractor.py, sources.py and xmap.py read the manifests through it
(P1-05); port/tools/versionmap/versionmap.py:52 (version) keeps its own until P2-10. A manifest
is returned as the parsed JSON, unchanged: its fields are described in docs/LAYOUT.md and each
game's README, and this module does not check them.

source_checkout is the one rule for where a version's sister project (its matching decomp, synced
into games/) is checked out, written from tools/openrac.py's source_checkout and tools/sources.py's
checkout, which said the same thing twice (P2-30). tools/fcheck_gate.py finds rac1-decomp by it.
"""
import json
import os
from pathlib import Path

from .paths import repo_root


def games_dir(root: Path | str | None = None) -> Path:
    """The games/ folder under ROOT, the top of OpenRAC (default: the checkout holding this package)."""
    return (Path(root) if root is not None else repo_root()) / "games"


def manifest_path(game: str, root: Path | str | None = None) -> Path:
    """Where GAME's manifest is: games/<game>/game.json under ROOT (default: this checkout)."""
    return games_dir(root) / game / "game.json"


def load(game: str, root: Path | str | None = None) -> dict:
    """GAME's manifest (games/<game>/game.json), parsed. Raises FileNotFoundError for a game
    OpenRAC does not have."""
    return json.loads(manifest_path(game, root).read_text(encoding="utf-8"))


def load_all(root: Path | str | None = None) -> list[dict]:
    """Every game's manifest, in game order (the directories' names sort as the release order)."""
    return [json.loads(p.read_text(encoding="utf-8")) for p in sorted(games_dir(root).glob("*/game.json"))]


def versions(root: Path | str | None = None) -> list[tuple[dict, str, dict]]:
    """(game, version name, version) for every version of every game, in game order and in each
    manifest's own order of versions."""
    return [(game, name, version) for game in load_all(root) for name, version in game["versions"].items()]


def version_dir(game: dict, name: str) -> str:
    """Version NAME's directory, relative to the top of OpenRAC: its "dir" in game.json (rac1's
    PAL tree is the merged games/rac1), else games/<game>/<version>."""
    return game["versions"][name].get("dir", f"games/{game['id']}/{name}")


def source_variable(game_id: str, version_name: str) -> str:
    """The environment variable that names a version's sister checkout, overriding game.json:
    OPENRAC_SOURCE_<GAME>_<VERSION> in capitals (OPENRAC_SOURCE_RAC1_PAL for rac1's PAL tree)."""
    return f"OPENRAC_SOURCE_{game_id}_{version_name}".upper()


def source_checkout(game_id: str, version_name: str, root: Path | str | None = None) -> Path | None:
    """Where version VERSION_NAME of game GAME_ID has its sister project checked out, or None when
    its game.json names no source project.

    The rule, decided by the maintainers: the variable source_variable names, when it is set and not
    empty, else the "checkout" path of the version's "source" in game.json. game.json writes it
    relative to the top of OpenRAC ("../rac1-decomp": the sister project cloned beside OpenRAC), so
    it holds on every machine whatever the folder above is called; ~ is expanded too. The variable
    is for a machine that keeps the project elsewhere. Neither is checked to exist: the caller says
    what is missing. Raises FileNotFoundError for a game OpenRAC does not have and KeyError for a
    version it does not have."""
    source = load(game_id, root)["versions"][version_name].get("source")
    if not source:
        return None
    override = os.environ.get(source_variable(game_id, version_name))

    # The variable wins; it is taken as given.
    if override:
        return Path(override)

    # A relative path in game.json is written from the top of OpenRAC, not from where the tool runs.
    checkout = Path(os.path.expanduser(source["checkout"]))
    top = Path(root) if root is not None else repo_root()
    return checkout if checkout.is_absolute() else (top / checkout).resolve()
