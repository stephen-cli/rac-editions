"""The mesh type every decoder produces and the glTF writer consumes."""

from dataclasses import dataclass, field
import math

Vec3 = tuple[float, float, float]


def spherical_normal(azimuth: int, elevation: int) -> Vec3:
    """Decode spherical angles in 256ths of a turn, as the moby and terrain readers do."""
    a, e = azimuth * math.pi / 128, elevation * math.pi / 128
    return (math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))


def unit_normal(normal: Vec3) -> Vec3:
    """Normalise an authored vector; zero stays zero for the renderer's invalid-normal fallback."""
    length = math.sqrt(sum(component * component for component in normal))
    return tuple(component / length for component in normal) if length else (0.0, 0.0, 0.0)


@dataclass
class Mesh:
    """Triangles grouped by texture, in game units and game axes (Z up).

    Vertex i is positions[i] with uvs[i] (image top-left origin, as in
    glTF) and, if present, colours[i] (RGBA, 0..1), normals[i] (unit
    length; without them each face gets its flat normal) and skins[i]
    ((joint, weight) pairs, weights in 256ths summing to 256) and
    light_slots[i] (a tie vertex's light slot, 0..63: which of an instance's
    64 lit colours it takes). Texture keys are (group, index) pairs, or None
    for untextured faces.
    """
    name: str
    positions: list[Vec3] = field(default_factory=list)
    uvs: list[tuple[float, float]] = field(default_factory=list)
    colours: list[tuple[float, float, float, float]] | None = None
    normals: list[Vec3] | None = None
    skins: list[tuple[tuple[int, int], ...]] | None = None
    light_slots: list[int] | None = None
    faces: dict[tuple[str, int] | None, list[tuple[int, int, int]]] = field(default_factory=dict)

    def add_face(self, texture: tuple[str, int] | None, face: tuple[int, int, int]) -> None:
        self.faces.setdefault(texture, []).append(face)

    @property
    def triangles(self) -> int:
        return sum(map(len, self.faces.values()))

    def bounds(self) -> tuple[Vec3, Vec3]:
        return (tuple(min(p[i] for p in self.positions) for i in range(3)),
                tuple(max(p[i] for p in self.positions) for i in range(3)))
