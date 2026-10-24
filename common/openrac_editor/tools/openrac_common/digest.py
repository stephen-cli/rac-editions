# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
Digests of files, read in blocks so a disc image never has to fit in memory.

  from openrac_common import digest
  digest.sha1(path)            # hex string, as game.json records a boot executable
  digest.sha256(path)
  digest.file_digest(path, "md5")

Written from the probe tools' bake_all.py (sha256 in 1 MiB blocks). The other copies read the
whole file at once (editor/assets.py's file_hash and overrides.py's hashes of the textures'
files) or hash several algorithms in one pass (tools/openrac.py's checksums, which stays: it
reads a disc image once for all of them); every one of them returns the same hex digest for the
same file. tools/shared.py, openrac.py, extractor.py, build_android.py, build_port.py and
fcheck_gate.py hash their files through it (P1-05), the probe tools (P2-10), and the editor's
assets.py and overrides.py (P2-32); the editor's util.sha256 stays its own, since it hashes
bytes already in memory, not a file.
"""
import hashlib
from pathlib import Path

# A block of 1 MiB keeps memory flat on a 4 GiB image and costs nothing on a small file.
BLOCK_BYTES = 1 << 20


def file_digest(path: Path | str, algorithm: str) -> str:
    """The hex digest of the file at PATH by ALGORITHM (any name hashlib.new accepts)."""
    hasher = hashlib.new(algorithm)
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(BLOCK_BYTES), b""):
            hasher.update(block)
    return hasher.hexdigest()


def sha1(path: Path | str) -> str:
    """The SHA-1 of the file at PATH, as a hex string."""
    return file_digest(path, "sha1")


def sha256(path: Path | str) -> str:
    """The SHA-256 of the file at PATH, as a hex string."""
    return file_digest(path, "sha256")
