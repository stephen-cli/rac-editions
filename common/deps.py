#!/usr/bin/env python3
"""
The editions' pinned dependencies (deps.json) and the OpenRAC code they carry.

Building an edition needs OpenRAC and the editions' wrench fork checked out
beside this repository, at the commits deps.json pins. Making an edition from
a player's discs needs neither: the few OpenRAC modules that read the discs
are carried in common/openrac_editor, copied from the pinned commit.

  python common/deps.py check  [--openrac DIR] [--wrench DIR]   # checkouts at the pins? pins pushed? copies as at the pin?
  python common/deps.py lock   [--openrac DIR] [--wrench DIR]   # pin the checkouts' current HEADs
  python common/deps.py vendor [--openrac DIR]                  # copy the carried modules from the pinned OpenRAC commit

OPENRAC and WRENCH override the default checkouts (../OpenRAC, ../wrench).
check exits 1 when a carried module differs from the pinned commit's.
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


def check(deps: dict, dirs: dict[str, Path]) -> int:
    bad = 0
    for name, dep in deps.items():
        repo, pin = dirs[name], dep["commit"]
        at = head(repo)
        if at is None:
            print(f"{name}: no checkout at {repo}")
            continue
        state = "at the pin" if at == pin else f"at {at[:12]}, pin is {pin[:12]} (deps.py lock to move the pin)"
        print(f"{name}: {repo} {state}")
        if git(repo, "cat-file", "-e", f"{pin}^{{commit}}").returncode != 0:
            print(f"{name}:   pinned commit {pin[:12]} is not in this checkout")
            continue
        remotes = git(repo, "branch", "-r", "--contains", pin).stdout.decode().split()
        if not remotes:
            print(f"{name}:   pinned commit {pin[:12]} is on no remote branch yet: push it to {dep['url']} ({dep['branch']})")
        vendor = dep.get("vendor")
        if vendor:
            for f in vendor["files"]:
                want = blob(repo, pin, f"{vendor['from']}/{f}")
                have = ROOT / vendor["to"] / f
                if want is None or not have.exists() or have.read_bytes() != want:
                    print(f"{name}:   {vendor['to']}/{f} differs from {vendor['from']}/{f} at the pin (deps.py vendor)")
                    bad = 1
    return bad


def lock(deps: dict, dirs: dict[str, Path]) -> int:
    for name, dep in deps.items():
        at = head(dirs[name])
        if at is None:
            print(f"{name}: no checkout at {dirs[name]}", file=sys.stderr)
            return 1
        dep["commit"] = at
        print(f"{name}: pinned {at}")
    DEPS.write_text(json.dumps(deps, indent=2) + "\n")
    return 0


def vendor(deps: dict, dirs: dict[str, Path]) -> int:
    dep = deps["openrac"]
    v, pin = dep["vendor"], dep["commit"]
    out = ROOT / v["to"]
    out.mkdir(parents=True, exist_ok=True)
    for f in v["files"]:
        data = blob(dirs["openrac"], pin, f"{v['from']}/{f}")
        if data is None:
            print(f"{v['from']}/{f} not found at {pin[:12]} in {dirs['openrac']}", file=sys.stderr)
            return 1
        (out / f).write_bytes(data)
    (out / "README.md").write_text(
        "# OpenRAC's disc readers\n\n"
        f"Copied unchanged from OpenRAC's `{v['from']}/` at commit `{pin}`\n"
        f"({dep['url']}), under its MIT license ([LICENSE](LICENSE)). The editions'\n"
        "tools import these instead of an OpenRAC checkout, so making an edition\n"
        "from a player's discs needs no OpenRAC.\n\n"
        "Do not edit them here: change them in OpenRAC, move the pin\n"
        "(`python common/deps.py lock`) and copy them again\n"
        "(`python common/deps.py vendor`). `python common/deps.py check` reports a\n"
        "copy that differs from the pinned commit's.\n"
    )
    print(f"copied {len(v['files'])} files from {v['from']}/ at {pin[:12]} to {v['to']}/")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("command", choices=["check", "lock", "vendor"])
    ap.add_argument("--openrac", type=Path, default=Path(os.environ.get("OPENRAC") or ROOT.parent / "OpenRAC"))
    ap.add_argument("--wrench", type=Path, default=Path(os.environ.get("WRENCH") or ROOT.parent / "wrench"))
    args = ap.parse_args()
    deps = json.loads(DEPS.read_text())
    dirs = {"openrac": args.openrac, "wrench": args.wrench}
    return {"check": check, "lock": lock, "vendor": vendor}[args.command](deps, dirs)


if __name__ == "__main__":
    sys.exit(main())
