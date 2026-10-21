"""Moby classes: each class blob's high-LOD mesh in its bind pose, and its
skeleton and animations (moby_anim.py).

The core index's moby class table (+0x18) has 32-byte entries: the core
offset of the class blob (0 for classes without one), the class number,
two unknown words and 16 texture slots into the moby texture table. The
blob layout follows ReRAC's format notes and loader (ISC;
docs/formats/moby_rac1.md and crates/rc-formats/src/moby.rs), which
derive it from Wrench's moby reader and the game's own code. The PAL
disc has the same layout.

A class mesh is split into packets, each a VIF list (texture coordinates,
an index stream and optional GS texture blocks) and a vertex table. The
vertex cache, the VU0 matrix slots and the current texture carry over
from one packet to the next within a LOD list.
"""

from dataclasses import dataclass, field
import math
import struct

from formats import wad, FormatError, Texture, span, unpack
import games
from mesh import Mesh
from moby_anim import Skeleton, class_sequences, ratchet_sequences, skeleton

HEADER_SIZE = 0x48
# UNPACK commands: V2-16 with the write mask (texture coordinates), V4-8
# (index stream) and V4-32 (GS texture blocks), and their payload bytes per
# element. Addresses are relative to VIF1 TOPS (flag 0x8000).
ST, INDICES, GS_BLOCKS = 0x75, 0x6e, 0x6c
ELEMENT_SIZES = {ST: 4, INDICES: 4, GS_BLOCKS: 16}
ST_ADDRESS, INDEX_ADDRESS = 0x80c2, 0x812d
NO_STORE = 0xf4  # VU0 address meaning "do not keep this matrix".
# A GS texture block whose TEX0 word is -1: the game binds an 8x8 texture of
# 0x80 texels instead, so the faces show plain vertex colour. The mesh uses
# this key and the writer pairs it with GREY.
UNTEXTURED = ("moby", -1)
GREY = Texture(8, 8, bytes(64), bytes((0x80,) * 4) + bytes(1020))
# A metal packet's GS texture block names the level's chrome (-2) or glass (-3) map, which the game
# binds at load time (ReRAC moby_skinning_lighting.md 9: "Texture").
METAL_CHROME, METAL_GLASS = ("metal", -2), ("metal", -3)


@dataclass
class MobyClass:
    """A class's scale, its high-LOD mesh in model units (packed / 1024)
    and, for an animated class, its skeleton.

    Drawn size is model units times the class scale times the instance's
    own scale. skins[i] lists the (joint, weight) pairs of mesh vertex i,
    weights in 256ths summing to 256; the mesh carries them only when the
    class has a skeleton.
    """
    scale: float
    mesh: Mesh | None
    joint_count: int = 0
    skins: list[tuple[tuple[int, int], ...]] = field(default_factory=list)
    skeleton: Skeleton | None = None
    # The metal ("shine") packets' mesh: the parts the game draws a second time, sphere-mapped
    # with the level's chrome or glass map (texture keys METAL_CHROME / METAL_GLASS); its skins
    # are in metal.skins like the main mesh's. None for a class without metal packets.
    metal: Mesh | None = None


def normal(azimuth: int, elevation: int) -> tuple[float, float, float]:
    """Spherical angles in 256ths of a turn, as ReRAC decodes them from VU0 code."""
    a, e = azimuth * math.pi / 128, elevation * math.pi / 128
    return (math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))


def unpacks(data: bytes) -> list[tuple[int, int, int, bytes]]:
    """(command, immediate, count, payload) of each UNPACK in a VIF list; NOPs are skipped."""
    result, pos = [], 0
    while pos < len(data):
        word, = unpack("<I", data, pos)
        command, count, immediate = word >> 24, (word >> 16) & 0xff or 256, word & 0xffff
        pos += 4
        if command == 0:
            continue
        if command not in ELEMENT_SIZES:
            raise FormatError(f"unsupported moby VIF command {command:#x}")
        size = ELEMENT_SIZES[command] * count
        result.append((command, immediate, count, span(data, pos, size)))
        pos += size
    return result


class Slots:
    """VU0 matrix slots (ReRAC moby_rac1.md §2.10): each holds a joint or a blend of joints."""

    def __init__(self):
        self.slots: dict[int, tuple[tuple[int, int], ...]] = {}

    def store(self, address: int, skin: tuple) -> None:
        if address % 4:
            raise FormatError("unaligned moby VU0 address")
        self.slots[address] = skin

    def load(self, address: int) -> tuple:
        if address % 4 or address >= NO_STORE or address not in self.slots:
            raise FormatError("moby vertex loads an unwritten or invalid VU0 slot")
        return self.slots[address]

    def joint(self, address: int) -> int:
        skin = self.load(address)
        if len(skin) != 1:
            raise FormatError("moby blend of a blend")
        return skin[0][0]

    def vertex(self, kind: int, r: bytes) -> tuple:
        """Bytes 0-7 of a two-way (0), three-way (1) or single (2) vertex. Stores come first."""
        if kind == 0:
            self.store(r[6], ((r[1] >> 1, 256),))
            skin = ((self.joint(r[2]), r[4]), (self.joint(r[3]), r[5]))
        elif kind == 1:
            skin = ((self.joint(r[2]), r[4]), (self.joint(r[3]), r[5]), (self.joint(r[1] & 0xfe), r[6]))
        else:
            self.store(r[3], ((r[1] >> 1, 256),))
            return self.load(r[2])
        if sum(w for _, w in skin) != 256:
            raise FormatError("moby blend weights do not sum to 256")
        self.store(r[7], skin)
        return skin


class ListState:
    """What carries over between the packets of one LOD list."""

    def __init__(self):
        self.cache: dict[int, tuple] = {}
        self.slots = Slots()
        self.texture: int | None = None


def packet(data: bytes, entry: bytes, state: ListState, mesh: Mesh, remap: bytes,
           skins: list, glow: bool = False, compact: bool = False, empty_slots: bool = False) -> None:
    """Add one regular packet's vertices and drawn triangles to mesh. A glow packet's
    vertices get light slot -2 (the others -1): with the moby's mode bit 0x10 the game draws
    them unlit in its glow colour (MobyProc's glow list, ReRAC moby_skinning_lighting.md 10).

    COMPACT: the vertex table header is eight u16 (Going Commando on; Wrench's
    GcUyaDlVertexTableHeader), not eight u32, and the vertex data ends with the epilogue: there
    is no colour multiplier block after it, so the mesh gets no vertex colours.

    EMPTY_SLOTS: a face whose texture slot the class leaves empty (0xff) is drawn untextured
    instead of refused. Some of the later games' classes have no textures of their own (their
    pictures come from elsewhere at run time, not traced yet)."""
    list_offset, list_size, _, table, data_size, positions_qwc, colours_qwc, transfer = unpack("<IHHIBBBB", entry)
    commands = unpacks(span(data, list_offset, list_size * 16))
    if [c[0] for c in commands] not in ([ST, INDICES], [ST, INDICES, GS_BLOCKS]):
        raise FormatError("unsupported moby VIF program")
    (_, st_at, st_count, st), (_, index_at, index_count, index_data) = commands[:2]
    if (st_at, index_at) != (ST_ADDRESS, INDEX_ADDRESS) or index_data[3]:
        raise FormatError("unexpected moby VIF addresses")
    secrets, textures = [index_data[2]], []
    if len(commands) == 3:
        _, blocks_at, count, blocks = commands[2]
        if count % 4 or blocks_at != INDEX_ADDRESS + index_count or index_data[1] != index_count:
            raise FormatError("misplaced moby GS texture blocks")
        for b in range(count // 4):
            textures.append(unpack("<i", blocks, b * 64 + 32)[0])  # TEX0 data, patched at load time.
            secrets.append(blocks[b * 16 + 12])  # Extra indices ride in successive qwords.
    if positions_qwc != (transfer * 6 + 15) // 16 or colours_qwc != (transfer + 3) // 4:
        raise FormatError("moby packet entry sizes disagree")

    header_size = 0x10 if compact else 0x20
    transfers, two, three, single, duplicates, total, vertex_offset, multipliers = \
        unpack("<8H" if compact else "<8I", data, table)
    in_file = two + three + single
    if total != in_file + duplicates or total != transfer:
        raise FormatError("moby vertex counts disagree")
    pairs = span(data, table + header_size, transfers * 2)
    at = table + header_size + transfers * 2
    at += 2 if at % 4 else 0
    at += 4 if at % 8 else 0
    copies = struct.unpack(f"<{duplicates}H", span(data, at, duplicates * 2))
    if compact:
        epilogue = data_size - vertex_offset // 16 - in_file
        if not 1 <= epilogue <= 6 or vertex_offset % 16:
            raise FormatError("invalid moby epilogue vertex count")
    else:
        epilogue = (multipliers - vertex_offset) // 16 - in_file if multipliers >= vertex_offset else -1
        if not 1 <= epilogue <= 6 or (multipliers - vertex_offset) % 16:
            raise FormatError("invalid moby epilogue vertex count")
        if data_size * 16 - multipliers != (transfer * 4 + 15) // 16 * 16:
            raise FormatError("moby colour multipliers do not fit the vertex data")
    records = span(data, table + vertex_offset, (in_file + epilogue) * 16)
    # Each record carries the 9-bit cache ID of the vertex seven places earlier;
    # the last ones continue in the six halfwords at +4 of the final record.
    ids = [unpack("<H", records, i * 16)[0] & 0x1ff for i in range(7, in_file + epilogue)]
    ids += [h & 0x1ff for h in unpack("<6H", records, len(records) - 12)]
    if len(ids) < in_file:
        raise FormatError("moby packet lacks vertex IDs")

    for i in range(transfers):
        state.slots.store(pairs[i * 2 + 1], ((pairs[i * 2], 256),))
    vertices = []
    for i in range(in_file):
        record = records[i * 16:(i + 1) * 16]
        kind = 0 if i < two else 1 if i < two + three else 2
        skin = state.slots.vertex(kind, record)
        x, y, z = unpack("<3h", record, 10)
        vertices.append(((x / 1024, y / 1024, z / 1024), normal(record[8], record[9]), skin))
    for i, vertex in enumerate(vertices):
        state.cache[ids[i]] = vertex
    for copy in copies:
        if copy & 0x7f or (copy >> 7) not in state.cache:
            raise FormatError("moby duplicate vertex refers to an empty cache slot")
        vertices.append(state.cache[copy >> 7])
    if st_count < len(vertices):
        raise FormatError("moby packet has fewer texture coordinates than vertices")

    # Each transferred vertex's RGBA multiplier (0x80 = 1.0), which the game's lighting pass
    # scales its lit colour by (ReRAC's moby_light.rs); as the vertex colour, / 128.
    multiplier = None if compact else span(data, table + multipliers, transfer * 4)
    base = len(mesh.positions)
    for i, (position, n, skin) in enumerate(vertices):
        s, t = unpack("<2h", st, i * 4)
        mesh.positions.append(position)
        mesh.uvs.append((s / 4096, t / 4096))
        mesh.normals.append(n)
        if mesh.colours is not None and multiplier is not None:
            mesh.colours.append(tuple(b / 128 for b in multiplier[i * 4:i * 4 + 4]))
        if mesh.light_slots is not None:
            mesh.light_slots.append(-2 if glow else -1)
        skins.append(skin)

    def key(texture: int):
        if texture < -1:  # chrome and glass belong to the metal packets
            raise FormatError(f"invalid moby texture index {texture}")
        return UNTEXTURED if texture < 0 or remap[texture] == 255 else ("moby", remap[texture])

    faces(index_data, secrets, textures, state, len(vertices), base, mesh, key, None if empty_slots else remap)


def faces(index_data: bytes, secrets: list, textures: list, state: ListState, count: int, base: int,
          mesh: Mesh, key, remap: bytes | None) -> None:
    """Walk a packet's index stream into mesh faces; key turns a texture index into a mesh key."""
    # Index stream: 1-based, bit 7 suppresses the drawing kick. A 0 switches
    # to the next GS block's texture and pushes the next extra index; an
    # extra index of 0 ends the packet. The last three pushes (1, 1, 1)
    # flush the VU1 pipeline and are never drawn.
    pushes, secret, block = [], 0, 0
    for byte in index_data[4:]:
        if byte == 0:
            if secret >= len(secrets):
                raise FormatError("moby index stream runs out of extra indices")
            byte, secret = secrets[secret], secret + 1
            if byte == 0:
                break
            if block >= len(textures):
                raise FormatError("moby texture switch without a GS block")
            state.texture, block = textures[block], block + 1
            byte |= 0x80
        if not 0 < byte & 0x7f <= count:
            raise FormatError("moby vertex index out of range")
        pushes.append(((byte & 0x7f) - 1, byte & 0x80 == 0, state.texture))
    else:
        raise FormatError("moby index stream is not terminated")
    if block != len(textures) or [p[0] for p in pushes[-3:]] != [0, 0, 0]:
        raise FormatError("moby packet does not end with its flush trailer")
    pushes = pushes[:-3]
    for n in range(2, len(pushes)):
        index, kick, texture = pushes[n]
        if not kick:
            continue
        if texture is None or texture < -3 or texture >= 16:
            raise FormatError(f"invalid moby texture index {texture}")
        if texture >= 0 and remap is not None and remap[texture] == 255:
            raise FormatError("moby texture slot has no texture")
        a, b, c = (base + pushes[n - 2][0], base + pushes[n - 1][0], base + index)
        mesh.add_face(key(texture), orient(mesh, a, b, c))


def metal_packet(data: bytes, entry: bytes, state: ListState, mesh: Mesh, skins: list) -> None:
    """Add one metal packet's vertices and faces to mesh. Its VIF list has the index stream and
    the GS blocks only (the game makes the texture coordinates per frame, a sphere map); its vertex
    table is a 0x10-byte header (the vertex count, then the game's output offsets) and 16-byte
    vertices: s16 x, y, z, the normal's azimuth and elevation, joint[3], their count, weight[3]
    (in 256ths; a count of 1 or less is joint 0 alone). After ReRAC's read_packet and metal_skin
    (crates/rc-formats/src/moby.rs; ISC License, Copyright (c) 2026 ReRAC contributors)."""
    list_offset, list_size, _, table, _data_size, _p, _c, _t = unpack("<IHHIBBBB", entry)
    commands = unpacks(span(data, list_offset, list_size * 16))
    if [c[0] for c in commands] not in ([INDICES], [INDICES, GS_BLOCKS]):
        raise FormatError("unsupported moby metal VIF program")
    _, _index_at, _index_count, index_data = commands[0]
    secrets, textures = [index_data[2]], []
    if len(commands) == 2:
        _, _blocks_at, count, blocks = commands[1]
        if count % 4:
            raise FormatError("misplaced moby metal GS blocks")
        for b in range(count // 4):
            textures.append(unpack("<i", blocks, b * 64 + 32)[0])
            secrets.append(blocks[b * 16 + 12])
    count, = unpack("<i", data, table)
    if not 0 < count <= 4096:
        raise FormatError("implausible moby metal vertex count")
    records = span(data, table + 0x10, count * 16)
    base = len(mesh.positions)
    for i in range(count):
        r = records[i * 16:(i + 1) * 16]
        x, y, z = unpack("<3h", r)
        joints, n, weights = r[8:11], r[11], r[12:15]
        if n <= 1:
            skin = ((joints[0], 256),)
        elif n <= 3 and sum(weights[:n]) == 256:
            skin = tuple((joints[k], weights[k]) for k in range(n))
        else:
            raise FormatError("invalid moby metal vertex skin")
        mesh.positions.append((x / 1024, y / 1024, z / 1024))
        mesh.uvs.append((0.0, 0.0))
        mesh.normals.append(normal(r[6], r[7]))
        skins.append(skin)

    def key(texture: int):
        if texture not in (-2, -3):
            raise FormatError(f"moby metal packet uses texture {texture}")
        return METAL_CHROME if texture == -2 else METAL_GLASS

    faces(index_data, secrets, textures, state, count, base, mesh, key, None)


def orient(mesh: Mesh, a: int, b: int, c: int) -> tuple[int, int, int]:
    """Strip order keeps no consistent winding; face the stored vertex normals."""
    pa, pb, pc = (mesh.positions[v] for v in (a, b, c))
    ab = [pb[j] - pa[j] for j in range(3)]
    ac = [pc[j] - pa[j] for j in range(3)]
    cross = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
    dot = sum(cross[j] * (mesh.normals[a][j] + mesh.normals[b][j] + mesh.normals[c][j]) for j in range(3))
    return (c, b, a) if dot < 0 else (a, b, c)


def moby_class(data: bytes, remap: bytes, name: str, sequences: list | None = None,
               fmt: games.Format = games.RAC1, joints: bool = True) -> MobyClass:
    """Decode a class blob's header, high-LOD packets (low LOD and metal are
    not read), skeleton and sequences: the class's own unless sequences
    lists others, as moby_anim.skeleton takes them. FMT: the game's format (games.py): the
    packets' vertex table header, Deadlocked's 3x4 skeleton, and whether its animations are
    read (games.Format.moby_animations; without them the class keeps its skeleton only).
    JOINTS: False leaves the skeleton out (the mesh in its bind pose, unskinned)."""
    header = span(data, 0, HEADER_SIZE)
    packet_table, = unpack("<i", header)
    high, low, metal, metal_begin, joint_count = header[4:9]
    scale, = unpack("<f", header, 0x24)
    if not math.isfinite(scale) or scale <= 0:
        raise FormatError("invalid moby class scale")
    if not packet_table or not high:
        return MobyClass(scale, None, joint_count)
    if metal and metal_begin != high + low:
        raise FormatError("unexpected moby metal packet position")
    # Going Commando's classes mostly have the compact vertex tables; one whose byte 0xb is set
    # keeps Ratchet & Clank's (Wrench's read_class, force_rac1_format).
    compact = fmt.compact_vertex_table and not (fmt.game == "rac2" and header[0xb])
    mesh, skins, state = Mesh(name, normals=[], colours=None if compact else [], light_slots=[]), [], ListState()
    # The high LOD's first glow packet (Ratchet & Clank's byte 0xa; not read for the others yet).
    glow_from = header[0xa] if fmt.moby_glow else high
    for i in range(high):
        try:
            packet(data, span(data, packet_table + i * 16, 16), state, mesh, remap, skins, i >= glow_from, compact,
                   fmt.moby_empty_slots)
        except FormatError as exc:
            raise FormatError(f"packet {i}: {exc}") from exc
    if not mesh.triangles:
        raise FormatError("moby class has no drawn triangles")
    metal_mesh, metal_skins, metal_state = Mesh(f"{name}_metal", normals=[]), [], ListState()
    for i in range(metal):
        try:
            metal_packet(data, span(data, packet_table + (metal_begin + i) * 16, 16), metal_state, metal_mesh,
                         metal_skins)
        except FormatError as exc:
            raise FormatError(f"metal packet {i}: {exc}") from exc
    for skin in skins:
        if any(j >= max(joint_count, 1) for j, _ in skin):
            raise FormatError("moby vertex uses a joint past the class's joint count")
    if sequences is None:
        sequences = class_sequences(data) if fmt.moby_animations else []
    bones = skeleton(data, sequences, fmt.skeleton_3x4) if joints else None
    if bones is not None:
        mesh.skins = skins
        metal_mesh.skins = metal_skins
    return MobyClass(scale, mesh, joint_count, skins, bones, metal_mesh if metal_mesh.triangles else None)


def moby_classes(level) -> dict[int, MobyClass | None]:
    """Class number -> decoded class, or None for a class without a blob,
    for every entry of the core index's moby class table. Ratchet (class 0)
    takes the level's ratchet sequences."""
    result = {}
    # Ratchet's hand-held items (the wrench, the weapons and gadgets) are the core's gadget table
    # (index +0x80: offset of a WAD stream in the core data, o_class, compressed size, pad); each
    # stream is an ordinary class blob, and its texture slots are its moby class table entry's
    # (ReRAC's rc-formats gadget.rs; ISC License, Copyright (c) 2026 ReRAC contributors).
    fmt = games.of(level)
    gadgets = {}
    for g in level.table(0x80, 16) if fmt.gadgets else ():
        offset, class_id, size, _pad = unpack("<iiii", g)
        if offset > 0 and size > 0 and offset + size <= len(level.core):
            gadgets[class_id] = level.core[offset:offset + size]
    for entry in level.table(0x18, 32):
        start, class_id = unpack("<ii", entry)
        if class_id in result:
            raise FormatError(f"duplicate moby class {class_id}")
        if not start and class_id in gadgets:
            try:
                result[class_id] = moby_class(wad(gadgets[class_id]), entry[16:32], f"moby_{class_id}")
            except FormatError:
                result[class_id] = None
            continue
        if not start:
            result[class_id] = None
            continue
        try:
            result[class_id] = moby_class(level.block(start), entry[16:32], f"moby_{class_id}",
                                          ratchet_sequences(level) if class_id == 0 and fmt.moby_animations else None,
                                          fmt)
        except FormatError as exc:
            if not fmt.lenient:
                raise FormatError(f"moby class {class_id}: {exc}") from exc
            result[class_id] = fallback(level, start, entry, class_id, fmt, exc)
    return result


def fallback(level, start: int, entry: bytes, class_id: int, fmt: games.Format, exc: FormatError) -> MobyClass | None:
    """A later game's class that did not decode whole (games.Format.lenient): its mesh and
    skeleton without the animations, else its mesh alone, else nothing; what was left out goes
    into level.problems, which the export lists."""
    for what, kwargs in (("animations", {"sequences": []}), ("skeleton", {"sequences": [], "joints": False})):
        try:
            moby = moby_class(level.block(start), entry[16:32], f"moby_{class_id}", fmt=fmt, **kwargs)
        except FormatError:
            continue
        level.problems.append(f"moby class {class_id}: {what} left out ({exc})")
        return moby
    level.problems.append(f"moby class {class_id}: not extracted ({exc})")
    return None
