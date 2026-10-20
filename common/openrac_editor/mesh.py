"""The mesh type every decoder produces and the glTF writer consumes."""

from dataclasses import dataclass, field

Vec3 = tuple[float, float, float]


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
