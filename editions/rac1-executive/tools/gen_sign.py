#!/usr/bin/env python3
"""
Kalebo III's rotating sign reads GADGETRON again, as on the NTSC disc, rebuilt
from the PAL disc's own GALACTRON sign.

The sign is moby class 1891, which only Kalebo III holds: a half ring of nine
3D letters, placed twice back to back to make the ring the level turns. The
PAL release spells GALACTRON and has no other copy of the word on the disc,
so the class is rebuilt here from the owner's PAL class (wrench's unpack of
the disc), nothing from any other disc:

  * Each letter is a flat plate, extruded radially: a front face (texture 1),
    a back face and side walls (texture 0), flat-shaded, each quad mapped onto
    a whole texture. GALACTRON and GADGETRON share six places: G A _ _ _ T R O
    N. On the NTSC disc those six are the PAL letters turned about the ring's
    axis (measured: T R O N by -1.3623 degrees, G A by +0.7377), and its
    second G is the first one turned by -51.5149 degrees. All three moves are
    applied to the PAL letters as they are.
  * L A C become D G E. The D and the E are built from the L: its stem and
    its foot's height (0.184 of the letter), the top bar's height that the T
    shares (0.831 to 1), and the L's front and back planes. The E is the L's
    stem with three bars, the D the stem with two bars and a bowl, cut where
    the strokes meet as the NTSC letters are (no T-junctions). Placed by
    turning the L about the axis (E -35.3284 degrees, D +0.7380, where the
    NTSC letters' stems are).
  * The shapes the PAL disc does not have are measured from the NTSC letters
    in letter units (height 1), relative to the stem's right edge: the E's top
    and bottom bars run 0.767 and its middle bar 0.432, at heights 0.435 to
    0.601; the D's bowl is ten 18-degree segments between two half ellipses,
    the outer one centred 0.437 right of the stem with a half width of 0.532
    and the full height, the inner one 0.405 and 0.367 between the bars.

wrench's mobyconvert (the edition's wrench fork, --mesh) packs the new mesh
into both of the class's LODs, keeping the rest of the class and retail's
packet conventions; the class's bounding spheres (header +0x30 and its one
sequence's) are recomputed here as the smallest sphere around the vertices,
which is how retail's agree to 0.03%. The result is read back with OpenRAC's
moby reader and checked.

Writes build/sign_bank (a wrench bank overriding class 1891, for the wrench
route; tools/splice_sign.py takes its core.bin for the quick route) and
build/sign/gadgetron_sign.glb (the mesh, for viewing).

  python tools/gen_sign.py [--openrac DIR] [--wrench DIR] [-o build/sign_bank]
"""
import argparse
import bisect
import json
import math
import random
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CLASS_ID = 1891
CLASS_DIR = "moby_classes/prop/1891_gadgetron_sign"

# The PAL sign's letters, by the angle (degrees, atan2(z, x) in wrench's glb axes, y up) of
# their strips' mean vertex; the word reads towards decreasing angles. Triangle counts check
# that the input is the PAL sign.
PAL_CUTS = [-58.0, -39.5, -23.8, -8.4, 9.4, 27.4, 40.7, 58.5]
PAL_LETTERS = ["N", "O", "R", "T", "C", "A2", "L", "A1", "G1"]
PAL_TRIANGLES = {"N": 64, "O": 128, "R": 136, "T": 36, "C": 140, "A2": 104, "L": 28, "A1": 104, "G1": 156}

# Turns about the ring's axis that put the PAL letters where the NTSC letters are (degrees).
TURN_TRON = -1.3623
TURN_GA = 0.7377
TURN_SECOND_G = -51.5149
TURN_E = -35.3284
TURN_D = 0.7380

# Letter units (height 1), from the stem's right edge; see the docstring.
E_BAR, E_MIDDLE_BAR, E_MIDDLE = 0.767, 0.432, (0.435, 0.601)
D_OUTER_CENTRE, D_OUTER_HALF_WIDTH = 0.437, 0.532
D_INNER_CENTRE, D_INNER_HALF_WIDTH = 0.405, 0.367
D_SEGMENTS = 10
MAX_TEXTURE_BLOCKS = 2     # per packet; the retail sign has one at most


# ---- glb ----

def read_glb(path: Path) -> list[dict]:
    """The node 'moby' of a wrench glb: one dict per primitive (one triangle strip each):
    material name, POSITION/NORMAL/TEXCOORD_0 lists and indices."""
    d = path.read_bytes()
    jl, = struct.unpack_from("<I", d, 12)
    j = json.loads(d[20:20 + jl])
    blob = 20 + jl + 8

    def accessor(i):
        a = j["accessors"][i]
        bv = j["bufferViews"][a["bufferView"]]
        n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        f = {5126: "f", 5123: "H", 5125: "I", 5121: "B"}[a["componentType"]]
        stride = bv.get("byteStride", struct.calcsize(f) * n)
        at = blob + bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        return [struct.unpack_from("<" + f * n, d, at + k * stride) for k in range(a["count"])]

    node = next((n for n in j["nodes"] if n.get("name") == "moby" and "mesh" in n), None)
    if node is None:
        raise SystemExit(f"{path}: no node 'moby'")
    names = [m.get("name") for m in j.get("materials", [])]
    out = []
    for p in j["meshes"][node["mesh"]]["primitives"]:
        if p.get("mode", 4) != 5:
            raise SystemExit(f"{path}: expected one triangle strip per primitive")
        out.append({"material": names[p["material"]],
                    "pos": accessor(p["attributes"]["POSITION"]),
                    "nrm": accessor(p["attributes"]["NORMAL"]),
                    "uv": accessor(p["attributes"]["TEXCOORD_0"]),
                    "indices": [i[0] for i in accessor(p["indices"])]})
    return out


def write_glb(path: Path, strips: list[dict]) -> None:
    """Strips as one mesh 'moby' (node 'moby'), a triangle strip per primitive, the way
    wrench's unpack writes a moby class, which mobyconvert --mesh reads back."""
    blob = bytearray()
    views, accessors, primitives = [], [], []
    materials = sorted({s["material"] for s in strips})

    def add(values, kind, ctype, fmt, minmax=False):
        while len(blob) % 4:
            blob.append(0)
        start = len(blob)
        for v in values:
            blob.extend(struct.pack("<" + fmt * len(v), *v))
        views.append({"buffer": 0, "byteOffset": start, "byteLength": len(blob) - start})
        acc = {"bufferView": len(views) - 1, "componentType": ctype, "count": len(values), "type": kind}
        if minmax:
            acc["min"] = [min(v[i] for v in values) for i in range(len(values[0]))]
            acc["max"] = [max(v[i] for v in values) for i in range(len(values[0]))]
        accessors.append(acc)
        return len(accessors) - 1

    for s in strips:
        attrs = {"POSITION": add(s["pos"], "VEC3", 5126, "f", True),
                 "NORMAL": add(s["nrm"], "VEC3", 5126, "f"),
                 "TEXCOORD_0": add(s["uv"], "VEC2", 5126, "f")}
        primitives.append({"attributes": attrs, "indices": add([(i,) for i in s["indices"]], "SCALAR", 5123, "H"),
                           "material": materials.index(s["material"]), "mode": 5})
    gltf = {"asset": {"version": "2.0", "generator": "rac1-executive-edition tools/gen_sign.py"},
            "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": "moby", "mesh": 0}],
            "meshes": [{"name": "moby", "primitives": primitives}],
            "materials": [{"name": m} for m in materials],
            "buffers": [{"byteLength": len(blob)}], "bufferViews": views, "accessors": accessors}
    text = json.dumps(gltf).encode()
    text += b" " * (-len(text) % 4)
    blob += bytes(-len(blob) % 4)
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, 28 + len(text) + len(blob))
                     + struct.pack("<I4s", len(text), b"JSON") + text
                     + struct.pack("<I4s", len(blob), b"BIN\0") + bytes(blob))


# ---- geometry ----
# wrench's glb has positions y up, and normals with z up: a normal (x, y, z) points along the
# position direction (x, z, y). A turn about the ring's axis by t degrees adds t to
# atan2(z, x) of positions and to atan2(y, x) of normals.

def turn_pos(p, t):
    c, s = math.cos(math.radians(t)), math.sin(math.radians(t))
    return (p[0] * c - p[2] * s, p[1], p[0] * s + p[2] * c)


def turn_nrm(n, t):
    c, s = math.cos(math.radians(t)), math.sin(math.radians(t))
    return (n[0] * c - n[1] * s, n[0] * s + n[1] * c, n[2])


def turned(strip: dict, t: float) -> dict:
    return dict(strip, pos=[turn_pos(p, t) for p in strip["pos"]], nrm=[turn_nrm(n, t) for n in strip["nrm"]])


def strip_triangles(strip: dict) -> int:
    i = strip["indices"]
    return sum(1 for k in range(len(i) - 2) if len({i[k], i[k + 1], i[k + 2]}) == 3)


def letters(strips: list[dict]) -> dict[str, list[dict]]:
    out = {k: [] for k in PAL_LETTERS}
    for s in strips:
        angles = [math.degrees(math.atan2(s["pos"][i][2], s["pos"][i][0])) for i in s["indices"]]
        name = PAL_LETTERS[bisect.bisect(PAL_CUTS, sum(angles) / len(angles))]
        if any(PAL_LETTERS[bisect.bisect(PAL_CUTS, a)] != name for a in angles):
            raise SystemExit("a strip of the sign crosses from one letter into another: not the PAL sign")
        out[name].append(s)
    counts = {k: sum(map(strip_triangles, v)) for k, v in out.items()}
    if counts != PAL_TRIANGLES:
        raise SystemExit(f"not the PAL GALACTRON sign: triangles per letter {counts}")
    return out


class Plate:
    """A letter's frame: u along the plate in reading order, v up (0 to 1), w out of the ring.
    The plate faces the angle of its front faces' normals."""

    def __init__(self, strips: list[dict]):
        angles = [math.degrees(math.atan2(n[1], n[0])) for s in strips if s["material"] == "material_1"
                  for n in s["nrm"] if abs(n[2]) < 0.01]
        self.angle = sum(angles) / len(angles)
        self.points = [self.local(p) for s in strips for p in s["pos"]]

    def local(self, p):
        x, y, z = turn_pos(p, -self.angle)
        return (-z, y, x)

    def world(self, u, v, w):
        return turn_pos((w, v, -u), self.angle)

    def normal(self, nu, nv, nw):
        x, y, z = turn_pos((nw, nv, -nu), self.angle)
        return (x, z, y)


def fit_line(points):
    """w = a + b u through (u, w) points, least squares."""
    n = len(points)
    su = sum(u for u, _ in points); sw = sum(w for _, w in points)
    suu = sum(u * u for u, _ in points); suw = sum(u * w for u, w in points)
    b = (n * suw - su * sw) / (n * suu - su * su)
    return (sw - b * su) / n, b


class Extruder:
    """Flat-shaded plate letters in a Plate's frame, cut from quads in (u, v): a front (texture
    1), a back and walls on every edge no other quad shares (texture 0), as the sign's letters
    are made. Each face is a strip of its own, each quad face mapped onto a whole texture."""

    def __init__(self, plate: Plate, front, back):
        self.plate, self.front, self.back = plate, front, back
        self.strips = []

    def face(self, material, corners, normal):
        # corners in order around the face; strip order 0 1 3 2
        uv = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
        order = [0, 1, 3, 2]
        n = self.plate.normal(*normal)
        self.strips.append({"material": material,
                            "pos": [self.plate.world(*corners[k]) for k in order],
                            "nrm": [n] * 4, "uv": [uv[k] for k in order], "indices": [0, 1, 2, 3]})

    def w(self, plane, u):
        return plane[0] + plane[1] * u

    def build(self, quads):
        def key(p):
            return (round(p[0], 5), round(p[1], 5))
        edges = {}
        for q in quads:
            area = sum(q[i][0] * q[(i + 1) % 4][1] - q[(i + 1) % 4][0] * q[i][1] for i in range(4))
            if area <= 0:
                raise SystemExit("gen_sign: a quad is not counter-clockwise")
            for i in range(4):
                edges.setdefault((key(q[i]), key(q[(i + 1) % 4])), 0)
                edges[(key(q[i]), key(q[(i + 1) % 4]))] += 1
        for q in quads:
            f = [(u, v, self.w(self.front, u)) for u, v in q]
            b = [(u, v, self.w(self.back, u)) for u, v in q]
            self.face("material_1", f, (0.0, 0.0, 1.0))
            self.face("material_0", b[::-1], (0.0, 0.0, -1.0))
            for i in range(4):
                a, c = q[i], q[(i + 1) % 4]
                if (key(c), key(a)) in edges:
                    continue  # shared with the next quad: inside the letter
                du, dv = c[0] - a[0], c[1] - a[1]
                length = math.hypot(du, dv)
                self.face("material_0", [f[i], b[i], b[(i + 1) % 4], f[(i + 1) % 4]],
                          (dv / length, -du / length, 0.0))
        return self.strips


def rect(u0, u1, v0, v1):
    return [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]


def build_word(strips: list[dict]) -> list[dict]:
    by = letters(strips)
    out = []
    for name in ("T", "R", "O", "N"):
        out += [turned(s, TURN_TRON) for s in by[name]]
    for name in ("G1", "A1"):
        out += [turned(s, TURN_GA) for s in by[name]]
    out += [turned(s, TURN_SECOND_G) for s in by["G1"]]

    # The L's measures, in its own plate's frame.
    plate = Plate(by["L"])
    pts = plate.points
    mid_w = (max(p[2] for p in pts) + min(p[2] for p in pts)) / 2
    front = fit_line([(u, w) for u, v, w in pts if w > mid_w])
    back = fit_line([(u, w) for u, v, w in pts if w < mid_w])
    top = max(v for u, v, w in pts)
    bottom = min(v for u, v, w in pts)
    stem = sorted({round(u, 3) for u, v, w in pts if v > top - 0.01 and w > mid_w})
    s0, s1 = stem[0], stem[-1]
    foot = max(v for u, v, w in pts if u > s1 + 0.05)
    t_heights = sorted({round(p[1], 3) for s in by["T"] for p in s["pos"]})
    if len(t_heights) != 3 or not (abs(top - 1) < 0.01 and abs(bottom) < 0.01 and len(stem) == 2):
        raise SystemExit(f"gen_sign: unexpected L or T (stem {stem}, T heights {t_heights})")
    bar = t_heights[1]                      # the top bar's lower edge, 0.831

    # E: the stem cut where its three bars meet it.
    m0, m1 = E_MIDDLE
    e = [rect(s0, s1, bottom, foot), rect(s0, s1, foot, m0), rect(s0, s1, m0, m1),
         rect(s0, s1, m1, bar), rect(s0, s1, bar, top),
         rect(s1, s1 + E_BAR, bottom, foot), rect(s1, s1 + E_MIDDLE_BAR, m0, m1), rect(s1, s1 + E_BAR, bar, top)]
    out += [turned(s, TURN_E) for s in Extruder(plate, front, back).build(e)]

    # D: the stem, the bars to where the bowl starts, and the bowl.
    oc, ic = s1 + D_OUTER_CENTRE, s1 + D_INNER_CENTRE
    o_mid, o_half = (bottom + top) / 2, (top - bottom) / 2
    i_mid, i_half = (foot + bar) / 2, (bar - foot) / 2

    def outer(k):
        a = math.pi * k / D_SEGMENTS
        return (oc + D_OUTER_HALF_WIDTH * math.sin(a), o_mid - o_half * math.cos(a))

    def inner(k):
        a = math.pi * k / D_SEGMENTS
        return (ic + D_INNER_HALF_WIDTH * math.sin(a), i_mid - i_half * math.cos(a))

    d = [rect(s0, s1, bottom, foot), rect(s0, s1, foot, bar), rect(s0, s1, bar, top),
         [(s1, bottom), outer(0), inner(0), (s1, foot)],
         [(s1, bar), inner(D_SEGMENTS), outer(D_SEGMENTS), (s1, top)]]
    d += [[outer(k), outer(k + 1), inner(k + 1), inner(k)] for k in range(D_SEGMENTS)]
    out += [turned(s, TURN_D) for s in Extruder(plate, front, back).build(d)]
    # Strips grouped by texture: the packer starts a GS texture block wherever the texture
    # changes, and each block takes VU1 memory beside the index stream (0x37 quadwords for
    # both); interleaved, the first build had up to nine in a packet, where retail has at most
    # five and the PAL sign one.
    return sorted(out, key=lambda s: s["material"])


# ---- the class ----

def enclosing_sphere(points):
    """Smallest sphere around the points (Welzl)."""
    def from_support(r):
        if not r:
            return (0.0, 0.0, 0.0), -1.0
        if len(r) == 1:
            return r[0], 0.0
        if len(r) == 2:
            c = tuple((a + b) / 2 for a, b in zip(*r))
            return c, math.dist(c, r[0])
        a = r[0]
        rows = [[2 * (p[i] - a[i]) for i in range(3)] for p in r[1:]]
        rhs = [sum(p[i] ** 2 - a[i] ** 2 for i in range(3)) for p in r[1:]]
        if len(r) == 3:  # the circle's centre: in the plane of the three points
            n = (rows[0][1] * rows[1][2] - rows[0][2] * rows[1][1], rows[0][2] * rows[1][0] - rows[0][0] * rows[1][2],
                 rows[0][0] * rows[1][1] - rows[0][1] * rows[1][0])
            rows.append(list(n))
            rhs.append(sum(n[i] * a[i] for i in range(3)))

        def det(m):
            return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                    + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
        dm = det(rows)
        if abs(dm) < 1e-9:
            far = max(((p, q) for p in r for q in r), key=lambda pq: math.dist(*pq))
            return from_support(list(far))
        c = []
        for k in range(3):
            mk = [row[:] for row in rows]
            for i in range(3):
                mk[i][k] = rhs[i]
            c.append(det(mk) / dm)
        return tuple(c), math.dist(c, a)

    pts = list(set(points))
    random.Random(0).shuffle(pts)
    sys.setrecursionlimit(max(sys.getrecursionlimit(), 4 * len(pts) + 100))

    def welzl(n, r):
        s = from_support(r)
        if len(r) == 4:
            return s
        for i in range(n):
            if math.dist(s[0], pts[i]) > s[1] + 1e-6:
                s = welzl(i, r + [pts[i]])
        return s
    return welzl(len(pts), [])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--openrac", type=Path, default=ROOT.parent / "OpenRAC")
    ap.add_argument("--wrench", type=Path, default=ROOT.parent / "wrench")
    ap.add_argument("--bank", type=Path, default=None, help="wrench unpack of the PAL disc (default OpenRAC/assets/wrench/rac1-pal)")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "build/sign_bank")
    args = ap.parse_args()

    src = (args.bank or args.openrac / "assets/wrench/rac1-pal") / CLASS_DIR
    for need in ("core.bin", "mesh.glb", "0.png", "1.png"):
        if not (src / need).is_file():
            raise SystemExit(f"missing {src / need} (wrench unpack of the PAL disc)")
    convert = args.wrench / "bin/mobyconvert.exe"
    if not convert.is_file():
        raise SystemExit(f"missing {convert} (the edition's wrench fork, branch executive-edition)")

    strips = build_word(read_glb(src / "mesh.glb"))
    view = ROOT / "build/sign"
    view.mkdir(parents=True, exist_ok=True)
    glb = view / "gadgetron_sign.glb"
    write_glb(glb, strips)

    out = args.out
    if out.exists():
        shutil.rmtree(out)
    cls_dir = out / "moby_classes/1891_gadgetron_sign"
    cls_dir.mkdir(parents=True)
    core = cls_dir / "core.bin"
    result = subprocess.run([str(convert), str(src / "core.bin"), "rac", str(core), "rac", "--mesh", str(glb)],
                            capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit("mobyconvert failed")

    # Bounding spheres: the class's (+0x30) and its sequences' (each sequence starts with one),
    # in vertex units, from OpenRAC's reading of the new class.
    sys.path.insert(0, str(args.openrac / "editor"))
    from moby_class import moby_class
    data = bytearray(core.read_bytes())
    mesh = moby_class(bytes(data), bytes(range(16)), "sign").mesh
    points = [tuple(c * 1024 for c in p) for p in mesh.positions]
    centre, radius = enclosing_sphere(points)
    sphere = struct.pack("<4f", *centre, radius + 4.0)
    struct.pack_into("<16s", data, 0x30, sphere)
    for i in range(data[0x0C]):
        seq, = struct.unpack_from("<I", data, 0x48 + 4 * i)
        if seq:
            struct.pack_into("<16s", data, seq, sphere)
    core.write_bytes(bytes(data))

    # Read back: the triangles expected, every vertex inside the sphere, and no packet with more
    # than MAX_TEXTURE_BLOCKS texture blocks (packet entry +6: the blocks' quadwords, 4 each).
    table_at, = struct.unpack_from("<I", data, 0)
    blocks = [struct.unpack_from("<H", data, table_at + 16 * i + 6)[0] // 4 for i in range(data[4] + data[5])]
    if max(blocks) > MAX_TEXTURE_BLOCKS:
        raise SystemExit(f"gen_sign: a packet has {max(blocks)} texture blocks (at most {MAX_TEXTURE_BLOCKS})")
    # Each packet's colour multipliers (vertex table +0x1C, 4 bytes per transfer vertex) must
    # be padded with zeros as on the disc: with 0x80 there, the level stopped presenting frames.
    for i in range(data[4] + data[5]):
        table, = struct.unpack_from("<I", data, table_at + 16 * i + 8)
        size = data[table_at + 16 * i + 12] * 16
        transfer, mult = struct.unpack_from("<I", data, table + 0x14)[0], struct.unpack_from("<I", data, table + 0x1C)[0]
        if any(data[table + mult + 4 * transfer:table + size]):
            raise SystemExit(f"gen_sign: packet {i}'s colour multipliers are not padded with zeros (wrench fork too old?)")
    check = moby_class(bytes(data), bytes(range(16)), "sign").mesh
    expected = sum(map(strip_triangles, strips))
    if check.triangles != expected:
        raise SystemExit(f"gen_sign: the class reads back with {check.triangles} triangles, not {expected}")
    if any(math.dist(centre, tuple(c * 1024 for c in p)) > radius + 4.0 for p in check.positions):
        raise SystemExit("gen_sign: a vertex lies outside the bounding sphere")

    for png in ("0.png", "1.png"):
        shutil.copyfile(src / png, cls_dir / png)
    (cls_dir / "gadgetron_sign.asset").write_text(f"""MobyClass rac.moby_classes.{CLASS_ID} {{
	id: {CLASS_ID}
	has_moby_table_entry: true

	materials {{
		Material 0 {{
			name: "material_0"

			Texture diffuse {{
				src: "0.png"
			}}
		}}

		Material 1 {{
			name: "material_1"

			Texture diffuse {{
				src: "1.png"
			}}
		}}
	}}

	Binary core {{
		src: "core.bin"
	}}
}}
""")
    (out / "gameinfo.txt").write_text("""format_version: 30
name: "Executive Edition: Gadgetron sign"
type: "mod"
supported_games: [
	"rac"
]
author: ""
description: "Kalebo III's sign reads GADGETRON, rebuilt from the PAL sign (generated by tools/gen_sign.py)"
version: "0.0.1"
images: []
builds: []
""")
    print(f"wrote {out}: class {CLASS_ID}, {expected} triangles ({len(strips)} strips), "
          f"{len(data)} bytes, bounding sphere radius {radius + 4.0:.0f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
