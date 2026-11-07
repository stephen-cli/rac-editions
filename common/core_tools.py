"""
The installer core's tools, which the editions' Python tools run for the heavy work: rac-wad (the levels' WAD
codec), rac-vag (the HelpDesk lines' audio) and rac-moby (moby classes). installer/core is C; its CMake builds them
into installer/core/build.
"""
import subprocess
import sys
from pathlib import Path

BUILD = Path(__file__).resolve().parents[1] / "installer/core/build"
EXE = ".exe" if sys.platform == "win32" else ""


def tool(name: str, build: Path = BUILD) -> Path:
    """The tool NAME (rac-wad, rac-vag, rac-moby) in the core's build folder BUILD."""
    return build / f"{name}{EXE}"


def run(*command) -> None:
    """A core tool, its output passed on; a tool that fails stops the caller."""
    result = subprocess.run([str(c) for c in command], capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit(f"{Path(str(command[0])).name} {command[1]} failed")
