#!/usr/bin/env python3
"""
Make the Executive Edition's image from a release payload (tools/payload.py,
written by tools/release.py) and the player's own discs: Ratchet & Clank
(PAL), and Going Commando (USA) for the parts built from it. This is the
installer's reference implementation: the C installer core makes the same
image byte for byte.

  python tools/install.py [--rac1 RAC1.iso] [--rac2 RAC2.iso | --no-rac2] [-o OUT.iso | --in-place]
                          [--economy skill|classic] [--without SWITCH ...]
                          [--payload build/payload] [--wrench DIR] [--unpacked DIR] [--data DIR]
  python tools/install.py --serve [--payload build/payload] [--wrench DIR] [--unpacked DIR] [--data DIR]

The discs default to the OpenRAC checkout's baseroms beside rac-editions,
the image to build/iso/executive-edition.iso; when an emulator has that
open, the image is numbered instead (executive-edition-2.iso, and so on).
--in-place makes the PAL disc's image itself the edition, after writing
IMAGE.restore beside it. --economy is Challenge Mode's bolts: skill (the
default: x1 base and the no-hit bolt multiplier) or classic (retail's
doubled drops, without the bolt_multiplier feature's words). --without
leaves out one of the features a player may switch off (payload.SWITCHES:
charge_boots, strafing, quick_select_pages); without Going Commando
(--no-rac2) the Charge Boots and strafing are left out.

--serve speaks the builder's protocol (installer/README.md, "The
protocol") on standard input and output instead. --data is a folder kept
between runs, with the discs' hashes, so a disc is hashed once.

In order: the discs are checked by hash; the image is a copy of the PAL disc;
the mod segment gets its holes filled from the discs and is added to the
player's boot executable as a second loadable segment, with the
executable's own changed words, and the executable goes in new sectors at
the end of the image; the level programs' changed words are written in
place; then the levels' data archives get the classes the edition adds
(the Charge Boots, Kalebo III's sign, the infobot and the one-planet items)
and the seed glyphs in their fonts, and the edition's HelpDesk lines are
added at the end.

A feature left out loses its words, its holes stay zero, and its HelpDesk
lines and its class are not added; the mod segment's ee_features word
(src/features.c) says which features are in.

Until the edition's own converters and compressor replace them, the class
splices still use wrench (--wrench: the edition's fork, built) and wrench's
unpacks of the two discs (--unpacked: OpenRAC's assets/wrench, with
rac1-pal and rac2-ntsc in it).
"""
import argparse
import contextlib
import errno
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
COMMON = HERE.parents[2] / "common"
sys.path.insert(0, str(COMMON))
sys.path.insert(0, str(HERE))

import iso_image  # noqa: E402
import payload  # noqa: E402

EXE_NAME = "SCES_509.16;1"
PROTOCOL = 1
# An image grows by the executable, the archives moved to new sectors and the HelpDesk lines, about 270 MB;
# updating in place also needs the restore file, about as large.
GROWTH = 320 << 20
# The games' discs the installer knows by their boot file (SYSTEM.CNF's BOOT2): the game and the region, so a
# wrong version is named as the edition names that game's disc.
BOOT_FILES = {
    "SCES_509.16": ("rac1", "PAL"),
    "SCUS_971.99": ("rac1", "USA"),
    "SCUS_972.68": ("rac2", "USA"),
    "SCES_516.07": ("rac2", "PAL"),
}
RESTORE_MAGIC = b"RCEDREST"


class InstallError(Exception):
    """A failure the builder turns into words: CODE is one of the protocol's error codes."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


class Job:
    """What a build reports, and whether it has been cancelled. The command line prints; --serve sends events."""

    def __init__(self):
        self.cancelled = threading.Event()
        self.process = None

    def log(self, text: str) -> None:
        print(text, flush=True)

    def progress(self, stage: str, done: int, total: int) -> None:
        pass

    def check(self) -> None:
        if self.cancelled.is_set():
            raise InstallError("cancelled", "cancelled")

    def run(self, *cmd, on_line=None) -> None:
        """A tool of the pipeline, its output as log lines (each also given to ON_LINE); stopped when the job is
        cancelled."""
        self.check()
        self.log("== " + Path(str(cmd[1])).name)
        # stdin is closed: under --serve the installer's own stdin is the builder's protocol, never a tool's.
        # Unbuffered, a tool's lines arrive as it prints them, so the progress that counts them moves with them.
        self.process = subprocess.Popen([str(c) for c in cmd], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, errors="replace",
                                        env={**os.environ, "PYTHONUNBUFFERED": "1"})
        for line in self.process.stdout:
            self.log(line.rstrip())
            if on_line:
                on_line(line)
        code = self.process.wait()
        self.process = None
        self.check()
        if code != 0:
            raise InstallError("internal", f"{Path(str(cmd[1])).name} failed (exit {code})")


def free_name(path: Path) -> Path:
    """PATH, or a numbered name beside it while an emulator has PATH open (it cannot be overwritten)."""
    n = 1
    out = path
    while out.exists():
        try:
            out.open("ab").close()
            return out
        except OSError:
            n += 1
            out = path.with_name(f"{path.stem}-{n}{path.suffix}")
    return out


def hash_file(path: Path, job: Job, stage: str = "hash") -> str:
    import hashlib
    h = hashlib.sha256()
    total = path.stat().st_size
    done = 0
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 24), b""):
            job.check()
            h.update(chunk)
            done += len(chunk)
            job.progress(stage, done, total)
    return h.hexdigest()


class HashCache:
    """The SHA-256 of every disc image hashed before, by its resolved path, size and modification time: a disc
    checked when it was added is not hashed again for the build. Kept in DATA/hashes.json between runs."""

    def __init__(self, data: Path):
        self.path = data / "hashes.json"
        self.lock = threading.Lock()
        try:
            self.known = json.loads(self.path.read_text())
        except (OSError, ValueError):
            self.known = {}

    @staticmethod
    def key(path: Path) -> str:
        st = path.stat()
        return f"{path.resolve()}|{st.st_size}|{st.st_mtime_ns}"

    def get(self, path: Path) -> str | None:
        with self.lock:
            return self.known.get(self.key(path))

    def put(self, path: Path, digest: str) -> None:
        with self.lock:
            self.known[self.key(path)] = digest
            try:                                # a cache that cannot be saved only costs a second hash later
                self.path.parent.mkdir(parents=True, exist_ok=True)
                part = self.path.with_name(self.path.name + ".part")
                part.write_text(json.dumps(self.known, indent=1))
                os.replace(part, self.path)
            except OSError:
                pass


def disc_hash(path: Path, stage: str, job: Job, cache: HashCache) -> tuple[str, bool]:
    """PATH's SHA-256 and whether it was checked earlier: from CACHE then (one progress event, done == total),
    else hashed with progress events in STAGE."""
    size = path.stat().st_size
    digest = cache.get(path)
    if digest:
        job.progress(stage, size, size)
        return digest, True
    job.log(f"checking {path} ({size / (1 << 30):.1f} GB)")
    digest = hash_file(path, job, stage)
    cache.put(path, digest)
    return digest, False


def disc_label(edition: dict, game: str, region: str | None = None) -> str:
    names = edition["disc_names"][game]
    return f"{names['name']} ({region or names['region']})"


@contextlib.contextmanager
def writing(path: Path):
    """Failures to write PATH as the protocol's errors: no_space for a full drive, else not_writable."""
    try:
        yield
    except OSError as e:
        if e.errno == errno.ENOSPC:
            raise InstallError("no_space", f"{path}: the drive is full") from e
        raise InstallError("not_writable", f"{path}: {e.strerror or e}") from e


def copy_file(src: Path, dst: Path, job: Job) -> None:
    total = src.stat().st_size
    done = 0
    with src.open("rb") as f, dst.open("wb") as out:
        for chunk in iter(lambda: f.read(1 << 24), b""):
            job.check()
            out.write(chunk)
            done += len(chunk)
            job.progress("write", done, total)


def identify(path: Path, edition: dict, job: Job, cache: HashCache) -> dict:
    """Which disc PATH is: by its boot file, then by its hash against the edition's."""
    if not path.is_file():
        raise InstallError("missing_disc", f"{path} does not exist")
    try:
        with path.open("rb") as f:
            cnf = iso_image.read_file(f, "SYSTEM.CNF;1").decode("ascii", "replace")
            f.seek(iso_image.VOLUME_SIZE)
            volume = struct.unpack("<I", f.read(4))[0] * iso_image.SECTOR
    except (SystemExit, OSError, struct.error, IndexError, ValueError):     # not an ISO 9660 image of a PS2 disc
        job.log(f"{path}: not an image of a PS2 disc")
        return {"disc": None, "game": None, "name": None, "region": None, "sha256": None, "known": False}
    boot = next((line.split("\\")[-1].split(";")[0] for line in cnf.splitlines() if line.startswith("BOOT2")), "")
    game, region = BOOT_FILES.get(boot, (None, None))
    name = edition["disc_names"][game]["name"] if game in edition["disc_names"] else None
    if path.stat().st_size < volume:
        raise InstallError("image_incomplete", f"{path} ends before its last sector: the image is incomplete")
    digest, earlier = disc_hash(path, "hash", job, cache)
    known = game is not None and edition["discs"].get(game) == digest
    if earlier:
        job.log(f"{path}: checked earlier")
    if known:
        job.log(f"sha256 {digest} matches {disc_label(edition, game)}")
    elif name:
        job.log(f"sha256 {digest}: {name} ({region}), not the version this edition is built from")
    else:
        job.log(f"sha256 {digest}: not a disc of this edition")
    return {"disc": game if known else None, "game": game, "name": name, "region": region, "sha256": digest,
            "known": known}


def mod_segment(pay: Path, discs: dict, left_out: set[str], step) -> bytes:
    """The mod segment with its holes filled, but for LEFT_OUT's, which stay zero; STEP() after each hole."""
    seg = bytearray((pay / "modseg.bin").read_bytes())
    filler = payload.Filler(discs)
    for hole in json.loads((pay / "holes.json").read_text()):
        if hole["symbol"] not in left_out:
            blob = filler.fill(hole["symbol"], hole["fill"])
            if len(blob) != hole["size"]:
                raise InstallError("internal",
                                   f"{hole['symbol']}: {len(blob)} bytes made, the hole holds {hole['size']}")
            seg[hole["offset"]:hole["offset"] + hole["size"]] = blob
        step()
    return bytes(seg)


def tsv(path: Path) -> list[list[str]]:
    return [line.split("\t") for line in path.read_text().splitlines() if line and not line.startswith("#")]


def check_output(discs: dict, output: dict, bytes_needed: int) -> None:
    """Raises not_writable for an output the build could not write (a folder, one of the discs, a file in use or
    read-only, a folder that cannot be made or written), and no_space for a drive with less than BYTES_NEEDED free."""
    rac1 = Path(discs["rac1"])
    try:
        if output["mode"] == "new":
            target = Path(output["path"])
            if target.is_dir():
                raise InstallError("not_writable", f"{target} is a folder: the image needs a file name")
            if any(target.resolve() == Path(p).resolve() for p in discs.values()):
                raise InstallError("not_writable", f"{target} is one of the discs the edition is built from")
            if target.exists():
                target.open("ab").close()       # in use (an emulator has it open) or read-only
            elif target.parent.is_dir():        # a name the system takes, in a folder that takes files
                target.open("xb").close()
                target.unlink()
            folder = target.parent
        elif output["mode"] == "in_place":
            rac1.open("r+b").close()
            folder = rac1.parent
        else:
            raise InstallError("internal", f"unknown output mode {output['mode']}")
        while not folder.exists():              # the folders the build would make: the nearest one there must take them
            if folder == folder.parent:         # a drive that is not there
                raise InstallError("not_writable", f"{folder} does not exist")
            folder = folder.parent
        if not folder.is_dir():
            raise InstallError("not_writable", f"{folder} is a file, not a folder")
        tempfile.TemporaryFile(dir=folder).close()
        free = shutil.disk_usage(folder).free
    except OSError as e:
        raise InstallError("not_writable", f"{e.filename or output.get('path')}: {e.strerror or e}") from e
    except ValueError as e:                     # a path the system cannot name
        raise InstallError("not_writable", f"{output.get('path')}: {e}") from e
    if free < bytes_needed:
        raise InstallError("no_space", f"{folder} has {free >> 20} MB free; the build needs {bytes_needed >> 20} MB")


def plan(edition: dict, discs: dict, options: dict, output: dict | None = None) -> dict:
    """What a build with these discs and options has in, what it leaves out and why, and the space it needs; with
    OUTPUT, also whether it can be written there."""
    if "rac1" not in discs:
        raise InstallError("missing_disc", "the edition is built from Ratchet & Clank (PAL)")
    for path in discs.values():
        if not Path(path).is_file():
            raise InstallError("missing_disc", f"{path} does not exist")
    without = options.get("without", [])
    unknown = [s for s in without if s not in payload.SWITCHES]
    if unknown or options.get("economy", "skill") not in payload.ECONOMIES:
        raise InstallError("internal", f"unknown options: {unknown or options.get('economy')}")
    on = [name for name, s in payload.SWITCHES.items() if name not in without and s["disc"] in discs]
    left_out = [{"id": name, "because": "chosen" if name in without else f"no_{s['disc']}"}
                for name, s in payload.SWITCHES.items() if name not in on]
    # updating in place also writes the restore file, beside a full copy made first
    growth = 2 * GROWTH if output and output["mode"] == "in_place" else GROWTH
    needed = Path(discs["rac1"]).stat().st_size + growth
    if output:
        check_output(discs, output, needed)
    return {"in": on, "left_out": left_out, "bytes_needed": needed}


class Steps:
    """The patch stage's progress, in shares of the time each step took in a measured build (PATCH_SHARES, out of
    about 100), so done/total rises evenly. A step whose tool prints a line per unit of work (a level spliced, a
    HelpDesk line made) moves within its share as the lines come."""

    def __init__(self, job: Job, steps: list):
        self.job, self.steps = job, steps
        self.total = sum(PATCH_SHARES[s[0]] for s in steps)

    def run(self, after: dict) -> None:
        """Each step's tool, then AFTER[its name]() where there is one."""
        done = 0
        self.job.progress("patch", 0, self.total)
        for name, cmd, *unit in self.steps:
            share = PATCH_SHARES[name]
            seen = 0

            def tick(line, start=done, share=share):
                nonlocal seen
                pattern, count = unit[0]
                if re.match(pattern, line):
                    seen += 1
                    self.job.progress("patch", start + share * min(seen, count) // count, self.total)

            self.job.run(*cmd, on_line=tick if unit else None)
            if name in after:
                after[name]()
            done += share
            self.job.progress("patch", done, self.total)


# Each patch step's share of the time a full build took, out of 100 (measured: the two level splices about 170 s
# each, the HelpDesk lines 47 s, Kalebo III's sign 9 s, the others under a second), and the line a tool prints per
# level it splices, of 19 (extra_classes first reads its 6 classes from the levels they come from, a line each).
PATCH_SHARES = {"gen_boots": 1, "splice_boots": 42, "splice_fonts": 42, "gen_sign": 1, "splice_sign": 2,
                "extra_classes": 42, "gen_helpdesk": 12, "splice_helpdesk": 1}
LEVEL_LINE = (r"level +\d+:", 19)
CLASS_OR_LEVEL_LINE = (r"(level +\d+:|class \d+ \()", 6 + 19)


def install(pay: Path, discs: dict, options: dict, output: Path, tools: dict, job: Job, cache: HashCache) -> None:
    """Make the image at OUTPUT (a new file)."""
    edition = json.loads((pay / "edition.json").read_text())
    for name, path in discs.items():
        digest, earlier = disc_hash(Path(path), "verify", job, cache)
        if digest != edition["discs"][name]:
            raise InstallError("unknown_disc", f"{path}: not the disc this edition is built from ({name})")
        label = disc_label(edition, name)
        job.log(f"{label}: checked earlier, sha256 {digest}" if earlier else f"sha256 {digest} matches {label}")
    rac1 = Path(discs["rac1"])
    discs = {name: Path(path) for name, path in discs.items()}
    economy = options.get("economy", "skill")
    on = plan(edition, discs, options)["in"]
    off = [name for name in payload.SWITCHES if name not in on]
    job.log(f"features left out: {', '.join(off) or 'none'}")
    left_out_holes = {h for name in off for h in payload.SWITCHES[name]["holes"]}
    left_out_words = set(payload.ECONOMIES[economy])
    left_out_words |= {f for name in off for f in payload.SWITCHES[name]["features"]}
    left_out_lines = {u for name in off for u in payload.SWITCHES[name]["helpdesk"]}

    holes = len(json.loads((pay / "holes.json").read_text()))
    converted = 0

    def step():
        nonlocal converted
        converted += 1
        job.check()
        job.progress("convert", converted, holes + 2)

    job.progress("convert", 0, holes + 2)
    seg = bytearray(mod_segment(pay, discs, left_out_holes, step))
    struct.pack_into("<I", seg, edition["features_word"], sum(payload.SWITCHES[name]["bit"] for name in on))
    switch_data = json.loads((pay / "switches.json").read_text())
    for name in off:
        for at, blob in switch_data.get(name, []):
            seg[at:at + len(blob) // 2] = bytes.fromhex(blob)
    with rac1.open("rb") as f:
        exe = bytearray(iso_image.add_segment(iso_image.read_file(f, EXE_NAME), edition["modseg_base"], bytes(seg)))
    step()
    for off_text, word in tsv(pay / "exe.tsv"):
        struct.pack_into("<I", exe, int(off_text, 16), int(word, 16))
    step()

    job.log(f"copying {rac1} -> {output}")
    with writing(output):
        output.parent.mkdir(parents=True, exist_ok=True)
        copy_file(rac1, output, job)
        image = iso_image.Image(output)
        image.replace_file(EXE_NAME, bytes(exe))
        words = [w for w in tsv(pay / "overlay.tsv") if w[0] not in left_out_words]
        for _feature, off_text, word in words:
            image.write_word(int(off_text, 16), int(word, 16))
        image.finish()
    job.log(f"the boot executable with the mod segment ({len(exe)} bytes); {len(words)} level-code words")

    py = sys.executable
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        unpacked = tools["unpacked"]
        sign = tmp / "gadgetron_sign.bin"
        boots = tmp / "charge_boots.bin"
        helpdesk = tmp / "helpdesk"
        openrac = unpacked.parents[1]           # gen_boots and gen_sign find the unpacks there
        wrenchbuild = tools["wrench"] / "bin/wrenchbuild.exe"
        if "charge_boots" in on:
            steps = [("gen_boots", [py, HERE / "gen_boots.py", "--openrac", openrac, "--wrench", tools["wrench"],
                                    "-o", boots]),
                     ("splice_boots", [py, HERE / "splice_boots.py", output, "--class-core", boots, "--texture",
                                       unpacked / "rac2-ntsc/moby_classes/unsorted/3696/0.png",
                                       "--wrench", wrenchbuild, "--work", tmp / "splice"], LEVEL_LINE)]
        else:
            steps = [("splice_fonts", [py, HERE / "splice_boots.py", output, "--fonts-only", "--wrench", wrenchbuild,
                                       "--work", tmp / "splice"], LEVEL_LINE)]
        steps += [
            ("gen_sign", [py, HERE / "gen_sign.py", "--openrac", openrac, "--wrench", tools["wrench"],
                          "--bank", unpacked / "rac1-pal", "-o", sign]),
            ("splice_sign", [py, HERE / "splice_sign.py", output, "--class-core", sign, "--retail", rac1,
                             "--wrench", wrenchbuild, "--work", tmp / "splice"]),
            ("extra_classes", [py, HERE / "extra_classes.py", "splice", output, "--wrench", wrenchbuild,
                               "--work", tmp / "classes"], CLASS_OR_LEVEL_LINE),
            ("gen_helpdesk", [py, HERE / "gen_helpdesk.py", "--iso", rac1, "--recipes", pay / "helpdesk.json",
                              "-o", helpdesk, "--c-out", tmp / "gen_helpdesk.c"],
             (r"slot \d+:", len(json.loads((pay / "helpdesk.json").read_text())["lines"]))),
            ("splice_helpdesk", [py, HERE / "splice_helpdesk.py", output, "--audio", helpdesk]),
        ]

        def leave_out_lines():
            for line in json.loads((pay / "helpdesk.json").read_text())["lines"]:
                if line["use"] in left_out_lines:
                    for vag in helpdesk.glob(f"{line['slot']}_*.vag"):
                        vag.unlink()

        Steps(job, steps).run(after={"gen_helpdesk": leave_out_lines})


def write_restore(original: Path, edition_image: Path, restore: Path, job: Job) -> None:
    """RESTORE: the original's length, then every sector the edition changes within it, with its original bytes.
    Layout: RESTORE_MAGIC, u64 original length, then records of u32 sector and 2048 bytes."""
    total = original.stat().st_size
    done = 0
    with original.open("rb") as a, edition_image.open("rb") as b, restore.open("wb") as out:
        out.write(RESTORE_MAGIC + struct.pack("<Q", total))
        sector = 0
        while True:
            old = a.read(1 << 20)
            if not old:
                break
            new = b.read(len(old))
            job.check()
            for k in range(0, len(old), iso_image.SECTOR):
                if old[k:k + iso_image.SECTOR] != new[k:k + iso_image.SECTOR]:
                    out.write(struct.pack("<I", sector + k // iso_image.SECTOR) + old[k:k + iso_image.SECTOR])
            sector += len(old) // iso_image.SECTOR
            done += len(old)
            job.progress("write", done, total)


def build(pay: Path, discs: dict, options: dict, output: dict, tools: dict, job: Job, cache: HashCache) -> dict:
    """A build as the protocol's build command asks for it: a new image, or the PAL disc's image updated in place
    (made beside it first, so a cancel or a failure leaves the original as it was)."""
    rac1 = Path(discs["rac1"])
    plan(json.loads((pay / "edition.json").read_text()), discs, options, output)
    target = Path(output["path"]) if output["mode"] == "new" else rac1
    work = target if output["mode"] == "new" else rac1.with_name(rac1.name + ".building")
    try:
        install(pay, discs, options, work, tools, job, cache)
        if output["mode"] == "in_place":
            restore = rac1.with_name(rac1.name + ".restore")
            with writing(restore):
                write_restore(rac1, work, restore, job)
            with writing(rac1):
                os.replace(work, rac1)
    except BaseException:
        with contextlib.suppress(OSError):
            work.unlink(missing_ok=True)
        raise
    return {"path": str(target), "sha256": hash_file(target, job, "verify")}


def editions_list(pay: Path) -> list[dict]:
    edition = json.loads((pay / "edition.json").read_text())
    return [{"id": edition["edition"], "name": edition["name"], "game": edition["game"], "version": edition["commit"],
             "discs": edition["disc_names"],
             "options": {"economy": list(payload.ECONOMIES),
                         "switches": [{"id": name, "needs": None if s["disc"] == "rac1" else s["disc"]}
                                      for name, s in payload.SWITCHES.items()]}}]


class ServeJob(Job):
    def __init__(self, send, cid):
        super().__init__()
        self.send, self.cid = send, cid

    def log(self, text: str) -> None:
        self.send({"event": "log", "id": self.cid, "text": text})

    def progress(self, stage: str, done: int, total: int) -> None:
        self.send({"event": "progress", "id": self.cid, "stage": stage, "done": done, "total": total})


def serve(pay: Path, tools: dict, cache: HashCache) -> int:
    """The builder's protocol: one JSON command per line in, one JSON event per line out. A build or an identify
    runs in a thread of its own, so discs are checked side by side and a cancel can reach any of them; the other
    commands answer in order."""
    lock = threading.Lock()
    out = sys.stdout

    def send(event: dict) -> None:
        with lock:
            out.write(json.dumps(event) + "\n")
            out.flush()

    sys.stdout = sys.stderr                     # anything printed by the pipeline stays off the protocol
    jobs, threads = {}, []
    edition = json.loads((pay / "edition.json").read_text())
    send({"event": "ready", "protocol": PROTOCOL, "installer": edition["commit"]})

    def answer(cid, fn, job):
        try:
            send({"event": "result", "id": cid, "value": fn(job)})
        except InstallError as e:
            send({"event": "error", "id": cid, "code": e.code, "message": str(e)})
        except Exception as e:                  # noqa: BLE001 (reported to the builder, never a traceback)
            send({"event": "error", "id": cid, "code": "internal", "message": f"{type(e).__name__}: {e}"})
        except SystemExit as e:
            send({"event": "error", "id": cid, "code": "internal", "message": str(e)})
        finally:
            jobs.pop(cid, None)

    for line in sys.stdin:
        if not line.strip():
            continue
        try:
            msg = json.loads(line)
            cid, cmd = msg["id"], msg["cmd"]
        except (json.JSONDecodeError, KeyError, TypeError) as e:
            send({"event": "error", "id": None, "code": "internal", "message": f"bad command: {e}"})
            continue
        job = ServeJob(send, cid)

        def background(fn):
            jobs[cid] = job
            thread = threading.Thread(target=answer, daemon=True, args=(cid, fn, job))
            threads.append(thread)
            thread.start()

        if cmd == "editions":
            answer(cid, lambda j: editions_list(pay), job)
        elif cmd == "identify":
            background(lambda j, path=Path(msg["path"]): identify(path, edition, j, cache))
        elif cmd == "plan":
            answer(cid, lambda j: plan(edition, msg["discs"], msg.get("options", {}), msg.get("output")), job)
        elif cmd == "build":
            background(lambda j, m=msg: build(pay, m["discs"], m.get("options", {}), m["output"], tools, j, cache))
        elif cmd == "cancel":
            running = jobs.get(msg.get("of"))
            if running:
                running.cancelled.set()
                if running.process:
                    running.process.kill()
            send({"event": "result", "id": cid, "value": {"cancelling": running is not None}})
        else:
            send({"event": "error", "id": cid, "code": "internal", "message": f"unknown command {cmd}"})
    for thread in threads:                      # the input's end: the commands still running finish and answer
        thread.join()
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    openrac = ROOT.parents[2] / "OpenRAC"
    ap.add_argument("--serve", action="store_true", help="speak the builder's protocol on stdin and stdout")
    ap.add_argument("--rac1", type=Path, default=openrac / "games/rac1/baserom/SCES_509.16.iso",
                    help="Ratchet & Clank (PAL)")
    ap.add_argument("--rac2", type=Path, default=openrac / "games/rac2/ntsc/baserom/SCUS_972.68.iso",
                    help="Ratchet & Clank: Going Commando (USA)")
    ap.add_argument("--no-rac2", action="store_true", help="build without Going Commando")
    ap.add_argument("--economy", choices=payload.ECONOMIES, default="skill")
    ap.add_argument("--without", action="append", default=[], choices=payload.SWITCHES)
    ap.add_argument("--payload", type=Path, default=ROOT / "build/payload")
    ap.add_argument("--wrench", type=Path, default=ROOT.parents[2] / "wrench")
    ap.add_argument("--unpacked", type=Path, default=openrac / "assets/wrench")
    ap.add_argument("--data", type=Path, default=ROOT / "build/installer-data",
                    help="a folder the installer keeps between runs (the discs' hashes)")
    ap.add_argument("-o", "--output", type=Path, default=None)
    ap.add_argument("--in-place", action="store_true", help="make the PAL disc's image the edition")
    args = ap.parse_args()
    tools = {"wrench": args.wrench, "unpacked": args.unpacked}
    cache = HashCache(args.data)
    if args.serve:
        return serve(args.payload, tools, cache)
    discs = {"rac1": args.rac1} if args.no_rac2 else {"rac1": args.rac1, "rac2": args.rac2}
    options = {"economy": args.economy, "without": args.without}
    output = {"mode": "in_place"} if args.in_place else \
        {"mode": "new", "path": str(args.output or free_name(ROOT / "build/iso/executive-edition.iso"))}
    try:
        result = build(args.payload, discs, options, output, tools, Job(), cache)
    except InstallError as e:
        print(f"install.py: {e} ({e.code})", file=sys.stderr)
        return 1
    print(f"-> {result['path']} (SHA-256 {result['sha256']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
