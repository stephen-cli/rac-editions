# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
Where the repository is: the one way a tool finds OpenRAC's top directory.

  from openrac_common import paths
  root = paths.repo_root()              # from this package's own place
  root = paths.repo_root(__file__)      # from the calling script's place

23 scripts each wrote `Path(__file__).resolve().parents[N]` with their own N, which breaks
when a script moves. This walks up from a path instead and stops at the first directory that
holds `games/` and one of the two markers below. tools/*.py take their ROOT from it (P1-05),
port/tools and the probe tools too (P2-10).

A script outside tools/ cannot import this package before it has found it, so it first runs
BOOTSTRAP, one line written the same in every such script (tools/test_openrac_common.py checks
them against the text below):

  sys.path.insert(1, str(next(p for p in Path(__file__).resolve().parents if (p / "tools/openrac_common").is_dir()) / "tools"))
  from openrac_common import paths  # noqa: E402

It finds the package by the folder that holds it, not by CONTRIBUTING.md, so it works in a
checkout and in the launcher's bundled copy alike (both carry tools/openrac_common; only a checkout
has CONTRIBUTING.md). It puts tools/ second on the path: after the script's own folder, whose
modules keep their names (editor/port.py beside tools/port.py), and before the installed packages,
so a module tools/ holds is not shadowed by one installed under the same name.
"""
from pathlib import Path

# A checkout of OpenRAC has CONTRIBUTING.md at its top, beside games/.
CHECKOUT_MARKER = "CONTRIBUTING.md"

# The launcher's bundled copy of the tools (tools/bundle_standalone.py) has no CONTRIBUTING.md: its
# top holds games/*/game.json and a STANDALONE note, and the tools in it look for their root there.
# Accepting it is decided, not a leftover: the copy's tools move onto this package with the rest
# (P1-05), and the launcher recognises the copy by the same file (launcher/core/src/detect.rs).
STANDALONE_MARKER = "STANDALONE"

# Where this package is, relative to the top of either kind of root: the bootstrap walks up to
# the folder holding it.
PACKAGE_DIR = "tools/openrac_common"

# The one line a script outside tools/ runs before it imports this package (the docstring says why
# it is written so). Every such script holds it verbatim; tools/test_openrac_common.py checks them.
BOOTSTRAP = ('sys.path.insert(1, str(next(p for p in Path(__file__).resolve().parents '
             'if (p / "tools/openrac_common").is_dir()) / "tools"))')


def is_root(directory: Path) -> bool:
    """Whether DIRECTORY is the top of a checkout of OpenRAC or of the launcher's bundled copy."""
    if not (directory / "games").is_dir():
        return False
    return (directory / CHECKOUT_MARKER).is_file() or (directory / STANDALONE_MARKER).is_file()


def repo_root(start: Path | str | None = None) -> Path:
    """OpenRAC's top directory: START (a file or directory; default this package) or the nearest
    directory above it that is_root accepts. Raises FileNotFoundError when there is none."""
    here = Path(start if start is not None else __file__).resolve()
    for directory in (here, *here.parents):
        if is_root(directory):
            return directory
    raise FileNotFoundError(
        f"{here} is not inside OpenRAC: no directory above it holds games/ and "
        f"{CHECKOUT_MARKER} or {STANDALONE_MARKER}")
