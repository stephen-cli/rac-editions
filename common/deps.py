#!/usr/bin/env python3
"""
The editions' pinned dependencies (deps.json) and the OpenRAC code they carry.

Building an edition needs OpenRAC and the editions' wrench fork checked out
beside this repository, at the commits deps.json pins. Making an edition from
a player's discs needs neither: the few OpenRAC modules that read the discs
are carried in common/openrac_editor, copied from the pinned commit.

  # checkouts at the pins? pins pushed? copies as at the pin?
  python common/deps.py check  [--openrac DIR] [--wrench DIR] [--strict]
  # pin the checkouts' current HEADs
  python common/deps.py lock   [--openrac DIR] [--wrench DIR]
  # copy the carried modules from the pinned OpenRAC commit
  python common/deps.py vendor [--openrac DIR]

OPENRAC and WRENCH override the default checkouts (../OpenRAC, ../wrench).
check exits 1 when a carried module differs from the pinned commit's; with
--strict (CI), also when a checkout is missing or a pin is on no remote branch.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEPS = ROOT / "deps.json"


def git(repo: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True)


def head(repo: Path) -> str | None:
    r = git(repo, "rev-parse", "HEAD")
    return r.stdout.decode().strip() if r.returncode == 0 else None


def blob(repo: Path, commit: str, path: str) -> bytes | None:
    r = git(repo, "show", f"{commit}:{path}")
    return r.stdout if r.returncode == 0 else None


def check(deps: dict, dirs: dict[str, Path], strict: bool = False) -> int:
    bad = 0
    for name, dep in deps.items():
        repo, pin = dirs[name], dep["commit"]
        at = head(repo)
        if at is None:
            print(f"{name}: no checkout at {repo}")
            bad |= strict
            continue
        state = "at the pin" if at == pin else f"at {at[:12]}, pin is {pin[:12]} (deps.py lock to move the pin)"
        print(f"{name}: {repo} {state}")
        if git(repo, "cat-file", "-e", f"{pin}^{{commit}}").returncode != 0:
            print(f"{name}:   pinned commit {pin[:12]} is not in this checkout")
            bad |= strict
            continue
        remotes = git(repo, "branch", "-r", "--contains", pin).stdout.decode().split()
        if not remotes:
            print(f"{name}:   pinned commit {pin[:12]} is on no remote branch yet: "
                  f"push it to {dep['url']} ({dep['branch']})")
            bad |= strict
        vendor = dep.get("vendor")
        if vendor:
            for source, f in vendor["files"].items():
                want = blob(repo, pin, source)
                have = ROOT / vendor["to"] / f
                if want is None or not have.exists() or have.read_bytes() != want:
                    print(f"{name}:   {vendor['to']}/{f} differs from {source} at the pin (deps.py vendor)")
                    bad = 1
    return bad


def lock(deps: dict, dirs: dict[str, Path]) -> int:
    # Swap the hashes in the text, so deps.json keeps its layout and line endings.
    text = DEPS.read_bytes().decode()
    for name, dep in deps.items():
        at = head(dirs[name])
        if at is None:
            print(f"{name}: no checkout at {dirs[name]}", file=sys.stderr)
            return 1
        text = text.replace(f'"{dep["commit"]}"', f'"{at}"')
        print(f"{name}: pinned {at}")
    DEPS.write_bytes(text.encode())
    return 0


def vendor(deps: dict, dirs: dict[str, Path]) -> int:
    dep = deps["openrac"]
    v, pin = dep["vendor"], dep["commit"]
    out = ROOT / v["to"]
    out.mkdir(parents=True, exist_ok=True)
    for source, f in v["files"].items():
        data = blob(dirs["openrac"], pin, source)
        if data is None:
            print(f"{source} not found at {pin[:12]} in {dirs['openrac']}", file=sys.stderr)
            return 1
        (out / f).parent.mkdir(parents=True, exist_ok=True)
        (out / f).write_bytes(data)
    # openrac_common finds its root (repo_root) as the directory holding games/ and either
    # OpenRAC's AGENTS.md or the STANDALONE note of a bundled copy, the launcher's kind.
    (out / "STANDALONE").write_text(
        "OpenRAC's disc readers as rac-editions carries them (common/deps.py vendor).\n"
        f"commit: {pin}\n"
    )
    (out / "README.md").write_text(
        "# OpenRAC's disc readers\n\n"
        f"Copied unchanged from OpenRAC at commit `{pin}`\n"
        f"({dep['url']}): the readers in its `editor/`, at the top here, under the\n"
        "MIT license ([LICENSE](LICENSE)); the `tools/openrac_common` helpers they\n"
        "import and the `games/*/game.json` manifests they read, under the GNU GPL v3\n"
        "or later, as rac-editions is. The editions' tools import these instead of an\n"
        "OpenRAC checkout, so making an edition from a player's discs needs no OpenRAC.\n"
        "The `STANDALONE` note marks this directory as the readers' OpenRAC root, as\n"
        "in the launcher's bundled copy.\n\n"
        "Do not edit them here: change them in OpenRAC, move the pin\n"
        "(`python common/deps.py lock`) and copy them again\n"
        "(`python common/deps.py vendor`). `python common/deps.py check` reports a\n"
        "copy that differs from the pinned commit's.\n"
    )
    print(f"copied {len(v['files'])} files from OpenRAC at {pin[:12]} to {v['to']}/")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("command", choices=["check", "lock", "vendor"])
    ap.add_argument("--openrac", type=Path, default=Path(os.environ.get("OPENRAC") or ROOT.parent / "OpenRAC"))
    ap.add_argument("--wrench", type=Path, default=Path(os.environ.get("WRENCH") or ROOT.parent / "wrench"))
    ap.add_argument("--strict", action="store_true", help="check: a missing checkout or an unpushed pin fails too")
    args = ap.parse_args()
    deps = json.loads(DEPS.read_text())
    dirs = {"openrac": args.openrac, "wrench": args.wrench}
    if args.command == "check":
        return check(deps, dirs, args.strict)
    return {"lock": lock, "vendor": vendor}[args.command](deps, dirs)


if __name__ == "__main__":
    sys.exit(main())
