/*
 * Moby classes: the games' model classes (a moby is the games' word for an object with a model), read from one
 * game's format and written in another's. Written from wrench's moby engine (src/engine/moby_low.cpp,
 * moby_packet.cpp, moby_vertex.cpp and moby_animation.cpp, with core/vif.cpp's VIF parsing), by chaoticgd,
 * GPL-3.0; the output is byte for byte what wrench's read_class and write_class make of the same class, including
 * the fields wrench leaves out or rebuilds (see moby.c).
 *
 * A class core starts with a 0x48-byte header: the packet table's offset, the high LOD, low LOD and metal packet
 * counts, the joint, sequence and sound counts, and the offsets of the collision, skeleton, common transforms,
 * joint lists, GIF usage table and sound definitions. The sequence list follows it, then the sequences with their
 * frames and triggers, the packet table, and the parts the header points to. A packet is a VIF command list (the
 * texture coordinates, the index stream and the GS texture registers, UNPACKed to VU1) and a vertex table (the
 * matrix transfers to VU0, the vertices and the duplicate vertices). R&C1 and R&C2 differ in the vertex table's
 * header (32-bit fields and a colour multiplier per vertex in R&C1, 16-bit fields in R&C2), in where a sequence's
 * trigger data is counted from (the class in R&C1, the sequence in R&C2), and in header bytes 0xa and 0xb and the
 * short at 0x2e, which R&C1 keeps and R&C2 uses for the corncob.
 */
#ifndef RAC_MOBY_H
#define RAC_MOBY_H

#include <stddef.h>
#include <stdint.h>

enum moby_game {
    MOBY_RAC,   /* R&C1 */
    MOBY_GC,    /* R&C2 (Going Commando) */
};

/* Bytes the class owns. Multi-byte values in them stay little-endian, as in the file. */
struct moby_bytes {
    uint8_t *data;
    size_t size;
};

struct moby_vertex_table {
    uint8_t *transfers;             /* the matrix transfers before the vertices: 2 bytes each, scratchpad joint and
                                       VU0 address */
    size_t transfer_count;
    uint16_t *duplicates;           /* vertex indices, as stored >> 7 */
    size_t duplicate_count;
    int32_t two_way_count;          /* the vertices are two-way blends, then three-way blends, then the rest */
    int32_t three_way_count;
    int32_t main_count;
    uint8_t *vertices;              /* 16 bytes each; the low 9 bits of each one's first halfword are its own index
                                       (the file stores each index 7 vertices later) */
    size_t vertex_count;
    uint16_t unknown_e;
    struct moby_bytes unknown_e_data;   /* R&C1: an RGBA colour multiplier per transfer vertex, then padding */
};

/* What regular and metal packets share in their VIF command lists. */
struct moby_vif {
    struct moby_bytes indices;      /* the index stream after its 4-byte header */
    int8_t *secret_indices;         /* the header's, then one per texture */
    size_t secret_count;
    uint8_t *textures;              /* 0x40-byte GS register sets: TEX1, CLAMP, TEX0 (+0x20: the texture), MIPTBP1 */
    size_t texture_count;
    uint8_t index_header_first_byte;
};

struct moby_packet {
    struct moby_vertex_table table;
    struct moby_vif vif;
    uint8_t *sts;                   /* texture coordinates: 4 bytes each */
    size_t st_count;
};

struct moby_metal_packet {
    uint8_t *vertices;              /* 16 bytes each: position, then joints at 8..a, joint count at b, weights c..e */
    size_t vertex_count;
    uint32_t unknown_4, unknown_8, unknown_c;
    struct moby_vif vif;
};

struct moby_frame {
    /* a regular frame */
    uint32_t unknown_0;             /* a float, as its bits */
    uint16_t unknown_4;
    uint16_t unknown_c;
    struct moby_bytes joint_data;   /* 8 bytes per joint */
    struct moby_bytes thing_1;      /* 8 bytes each */
    struct moby_bytes thing_2;
    /* a special frame (Ratchet's and a few others') */
    uint16_t inverse_unknown_0;
    uint16_t special_unknown_4;
    struct moby_bytes first_part, second_part, third_part, fourth_part, fifth_part_1, fifth_part_2;
};

struct moby_sequence {
    int present;                    /* 0: the list's offset is 0 */
    uint8_t bounding_sphere[16];
    struct moby_frame *frames;
    size_t frame_count;
    struct moby_bytes triggers;     /* 4 bytes each */
    int has_trigger_data;
    uint8_t trigger_data[32];
    uint32_t animation_info;
    uint8_t sound_count;
    uint8_t unknown_13;
    int has_special_data;
    struct moby_bytes special_joint_data;   /* 6 bytes per joint */
    struct moby_bytes special_thing_1;      /* 8 bytes each */
    struct moby_bytes special_thing_2;
};

struct moby_joint_list {
    struct moby_bytes thing_one;
    struct moby_bytes thing_two;
};

struct moby_bangle {
    struct moby_packet *high_lod;
    size_t high_count;
    struct moby_packet *low_lod;
    size_t low_count;
    uint8_t vectors[16];            /* two of 4 shorts */
};

struct moby_corn_kernel {
    int present;
    uint8_t vec[16];
    struct moby_bytes vertices;     /* 8 bytes each */
};

struct moby_collision {
    int present;
    uint16_t unknown_0;
    uint16_t unknown_2;
    struct moby_bytes first_part;
    float *second_part;             /* x, y, z per vector: the file's shorts / 1024, as wrench keeps them */
    size_t second_count;            /* vectors */
    struct moby_bytes third_part;
};

struct moby_class {
    struct moby_packet *high_lod;
    size_t high_count;
    struct moby_packet *low_lod;
    size_t low_count;
    struct moby_metal_packet *metal;
    size_t metal_count;
    int has_packet_table;

    uint8_t joint_count;
    struct moby_sequence *sequences;
    size_t sequence_count;
    int has_skeleton;
    struct moby_bytes skeleton;     /* a 4x4 float matrix per joint */
    int has_common_trans;
    struct moby_bytes common_trans; /* 16 bytes per joint */
    struct moby_joint_list *joints;
    size_t joint_list_count;

    struct moby_bangle *bangles;
    size_t bangle_count;
    int has_corncob;                /* R&C2 */
    struct moby_corn_kernel kernels[16];
    struct moby_bytes shadow;
    struct moby_collision collision;
    struct moby_bytes sound_defs;   /* 0x20 bytes each */

    uint8_t unknown_9;
    uint8_t lod_trans;
    uint32_t scale;                 /* a float, as its bits */
    uint8_t mip_dist;
    uint8_t bounding_sphere[16];
    int32_t glow_rgba;
    int16_t mode_bits;
    uint8_t type;
    uint8_t mode_bits2;
    int32_t header_end_offset;      /* the input's first sequence (or bangles, corncob): written from there */
    int32_t packet_table_offset;    /* the input's: the writer pads up to it */
    uint8_t rac1_byte_a;            /* header bytes 0xa, 0xb and the short at 0x2e, which only R&C1 keeps */
    uint8_t rac1_byte_b;
    uint16_t rac1_short_2e;
    int force_rac1_format;          /* an R&C2 class in R&C1's format (some in the Insomniac Museum) */

    void **allocations;             /* everything above that the class owns */
    size_t allocation_count;
    size_t allocation_capacity;
};

/* The class core DATA (SIZE bytes) of GAME: a new class, or NULL with the reason in ERROR (ERROR_SIZE bytes). */
struct moby_class *moby_read(const uint8_t *data, size_t size, enum moby_game game, char *error, size_t error_size);

/* MOBY as a class core of GAME: a new array (*SIZE bytes), or NULL with the reason in ERROR. */
uint8_t *moby_write(const struct moby_class *moby, enum moby_game game, size_t *size, char *error,
                    size_t error_size);

void moby_free(struct moby_class *moby);

/* SIZE zeroed bytes that MOBY owns (moby_free frees them), for parts given to it; NULL when out of memory. */
void *moby_alloc(struct moby_class *moby, size_t size);

/* How many vertices moby_rebind changed, by part, or -1 everywhere on an error. */
struct moby_rebind_counts {
    int high, low, metal;
};

/* Moves the skin of the mesh below a height from joint FROM to joint TO (heights in the class's vertex units, z
 * up): vertices on FROM alone follow TO below the midpoint of Z_TOP and Z_BOTTOM, and two-way blends of the two
 * joints are re-weighted linearly from all FROM at Z_TOP to all TO at Z_BOTTOM. Vertices keep their order (it is
 * their VU1 slot); each packet gets one more matrix transfer, TO into a VU0 address no vertex uses. Returns 0, or
 * -1 with the reason in ERROR (no free VU0 address, or no memory). */
int moby_rebind(struct moby_class *moby, int from, int to, int z_top, int z_bottom, struct moby_rebind_counts *counts,
                char *error, size_t error_size);

#endif
