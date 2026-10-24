# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
Placing files: a hard link where the file system allows one, and copies of trees.

  from openrac_common import files
  files.link_or_copy(image, dest)                    # "linked" or "copied"
  files.copy_tree(src, dest, lambda rel: rel.suffix == ".py")

link_or_copy is written from the copies tools/extractor.py and tools/openrac.py (place_link,
whose checks of an existing DEST stay with it) had, and copy_tree from
tools/bundle_standalone.py's; those three use these since P1-05.
port/tools/hostgen/prep.py:759 has a copy_tree of the same name that rewrites C as it copies;
it is a different function and stays where it is.
"""
import os
import shutil
from collections.abc import Callable
from pathlib import Path


def link_or_copy(source: Path | str, dest: Path | str, *, keep_metadata: bool = True) -> str:
    """Hard-link SOURCE at DEST, or copy it when the link fails (another file system, or one
    without hard links); "linked" or "copied". DEST must not exist and its folder must.

    Hard links, not symbolic ones: a build container that mounts only part of the tree still sees
    the file (tools/openrac.py's reason). KEEP_METADATA copies the times and mode too, as
    tools/openrac.py does (shutil.copy2); False copies the bytes alone, as tools/extractor.py does
    (shutil.copyfile)."""
    try:
        os.link(source, dest)
        return "linked"
    except OSError:
        if keep_metadata:
            shutil.copy2(source, dest)
        else:
            shutil.copyfile(source, dest)
        return "copied"


def copy_tree(source: Path | str, dest: Path | str, keep: Callable[[Path], bool]) -> None:
    """Copy each file under SOURCE for which KEEP(its path relative to SOURCE) is true to the same
    place under DEST, with its times and mode, creating folders as needed. Python's caches
    (__pycache__) are never copied, and a link to a file is copied as the file it points to.
    Files already in DEST are overwritten, others there are left alone."""
    source, dest = Path(source), Path(dest)
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        if path.is_file() and "__pycache__" not in relative.parts and keep(relative):
            (dest / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest / relative)
