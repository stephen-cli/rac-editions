#!/usr/bin/env python3
"""
Going Commando's packed Ratchet animations, converted to R&C1's frame format.

R&C2 (SCUS_972.68) keeps Ratchet's sequences in each level's core data as
R&C1 does (core index + 0x78 -> 256 core offsets, frame offsets relative to
the sequence), but packs the frames: the top nibble of every frame offset is
0xF and the engine expands a frame at animation time into the very layout
R&C1 stores on disc (its expander is the hand-written routine at 0x308B20 in
the executable; this is a transcription of it):

  sequence   0x1C-byte header (bounding sphere, frame count, sound count,
             trigger count, triggers, rate override), the frame offsets, the
             triggers, then the sequence's packed data: a u32 of three
             bit-fields giving each frame's part offsets, a base pose of
             three s16 per joint, the scale and translation record counts
             (u8 each) and their base records (8 bytes each)
  frame      u16 1/rate, u16 (header word +4), then four parts whose
             offsets come from the u32: a bitstream of 10-bit values (three
             per 32-bit word, low bits first), s16 replacements, s8 deltas,
             one sign bit per joint (low bit first), and finally the scale
             and translation records' flags and values
  per joint  each of x, y, z starts from the base pose; the base's low two
             bits select: 0 keep, 1 add an s8 delta * 4, 2 add a 10-bit
             delta * 4, 3 replace with an s16; w = sqrt(1 - |xyz|^2) with the
             sign bit, in 1/32768 (clamped to 0x7FFF)
  records    each component starts from the base record; two flag bits per
             component: 0 keep, 1 add an s8 * 4, 2 replace with 16 bits
  output     R&C1's frame: 16-byte header (f32 rate, s16, u16 qwords, u16
             quaternion bytes = 8 * joints, u16 scale count, u16 translation
             offset, u16 translation count), 4 x s16 per joint, 8-byte scale
             records, 8-byte translation records (editor/moby_anim.py keys())

  python tools/rac2_anim.py validate [--rac2 ISO] [--rac1 ISO]   # R&C2 slot 4 (run) against R&C1's slot 4
  python tools/rac2_anim.py emit SLOT -o build/modseg/gen_anim.c  # one sequence as C data for the mod segment
"""
import argparse
import math
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OPENRAC = ROOT.parent / "OpenRAC"
RAC2_ISO = OPENRAC / "games/rac2/ntsc/baserom/SCUS_972.68.iso"
RAC1_ISO = OPENRAC / "games/rac1/baserom/SCES_509.16.iso"
SECTOR = 2048


def u8(b, o): return b[o]
def s8(b, o): return struct.unpack_from("<b", b, o)[0]
def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def s16(b, o): return struct.unpack_from("<h", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]


def wrap16(v: int) -> int:
    return ((v + 0x8000) & 0xFFFF) - 0x8000


# --- R&C2 disc -------------------------------------------------------------------

class Iso9660:
    """Just enough ISO 9660 to find a file by path (as games/rac2/ntsc/tools/levels.py)."""

    def __init__(self, path: Path):
        self.f = path.open("rb")

    def read(self, lba: int, size: int, offset: int = 0) -> bytes:
        self.f.seek(lba * SECTOR + offset)
        return self.f.read(size)

    def entries(self, lba: int, size: int) -> dict:
        d = self.read(lba, size)
        out, i = {}, 0
        while i < len(d):
            if d[i] == 0:
                i = (i // SECTOR + 1) * SECTOR
                continue
            at, length = u32(d, i + 2), u32(d, i + 10)
            name = d[i + 33:i + 33 + d[i + 32]].decode("latin1").split(";")[0]
            out[name] = (at, length)
            i += d[i]
        return out

    def find(self, path: str):
        pvd = self.read(16, SECTOR)
        at, length = u32(pvd, 158), u32(pvd, 166)
        for part in path.split("/"):
            at, length = self.entries(at, length)[part]
        return at, length


def rac2_ratchet(iso_path: Path, level: int = 1):
    """(joint count, {slot: sequence bytes}) of R&C2's Ratchet from level LEVEL's archive."""
    sys.path.insert(0, str(OPENRAC / "editor"))
    from level import decoded
    iso = Iso9660(iso_path)
    lba, _ = iso.find(f"G/LEVEL{level}.WAD")
    head = iso.read(lba, 0x60)
    data_sector, _ = struct.unpack_from("<II", head, 0x10)
    slots = struct.unpack_from("<24I", iso.read(lba, 0x60, data_sector * SECTOR), 0)
    index = iso.read(lba, slots[3], data_sector * SECTOR + slots[2])
    core = decoded(iso.read(lba, slots[19], data_sector * SECTOR + slots[18]))
    table_at = u32(index, 0x78)
    offsets = struct.unpack_from("<256I", index, table_at)
    ends = sorted(o for o in offsets if o) + [len(core)]
    seqs = {}
    for slot, o in enumerate(offsets):
        if o:
            seqs[slot] = core[o:ends[ends.index(o) + 1]]
    # the joint count: the class whose header words 1..3 match Ratchet's (bounding sphere w and
    # 111 joints); the sequences do not carry it. R&C1's Ratchet also has 111.
    return 111, seqs


# --- the expander ----------------------------------------------------------------

def sequence_parts(seq: bytes):
    """Frame count, (frame offsets without flags), rate override, special data offset."""
    fc, sc, tc, u13 = seq[0x10], seq[0x11], seq[0x12], seq[0x13]
    frames = [u32(seq, 0x1C + 4 * k) for k in range(fc)]
    if any(f >> 28 != 0xF for f in frames):
        raise SystemExit("not a packed sequence (frame flags)")
    special = 0x1C + 4 * fc + 4 * tc
    return fc, [f & 0x0FFFFFFF for f in frames], struct.unpack_from("<f", seq, 0x18)[0], special, (sc, tc, u13)


def unpack_frame(seq: bytes, joints: int, special: int, frame: int) -> bytes:
    J = joints
    packed = u32(seq, special)
    base = special + 4
    thing = special + 4 + 6 * J
    t1c, t2c = seq[thing], seq[thing + 1]
    t1 = thing + 2
    t2 = t1 + 8 * t1c
    inv, unk4 = u16(seq, frame), s16(seq, frame + 2)
    p1 = frame + 4
    p2 = frame + 4 + (packed & 0x3FF)
    p3 = frame + 4 + ((packed >> 10) & 0x7FF)
    p4 = frame + 4 + (packed >> 21)
    rate = (1.0 / inv) if inv else 0.0
    qwc = (J + t1c + t2c + 1) >> 1
    out = bytearray(struct.pack("<fh5H", rate, unk4, qwc, 8 * J, t1c, 8 * J + 8 * t1c, t2c))

    bits = {"t2": 22, "at": p1}

    def bits10() -> int:
        w = u32(seq, bits["at"])
        v = (w << bits["t2"]) & 0xFFFFFFFF
        v = v - 0x100000000 if v & 0x80000000 else v
        v >>= 22
        bits["t2"] -= 10
        if bits["t2"] <= 0:
            bits["t2"] = 22
            bits["at"] += 4
        return v

    xyz = []
    for j in range(J):
        comps = []
        for c in range(3):
            v = s16(seq, base + 6 * j + 2 * c)
            mode = v & 3
            if mode == 1:
                v += s8(seq, p3) << 2
                p3 += 1
            elif mode == 2:
                v += bits10() << 2
            elif mode == 3:
                v = s16(seq, p2)
                p2 += 2
            comps.append(wrap16(v))
        xyz.append(comps)
    for j in range(J):
        x, y, z = xyz[j]
        sign = (seq[p4 + j // 8] >> (j % 8)) & 1
        rest = 1.0 - (x * x + y * y + z * z) / float(1 << 30)
        w = int(math.sqrt(rest) * 32768.0) if rest > 0 else 0
        w = min(w, 0x7FFF)
        if sign:
            w = -w
        out += struct.pack("<4h", x, y, z, w)
    fp = p4 + (J + 7) // 8

    def records(count: int, at: int) -> int:
        nonlocal fp
        for i in range(count):
            rec = at + 8 * i
            flag = seq[fp]
            fp += 1
            vals = []
            for c in range(3):
                v = s16(seq, rec + 2 * c)
                m = (flag >> (2 * c)) & 3
                if m == 1:
                    v += s8(seq, fp) << 2
                    fp += 1
                elif m == 2:
                    v = (s8(seq, fp + 1) << 8) | seq[fp]
                    fp += 2
                vals.append(wrap16(v))
            out.extend(struct.pack("<3hH", *vals, u16(seq, rec + 6)))
        return at + 8 * count

    records(t1c, t1)
    records(t2c, t2)
    while len(out) % 16:
        out.append(0)
    if len(out) != 16 + 16 * qwc:
        raise SystemExit(f"frame size {len(out)} != 16 + 16 * {qwc}")
    return bytes(out)


def unpack_sequence(seq: bytes, joints: int):
    fc, frames, override, special, misc = sequence_parts(seq)
    return [unpack_frame(seq, joints, special, f) for f in frames], override, misc


# --- R&C1 reference ----------------------------------------------------------------

def rac1_ratchet(iso_path: Path, level: int = 0):
    sys.path.insert(0, str(OPENRAC / "editor"))
    from disc import Disc
    from level import load_level
    from formats import unpack
    with Disc(iso_path) as disc:
        info = next(i for i in disc.survey()["levels"] if i["id"] == level)
        lv = load_level(disc, info)
        at, = unpack("<I", lv.index, 0x78)
        offsets = unpack("<256i", lv.index, at)
        return {slot: lv.block(o) for slot, o in enumerate(offsets) if o > 0}


def rac1_frames(seq: bytes):
    fc = seq[0x10]
    out = []
    for k in range(fc):
        f = u32(seq, 0x1C + 4 * k)
        qwc = u16(seq, f + 6)
        out.append(seq[f:f + 16 + 16 * qwc])
    return out, struct.unpack_from("<f", seq, 0x18)[0]


def validate(args) -> int:
    joints, seqs = rac2_ratchet(args.rac2)
    r1 = rac1_ratchet(args.rac1)
    worst = 0
    for slot in (4, 3, 6):
        a, override2, misc = unpack_sequence(seqs[slot], joints)
        b, override1 = rac1_frames(r1[slot])
        print(f"slot {slot:#x}: R&C2 {len(a)} frames (override {override2}), R&C1 {len(b)} frames (override {override1})")
        if len(a) != len(b):
            print("  frame counts differ")
            continue
        for k, (fa, fb) in enumerate(zip(a, b)):
            ha, hb = struct.unpack_from("<fh5H", fa, 0), struct.unpack_from("<fh5H", fb, 0)
            qa = struct.unpack_from(f"<{4 * joints}h", fa, 16)
            qb = struct.unpack_from(f"<{4 * joints}h", fb, 16)
            dq = max(abs(x - y) for x, y in zip(qa, qb))
            dxyz = max(abs(x - y) for i, (x, y) in enumerate(zip(qa, qb)) if i % 4 != 3)
            dw = max(abs(x - y) for i, (x, y) in enumerate(zip(qa, qb)) if i % 4 == 3)
            recs_same = fa[16 + 8 * joints:] == fb[16 + 8 * joints:]
            worst = max(worst, dq)
            if k < 3 or dq > 8 or not recs_same or ha[2:] != hb[2:]:
                print(f"  frame {k:2d}: header R&C2 {ha} R&C1 {hb}; max |dxyz| {dxyz}, max |dw| {dw}, records equal {recs_same}")
    print(f"worst quaternion component difference: {worst} (of 32768)")
    return 0


def emit(args) -> int:
    joints, seqs = rac2_ratchet(args.rac2)
    frames, override, (sc, tc, u13) = unpack_sequence(seqs[args.slot], joints)
    seq = seqs[args.slot]
    name = args.name
    lines = [f"/* generated by tools/rac2_anim.py from Going Commando's Ratchet sequence {args.slot:#x} ({len(frames)} frames),",
             " * expanded to R&C1's frame layout; see the tool's docstring. Installed by src/strafe.c. */",
             "typedef unsigned int u32; typedef unsigned char u8;",
             f"static const u8 {name}_frames[] __attribute__((aligned(16))) = {{"]
    offsets, pos = [], 0
    blob = bytearray()
    for f in frames:
        offsets.append(len(blob))
        blob += f
    for i in range(0, len(blob), 16):
        lines.append("    " + ", ".join(f"0x{x:02X}" for x in blob[i:i + 16]) + ",")
    lines.append("};")
    bsphere = struct.unpack_from("<4I", seq, 0)
    counts = len(frames) | (sc << 8) | (0 << 16) | (u13 << 24)
    info = u32(seq, 0x18)
    words = [f"0x{w:08X}" for w in bsphere] + [f"0x{counts:08X}", "0", f"0x{info:08X}"] + \
            [f"(u32)({name}_frames + 0x{o:X})" for o in offsets]
    lines.append(f"const u32 {name}[{7 + len(frames)}] __attribute__((aligned(16))) = {{")
    lines.append("    " + ", ".join(words[:7]) + ",")
    for i in range(7, len(words), 4):
        lines.append("    " + ", ".join(words[i:i + 4]) + ",")
    lines.append("};")
    lines.append(f"const u32 {name}_frame_count = {len(frames)};")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n")
    print(f"wrote {args.output}: sequence {args.slot:#x}, {len(frames)} frames, {len(blob)} bytes of frames")
    return 0


# --- the gun-arm classes ------------------------------------------------------------

def rac2_class(iso_path: Path, class_id: int, level: int = 1) -> bytes:
    """Moby class CLASS_ID's blob from R&C2 level LEVEL: the core index's moby class
    table (+0x18: count, offset; 32-byte entries, core offset then class id), the blob
    ending where the next class starts."""
    sys.path.insert(0, str(OPENRAC / "editor"))
    from level import decoded
    iso = Iso9660(iso_path)
    lba, _ = iso.find(f"G/LEVEL{level}.WAD")
    data_sector, _ = struct.unpack_from("<II", iso.read(lba, 0x60), 0x10)
    slots = struct.unpack_from("<24I", iso.read(lba, 0x60, data_sector * SECTOR), 0)
    index = iso.read(lba, slots[3], data_sector * SECTOR + slots[2])
    core = decoded(iso.read(lba, slots[19], data_sector * SECTOR + slots[18]))
    count, at = struct.unpack_from("<II", index, 0x18)
    table = [struct.unpack_from("<ii", index, at + 32 * i) for i in range(count)]
    starts = sorted({o for o, _ in table if o > 0}) + [len(core)]
    start = next(o for o, cid in table if cid == class_id)
    return core[start:starts[starts.index(start) + 1]]


def class_sequence(cls: bytes, slot: int):
    """Sequence SLOT of a class stored unpacked (R&C1's frame layout, frame offsets
    relative to the class): its 0x1C-byte header and its frames."""
    if slot >= cls[0x0C]:
        raise SystemExit(f"the class has {cls[0x0C]} sequences, no {slot:#x}")
    seq = u32(cls, 0x48 + 4 * slot)
    frames = []
    for k in range(cls[seq + 0x10]):
        f = u32(cls, seq + 0x1C + 4 * k)
        if f >> 28 == 0xF:
            raise SystemExit("a packed sequence: use emit")
        frames.append(cls[f:f + 16 + 16 * u16(cls, f + 6)])
    return cls[seq:seq + 0x1C], frames


def emit_arm(args) -> int:
    """R&C2's gun-arm classes (moby classes 1 and 2: the right and the left arm, 20
    joints, the layer R&C2 and R&C1 alike play over Ratchet's arms while a trigger
    weapon is held) keep their sequences unpacked, in R&C1's layout, and match R&C1's
    arm classes sequence for sequence except 0x14 (R&C2's 30-frame backpedal arm,
    where R&C1 has the 14-frame stop) and a 24th sequence. Sequence SLOT of both, as
    loaded sequences (header, frame pointers) for the mod segment."""
    lines = [f"/* generated by tools/rac2_anim.py emit-arm from Going Commando's gun-arm classes 1 and 2,",
             f" * sequence {args.slot:#x}, R&C1's layout already; installed by src/gun_arm.c. */",
             "typedef unsigned int u32; typedef unsigned char u8;"]
    for class_id in (1, 2):
        header, frames = class_sequence(rac2_class(args.rac2, class_id), args.slot)
        name = f"{args.name}_{class_id}"
        lines.append(f"static const u8 {name}_frames[] __attribute__((aligned(16))) = {{")
        blob, offsets = bytearray(), []
        for f in frames:
            offsets.append(len(blob))
            blob += f
        for i in range(0, len(blob), 16):
            lines.append("    " + ", ".join(f"0x{x:02X}" for x in blob[i:i + 16]) + ",")
        lines.append("};")
        words = [f"0x{w:08X}" for w in struct.unpack_from("<7I", header)] + \
                [f"(u32)({name}_frames + 0x{o:X})" for o in offsets]
        lines.append(f"const u32 {name}[{len(words)}] __attribute__((aligned(16))) = {{")
        for i in range(0, len(words), 4):
            lines.append("    " + ", ".join(words[i:i + 4]) + ",")
        lines.append("};")
        print(f"class {class_id} sequence {args.slot:#x}: {len(frames)} frames, {len(blob)} bytes")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n")
    print(f"wrote {args.output}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    v = sub.add_parser("validate")
    v.add_argument("--rac2", type=Path, default=RAC2_ISO)
    v.add_argument("--rac1", type=Path, default=RAC1_ISO)
    v.set_defaults(fn=validate)
    e = sub.add_parser("emit")
    e.add_argument("slot", type=lambda s: int(s, 0))
    e.add_argument("--rac2", type=Path, default=RAC2_ISO)
    e.add_argument("--name", default="ee_backpedal_seq")
    e.add_argument("-o", "--output", type=Path, required=True)
    e.set_defaults(fn=emit)
    a = sub.add_parser("emit-arm")
    a.add_argument("slot", type=lambda s: int(s, 0))
    a.add_argument("--rac2", type=Path, default=RAC2_ISO)
    a.add_argument("--name", default="ee_gunarm_seq")
    a.add_argument("-o", "--output", type=Path, required=True)
    a.set_defaults(fn=emit_arm)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
