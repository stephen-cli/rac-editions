/*
 * Moby class meshes as glTF: a class's packets written as the mesh wrench's unpack writes (mesh.glb), and a class's
 * high and low LOD rebuilt from such a mesh as the Executive Edition's mobyconvert --mesh rebuilds them. Written
 * from wrench (src/engine/moby_high.cpp, moby_vertex.cpp and moby_skinning.cpp, src/core/gltf.cpp, tristrip.cpp,
 * tristrip_packet.cpp and material.cpp, src/wrenchbuild/classes/moby_class.cpp, and the fork's src/mobyconvert.cpp),
 * by chaoticgd, GPL-3.0; the values are wrench's, float operation for float operation (see moby_mesh.c).
 *
 * A packet draws triangle strips through up to 97 vertices (VU1's vertex buffer) with one index byte per strip
 * vertex (its restart bit set at a strip's start) and a GS register set wherever the texture changes; a vertex is
 * 16 bytes: skinning bytes, the normal as two angles (1/256 of a turn each), and the position in 1/1024 of the
 * class's scale. In glTF the mesh is one node per LOD ("moby", "moby_low_lod", then "bangle_<i>" and
 * "bangle_<i>_low_lod"), a triangle strip per primitive with zero-area triangles where the packet restarts a strip,
 * positions y up (the game's are z up), and a material material_<i> for the class's texture i.
 */
#ifndef RAC_MOBY_MESH_H
#define RAC_MOBY_MESH_H

#include <stddef.h>
#include <stdint.h>

#include "moby.h"

/* The mesh of MOBY as wrench's unpack writes it: a new glb (*SIZE bytes), or NULL with the reason in ERROR.
 * TEXTURE_COUNT is the class's texture count (the level's list, which the core does not hold), so materials
 * material_0 .. material_<TEXTURE_COUNT - 1>; below 0, as many as the packets use (at most 16). A primitive drawing
 * another texture gets "error", and one drawing none, chrome or glass (-1, -2, -3) "none", "chrome" or "glass". */
uint8_t *moby_mesh_export(const struct moby_class *moby, int texture_count, size_t *size, char *error,
                          size_t error_size);

struct moby_mesh_counts {
    size_t vertices;                /* the glb mesh's, after merging equal vertices */
    size_t primitives;
    size_t packets;                 /* per LOD */
};

/* MOBY's high and low LOD rebuilt from the mesh of node "moby" in the glb GLB (GLB_SIZE bytes): one triangle strip
 * per primitive, and materials named material_<i> for the class's texture i. Only for a class without joints, metal
 * packets or bangles. The packets are wrench's split and build, with the class's own conventions where wrench's
 * builder has its own: packet 0's matrix transfers, every vertex loading joint 0 from there, the index header's first
 * byte, and the GS registers of its first texture. Returns 0 (COUNTS, when given, says what was built), or -1 with
 * the reason in ERROR. */
int moby_mesh_replace(struct moby_class *moby, const uint8_t *glb, size_t glb_size, struct moby_mesh_counts *counts,
                      char *error, size_t error_size);

#endif
