"""Moby skeletons and animation sequences, as glTF-ready joints and keys.

Layouts and semantics follow ReRAC (ISC): docs/formats/moby_rac1.md §3-§4
and docs/plan/moby_animation.md, which read them from the game's
evaluator (fun_0020e0e0) and its per-tick step (fun_0020d580).

- Skeleton (class +0x14): one matrix per joint, four rows of four floats;
  rows 0-2 are the images of the axes, row 3 the translation in packed
  model units (x 1024). The game skins with F = P * S, P the joint's pose,
  so S is the inverse bind matrix; the rows' w lanes are never read.
- common_trans (class +0x18): 16 bytes per joint, the rest translation
  relative to the parent and a word that is 0 for a root, else
  0x70000000 + 0x40 * parent (a scratchpad address).
- Sequences: class +0x48 lists sequence offsets (0: empty). A sequence is
  a 0x1c-byte header (frame count at +0x10, a rate override at +0x18),
  then frame offsets. A frame is a 16-byte header (rate, time in 1/8
  ticks, payload qwords, quaternion bytes, scale count, translation
  offset and count), one quaternion per joint (4 x s16 / 32768), then
  scale records (3 x u16 / 4096, joint, flags: 0x80 inherited by the
  children, else applied after the chain) and translation records
  (3 x s16 in packed units, joint).
- A joint's local matrix is [R(q)^T * diag(s) | t]. R(q)^T is the
  rotation of the conjugate quaternion, so glTF gets (-x, -y, -z, w).
"""

from dataclasses import dataclass, field
import math

from formats import FormatError, span, unpack

TICKS_PER_SECOND = 60  # ReRAC infers a 60 Hz animation tick.
SCRATCHPAD = 0x70000000
# A channel whose keys all stay this close to the bind pose is left out
# (model units and scale factors; quaternion components use ROTATION).
# The game's rest translations and bind matrices disagree by about 1e-6.
TOLERANCE = 1e-5
ROTATION = 1e-4
ONE = (1.0, 1.0, 1.0)

Vec3 = tuple[float, float, float]
Quat = tuple[float, float, float, float]


@dataclass
class Joint:
    """A joint's bind pose relative to its parent, in model units."""
    parent: int | None
    translation: Vec3
    rotation: Quat
    scale: Vec3
    inverse_bind: list[float]  # Column-major 4x4: model space to joint space.
    post_scale: bool = False   # Some key scales it after the chain.


@dataclass
class Animation:
    """A sequence: key times in seconds and a value per key for each
    channel, keyed by (joint, path) with path "rotation", "translation",
    "scale" or "post_scale". Channels that stay at the bind pose are left
    out. A looping sequence repeats its first key at the end."""
    name: str
    times: list[float]
    tracks: dict[tuple[int, str], list[tuple]] = field(default_factory=dict)
    loop: bool = False


@dataclass
class Skeleton:
    joints: list[Joint]
    animations: list[Animation]


def inverse3(a):
    """Inverse of a 3x3 matrix (rows) and its determinant."""
    (p, q, r), (s, t, u), (v, w, x) = a
    det = p * (t * x - u * w) - q * (s * x - u * v) + r * (s * w - t * v)
    if not abs(det) > 1e-12:
        raise FormatError("singular moby skeleton matrix")
    return [[(t * x - u * w) / det, (r * w - q * x) / det, (q * u - r * t) / det],
            [(u * v - s * x) / det, (p * x - r * v) / det, (r * s - p * u) / det],
            [(s * w - t * v) / det, (q * v - p * w) / det, (p * t - q * s) / det]], det


def multiply(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def apply(a, v):
    return [sum(a[i][k] * v[k] for k in range(3)) for i in range(3)]


def normalised(q) -> Quat:
    length = math.sqrt(sum(c * c for c in q))
    if not length > 0.5:
        raise FormatError("invalid moby joint quaternion")
    return tuple(c / length for c in q)


def quaternion(m) -> Quat:
    """The unit quaternion (x, y, z, w), w >= 0, of a rotation matrix (rows, column vectors)."""
    trace = m[0][0] + m[1][1] + m[2][2]
    if trace > 0:
        s = 2 * math.sqrt(trace + 1)
        q = ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, s / 4)
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = 2 * math.sqrt(1 + m[0][0] - m[1][1] - m[2][2])
        q = (s / 4, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    elif m[1][1] > m[2][2]:
        s = 2 * math.sqrt(1 + m[1][1] - m[0][0] - m[2][2])
        q = ((m[0][1] + m[1][0]) / s, s / 4, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    else:
        s = 2 * math.sqrt(1 + m[2][2] - m[0][0] - m[1][1])
        q = ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, s / 4, (m[1][0] - m[0][1]) / s)
    return normalised(q if q[3] >= 0 else tuple(-c for c in q))


def decompose(a, t) -> tuple[Vec3, Quat, Vec3]:
    """Translation, rotation and scale of [a | t]; a must have no shear."""
    scale = [math.sqrt(sum(a[r][c] ** 2 for r in range(3))) for c in range(3)]
    if min(scale) < 1e-9:
        # A joint collapsed to nothing in the bind pose (the Blaster's and the Pyrocitor's, parts
        # their animations scale up): no rotation of its own, its axes' lengths as they are.
        return tuple(t), (0.0, 0.0, 0.0, 1.0), tuple(s if s >= 1e-9 else 0.0 for s in scale)
    if inverse3(a)[1] < 0:
        scale[0] = -scale[0]  # Mirrored: flip one axis so that the rest is a rotation.
    rotation = [[a[r][c] / scale[c] for c in range(3)] for r in range(3)]
    for i in range(3):
        for j in range(3):
            if abs(sum(rotation[r][i] * rotation[r][j] for r in range(3)) - (i == j)) > 1e-4:
                raise FormatError("moby joint matrix has shear")
    return tuple(t), quaternion(rotation), tuple(scale)


def bind_pose(data: bytes, joint_count: int, skeleton: int, common: int) -> tuple[list[Joint], list[Vec3]]:
    """Joints in the bind pose the skeleton matrices invert, and each joint's
    rest translation from common_trans, in model units."""
    joints, matrices, rest = [], [], []
    for j in range(joint_count):
        rows = [unpack("<3f", data, skeleton + j * 64 + r * 16) for r in range(4)]
        if not all(math.isfinite(v) for row in rows for v in row):
            raise FormatError("invalid moby skeleton matrix")
        x, y, z, word = unpack("<3fI", data, common + j * 16)
        if word == 0:
            parent = None
        elif word >= SCRATCHPAD and (word - SCRATCHPAD) % 0x40 == 0 and (word - SCRATCHPAD) // 0x40 < j:
            parent = (word - SCRATCHPAD) // 0x40
        else:
            raise FormatError("invalid moby joint parent")
        rest.append((x / 1024, y / 1024, z / 1024))
        # Column-vector form S = [a | b]: a's columns are the stored rows.
        a = [[rows[c][r] for c in range(3)] for r in range(3)]
        b = [v / 1024 for v in rows[3]]
        matrices.append((a, b))
        pose_a, _ = inverse3(a)  # The bind pose P = S^-1 ...
        pose_b = [-v for v in apply(pose_a, b)]
        if parent is not None:  # ... relative to the parent: S_parent * P.
            sa, sb = matrices[parent]
            pose_a, pose_b = multiply(sa, pose_a), [v + w for v, w in zip(apply(sa, pose_b), sb)]
        joints.append(Joint(parent, *decompose(pose_a, pose_b),
                            [v for row in rows[:3] for v in (*row, 0.0)] + [*b, 1.0]))
    return joints, rest


def keys(base: bytes, at: int, joint_count: int, rest: list[Vec3]) -> tuple[list[float], list[dict], bool]:
    """A sequence's key times in ticks, each key's channel values, and whether it loops.

    Each interval lasts 1 / rate ticks: the sequence's rate override, or
    else the earlier frame's own rate. The last frame's rate leads back to
    the first; a rate of 0 there stops the sequence on its last frame. So
    does a negative one here (two classes on level 2), where the game would
    step backwards. A zero-length interval (rate infinity) drops its first
    key.
    """
    count, = unpack("<B", base, at + 0x10)
    override, = unpack("<f", base, at + 0x18)
    if not count or not (override == 0 or 0 < override < math.inf):
        raise FormatError("invalid moby sequence frame count or rate")
    times, values, tick = [], [], 0.0
    for k in range(count):
        frame, = unpack("<I", base, at + 0x1c + 4 * k)
        rate, _, qwc, quat_bytes, scales, translations_at, translations = unpack("<fh5H", base, frame)
        if (frame >> 28 or quat_bytes != 8 * joint_count or translations_at != quat_bytes + 8 * scales
                or qwc != (translations_at + 8 * translations + 15) >> 4):
            raise FormatError("unexpected moby frame layout")
        payload = span(base, frame + 16, qwc * 16)
        key = {}
        for j in range(joint_count):
            x, y, z, w = unpack("<4h", payload, j * 8)
            key[(j, "rotation")] = normalised((-x / 32768, -y / 32768, -z / 32768, w / 32768))
            key[(j, "translation")] = rest[j]
            key[(j, "scale")] = key[(j, "post_scale")] = ONE
        for i in range(scales):
            sx, sy, sz, j, flags = unpack("<3H2B", payload, quat_bytes + i * 8)
            if j >= joint_count or flags & 0x7f:
                raise FormatError("invalid moby scale record")
            key[(j, "scale" if flags & 0x80 else "post_scale")] = (sx / 4096, sy / 4096, sz / 4096)
        for i in range(translations):
            tx, ty, tz, j, pad = unpack("<3h2B", payload, translations_at + i * 8)
            if j >= joint_count or pad:
                raise FormatError("invalid moby translation record")
            key[(j, "translation")] = (tx / 1024, ty / 1024, tz / 1024)
        if times and times[-1] == tick:
            times.pop()
            values.pop()
        times.append(tick)
        values.append(key)
        step = override or rate
        if k + 1 == count:
            loop = count > 1 and 0 < step < math.inf
            if loop:
                times.append(tick + 1 / step)
                values.append(values[0])
        elif not 0 < step <= math.inf:
            raise FormatError("invalid moby frame rate")
        elif step != math.inf:
            tick += 1 / step
    return times, values, loop


def skeleton(data: bytes, sequences: list[tuple[str, bytes, int]]) -> Skeleton | None:
    """The class's joints in their bind pose and its animations, or None for
    a class without joints, skeleton matrices or sequences.

    sequences lists (name, data the frame offsets are relative to, offset).
    """
    joint_count = data[8]
    matrices, common = unpack("<2i", data, 0x14)
    if not joint_count or not matrices or not common:
        return None
    joints, rest = bind_pose(data, joint_count, matrices, common)
    if not sequences:
        # No sequences of its own (Ratchet's come from ratchet_seq, or the game fills the slots at
        # run time): the skin still matters, the port poses it from the game's memory.
        return Skeleton(joints, [])
    decoded = [(name, *keys(base, at, joint_count, rest)) for name, base, at in sequences]

    def bind(channel):
        joint, path = joints[channel[0]], channel[1]
        return {"rotation": joint.rotation, "translation": joint.translation,
                "scale": joint.scale, "post_scale": ONE}[path]

    def moves(channel, value) -> bool:
        reference = bind(channel)
        if channel[1] == "rotation":  # q and -q are the same rotation.
            return min(sum((a - b) ** 2 for a, b in zip(value, reference)),
                       sum((a + b) ** 2 for a, b in zip(value, reference))) > ROTATION ** 2
        return max(abs(a - b) for a, b in zip(value, reference)) > TOLERANCE

    channels = sorted({channel for _, _, values, _ in decoded for key in values for channel, value in key.items()
                       if moves(channel, value)})
    if not channels:
        return Skeleton(joints, [])  # Every key is the bind pose; the skin is still needed.
    for joint, path in channels:
        joints[joint].post_scale |= path == "post_scale"
    animations = []
    for name, times, values, loop in decoded:
        animation = Animation(name, [t / TICKS_PER_SECOND for t in times], loop=loop)
        for channel in channels:
            track = [key[channel] for key in values]
            if not any(moves(channel, value) for value in track):
                continue  # Left at the bind pose, as Godot does for a missing track.
            if channel[1] == "rotation":  # Keep neighbours in one hemisphere.
                for i in range(1, len(track)):
                    if sum(a * b for a, b in zip(track[i], track[i - 1])) < 0:
                        track[i] = tuple(-c for c in track[i])
            animation.tracks[channel] = track
        if not animation.tracks:  # glTF needs a channel: hold joint 0 still.
            animation.tracks[(0, "rotation")] = [joints[0].rotation] * len(times)
        animations.append(animation)
    return Skeleton(joints, animations)


def class_sequences(data: bytes) -> list[tuple[str, bytes, int]]:
    """The class's own sequences: (name, blob, offset), named by slot."""
    count = data[0xc]
    offsets = unpack(f"<{count}i", data, 0x48)
    return [(f"seq_{i:02}", data, offset) for i, offset in enumerate(offsets) if offset > 0]


def ratchet_sequences(level) -> list[tuple[str, bytes, int]]:
    """Ratchet's sequences: the core index +0x78 points to 256 core offsets,
    each a sequence whose frame offsets are relative to itself. ReRAC takes
    the slot as the sequence number."""
    at, = unpack("<I", level.index, 0x78)
    if not at:
        return []
    return [(f"ratchet_seq_{i:03}", level.block(offset), 0)
            for i, offset in enumerate(unpack("<256i", level.index, at)) if offset > 0]
