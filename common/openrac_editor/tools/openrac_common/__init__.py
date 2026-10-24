# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
The helpers OpenRAC's Python tools share: one way to find the repository, read a game's
manifest or a table, hash a file, place files, run a program and read a disc image.

  paths     repo_root(): the top of OpenRAC, found by walking up from a path
  games     load(game), load_all(), versions(), version_dir(): games/<game>/game.json
  tables    read_tsv(path, columns=None), header(path): the tab-separated tables
  digest    sha1(path), sha256(path), file_digest(path, algorithm), read in blocks
  files     link_or_copy(source, dest), copy_tree(source, dest, keep)
  proc      run(cmd, ...), git(repo, *args, ...)
  iso9660   boot_serial(image), files(image), read(image, path), find(f, path)

A script in tools/ imports it directly (`from openrac_common import games`): Python puts the
script's own folder on the path. A script elsewhere puts tools/ on the path once, from its own
place, and then imports it the same way:

  sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents if (p / "AGENTS.md").is_file()) / "tools"))

Rules for this package: standard library only, Python 3.10 and newer (CI runs 3.12); it imports
none of its callers, reads no environment variable and prints only what a caller asks for
(proc.run's echo); each helper is written from the best existing copy, which its module
docstring names. The README beside this file says how it is tested and what is not in it yet.
"""
