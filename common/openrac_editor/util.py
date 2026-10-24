"""The small helpers several of the editor's modules share, one copy of each.

  import util
  util.sha256(data)                           # hex digest of bytes
  util.read_json(path, lambda p, exc: MyError(f"cannot read {p}: {exc}"))
  util.f32(x)                                 # a number rounded to the nearest 32-bit float
  util.cross(a, b)                            # the cross product of two 3-vectors
  util.disc_identity("rac1", "pal")           # what identifies the disc's boot executable

A helper belongs here when at least two of the editor's modules need it and it carries no format
knowledge of its own: a helper that raises FormatError lives in formats.py, beside the error.
Each was written two or more times over before; its docstring names the modules that had a copy.

What the repository's shared helpers (tools/openrac_common) already do, the editor takes from
there, reached through paths.BOOTSTRAP, which finds the package in a checkout and in the
launcher's bundled copy alike (P2-32): a game's manifest is games.load, a file's digest
digest.sha256. What stays here differs from them: sha256 hashes bytes in memory (digest hashes a
file), read_json raises the caller's own error, and openrac_common has no f32, cross or disc
identity.

Standard library and openrac_common only, no state: every function reads what it is given and
returns.
"""

from collections.abc import Callable
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(1, str(next(p for p in Path(__file__).resolve().parents if (p / "tools/openrac_common").is_dir()) / "tools"))
from openrac_common import games  # noqa: E402


def sha256(data: bytes) -> str:
    """The SHA-256 of DATA, as a hex string (assets.py and overrides.py had one each). For a
    file's, openrac_common's digest.sha256 reads it in blocks instead."""
    return hashlib.sha256(data).hexdigest()


def read_json(path: Path | str, error: Callable[[Path | str, Exception], Exception]):
    """The JSON value in the file at PATH. When the file cannot be read or is not JSON, raises
    what ERROR makes of PATH and the cause, without the cause chained: each tool keeps its own
    error type and wording at its boundary (assets.py's AssetError, overrides.py's PackError)."""
    try:
        return json.loads(Path(path).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise error(path, exc) from None


def f32(x: float) -> float:
    """X rounded to the nearest 32-bit float, as the game stores it (gamedata.py and
    moby_collision.py had one each; pack.py's f32 returns the packed bytes and writes -0 as 0,
    so it stays pack.py's own)."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def cross(a: tuple, b: tuple) -> tuple:
    """The cross product A x B of two 3-vectors (moby_collision_scene.py and sky.py had one each)."""
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def disc_identity(game: str, version: str) -> dict:
    """What identifies VERSION's disc of GAME, from its game.json, under the names disc.Version
    and wads.Version give them: region ("PAL", "NTSC-U": the manifest's region without its
    parenthesis), label ("PAL v2.00": the region and the disc's version), executable (the boot
    executable's file name, which is the serial), executable_size and executable_sha1 (its size
    and SHA-1). The manifest is games.load's, from the checkout or bundled copy the editor is in.
    Raises FileNotFoundError for a game OpenRAC does not have and KeyError for a version its
    manifest does not list."""
    entry = games.load(game)["versions"][version]
    region = entry["region"].split(" (")[0]
    return {"region": region, "label": f"{region} v{entry['disc']['version']}",
            "executable": entry["boot"]["file"], "executable_size": entry["boot"]["size"],
            "executable_sha1": entry["boot"]["sha1"]}
