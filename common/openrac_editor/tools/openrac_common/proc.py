# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
Running other programs: a command, and git in a given checkout.

  from openrac_common import proc
  proc.run(["cmake", "--build", build], cwd=port, echo=True)   # fails loudly, shows the command
  head = proc.git(checkout, "rev-parse", "HEAD")                # stdout, stripped
  dirty = proc.git(decomp, "status", "--porcelain", check=False)

run is written from tools/build_android.py's run (echo the command, fail on a non-zero exit)
and port/tools/hostgen/clangast.py:76 (capture text, replacing what does not decode); git from
tools/sources.py's git (check, optionally bytes) and tools/fcheck_gate.py's (no check). Both
take paths as well as strings in a command and pass every argument as a string. Nothing here
goes through a shell, so no argument is ever split or expanded. Every git call in tools/*.py
goes through git (P1-05); a command needing what run does not offer (input, a timeout, output
to a file, a shell) keeps subprocess.
"""
import subprocess
from collections.abc import Mapping, Sequence
from pathlib import Path

Argument = str | Path | int


def run(cmd: Sequence[Argument], *, cwd: Path | str | None = None, env: Mapping[str, str] | None = None,
        echo: bool = False, check: bool = True, capture: bool = False) -> subprocess.CompletedProcess:
    """Run CMD (no shell) in CWD with ENV (default: this process's), and return how it ended.

    ECHO prints "+ CMD" first and flushes, so the command shows in a log before its own output.
    CHECK raises subprocess.CalledProcessError on a non-zero exit. CAPTURE keeps stdout and
    stderr as text on the result instead of letting them through, replacing bytes that are not
    UTF-8 rather than failing on them (a compiler's message may quote any byte of a source)."""
    args = [str(arg) for arg in cmd]
    if echo:
        print("+", " ".join(args), flush=True)
    if capture:
        return subprocess.run(args, cwd=cwd, env=env, check=check, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
    return subprocess.run(args, cwd=cwd, env=env, check=check)


def git(repo: Path | str, *args: Argument, check: bool = True, binary: bool = False) -> str | bytes:
    """git's standard output for `git -C REPO ARGS...`: as bytes when BINARY (git archive, git
    show of a file), else as UTF-8 text with the surrounding whitespace stripped. CHECK raises
    subprocess.CalledProcessError when git fails; without it, a failure gives what git printed,
    usually nothing. git's error output is captured and not shown."""
    done = subprocess.run(["git", "-C", str(repo), *(str(arg) for arg in args)], capture_output=True,
                          check=check)
    return done.stdout if binary else done.stdout.decode().strip()
