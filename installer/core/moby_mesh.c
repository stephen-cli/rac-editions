/*
 * Moby class meshes as glTF (moby_mesh.h): wrench's mesh recovery and packet building in C, written from wrench
 * (src/engine/moby_high.cpp, moby_vertex.cpp and moby_skinning.cpp; src/core/gltf.cpp, tristrip.cpp,
 * tristrip_packet.cpp, material.cpp and algorithm.h; src/wrenchbuild/classes/moby_class.cpp) and the Executive
 * Edition's src/mobyconvert.cpp (--mesh) in wrench's fork, by chaoticgd, GPL-3.0.
 *
 * The values are wrench's, so its ways are kept where they look odd:
 *  - floats are f32 throughout, each operation in wrench's order: a glTF axis swap is a matrix product with its
 *    zero terms, a normal is normalised as glm does (x*x + y*y + z*z, then 1 / sqrt), and the angle scales are
 *    doubles (wrench's pi is a double), rounded to float before roundf; a stored normal's sines and cosines come
 *    from a table of what wrench's libm gives (libms differ in the last bit), while atan2f, asinf and sqrtf come
 *    from the platform's, whose last bits only matter where an angle rounds to the half (none in the tests);
 *  - a float goes to a narrow integer through a 32-bit int, as wrench's compiler converts it;
 *  - equal vertices merge into the first of them, compared as wrench's Vertex compares them (position, normal,
 *    skin, colour and texture coordinates; not the vertex index);
 *  - VU0's matrix slots are followed through each LOD as wrench does to know each vertex's skin, which decides which
 *    vertices are equal, even in a class without joints (where joint 0's transfer is the identity);
 *  - a strip vertex's index is wrench's 7-bit one minus 1, so the restart byte 0x80 is vertex -1;
 *  - a texture index outside the class's textures takes material "error", but only after the special materials
 *    ("none", "chrome", "glass") have taken their places, which counts them among the valid ones;
 *  - glTF texture coordinates and weights in bytes are read with wrench's strides (2 per vertex), joints in shorts
 *    by their low bytes, and float weights as wrench reads them;
 *  - the packet splitter carries the texture across packets, so a packet that goes on with the last one's texture
 *    has no GS registers of its own.
 * Floating-point contraction (FMA) would change values: build without it (gcc -ffp-contract=off; MSVC's default).
 */
#include "moby_mesh.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

#define VERTEX_SIZE 0x10
#define TEXTURE_SIZE 0x40
#define VU0_UNUSED 0xf4
#define WRENCH_PI 3.14159265358979323846
#define RESTART_BIT 0x80000000u
#define MAX_MATERIALS 16            /* wrench's unpack makes at most 16 texture materials */
#define TRIANGLE_STRIP 5

/* glTF's component types */
#define GLTF_BYTE 5120
#define GLTF_UNSIGNED_BYTE 5121
#define GLTF_SHORT 5122
#define GLTF_UNSIGNED_SHORT 5123
#define GLTF_UNSIGNED_INT 5125
#define GLTF_FLOAT 5126

/* The attributes a primitive has, as bits */
enum {
    ATTRIBUTE_POSITION = 1 << 0,
    ATTRIBUTE_TEXCOORD_0 = 1 << 1,
    ATTRIBUTE_NORMAL = 1 << 2,
    ATTRIBUTE_COLOR_0 = 1 << 3,
    ATTRIBUTE_JOINTS_0 = 1 << 4,
    ATTRIBUTE_WEIGHTS_0 = 1 << 5,
};

/* glm matrices, column by column: glTF (y up) to the game (z up), and back */
static const float GLTF_TO_RATCHET[9] = {0, 1, 0, 0, 0, 1, 1, 0, 0};
static const float RATCHET_TO_GLTF[9] = {0, 0, 1, 1, 0, 0, 0, 1, 0};

/* ------------------------------------------------------------------------------------------------- bytes */

static uint16_t get_u16(const uint8_t *at)
{
    return (uint16_t)(at[0] | at[1] << 8);
}

static uint32_t get_u32(const uint8_t *at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

static void set_u16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
}

static void set_u32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    at[2] = (uint8_t)(value >> 16);
    at[3] = (uint8_t)(value >> 24);
}

static float bits_to_float(uint32_t bits)
{
    float value;

    memcpy(&value, &bits, sizeof value);
    return value;
}

static uint32_t float_to_bits(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static float get_f32(const uint8_t *at)
{
    return bits_to_float(get_u32(at));
}

/* A float to a narrow integer as wrench's compiler converts it: through a 32-bit int, then truncated. */
static int32_t to_int(float value)
{
    return (int32_t)value;
}

/* glm's mat3 * vec3, terms in its order (a zero term still counts: it decides the sign of a zero). */
static void transform(const float m[9], const float v[3], float out[3])
{
    for (int row = 0; row < 3; row++) {
        float sum = m[row] * v[0];

        sum = sum + m[3 + row] * v[1];
        out[row] = sum + m[6 + row] * v[2];
    }
}

/* ------------------------------------------------------------------------------------------------ errors */

struct status {
    char *error;
    size_t error_size;
    int failed;
};

/* Keeps the first reason. Returns -1, for the callers to pass on. */
static int fail(struct status *s, const char *format, ...)
{
    va_list args;

    if (s->failed)
        return -1;
    s->failed = 1;
    if (s->error && s->error_size) {
        va_start(args, format);
        vsnprintf(s->error, s->error_size, format, args);
        va_end(args);
    }
    return -1;
}

static int out_of_memory(struct status *s)
{
    return fail(s, "out of memory");
}

/* ------------------------------------------------------------------------------------------------ arrays */

/* ITEMS (CAPACITY elements of ELEMENT bytes) with room for NEEDED: the same or a moved block, or NULL when out of
 * memory (ITEMS is then unchanged). */
static void *reserve(void *items, size_t *capacity, size_t needed, size_t element)
{
    size_t grown = *capacity ? *capacity : 16;
    void *block;

    if (items && needed <= *capacity)
        return items;
    while (grown < needed)
        grown *= 2;
    block = realloc(items, grown * element);
    if (block)
        *capacity = grown;
    return block;
}

struct ints {
    int32_t *items;
    size_t count;
    size_t capacity;
};

static int ints_push(struct ints *list, int32_t value)
{
    int32_t *items = reserve(list->items, &list->capacity, list->count + 1, sizeof *items);

    if (!items)
        return -1;
    list->items = items;
    list->items[list->count++] = value;
    return 0;
}

struct bytes {
    uint8_t *items;
    size_t count;
    size_t capacity;
};

static int bytes_append(struct bytes *list, const void *data, size_t size)
{
    uint8_t *items = reserve(list->items, &list->capacity, list->count + size, 1);

    if (!items)
        return -1;
    list->items = items;
    if (size)
        memcpy(list->items + list->count, data, size);
    list->count += size;
    return 0;
}

static int bytes_push(struct bytes *list, uint8_t value)
{
    return bytes_append(list, &value, 1);
}

/* ------------------------------------------------------------------------------------------------ meshes */

/* A vertex's skin: up to three joints and their weights */
struct skin {
    uint8_t count;
    int8_t joints[3];
    uint8_t weights[3];
};

/* A mesh vertex, with the fields wrench compares to merge equal vertices */
struct vertex {
    float pos[3];
    float normal[3];
    struct skin skin;
    uint8_t colour[4];
    float tex_coord[2];
    uint16_t vertex_index;          /* the packet's VU1 slot (moby only), not compared */
};

/* A glTF primitive: a triangle strip with zero-area triangles, or, in the packet splitter, with restart bits. */
struct primitive {
    struct ints indices;
    uint32_t attributes;
    int has_material;
    int32_t material;
    int has_mode;
    int32_t mode;
};

struct mesh {
    struct vertex *vertices;
    size_t vertex_count;
    size_t vertex_capacity;
    struct primitive *primitives;
    size_t primitive_count;
    size_t primitive_capacity;
};

/* COUNT more vertices at the mesh's end: zero, with vertex index 0xff, as wrench makes them. */
static int add_vertices(struct mesh *mesh, size_t count)
{
    struct vertex *vertices;

    if (count == 0)
        return 0;
    vertices = reserve(mesh->vertices, &mesh->vertex_capacity, mesh->vertex_count + count, sizeof *vertices);
    if (!vertices)
        return -1;
    mesh->vertices = vertices;
    for (size_t i = 0; i < count; i++) {
        struct vertex *vertex = &vertices[mesh->vertex_count++];

        memset(vertex, 0, sizeof *vertex);
        vertex->vertex_index = 0xff;
    }
    return 0;
}

static struct primitive *add_primitive(struct mesh *mesh)
{
    struct primitive *primitives = reserve(mesh->primitives, &mesh->primitive_capacity, mesh->primitive_count + 1,
                                           sizeof *primitives);
    struct primitive *primitive;

    if (!primitives)
        return NULL;
    mesh->primitives = primitives;
    primitive = &primitives[mesh->primitive_count++];
    memset(primitive, 0, sizeof *primitive);
    return primitive;
}

static void free_mesh(struct mesh *mesh)
{
    for (size_t i = 0; i < mesh->primitive_count; i++)
        free(mesh->primitives[i].indices.items);
    free(mesh->primitives);
    free(mesh->vertices);
    memset(mesh, 0, sizeof *mesh);
}

/* Vertices ordered as wrench orders them, -1, 0 or 1: position z, y, x, normal z, y, x, skin, colour, then the
 * texture coordinates t, s (which the moby code relies on coming last). */
static int compare_vertices(const struct vertex *l, const struct vertex *r)
{
    const float lf[6] = {l->pos[2], l->pos[1], l->pos[0], l->normal[2], l->normal[1], l->normal[0]};
    const float rf[6] = {r->pos[2], r->pos[1], r->pos[0], r->normal[2], r->normal[1], r->normal[0]};
    const int li[11] = {l->skin.count, l->skin.joints[0], l->skin.joints[1], l->skin.joints[2], l->skin.weights[0],
                        l->skin.weights[1], l->skin.weights[2], l->colour[0], l->colour[1], l->colour[2],
                        l->colour[3]};
    const int ri[11] = {r->skin.count, r->skin.joints[0], r->skin.joints[1], r->skin.joints[2], r->skin.weights[0],
                        r->skin.weights[1], r->skin.weights[2], r->colour[0], r->colour[1], r->colour[2],
                        r->colour[3]};

    for (int i = 0; i < 6; i++) {
        if (lf[i] != rf[i])
            return lf[i] < rf[i] ? -1 : 1;
    }
    for (int i = 0; i < 11; i++) {
        if (li[i] != ri[i])
            return li[i] < ri[i] ? -1 : 1;
    }
    if (l->tex_coord[1] != r->tex_coord[1])
        return l->tex_coord[1] < r->tex_coord[1] ? -1 : 1;
    if (l->tex_coord[0] != r->tex_coord[0])
        return l->tex_coord[0] < r->tex_coord[0] ? -1 : 1;
    return 0;
}

/* ORDER (COUNT vertex indices) sorted by vertex, equal vertices by index (a merge sort; SCRATCH holds COUNT). */
static void sort_vertices(int32_t *order, int32_t *scratch, size_t count, const struct vertex *vertices)
{
    for (size_t width = 1; width < count; width *= 2) {
        for (size_t low = 0; low < count; low += 2 * width) {
            size_t middle = low + width < count ? low + width : count;
            size_t high = low + 2 * width < count ? low + 2 * width : count;
            size_t a = low, b = middle, out = low;

            while (a < middle || b < high) {
                int take_a = b >= high;

                if (a < middle && b < high) {
                    int c = compare_vertices(&vertices[order[a]], &vertices[order[b]]);

                    take_a = c < 0 || (c == 0 && order[a] < order[b]);
                }
                scratch[out++] = take_a ? order[a++] : order[b++];
            }
        }
        memcpy(order, scratch, count * sizeof *order);
    }
}

/* Equal vertices become the first of them; the rest keep their order. */
static int deduplicate_vertices(struct mesh *mesh, struct status *s)
{
    size_t count = mesh->vertex_count;
    int32_t *canonical = malloc((count ? count : 1) * sizeof *canonical);
    int32_t *order = malloc((count ? count : 1) * sizeof *order);
    int32_t *scratch = malloc((count ? count : 1) * sizeof *scratch);
    size_t unique = 0, group = 0;

    if (!canonical || !order || !scratch) {
        free(canonical);
        free(order);
        free(scratch);
        return out_of_memory(s);
    }
    for (size_t i = 0; i < count; i++)
        order[i] = (int32_t)i;
    sort_vertices(order, scratch, count, mesh->vertices);
    for (size_t i = 0; i < count; i++) {
        if (i == count - 1 || compare_vertices(&mesh->vertices[order[i]], &mesh->vertices[order[i + 1]]) != 0) {
            for (size_t j = group; j <= i; j++)
                canonical[order[j]] = order[group];     /* sorted by index within the group: the first is lowest */
            group = i + 1;
        }
    }
    for (size_t i = 0; i < count; i++) {
        if ((size_t)canonical[i] == i) {
            canonical[i] = (int32_t)unique;
            mesh->vertices[unique++] = mesh->vertices[i];   /* never ahead of i */
        } else {
            canonical[i] = canonical[canonical[i]];
        }
    }
    mesh->vertex_count = unique;
    free(order);
    free(scratch);
    for (size_t p = 0; p < mesh->primitive_count; p++) {
        struct ints *indices = &mesh->primitives[p].indices;

        for (size_t i = 0; i < indices->count; i++) {
            if ((uint32_t)indices->items[i] >= count) {
                free(canonical);
                return fail(s, "Index too large.");
            }
            indices->items[i] = canonical[indices->items[i]];
        }
    }
    free(canonical);
    return 0;
}

/* ------------------------------------------------------------------------------------------- glTF reading */

static const struct {
    const char *name;
    int components;
} ACCESSOR_TYPES[] = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}, {"MAT2", 4}, {"MAT3", 9}, {"MAT4", 16}};

enum { TYPE_SCALAR, TYPE_VEC2, TYPE_VEC3, TYPE_VEC4 };

static int component_size(int32_t component_type)
{
    switch (component_type) {
    case GLTF_BYTE: case GLTF_UNSIGNED_BYTE: return 1;
    case GLTF_SHORT: case GLTF_UNSIGNED_SHORT: return 2;
    case GLTF_UNSIGNED_INT: case GLTF_FLOAT: return 4;
    default: return 0;
    }
}

/* An accessor's elements, packed (the buffer view's stride taken out). */
struct accessor {
    int32_t component_type;
    int32_t count;
    int type;                       /* an index into ACCESSOR_TYPES */
    uint8_t *bytes;
};

struct buffer_view {
    int32_t buffer;
    int has_offset;
    int32_t offset;
    int has_stride;
    int32_t stride;
};

struct node {
    const char *name;               /* NULL when unnamed; in the JSON tree */
    int has_mesh;
    int32_t mesh;
};

struct gltf {
    Json *root;
    struct accessor *accessors;
    size_t accessor_count;
    struct node *nodes;
    size_t node_count;
    struct mesh *meshes;
    size_t mesh_count;
    const char **material_names;    /* NULL for an unnamed one; in the JSON tree */
    size_t material_count;
};

static void free_gltf(struct gltf *gltf)
{
    for (size_t i = 0; i < gltf->accessor_count; i++)
        free(gltf->accessors[i].bytes);
    for (size_t i = 0; i < gltf->mesh_count; i++)
        free_mesh(&gltf->meshes[i]);
    free(gltf->accessors);
    free(gltf->nodes);
    free(gltf->meshes);
    free(gltf->material_names);
    json_free(gltf->root);
}

/* KEY of OBJECT in *VALUE: 1, 0 when it is missing, -1 (failed) when it is no number. */
static int get_int(struct status *s, const Json *object, const char *key, int32_t *value)
{
    const Json *item = json_get(object, key);

    if (!item)
        return 0;
    if (item->type != JSON_NUMBER)
        return fail(s, "Property '%s' is of the incorrect type.", key);
    *value = item->is_integer ? (int32_t)item->integer : (int32_t)item->number;
    return 1;
}

static int get_required_int(struct status *s, const Json *object, const char *key, int32_t *value)
{
    int found = get_int(s, object, key, value);

    if (found == 0)
        return fail(s, "Missing required property '%s'.", key);
    return found < 0 ? -1 : 0;
}

/* KEY of OBJECT in *VALUE (NULL when missing); -1 (failed) when it is no string. */
static int get_string(struct status *s, const Json *object, const char *key, const char **value)
{
    const Json *item = json_get(object, key);

    *value = NULL;
    if (!item)
        return 0;
    if (item->type != JSON_STRING)
        return fail(s, "Property '%s' is of the incorrect type.", key);
    *value = item->string;
    return 0;
}

/* KEY of OBJECT as an array (NULL when missing); -1 (failed) when it is something else. */
static int get_array(struct status *s, const Json *object, const char *key, const Json **array)
{
    const Json *item = json_get(object, key);

    *array = NULL;
    if (!item)
        return 0;
    if (item->type != JSON_ARRAY)
        return fail(s, "Property '%s' is of the incorrect type.", key);
    *array = item;
    return 0;
}

/* Accessor SRC's elements, copied out of the BIN chunk through its buffer view. */
static int read_accessor(struct status *s, const Json *src, const struct buffer_view *views, size_t view_count,
                         const uint8_t *bin, size_t bin_size, struct accessor *dest)
{
    int32_t view_index = 0, byte_offset = 0, stride;
    const char *type;
    int found = get_int(s, src, "bufferView", &view_index);
    int size;
    const struct buffer_view *view;

    if (found < 0)
        return -1;
    if (found == 0)
        return fail(s, "Accessor without a buffer view (unimplemented).");
    if (view_index < 0 || (size_t)view_index >= view_count)
        return fail(s, "Accessor has invalid buffer view index.");
    if (get_int(s, src, "byteOffset", &byte_offset) < 0 ||
        get_required_int(s, src, "componentType", &dest->component_type) < 0 ||
        get_required_int(s, src, "count", &dest->count) < 0 || get_string(s, src, "type", &type) < 0)
        return -1;
    if (!type)
        return fail(s, "Missing required property 'type'.");
    dest->type = -1;
    for (int i = 0; i < (int)(sizeof ACCESSOR_TYPES / sizeof ACCESSOR_TYPES[0]); i++) {
        if (strcmp(type, ACCESSOR_TYPES[i].name) == 0)
            dest->type = i;
    }
    if (dest->type < 0)
        return fail(s, "Accessor has unknown type '%s'.", type);
    if (dest->count < 0)
        return fail(s, "Accessor has a negative count.");
    size = component_size(dest->component_type) * ACCESSOR_TYPES[dest->type].components;
    view = &views[view_index];
    stride = view->has_stride ? view->stride : size;
    if (view->buffer != 0)
        return fail(s, "GLB file has more than one buffer.");
    if (!view->has_offset)
        return fail(s, "Buffer view without a byte offset.");
    if (dest->count && size) {
        dest->bytes = malloc((size_t)dest->count * (size_t)size);
        if (!dest->bytes)
            return out_of_memory(s);
    }
    for (int32_t i = 0; i < dest->count; i++) {
        int64_t source = (int64_t)view->offset + (int64_t)i * stride + byte_offset;

        if (source < 0 || source + size > (int64_t)bin_size)
            return fail(s, "Buffer view out of range.");
        if (size)
            memcpy(dest->bytes + (size_t)i * (size_t)size, bin + source, (size_t)size);
    }
    return 0;
}

/* Attribute SEMANTIC of the accessor's count of vertices at DEST, from accessor A. */
static int read_attribute(struct status *s, struct vertex *dest, uint32_t semantic, const struct accessor *a)
{
    const uint8_t *b = a->bytes;

    switch (semantic) {
    case ATTRIBUTE_POSITION:
        if (a->type != TYPE_VEC3 || a->component_type != GLTF_FLOAT)
            return fail(s, "POSITION attribute is not a VEC3 of FLOAT components.");
        for (int32_t i = 0; i < a->count; i++) {
            const float v[3] = {get_f32(b + i * 12), get_f32(b + i * 12 + 4), get_f32(b + i * 12 + 8)};

            transform(GLTF_TO_RATCHET, v, dest[i].pos);
        }
        return 0;
    case ATTRIBUTE_TEXCOORD_0:
        if (a->type != TYPE_VEC2)
            return fail(s, "TEXCOORD attribute is not a VEC2.");
        for (int32_t i = 0; i < a->count; i++) {
            float *t = dest[i].tex_coord;

            switch (a->component_type) {
            case GLTF_FLOAT:
                t[0] = get_f32(b + i * 8);
                t[1] = get_f32(b + i * 8 + 4);
                break;
            case GLTF_UNSIGNED_BYTE:
                t[0] = b[i * 2] / 255.f;
                t[1] = b[i * 2 + 1] / 255.f;
                break;
            case GLTF_UNSIGNED_SHORT:
                t[0] = get_u16(b + i * 4) / 65535.f;
                t[1] = get_u16(b + i * 4 + 2) / 65535.f;
                break;
            default:
                return fail(s, "TEXCOORD attribute has an invalid component type (should be FLOAT, UNSIGNED_BYTE or "
                               "UNSIGNED_SHORT)");
            }
        }
        return 0;
    case ATTRIBUTE_NORMAL:
        if (a->type != TYPE_VEC3 || a->component_type != GLTF_FLOAT)
            return fail(s, "NORMAL attribute is not a VEC3 of FLOAT components.");
        for (int32_t i = 0; i < a->count; i++) {
            for (int k = 0; k < 3; k++)
                dest[i].normal[k] = get_f32(b + i * 12 + k * 4);
        }
        return 0;
    case ATTRIBUTE_COLOR_0: {
        int channels = a->type == TYPE_VEC3 ? 3 : 4;

        if (a->type != TYPE_VEC3 && a->type != TYPE_VEC4)
            return fail(s, "COLOR_0 attribute is not a VEC3 or a VEC4.");
        for (int32_t i = 0; i < a->count; i++) {
            uint8_t *c = dest[i].colour;

            c[3] = 255;
            for (int k = 0; k < channels; k++) {
                switch (a->component_type) {
                case GLTF_FLOAT:
                    c[k] = (uint8_t)to_int(get_f32(b + (i * channels + k) * 4) * 255.f);
                    break;
                case GLTF_UNSIGNED_BYTE:
                    c[k] = b[i * channels + k];
                    break;
                case GLTF_UNSIGNED_SHORT:
                    c[k] = (uint8_t)to_int(get_u16(b + (i * channels + k) * 2) * (1.f / 256.f));
                    break;
                default:
                    return fail(s, "COLOR_0 attribute has an invalid component type (should be FLOAT, UNSIGNED_BYTE "
                                   "or UNSIGNED_SHORT)");
                }
            }
        }
        return 0;
    }
    case ATTRIBUTE_JOINTS_0:
        if (a->type != TYPE_VEC4)
            return fail(s, "JOINTS_0 attribute is not a VEC4.");
        for (int32_t i = 0; i < a->count; i++) {
            for (int k = 0; k < 3; k++) {
                if (a->component_type == GLTF_UNSIGNED_BYTE)
                    dest[i].skin.joints[k] = (int8_t)b[i * 4 + k];
                else if (a->component_type == GLTF_UNSIGNED_SHORT)
                    dest[i].skin.joints[k] = (int8_t)b[i * 8 + k * 2];     /* the low byte */
                else
                    return fail(s, "JOINTS_0 attribute has an invalid component type (should be UNSIGNED_BYTE or "
                                   "UNSIGNED_SHORT)");
            }
        }
        return 0;
    case ATTRIBUTE_WEIGHTS_0:
        if (a->type != TYPE_VEC4)
            return fail(s, "WEIGHTS_0 attribute is not a VEC4.");
        for (int32_t i = 0; i < a->count; i++) {
            struct skin *skin = &dest[i].skin;
            uint8_t w[3] = {0, 0, 0};

            /* two weights each, at wrench's strides */
            switch (a->component_type) {
            case GLTF_FLOAT:
                w[0] = (uint8_t)to_int(roundf(get_f32(b + i * 8) / 255.f));
                w[1] = (uint8_t)to_int(roundf(get_f32(b + i * 8 + 4) / 255.f));
                break;
            case GLTF_UNSIGNED_BYTE:
                w[0] = b[i * 2];
                w[1] = b[i * 2 + 1];
                break;
            case GLTF_UNSIGNED_SHORT:
                w[0] = (uint8_t)to_int(roundf(get_u16(b + i * 4) * (1 / 256.f)));
                w[1] = (uint8_t)to_int(roundf(get_u16(b + i * 4 + 2) * (1 / 256.f)));
                break;
            default:
                return fail(s, "WEIGHTS_0 attribute has an invalid component type (should be FLOAT, UNSIGNED_BYTE or "
                               "UNSIGNED_SHORT)");
            }
            skin->count = 0;
            for (int k = 0; k < 3; k++) {
                skin->weights[k] = w[k];
                if (w[k] != 0)
                    skin->count++;
            }
        }
        return 0;
    default:
        return 0;
    }
}

static uint32_t attribute_from_string(const char *name)
{
    static const struct {
        const char *name;
        uint32_t bit;
    } names[] = {{"POSITION", ATTRIBUTE_POSITION}, {"NORMAL", ATTRIBUTE_NORMAL}, {"TEXCOORD_0", ATTRIBUTE_TEXCOORD_0},
                 {"COLOR_0", ATTRIBUTE_COLOR_0}, {"JOINTS_0", ATTRIBUTE_JOINTS_0}, {"WEIGHTS_0", ATTRIBUTE_WEIGHTS_0}};

    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(name, names[i].name) == 0)
            return names[i].bit;
    }
    return 0;
}

/* A primitive's index accessor as indices. */
static int read_indices(struct status *s, const Json *src, const struct gltf *gltf, struct ints *indices)
{
    int32_t index = 0;
    int found = get_int(s, src, "indices", &index);
    const struct accessor *a;

    if (found < 0)
        return -1;
    if (found == 0)
        return fail(s, "Support for non-indexed geometry not yet implemented.");
    if (index < 0 || (size_t)index >= gltf->accessor_count)
        return fail(s, "Mesh primitive has indices accessor index which is out of range.");
    a = &gltf->accessors[index];
    if (a->type != TYPE_SCALAR)
        return fail(s, "Indices accessor has an invalid type (must be a SCALAR).");
    for (int32_t i = 0; i < a->count; i++) {
        int32_t value;

        if (a->component_type == GLTF_UNSIGNED_BYTE)
            value = a->bytes[i];
        else if (a->component_type == GLTF_UNSIGNED_SHORT)
            value = get_u16(a->bytes + i * 2);
        else if (a->component_type == GLTF_UNSIGNED_INT)
            value = (int32_t)get_u32(a->bytes + i * 4);
        else
            return fail(s, "Indices accessor has an invalid component type (must be UNSIGNED_BYTE, UNSIGNED_SHORT or "
                           "UNSIGNED_INT).");
        if (ints_push(indices, value) < 0)
            return out_of_memory(s);
    }
    return 0;
}

/* A primitive of a glTF mesh: its vertices go on the end of the mesh's, its indices counted from there. */
static int read_primitive(struct status *s, const Json *src, const struct gltf *gltf, struct mesh *mesh)
{
    const Json *attributes = json_get(src, "attributes");
    int32_t vertex_count = 0;
    size_t base = mesh->vertex_count;
    struct primitive *primitive;

    if (!attributes || attributes->type != JSON_OBJECT)
        return fail(s, "Missing 'attributes' property.");
    for (size_t i = 0; i < attributes->count; i++) {
        const Json *item = &attributes->items[i];
        int64_t index = item->type == JSON_NUMBER ? (item->is_integer ? item->integer : (int64_t)item->number) : -1;

        if (index < 0 || (uint64_t)index >= gltf->accessor_count)
            return fail(s, "Mesh primitive has an attribute accessor index which is out of range.");
        if (gltf->accessors[index].count > vertex_count)
            vertex_count = gltf->accessors[index].count;
    }
    if (add_vertices(mesh, (size_t)vertex_count) < 0 || !(primitive = add_primitive(mesh)))
        return out_of_memory(s);
    for (size_t i = 0; i < attributes->count; i++) {
        const Json *item = &attributes->items[i];
        uint32_t semantic = attribute_from_string(attributes->keys[i]);
        int64_t index = item->is_integer ? item->integer : (int64_t)item->number;

        if (semantic) {
            if (read_attribute(s, mesh->vertices + base, semantic, &gltf->accessors[index]) < 0)
                return -1;
            primitive->attributes |= semantic;
        }
    }
    if (read_indices(s, src, gltf, &primitive->indices) < 0)
        return -1;
    for (size_t i = 0; i < primitive->indices.count; i++)
        primitive->indices.items[i] += (int32_t)base;
    if ((primitive->has_material = get_int(s, src, "material", &primitive->material)) < 0 ||
        (primitive->has_mode = get_int(s, src, "mode", &primitive->mode)) < 0)
        return -1;
    return 0;
}

/* A JSON array's items as the indices they are, each in 0 .. LIMIT - 1. */
static int check_indices(struct status *s, const Json *array, size_t limit, const char *what)
{
    for (size_t i = 0; array && i < array->count; i++) {
        const Json *item = &array->items[i];

        if (item->type != JSON_NUMBER || item->number < 0 || item->number >= (double)limit)
            return fail(s, "Out of bounds %s index.", what);
    }
    return 0;
}

/* The glTF's accessors, each read through its buffer view from the BIN chunk (BIN, BIN_SIZE bytes). */
static int read_accessors(struct status *s, struct gltf *gltf, const uint8_t *bin, size_t bin_size)
{
    struct buffer_view *views = NULL;
    size_t view_count = 0;
    const Json *array;
    int result = -1;

    if (get_array(s, gltf->root, "bufferViews", &array) < 0)
        return -1;
    if (array && array->count) {
        view_count = array->count;
        views = calloc(view_count, sizeof *views);
        if (!views)
            return out_of_memory(s);
        for (size_t i = 0; i < view_count; i++) {
            const Json *view = &array->items[i];
            int32_t byte_length;

            if (get_required_int(s, view, "buffer", &views[i].buffer) < 0 ||
                get_required_int(s, view, "byteLength", &byte_length) < 0 ||
                (views[i].has_offset = get_int(s, view, "byteOffset", &views[i].offset)) < 0 ||
                (views[i].has_stride = get_int(s, view, "byteStride", &views[i].stride)) < 0)
                goto done;
        }
    }
    if (get_array(s, gltf->root, "buffers", &array) < 0 || (array && array->count > 1)) {
        fail(s, "GLB file has more than one buffer.");
        goto done;
    }
    if (get_array(s, gltf->root, "accessors", &array) < 0)
        goto done;
    if (array && array->count) {
        gltf->accessors = calloc(array->count, sizeof *gltf->accessors);
        if (!gltf->accessors) {
            out_of_memory(s);
            goto done;
        }
        for (size_t i = 0; i < array->count; i++) {
            gltf->accessor_count++;
            if (read_accessor(s, &array->items[i], views, view_count, bin, bin_size, &gltf->accessors[i]) < 0)
                goto done;
        }
    }
    result = 0;

done:
    free(views);
    return result;
}

/* The checks of the indices between the parts a moby mesh reads: scenes' nodes, nodes' children and meshes,
 * primitives' materials, materials' textures and textures' samplers. */
static int validate_gltf(struct status *s, const struct gltf *gltf)
{
    const Json *array;
    int32_t texture_count, sampler_count;

    if (get_array(s, gltf->root, "textures", &array) < 0)
        return -1;
    texture_count = array ? (int32_t)array->count : 0;
    if (get_array(s, gltf->root, "samplers", &array) < 0)
        return -1;
    sampler_count = array ? (int32_t)array->count : 0;

    if (get_array(s, gltf->root, "scenes", &array) < 0)
        return -1;
    for (size_t i = 0; array && i < array->count; i++) {
        const Json *nodes;

        if (get_array(s, &array->items[i], "nodes", &nodes) < 0 ||
            check_indices(s, nodes, gltf->node_count, "node") < 0)
            return -1;
    }
    get_array(s, gltf->root, "nodes", &array);
    for (size_t i = 0; i < gltf->node_count; i++) {
        const Json *children;

        if (get_array(s, &array->items[i], "children", &children) < 0 ||
            check_indices(s, children, gltf->node_count, "child") < 0)
            return -1;
        if (gltf->nodes[i].has_mesh && (gltf->nodes[i].mesh < 0 || (size_t)gltf->nodes[i].mesh >= gltf->mesh_count))
            return fail(s, "Out of bounds mesh index %d for node '%s'.", (int)gltf->nodes[i].mesh,
                        gltf->nodes[i].name ? gltf->nodes[i].name : "unnamed");
    }
    for (size_t i = 0; i < gltf->mesh_count; i++) {
        for (size_t p = 0; p < gltf->meshes[i].primitive_count; p++) {
            const struct primitive *primitive = &gltf->meshes[i].primitives[p];

            if (primitive->has_material &&
                (primitive->material < 0 || (size_t)primitive->material >= gltf->material_count))
                return fail(s, "Out of bounds material index %d.", (int)primitive->material);
        }
    }
    get_array(s, gltf->root, "materials", &array);
    for (size_t i = 0; i < gltf->material_count; i++) {
        const Json *texture = json_get(json_get(&array->items[i], "pbrMetallicRoughness"), "baseColorTexture");
        int32_t index = 0;

        if (texture && (get_required_int(s, texture, "index", &index) < 0 || index < 0 || index >= texture_count))
            return fail(s, "Out of bounds texture index %d.", (int)index);
    }
    get_array(s, gltf->root, "textures", &array);
    for (int32_t i = 0; i < texture_count; i++) {
        int32_t sampler = 0;
        int found = get_int(s, &array->items[i], "sampler", &sampler);

        if (found < 0 || (found && (sampler < 0 || sampler >= sampler_count)))
            return fail(s, "Out of bounds sampler index %d.", (int)sampler);
    }
    return 0;
}

/* The glb DATA (SIZE bytes) as far as a moby mesh needs it: its accessors, asset, nodes, meshes (each with its
 * equal vertices merged) and material names, with the indices between them checked. */
static int read_glb(struct status *s, const uint8_t *data, size_t size, struct gltf *gltf)
{
    int64_t json_offset = -1, json_size = -1, bin_offset = -1, bin_size = -1;
    const Json *array, *asset;
    char parse_error[128];
    uint32_t length;

    if (size < 12)
        return fail(s, "The glb has no header.");
    length = get_u32(data + 8);
    for (int64_t offset = 12; offset < (int64_t)length;) {
        uint32_t chunk_length, type;

        if (offset + 8 > (int64_t)size)
            return fail(s, "A glb chunk is outside the file.");
        chunk_length = get_u32(data + offset);
        type = get_u32(data + offset + 4);
        if (type == 0x4e4f534au) {          /* "JSON" */
            json_offset = offset + 8;
            json_size = chunk_length;
        } else if (type == 0x004e4942u) {   /* "BIN\0" */
            bin_offset = offset + 8;
            bin_size = chunk_length;
        }
        offset += 8 + (int64_t)chunk_length;
    }
    if (json_offset < 0 || json_offset + json_size > (int64_t)size)
        return fail(s, "No valid JSON chunk present.");
    if (bin_offset < 0 || bin_offset + bin_size > (int64_t)size)
        return fail(s, "No valid BIN chunk present.");
    gltf->root = json_parse((const char *)data + json_offset, (size_t)json_size, parse_error, sizeof parse_error);
    if (!gltf->root)
        return fail(s, "The glb's JSON: %s", parse_error);
    if (read_accessors(s, gltf, data + bin_offset, (size_t)bin_size) < 0)
        return -1;

    asset = json_get(gltf->root, "asset");
    if (!asset || !json_get_string(asset, "version"))
        return fail(s, "Missing required property 'asset'.");
    if (get_array(s, gltf->root, "nodes", &array) < 0)
        return -1;
    if (array && array->count) {
        gltf->nodes = calloc(array->count, sizeof *gltf->nodes);
        if (!gltf->nodes)
            return out_of_memory(s);
        gltf->node_count = array->count;
        for (size_t i = 0; i < array->count; i++) {
            struct node *node = &gltf->nodes[i];

            if (get_string(s, &array->items[i], "name", &node->name) < 0 ||
                (node->has_mesh = get_int(s, &array->items[i], "mesh", &node->mesh)) < 0)
                return -1;
        }
    }
    if (get_array(s, gltf->root, "meshes", &array) < 0)
        return -1;
    if (array && array->count) {
        gltf->meshes = calloc(array->count, sizeof *gltf->meshes);
        if (!gltf->meshes)
            return out_of_memory(s);
        gltf->mesh_count = array->count;
        for (size_t i = 0; i < array->count; i++) {
            const Json *primitives;

            if (get_array(s, &array->items[i], "primitives", &primitives) < 0)
                return -1;
            if (!primitives)
                return fail(s, "Missing required property 'primitives'.");
            for (size_t p = 0; p < primitives->count; p++) {
                if (read_primitive(s, &primitives->items[p], gltf, &gltf->meshes[i]) < 0)
                    return -1;
            }
            if (deduplicate_vertices(&gltf->meshes[i], s) < 0)
                return -1;
        }
    }
    if (get_array(s, gltf->root, "materials", &array) < 0)
        return -1;
    if (array && array->count) {
        gltf->material_names = calloc(array->count, sizeof *gltf->material_names);
        if (!gltf->material_names)
            return out_of_memory(s);
        gltf->material_count = array->count;
        for (size_t i = 0; i < array->count; i++) {
            if (get_string(s, &array->items[i], "name", &gltf->material_names[i]) < 0)
                return -1;
        }
    }
    return validate_gltf(s, gltf);
}

/* ------------------------------------------------------------------------------------------------ strips */

/* A strip with zero-area triangles as one with restart bits: a repeated index becomes the restart bit on the next
 * one. */
static int to_restart_bits(const struct ints *indices, struct ints *out)
{
    int set_next_restart_bit = 0;

    out->count = 0;
    for (size_t i = 0; i < indices->count; i++) {
        if (i == 0 || indices->items[i - 1] != indices->items[i]) {
            uint32_t index = (uint32_t)indices->items[i] | (set_next_restart_bit ? RESTART_BIT : 0);

            if (ints_push(out, (int32_t)index) < 0)
                return -1;
            set_next_restart_bit = 0;
        } else {
            set_next_restart_bit = 1;
        }
    }
    return 0;
}

/* A strip with restart bits as one with zero-area triangles: the index before a restart is repeated. */
static int to_zero_area_tris(const int32_t *indices, size_t count, struct ints *out)
{
    out->count = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0 && indices[i] < 0 && ints_push(out, out->items[out->count - 1]) < 0)
            return -1;
        if (ints_push(out, (int32_t)((uint32_t)indices[i] & ~RESTART_BIT)) < 0)
            return -1;
    }
    return 0;
}

/* The packet splitter, as wrench configures it for mobies: index buffers, no instancing, and two limits on a packet,
 * VU1's vertex buffer (0xc2 quadwords, two per vertex) and the data UNPACKed to VU1 (from the fixed index buffer
 * address 0x12d, the index header and the terminating indices: four bytes per index in whole quadwords, and four
 * quadwords per GS register set). */
struct constraint {
    int32_t constant_cost, strip_cost, vertex_cost, index_cost, material_cost, max_cost, index_multiple;
};

static const struct constraint MOBY_CONSTRAINTS[2] = {
    {0, 0, 2, 0, 0, 0xc2, 1},
    {(0x12d + 1 + 1) * 16, 0, 0, 4, 4 * 16, 0x164 * 16, 16},
};

struct generator_packet {
    size_t primitive_begin;
    size_t primitive_count;
};

struct generator_primitive {
    size_t index_begin;
    size_t index_count;
    int32_t effective_material;
};

struct generator {
    int32_t strips, vertices, indices, materials;   /* the current packet's totals */
    int has_packet;
    int32_t current_material;
    int32_t current_packet;
    int32_t *last_packet_with_vertex;
    struct generator_packet *packets;
    size_t packet_count, packet_capacity;
    struct generator_primitive *primitives;
    size_t primitive_count, primitive_capacity;
    struct ints output;
    int out_of_memory;
};

static int fits(const struct generator *g, int32_t strips, int32_t vertices, int32_t indices, int32_t materials)
{
    if (!g->has_packet)
        return 0;
    for (int i = 0; i < 2; i++) {
        const struct constraint *c = &MOBY_CONSTRAINTS[i];
        int32_t index_cost = c->index_cost * indices;
        int32_t cost = c->constant_cost + c->strip_cost * strips + c->vertex_cost * vertices;

        if (index_cost % c->index_multiple != 0)
            index_cost += c->index_multiple - index_cost % c->index_multiple;
        cost += index_cost + c->material_cost * materials;
        if (cost > c->max_cost)
            return 0;
    }
    return 1;
}

static void new_packet(struct generator *g)
{
    struct generator_packet *packets = reserve(g->packets, &g->packet_capacity, g->packet_count + 1,
                                               sizeof *packets);

    g->strips = g->vertices = g->indices = g->materials = 0;
    if (!packets) {
        g->out_of_memory = 1;
        return;
    }
    g->packets = packets;
    packets[g->packet_count].primitive_begin = g->primitive_count;
    packets[g->packet_count].primitive_count = 0;
    g->packet_count++;
    g->has_packet = 1;
    g->current_packet++;
}

static int try_add_vertex(struct generator *g, int32_t index)
{
    size_t vertex = (uint32_t)index & ~RESTART_BIT;     /* below the vertex count: the glb's reader checked */

    if (g->last_packet_with_vertex[vertex] != g->current_packet) {
        int added = fits(g, g->strips, g->vertices + 1, g->indices + 1, g->materials);

        g->last_packet_with_vertex[vertex] = g->current_packet;
        if (added) {
            g->vertices++;
            g->indices++;
        }
        return added;
    }
    if (!fits(g, g->strips, g->vertices, g->indices + 1, g->materials))
        return 0;
    g->indices++;
    return 1;
}

static int zero_area(int32_t v0, int32_t v1, int32_t v2)
{
    return v0 == v1 || v0 == v2 || v1 == v2;
}

/* A strip with restart bits into the packets: a new packet whenever the material, the strip or a vertex does not
 * fit, and a strip that runs out of room goes on in a new packet. */
static void generate(struct generator *g, const int32_t *indices, size_t count, int32_t material)
{
    while (count >= 3 && !g->out_of_memory) {
        struct generator_primitive *primitive;
        size_t j;
        int valid;

        if (material != g->current_material) {
            if (!fits(g, g->strips, g->vertices, g->indices, g->materials + 1)) {
                new_packet(g);
                continue;
            }
            g->materials++;
            g->current_material = material;
        }
        if (!fits(g, g->strips + 1, g->vertices, g->indices, g->materials)) {
            new_packet(g);
            continue;
        }
        g->strips++;
        valid = try_add_vertex(g, indices[0]);
        valid &= try_add_vertex(g, indices[1]);
        valid &= try_add_vertex(g, indices[2]);
        if (!valid) {
            new_packet(g);
            continue;
        }

        primitive = reserve(g->primitives, &g->primitive_capacity, g->primitive_count + 1, sizeof *primitive);
        if (!primitive) {
            g->out_of_memory = 1;
            return;
        }
        g->primitives = primitive;
        primitive = &g->primitives[g->primitive_count++];
        g->packets[g->packet_count - 1].primitive_count++;
        primitive->index_begin = g->output.count;
        primitive->index_count = 3;
        primitive->effective_material = g->current_material;
        for (j = 0; j < 3; j++) {
            if (ints_push(&g->output, indices[j]) < 0)
                g->out_of_memory = 1;
        }
        for (j = 3; j < count; j++) {
            if (!try_add_vertex(g, indices[j]))
                break;
            primitive->index_count++;
            if (ints_push(&g->output, indices[j]) < 0)
                g->out_of_memory = 1;
        }
        if (j == count)
            return;
        /* split the strip: the rest goes on in a new packet, from the triangle before the next drawn one */
        new_packet(g);
        while (j < count && zero_area(indices[j - 2], indices[j - 1], indices[j]))
            j++;
        indices += j - 2;
        count -= j - 2;
    }
}

/* MESH split into packets, for meshes with each primitive's material an index into MATERIAL_TO_EFFECTIVE
 * (MATERIAL_COUNT of them). PACKETS gets a new array of packet meshes (*PACKET_COUNT), each with the vertices its
 * strips use in the order they first use them, and the effective materials as the primitives' materials. */
static int split_packets(struct status *s, const struct mesh *mesh, const int32_t *material_to_effective,
                         size_t material_count, struct mesh **packets, size_t *packet_count)
{
    struct generator g;
    struct ints strip = {NULL, 0, 0}, zero_area_strip = {NULL, 0, 0};
    int32_t *mapping = NULL;
    struct mesh *out = NULL;
    int result = -1;

    memset(&g, 0, sizeof g);
    g.current_material = -1;
    g.current_packet = -1;
    g.last_packet_with_vertex = malloc((mesh->vertex_count ? mesh->vertex_count : 1) * sizeof(int32_t));
    mapping = malloc((mesh->vertex_count ? mesh->vertex_count : 1) * sizeof *mapping);
    if (!g.last_packet_with_vertex || !mapping)
        goto out_of_memory;
    for (size_t i = 0; i < mesh->vertex_count; i++)
        g.last_packet_with_vertex[i] = -1;
    for (size_t p = 0; p < mesh->primitive_count; p++) {
        const struct primitive *primitive = &mesh->primitives[p];

        if (!primitive->has_material || primitive->material < 0 || (size_t)primitive->material >= material_count)
            continue;
        if (to_restart_bits(&primitive->indices, &strip) < 0)
            goto out_of_memory;
        generate(&g, strip.items, strip.count, material_to_effective[primitive->material]);
        if (g.out_of_memory)
            goto out_of_memory;
    }

    out = calloc(g.packet_count ? g.packet_count : 1, sizeof *out);
    if (!out)
        goto out_of_memory;
    for (size_t p = 0; p < g.packet_count; p++) {
        struct mesh *dest = &out[p];

        for (size_t i = 0; i < mesh->vertex_count; i++)
            mapping[i] = -1;
        for (size_t k = 0; k < g.packets[p].primitive_count; k++) {
            const struct generator_primitive *source = &g.primitives[g.packets[p].primitive_begin + k];
            struct primitive *primitive = add_primitive(dest);

            if (!primitive || to_zero_area_tris(g.output.items + source->index_begin, source->index_count,
                                                &zero_area_strip) < 0)
                goto out_of_memory;
            primitive->attributes = ATTRIBUTE_POSITION | ATTRIBUTE_TEXCOORD_0 | ATTRIBUTE_NORMAL;
            primitive->has_material = 1;
            primitive->material = source->effective_material;
            primitive->has_mode = 1;
            primitive->mode = TRIANGLE_STRIP;
            /* the packet's own vertices, in the order its strips first use them */
            for (size_t i = 0; i < zero_area_strip.count; i++) {
                int32_t index = zero_area_strip.items[i];

                if (mapping[index] == -1) {
                    mapping[index] = (int32_t)dest->vertex_count;
                    if (add_vertices(dest, 1) < 0)
                        goto out_of_memory;
                    dest->vertices[dest->vertex_count - 1] = mesh->vertices[index];
                }
                if (ints_push(&primitive->indices, mapping[index]) < 0)
                    goto out_of_memory;
            }
        }
    }
    *packets = out;
    *packet_count = g.packet_count;
    out = NULL;
    result = 0;
    goto done;

out_of_memory:
    out_of_memory(s);
done:
    if (out) {
        for (size_t p = 0; p < g.packet_count; p++)
            free_mesh(&out[p]);
        free(out);
    }
    free(g.last_packet_with_vertex);
    free(g.packets);
    free(g.primitives);
    free(g.output.items);
    free(strip.items);
    free(zero_area_strip.items);
    free(mapping);
    return result;
}

/* ------------------------------------------------------------------------------------------------ packing */

/* A vertex as the game stores it: the skinning bytes left zero, the
 * normal as azimuth and elevation (1/256 of a turn), the position in the class's units. */
static void pack_vertex(uint8_t *out, const struct vertex *vertex, float inverse_scale)
{
    const float *n = vertex->normal;
    float x2 = n[0] * n[0], y2 = n[1] * n[1], z2 = n[2] * n[2];
    float length_squared = x2 + y2;
    float inverse_length, normal[3], azimuth, elevation;
    uint8_t azimuth_byte, elevation_byte;

    length_squared = length_squared + z2;
    inverse_length = 1.f / sqrtf(length_squared);
    for (int k = 0; k < 3; k++)
        normal[k] = n[k] * inverse_length;
    azimuth = atan2f(normal[0], normal[1]);
    elevation = asinf(normal[2]);
    memset(out, 0, VERTEX_SIZE);
    azimuth_byte = (uint8_t)to_int(roundf((float)(azimuth * (128.f / WRENCH_PI))));
    elevation_byte = (uint8_t)to_int(roundf((float)(elevation * (128.f / WRENCH_PI))));
    /* a normal straight up has no azimuth: Insomniac's exporter gives it 0x80 more */
    if (elevation_byte == 0x40)
        azimuth_byte = (uint8_t)(azimuth_byte + 0x80);
    out[8] = azimuth_byte;
    out[9] = elevation_byte;
    for (int k = 0; k < 3; k++)
        set_u16(out + 0xa + k * 2, (uint16_t)(int16_t)to_int(roundf(vertex->pos[k] * inverse_scale)));
}

/* A packet as wrench's builder makes it, before the class's conventions. */
struct built_packet {
    const struct mesh *source;      /* its vertices, all main vertices in order */
    struct bytes indices;           /* signed bytes: vertex + 1, bit 7 the restart bit, 0 for a texture change */
    struct bytes secret_indices;    /* the index each texture change starts with, vertex + 1 */
    struct ints textures;           /* the texture of each GS register set */
};

static void free_built_packets(struct built_packet *packets, size_t count)
{
    for (size_t i = 0; packets && i < count; i++) {
        free(packets[i].indices.items);
        free(packets[i].secret_indices.items);
        free(packets[i].textures.items);
    }
    free(packets);
}

/* The index streams and texture changes of the split packets PARTS. TEXTURE_OF_EFFECTIVE is the texture of each
 * effective material. */
static int build_packets(struct status *s, const struct mesh *parts, size_t part_count,
                         const int32_t *texture_of_effective, struct built_packet **packets)
{
    struct built_packet *out = calloc(part_count ? part_count : 1, sizeof *out);
    struct ints strip = {NULL, 0, 0};
    int32_t last_effective_material = -1;

    if (!out)
        return out_of_memory(s);
    for (size_t p = 0; p < part_count; p++) {
        const struct mesh *src = &parts[p];
        struct built_packet *dest = &out[p];

        dest->source = src;
        for (size_t k = 0; k < src->primitive_count; k++) {
            const struct primitive *primitive = &src->primitives[k];
            int use_secret_index = 0;

            if (primitive->material != last_effective_material) {
                if (ints_push(&dest->textures, texture_of_effective[primitive->material]) < 0)
                    goto out_of_memory;
                last_effective_material = primitive->material;
                use_secret_index = 1;
            }
            if (to_restart_bits(&primitive->indices, &strip) < 0)
                goto out_of_memory;
            for (size_t j = 0; j < strip.count; j++) {
                int32_t index = (int32_t)((uint32_t)strip.items[j] & ~RESTART_BIT);
                int restart_bit = j < 2 || ((uint32_t)strip.items[j] & RESTART_BIT) != 0;
                /* each vertex stays where it is in the part: its index is its VU1 slot */
                if (use_secret_index) {
                    if (bytes_push(&dest->indices, 0) < 0 ||
                        bytes_push(&dest->secret_indices, (uint8_t)(index + 1)) < 0)
                        goto out_of_memory;
                    use_secret_index = 0;
                } else if (bytes_push(&dest->indices, (uint8_t)((index + 1) | restart_bit << 7)) < 0) {
                    goto out_of_memory;
                }
            }
        }
        /* fake indices that tell the microprogram to stop */
        if (bytes_append(&dest->indices, "\1\1\1\0", 4) < 0)
            goto out_of_memory;
    }
    free(strip.items);
    *packets = out;
    return 0;

out_of_memory:
    free(strip.items);
    free_built_packets(out, part_count);
    return out_of_memory(s);
}

static void *copy_to_class(struct moby_class *moby, const void *data, size_t size, int *failed)
{
    void *block;

    if (size == 0)
        return NULL;
    block = moby_alloc(moby, size);
    if (!block) {
        *failed = 1;
        return NULL;
    }
    memcpy(block, data, size);
    return block;
}

/* The built packets in the class, with the class's conventions where the builder has its own: the index
 * stream padded to whole quadwords with zeros, packet 0 transferring the class's packet
 * 0's matrices (joint 0 to VU0) and every vertex loading it from there without storing (bytes 2 and 3), the vertex
 * index 0xff (no duplicate vertex copies it), the index header's first byte the class's, and the GS registers the
 * class's first ones but for the texture. */
static int install_packets(struct status *s, struct moby_class *moby, const struct built_packet *built, size_t count,
                           float scale)
{
    const struct moby_packet *old = &moby->high_lod[0];
    const uint8_t *registers = old->vif.textures;
    uint8_t vu0 = old->table.transfer_count ? old->table.transfers[1] : 0;
    struct moby_packet *packets = moby_alloc(moby, count * sizeof *packets);
    float inverse_scale = 1024.f / scale;
    int failed = !packets;

    for (size_t p = 0; p < count && !failed; p++) {
        const struct built_packet *src = &built[p];
        const struct mesh *mesh = src->source;
        struct moby_packet *dest = &packets[p];
        size_t index_count = (src->indices.count + 3) / 4 * 4;
        uint8_t *vertices = mesh->vertex_count ? moby_alloc(moby, mesh->vertex_count * VERTEX_SIZE) : NULL;
        uint8_t *sts = mesh->vertex_count ? moby_alloc(moby, mesh->vertex_count * 4) : NULL;
        uint8_t *indices = moby_alloc(moby, index_count);
        uint8_t *textures = src->textures.count ? moby_alloc(moby, src->textures.count * TEXTURE_SIZE) : NULL;

        if ((mesh->vertex_count && (!vertices || !sts)) || !indices || (src->textures.count && !textures)) {
            failed = 1;
            break;
        }
        for (size_t i = 0; i < mesh->vertex_count; i++) {
            const struct vertex *vertex = &mesh->vertices[i];
            uint8_t *mv = vertices + i * VERTEX_SIZE;

            pack_vertex(mv, vertex, inverse_scale);
            set_u16(mv, 0xff);
            mv[2] = vu0;
            mv[3] = VU0_UNUSED;
            set_u16(sts + i * 4, (uint16_t)to_int(roundf(vertex->tex_coord[0] * 4096.f)));
            set_u16(sts + i * 4 + 2, (uint16_t)to_int(roundf(vertex->tex_coord[1] * 4096.f)));
        }
        memcpy(indices, src->indices.items, src->indices.count);
        for (size_t i = 0; i < src->textures.count; i++) {
            uint8_t *texture = textures + i * TEXTURE_SIZE;

            for (int quadword = 0; quadword < 4; quadword++)
                memcpy(texture + quadword * 0x10, registers + quadword * 0x10, 12);
            set_u32(texture + 0x20, (uint32_t)src->textures.items[i]);
        }
        dest->table.transfers = p == 0 ? old->table.transfers : NULL;
        dest->table.transfer_count = p == 0 ? old->table.transfer_count : 0;
        dest->table.main_count = (int32_t)mesh->vertex_count;
        dest->table.vertices = vertices;
        dest->table.vertex_count = mesh->vertex_count;
        dest->vif.indices.data = indices;
        dest->vif.indices.size = index_count;
        dest->vif.secret_indices = copy_to_class(moby, src->secret_indices.items, src->secret_indices.count, &failed);
        dest->vif.secret_count = src->secret_indices.count;
        dest->vif.textures = textures;
        dest->vif.texture_count = src->textures.count;
        dest->vif.index_header_first_byte = old->vif.index_header_first_byte;
        dest->sts = sts;
        dest->st_count = mesh->vertex_count;
    }
    if (failed)
        return out_of_memory(s);
    moby->high_lod = packets;
    moby->high_count = count;
    moby->low_lod = packets;
    moby->low_count = count;
    return 0;
}

/* sscanf(name, "material_%d", &texture) == 1 */
static int parse_material_name(const char *name, int32_t *texture)
{
    const char *at = name + 9;
    int negative = 0;
    int64_t value = 0;

    if (strncmp(name, "material_", 9) != 0)
        return 0;
    while (*at == ' ' || (*at >= '\t' && *at <= '\r'))
        at++;
    if (*at == '+' || *at == '-')
        negative = *at++ == '-';
    if (*at < '0' || *at > '9')
        return 0;
    while (*at >= '0' && *at <= '9') {
        if (value < INT32_MAX)
            value = value * 10 + (*at - '0');
        at++;
    }
    if (value > INT32_MAX)
        value = INT32_MAX;
    *texture = (int32_t)(negative ? -value : value);
    return 1;
}

int moby_mesh_replace(struct moby_class *moby, const uint8_t *glb, size_t glb_size, struct moby_mesh_counts *counts,
                      char *error, size_t error_size)
{
    struct status status = {error, error_size, 0};
    struct status *s = &status;
    struct gltf gltf;
    const struct node *node = NULL;
    struct mesh *mesh, *parts = NULL;
    size_t part_count = 0, effective_count = 0;
    int32_t *textures = NULL, *mapping = NULL, *material_to_effective = NULL, *texture_of_effective = NULL;
    struct built_packet *built = NULL;
    float scale = bits_to_float(moby->scale);
    int result = -1;

    memset(&gltf, 0, sizeof gltf);
    if (moby->joint_count != 0)
        return fail(s, "--mesh only rebuilds classes without joints.");
    if (moby->metal_count != 0 || moby->bangle_count != 0)
        return fail(s, "--mesh only rebuilds classes without metal packets or bangles.");
    if (read_glb(s, glb, glb_size, &gltf) < 0)
        goto done;
    for (size_t i = 0; i < gltf.node_count && !node; i++) {
        if (gltf.nodes[i].name && strcmp(gltf.nodes[i].name, "moby") == 0)
            node = &gltf.nodes[i];
    }
    if (!node || !node->has_mesh) {
        fail(s, "The glb has no node 'moby' with a mesh.");
        goto done;
    }
    mesh = &gltf.meshes[node->mesh];

    /* the materials: material_<i> for texture i, each primitive's taken to the last material of its name (as wrench
     * maps them), then merged by texture into effective materials */
    textures = malloc((gltf.material_count + 1) * sizeof *textures);
    mapping = malloc((gltf.material_count + 1) * sizeof *mapping);
    material_to_effective = malloc((gltf.material_count + 1) * sizeof *material_to_effective);
    texture_of_effective = malloc((gltf.material_count + 1) * sizeof *texture_of_effective);
    if (!textures || !mapping || !material_to_effective || !texture_of_effective) {
        out_of_memory(s);
        goto done;
    }
    for (size_t i = 0; i < gltf.material_count; i++) {
        const char *name = gltf.material_names[i];

        if (!name || !parse_material_name(name, &textures[i]) || textures[i] < 0) {
            fail(s, "Materials must be named material_<texture index>.");
            goto done;
        }
    }
    for (size_t i = 0; i < gltf.material_count; i++) {
        for (size_t j = 0; j < gltf.material_count; j++) {
            if (strcmp(gltf.material_names[i], gltf.material_names[j]) == 0)
                mapping[i] = (int32_t)j;
        }
        material_to_effective[i] = -1;
    }
    for (size_t p = 0; p < mesh->primitive_count; p++) {
        struct primitive *primitive = &mesh->primitives[p];

        if (primitive->has_material && primitive->material >= 0 && (size_t)primitive->material < gltf.material_count)
            primitive->material = mapping[primitive->material];
    }
    for (size_t i = 0; i < gltf.material_count; i++) {
        if (material_to_effective[i] != -1)
            continue;
        texture_of_effective[effective_count] = textures[i];
        for (size_t j = 0; j < gltf.material_count; j++) {
            if (material_to_effective[j] == -1 && textures[j] == textures[i])
                material_to_effective[j] = (int32_t)effective_count;
        }
        effective_count++;
    }

    if (split_packets(s, mesh, material_to_effective, gltf.material_count, &parts, &part_count) < 0 ||
        build_packets(s, parts, part_count, texture_of_effective, &built) < 0)
        goto done;
    if (part_count >= 256) {
        fail(s, "Too many packets (%zu).", part_count);
        goto done;
    }
    if (moby->high_count == 0 || moby->high_lod[0].vif.texture_count == 0) {
        fail(s, "The class has no textured packet to take conventions from.");
        goto done;
    }
    for (size_t p = 0; p < part_count; p++) {
        if (built[p].secret_indices.count < built[p].textures.count) {
            fail(s, "A packet has fewer extra indices than textures (a texture with no strip).");
            goto done;
        }
    }
    if (install_packets(s, moby, built, part_count, scale) < 0)
        goto done;
    if (counts) {
        counts->vertices = mesh->vertex_count;
        counts->primitives = mesh->primitive_count;
        counts->packets = part_count;
    }
    result = 0;

done:
    free_built_packets(built, part_count);
    for (size_t p = 0; parts && p < part_count; p++)
        free_mesh(&parts[p]);
    free(parts);
    free(textures);
    free(mapping);
    free(material_to_effective);
    free(texture_of_effective);
    free_gltf(&gltf);
    return result;
}

/* ----------------------------------------------------------------------------------------------- recovery */

/* sinf and cosf of each normal angle byte's angle (byte * pi / 128, wrench's float), as wrench's libm (MinGW-w64's)
 * gives them, so that every platform's normals are wrench's: glibc's differ in the last bit at 8 of the 256 angles,
 * and the correctly rounded cosine at one (byte 224). */
static const uint32_t ANGLE_SINES[256] = {
    0x00000000, 0x3cc90ab0, 0x3d48fb30, 0x3d96a905, 0x3dc8bd36, 0x3dfab272, 0x3e164083, 0x3e2f10a2,
    0x3e47c5c2, 0x3e605c13, 0x3e78cfcc, 0x3e888e93, 0x3e94a031, 0x3ea09ae5, 0x3eac7cd3, 0x3eb8442a,
    0x3ec3ef16, 0x3ecf7bca, 0x3edae880, 0x3ee63375, 0x3ef15ae9, 0x3efc5d26, 0x3f039c3d, 0x3f08f59b,
    0x3f0e39da, 0x3f13682b, 0x3f187fc0, 0x3f1d7fd1, 0x3f226799, 0x3f273656, 0x3f2beb4a, 0x3f3085bb,
    0x3f3504f3, 0x3f396841, 0x3f3daef9, 0x3f41d870, 0x3f45e403, 0x3f49d112, 0x3f4d9f02, 0x3f514d3d,
    0x3f54db31, 0x3f584853, 0x3f5b941a, 0x3f5ebe05, 0x3f61c597, 0x3f64aa59, 0x3f676bd8, 0x3f6a09a7,
    0x3f6c835e, 0x3f6ed89d, 0x3f710908, 0x3f731447, 0x3f74fa0b, 0x3f76ba07, 0x3f7853f8, 0x3f79c79d,
    0x3f7b14be, 0x3f7c3b28, 0x3f7d3aac, 0x3f7e1324, 0x3f7ec46d, 0x3f7f4e6d, 0x3f7fb10f, 0x3f7fec43,
    0x3f800000, 0x3f7fec43, 0x3f7fb10f, 0x3f7f4e6d, 0x3f7ec46d, 0x3f7e1323, 0x3f7d3aac, 0x3f7c3b28,
    0x3f7b14be, 0x3f79c79e, 0x3f7853f8, 0x3f76ba07, 0x3f74fa0a, 0x3f731447, 0x3f710908, 0x3f6ed89e,
    0x3f6c835f, 0x3f6a09a6, 0x3f676bd8, 0x3f64aa59, 0x3f61c598, 0x3f5ebe06, 0x3f5b941a, 0x3f584852,
    0x3f54db32, 0x3f514d3d, 0x3f4d9f02, 0x3f49d113, 0x3f45e404, 0x3f41d870, 0x3f3daef8, 0x3f396842,
    0x3f3504f3, 0x3f3085ba, 0x3f2beb4b, 0x3f273656, 0x3f226799, 0x3f1d7fd0, 0x3f187fc1, 0x3f13682a,
    0x3f0e39d9, 0x3f08f59c, 0x3f039c3d, 0x3efc5d25, 0x3ef15aed, 0x3ee63376, 0x3edae87f, 0x3ecf7bc7,
    0x3ec3ef17, 0x3eb8442a, 0x3eac7cd2, 0x3ea09ae8, 0x3e94a033, 0x3e888e92, 0x3e78cfc5, 0x3e605c17,
    0x3e47c5c1, 0x3e2f109d, 0x3e16408a, 0x3dfab276, 0x3dc8bd30, 0x3d96a8f5, 0x3d48fb3d, 0x3cc90aa6,
    0xb3bbbd2e, 0xbcc90a84, 0xbd48fb2c, 0xbd96a90c, 0xbdc8bd27, 0xbdfab26d, 0xbe164085, 0xbe2f10a9,
    0xbe47c5bd, 0xbe605c13, 0xbe78cfd0, 0xbe888e90, 0xbe94a030, 0xbea09ae6, 0xbeac7cd7, 0xbeb84428,
    0xbec3ef15, 0xbecf7bcd, 0xbedae87e, 0xbee63374, 0xbef15aeb, 0xbefc5d2a, 0xbf039c3c, 0xbf08f59b,
    0xbf0e39db, 0xbf136829, 0xbf187fc0, 0xbf1d7fd2, 0xbf226798, 0xbf273655, 0xbf2beb4a, 0xbf3085bc,
    0xbf3504f2, 0xbf396842, 0xbf3daefa, 0xbf41d86f, 0xbf45e403, 0xbf49d113, 0xbf4d9f03, 0xbf514d3f,
    0xbf54db2f, 0xbf584851, 0xbf5b9419, 0xbf5ebe05, 0xbf61c598, 0xbf64aa5a, 0xbf676bd9, 0xbf6a09a5,
    0xbf6c835e, 0xbf6ed89d, 0xbf710908, 0xbf731448, 0xbf74fa0b, 0xbf76ba08, 0xbf7853f7, 0xbf79c79d,
    0xbf7b14be, 0xbf7c3b28, 0xbf7d3aac, 0xbf7e1324, 0xbf7ec46d, 0xbf7f4e6d, 0xbf7fb10f, 0xbf7fec43,
    0xbf800000, 0xbf7fec43, 0xbf7fb10f, 0xbf7f4e6d, 0xbf7ec46d, 0xbf7e1324, 0xbf7d3aac, 0xbf7c3b28,
    0xbf7b14be, 0xbf79c79d, 0xbf7853f7, 0xbf76ba08, 0xbf74fa0b, 0xbf731448, 0xbf710908, 0xbf6ed89d,
    0xbf6c835d, 0xbf6a09a8, 0xbf676bd9, 0xbf64aa5a, 0xbf61c598, 0xbf5ebe05, 0xbf5b9419, 0xbf584851,
    0xbf54db33, 0xbf514d3f, 0xbf4d9f03, 0xbf49d112, 0xbf45e403, 0xbf41d86f, 0xbf3daef7, 0xbf396844,
    0xbf3504f5, 0xbf3085bc, 0xbf2beb4a, 0xbf273655, 0xbf226797, 0xbf1d7fcf, 0xbf187fc3, 0xbf13682c,
    0xbf0e39db, 0xbf08f59b, 0xbf039c3c, 0xbefc5d23, 0xbef15ae3, 0xbee6337b, 0xbedae884, 0xbecf7bcc,
    0xbec3ef15, 0xbeb84427, 0xbeac7ccf, 0xbea09ade, 0xbe94a037, 0xbe888e97, 0xbe78cfcf, 0xbe605c12,
    0xbe47c5bc, 0xbe2f1097, 0xbe164074, 0xbdfab28a, 0xbdc8bd44, 0xbd96a909, 0xbd48fb26, 0xbcc90a77,
};
static const uint32_t ANGLE_COSINES[256] = {
    0x3f800000, 0x3f7fec43, 0x3f7fb10f, 0x3f7f4e6d, 0x3f7ec46d, 0x3f7e1324, 0x3f7d3aac, 0x3f7c3b28,
    0x3f7b14be, 0x3f79c79d, 0x3f7853f8, 0x3f76ba07, 0x3f74fa0b, 0x3f731447, 0x3f710908, 0x3f6ed89e,
    0x3f6c835e, 0x3f6a09a7, 0x3f676bd8, 0x3f64aa59, 0x3f61c598, 0x3f5ebe06, 0x3f5b941a, 0x3f584853,
    0x3f54db31, 0x3f514d3d, 0x3f4d9f02, 0x3f49d113, 0x3f45e404, 0x3f41d870, 0x3f3daef9, 0x3f396842,
    0x3f3504f3, 0x3f3085bb, 0x3f2beb4a, 0x3f273656, 0x3f226799, 0x3f1d7fd1, 0x3f187fc0, 0x3f13682b,
    0x3f0e39da, 0x3f08f59b, 0x3f039c3e, 0x3efc5d27, 0x3ef15aeb, 0x3ee63374, 0x3edae881, 0x3ecf7bc9,
    0x3ec3ef15, 0x3eb8442b, 0x3eac7cd3, 0x3ea09ae5, 0x3e94a030, 0x3e888e93, 0x3e78cfd0, 0x3e605c12,
    0x3e47c5c4, 0x3e2f10a0, 0x3e164085, 0x3dfab26c, 0x3dc8bd35, 0x3d96a90b, 0x3d48fb29, 0x3cc90abe,
    0xb33bbd2e, 0xbcc90aad, 0xbd48fb21, 0xbd96a906, 0xbdc8bd31, 0xbdfab277, 0xbe164082, 0xbe2f10a6,
    0xbe47c5c2, 0xbe605c10, 0xbe78cfcd, 0xbe888e92, 0xbe94a033, 0xbea09ae4, 0xbeac7cd2, 0xbeb8442a,
    0xbec3ef14, 0xbecf7bcb, 0xbedae880, 0xbee63376, 0xbef15ae6, 0xbefc5d26, 0xbf039c3d, 0xbf08f59c,
    0xbf0e39d9, 0xbf13682a, 0xbf187fc1, 0xbf1d7fd0, 0xbf226799, 0xbf273656, 0xbf2beb4b, 0xbf3085ba,
    0xbf3504f3, 0xbf396843, 0xbf3daef8, 0xbf41d870, 0xbf45e404, 0xbf49d113, 0xbf4d9f02, 0xbf514d3d,
    0xbf54db32, 0xbf584852, 0xbf5b941a, 0xbf5ebe06, 0xbf61c597, 0xbf64aa59, 0xbf676bd8, 0xbf6a09a7,
    0xbf6c835e, 0xbf6ed89e, 0xbf710909, 0xbf731447, 0xbf74fa0b, 0xbf76ba07, 0xbf7853f8, 0xbf79c79d,
    0xbf7b14bf, 0xbf7c3b28, 0xbf7d3aac, 0xbf7e1323, 0xbf7ec46d, 0xbf7f4e6e, 0xbf7fb10f, 0xbf7fec43,
    0xbf800000, 0xbf7fec43, 0xbf7fb10f, 0xbf7f4e6d, 0xbf7ec46d, 0xbf7e1324, 0xbf7d3aac, 0xbf7c3b28,
    0xbf7b14bf, 0xbf79c79d, 0xbf7853f8, 0xbf76ba08, 0xbf74fa0b, 0xbf731447, 0xbf710908, 0xbf6ed89e,
    0xbf6c835e, 0xbf6a09a6, 0xbf676bd8, 0xbf64aa59, 0xbf61c597, 0xbf5ebe04, 0xbf5b941b, 0xbf584853,
    0xbf54db30, 0xbf514d3e, 0xbf4d9f02, 0xbf49d112, 0xbf45e405, 0xbf41d871, 0xbf3daef9, 0xbf396841,
    0xbf3504f4, 0xbf3085bb, 0xbf2beb49, 0xbf273657, 0xbf22679a, 0xbf1d7fd1, 0xbf187fbe, 0xbf136828,
    0xbf0e39dd, 0xbf08f59d, 0xbf039c3e, 0xbefc5d27, 0xbef15ae8, 0xbee63371, 0xbedae87b, 0xbecf7bd1,
    0xbec3ef1a, 0xbeb8442c, 0xbeac7cd4, 0xbea09ae3, 0xbe94a02d, 0xbe888e8d, 0xbe78cfd9, 0xbe605c1c,
    0xbe47c5c6, 0xbe2f10a2, 0xbe16407f, 0xbdfab260, 0xbdc8bd1a, 0xbd96a91f, 0xbd48fb52, 0xbcc90acf,
    0x324cde2e, 0x3cc90adb, 0x3d48fb58, 0x3d96a922, 0x3dc8bd1d, 0x3dfab263, 0x3e164080, 0x3e2f10a4,
    0x3e47c5c8, 0x3e605c1e, 0x3e78cfdb, 0x3e888e8d, 0x3e94a02e, 0x3ea09ae3, 0x3eac7cd5, 0x3eb8442d,
    0x3ec3ef1b, 0x3ecf7bc3, 0x3edae87b, 0x3ee63372, 0x3ef15ae9, 0x3efc5d28, 0x3f039c3e, 0x3f08f59d,
    0x3f0e39d7, 0x3f136828, 0x3f187fbf, 0x3f1d7fd1, 0x3f22679a, 0x3f273657, 0x3f2beb4c, 0x3f3085b8,
    0x3f3504f2, 0x3f396841, 0x3f3daef9, 0x3f41d871, 0x3f45e405, 0x3f49d114, 0x3f4d9f00, 0x3f514d3c,
    0x3f54db31, 0x3f584853, 0x3f5b941b, 0x3f5ebe07, 0x3f61c599, 0x3f64aa58, 0x3f676bd7, 0x3f6a09a6,
    0x3f6c835f, 0x3f6ed89e, 0x3f710909, 0x3f731449, 0x3f74fa0a, 0x3f76ba07, 0x3f7853f8, 0x3f79c79d,
    0x3f7b14bf, 0x3f7c3b28, 0x3f7d3aad, 0x3f7e1323, 0x3f7ec46d, 0x3f7f4e6d, 0x3f7fb10f, 0x3f7fec43,
};

/* VU0's matrix slots as wrench follows them: what each 4-quadword slot was last given. */
struct blend_slot {
    int set;
    struct skin skin;
};

static struct skin joint_skin(int8_t joint)
{
    struct skin skin = {1, {0, 0, 0}, {255, 0, 0}};

    skin.joints[0] = joint;
    return skin;
}

static int load_skin(struct status *s, const struct blend_slot *cache, uint8_t address, struct skin *skin)
{
    if (address % 4 != 0)
        return fail(s, "Unaligned VU0 matrix load address 0x%x.", address);
    if (!cache[address / 4].set)
        return fail(s, "Matrix load from uninitialised VU0 address 0x%x.", address);
    *skin = cache[address / 4].skin;
    return 0;
}

static int store_skin(struct status *s, struct blend_slot *cache, uint8_t address, struct skin skin)
{
    if (address % 4 != 0)
        return fail(s, "Unaligned VU0 matrix store address 0x%x.", address);
    cache[address / 4].set = 1;
    cache[address / 4].skin = skin;
    return 0;
}

/* The skin of vertex INDEX (STORED, as the game stores it), by its blend kind, with VU0's slots followed in CACHE. */
static int read_skin(struct status *s, struct blend_slot *cache, const uint8_t *stored, size_t index, size_t two_way,
                     size_t three_way, struct skin *skin)
{
    int8_t bits_9_15 = (int8_t)((get_u16(stored) & 0xfe00) >> 9);
    struct skin src[3] = {0};   /* read only after its loads succeed; zeroed because gcc cannot see that */

    if (index < two_way) {
        /* loads bytes 2 and 3, weights 4 and 5, transfer store 6, blend store 7 */
        if (store_skin(s, cache, stored[6], joint_skin(bits_9_15)) < 0)
            return -1;
        if (stored[2] == stored[6] || stored[3] == stored[6])
            return fail(s, "Loading from and storing to the same VU0 address (%02x) in the same loop iteration.",
                        stored[6]);
        if (load_skin(s, cache, stored[2], &src[0]) < 0 || load_skin(s, cache, stored[3], &src[1]) < 0)
            return -1;
        if (src[0].count >= 2 || src[1].count >= 2)
            return fail(s, "Input to two-way matrix blend operation has already been blended.");
        skin->count = 2;
        skin->joints[0] = src[0].joints[0];
        skin->joints[1] = src[1].joints[0];
        skin->joints[2] = 0;
        skin->weights[0] = stored[4];
        skin->weights[1] = stored[5];
        skin->weights[2] = 0;
        return store_skin(s, cache, stored[7], *skin);
    }
    if (index < two_way + three_way) {
        /* loads bytes 2 and 3 and the high bits of the low halfword times two, weights 4 to 6, blend store 7 */
        if (load_skin(s, cache, stored[2], &src[0]) < 0 || load_skin(s, cache, stored[3], &src[1]) < 0 ||
            load_skin(s, cache, (uint8_t)(bits_9_15 * 2), &src[2]) < 0)
            return -1;
        if (src[0].count >= 2 || src[1].count >= 2 || src[2].count >= 2)
            return fail(s, "Input to three-way matrix blend operation has already been blended.");
        skin->count = 3;
        for (int k = 0; k < 3; k++) {
            skin->joints[k] = src[k].joints[0];
            skin->weights[k] = stored[4 + k];
        }
        return store_skin(s, cache, stored[7], *skin);
    }
    /* transfer store byte 3 (done first), load 2 */
    if (store_skin(s, cache, stored[3], joint_skin(bits_9_15)) < 0)
        return -1;
    if (stored[2] == stored[3])
        return fail(s, "Loading from and storing to the same VU0 address (%02x) in the same loop iteration.",
                    stored[3]);
    return load_skin(s, cache, stored[2], skin);
}

/* The vertices of packet P (SRC) on the end of OUT's: its vertices, unpacked with their skins (VU0's slots followed
 * in BLEND_CACHE from the packet's matrix transfers), then a copy of an earlier vertex for each duplicate index, each
 * with its own texture coordinates. VERTEX_CACHE holds the latest vertex in each VU1 slot, without its texture
 * coordinates (CACHED: whether the slot has one). */
static int unpack_vertices(struct status *s, const struct moby_packet *src, size_t p, float unit, int animated,
                           struct blend_slot *blend_cache, struct vertex *vertex_cache, uint8_t *cached,
                           struct mesh *out)
{
    const struct moby_vertex_table *table = &src->table;
    size_t base = out->vertex_count, packet_vertices = table->vertex_count;

    for (size_t i = 0; i < table->transfer_count; i++) {
        uint8_t joint = table->transfers[i * 2], address = table->transfers[i * 2 + 1];
        struct skin identity = {0, {0, 0, 0}, {0, 0, 0}};

        if (address % 4 != 0)
            return fail(s, "Unaligned pre-loop joint address 0x%x.", address);
        blend_cache[address / 4].set = 1;
        blend_cache[address / 4].skin = !animated && joint == 0 ? identity : joint_skin((int8_t)joint);
    }
    if (add_vertices(out, packet_vertices) < 0)
        return out_of_memory(s);
    for (size_t i = 0; i < packet_vertices; i++) {
        const uint8_t *stored = table->vertices + i * VERTEX_SIZE;
        struct vertex *v = &out->vertices[base + i];
        float cos_azimuth = bits_to_float(ANGLE_COSINES[stored[8]]);
        float sin_azimuth = bits_to_float(ANGLE_SINES[stored[8]]);
        float cos_elevation = bits_to_float(ANGLE_COSINES[stored[9]]);
        float sin_elevation = bits_to_float(ANGLE_SINES[stored[9]]);

        for (int k = 0; k < 3; k++)
            v->pos[k] = (int16_t)get_u16(stored + 0xa + k * 2) * unit;
        /* the normal is stored as angles; VU0 makes it from a sine and cosine table */
        v->normal[0] = sin_azimuth * cos_elevation;
        v->normal[1] = cos_azimuth * cos_elevation;
        v->normal[2] = sin_elevation;
        if (read_skin(s, blend_cache, stored, i, (size_t)table->two_way_count, (size_t)table->three_way_count,
                      &v->skin) < 0)
            return -1;
        v->vertex_index = get_u16(stored) & 0x1ff;
    }
    for (size_t i = 0; i < packet_vertices; i++) {
        struct vertex *v = &out->vertices[base + i];

        vertex_cache[v->vertex_index & 0x1ff] = *v;     /* before its texture coordinates */
        cached[v->vertex_index & 0x1ff] = 1;
        if (i >= src->st_count)
            return fail(s, "Packet %d has too few texture coordinates.", (int)p);
        v->tex_coord[0] = (int16_t)get_u16(src->sts + i * 4) * (1.f / 4096.f);
        v->tex_coord[1] = (int16_t)get_u16(src->sts + i * 4 + 2) * (1.f / 4096.f);
    }
    for (size_t d = 0; d < table->duplicate_count; d++) {
        uint16_t duplicate = table->duplicates[d];
        struct vertex *v;

        if (!cached[duplicate])
            return fail(s, "Packet %d has bad duplicate vertex.", (int)p);
        if (packet_vertices >= src->st_count)
            return fail(s, "Packet %d has too few texture coordinates.", (int)p);
        if (add_vertices(out, 1) < 0)
            return out_of_memory(s);
        v = &out->vertices[out->vertex_count - 1];
        *v = vertex_cache[duplicate];
        v->tex_coord[0] = (int16_t)get_u16(src->sts + packet_vertices * 4) * (1.f / 4096.f);
        v->tex_coord[1] = (int16_t)get_u16(src->sts + packet_vertices * 4 + 2) * (1.f / 4096.f);
        packet_vertices++;
    }
    return 0;
}

/* The strips of packet P's (SRC's) index stream as primitives of OUT, with ATTRIBUTES, whose vertices start at BASE.
 * An index of 0 takes the next extra index: a texture change (*TEXTURE becomes its GS register set's texture, which
 * the next packet goes on with), or 0 for the packet's end. */
static int recover_strips(struct status *s, const struct moby_packet *src, size_t p, size_t base, uint32_t attributes,
                          int32_t *texture, struct mesh *out)
{
    int64_t primitive = -1;     /* this packet's current one, in OUT */
    size_t register_set = 0;

    for (size_t j = 0; j < src->vif.indices.size; j++) {
        int8_t index = (int8_t)src->vif.indices.data[j];
        struct ints *indices;

        if (index == 0) {
            /* an index from the index header or a GS register set: a texture change, or the packet's end */
            int8_t secret_index;

            if (register_set >= src->vif.secret_count)
                return fail(s, "Packet %d has bad index buffer (no extra index).", (int)p);
            secret_index = src->vif.secret_indices[register_set];
            if (secret_index == 0) {
                /* VU1 has vertices in flight: the last three are not drawn */
                if (primitive < 0 || out->primitives[primitive].indices.count < 3)
                    return fail(s, "Packet %d has bad index buffer.", (int)p);
                out->primitives[primitive].indices.count -= 3;
                return 0;
            }
            index = (int8_t)(secret_index - 0x80);
            if (register_set >= src->vif.texture_count)
                return fail(s, "Packet %d has fewer GS register sets than texture changes.", (int)p);
            *texture = (int32_t)get_u32(src->vif.textures + register_set * TEXTURE_SIZE + 0x20);
            if (*texture < -1)
                return fail(s, "Packet %d has a bad texture index (%d).", (int)p, (int)*texture);
            register_set++;
        }
        /* both this and the next index with the restart bit: a new strip (one alone is a swap) */
        if (index <= 0) {
            if (j + 1 < src->vif.indices.size && (int8_t)src->vif.indices.data[j + 1] <= 0) {
                struct primitive *added = add_primitive(out);

                if (!added)
                    return out_of_memory(s);
                added->attributes = attributes;
                added->has_material = 1;
                added->material = *texture;
                added->has_mode = 1;
                added->mode = TRIANGLE_STRIP;
                primitive = (int64_t)out->primitive_count - 1;
            } else {
                /* a restart within the strip: a zero-area triangle */
                if (primitive < 0 || out->primitives[primitive].indices.count < 1)
                    return fail(s, "Packet %d has bad index buffer.", (int)p);
                indices = &out->primitives[primitive].indices;
                if (ints_push(indices, indices->items[indices->count - 1]) < 0)
                    return out_of_memory(s);
            }
        }
        if (primitive < 0)
            return fail(s, "Packet %d has bad index buffer.", (int)p);
        if (ints_push(&out->primitives[primitive].indices, (int32_t)base + ((index & 0x7f) - 1)) < 0)
            return out_of_memory(s);
    }
    return 0;
}

/* The packets of a LOD as one mesh (each primitive a strip with zero-area triangles where a strip restarts, its
 * material the texture index), equal vertices merged. */
static int recover_lod(struct status *s, const struct moby_packet *packets, size_t count, float scale, int animated,
                       struct mesh *out)
{
    struct blend_slot blend_cache[64];
    struct vertex *vertex_cache = calloc(512, sizeof *vertex_cache);
    uint8_t *cached = calloc(512, 1);
    float unit = scale / 1024.f;
    int32_t texture = 0;
    uint32_t attributes = ATTRIBUTE_POSITION | ATTRIBUTE_TEXCOORD_0 | ATTRIBUTE_NORMAL;
    int result = 0;

    if (animated)
        attributes |= ATTRIBUTE_JOINTS_0 | ATTRIBUTE_WEIGHTS_0;
    memset(blend_cache, 0, sizeof blend_cache);
    if (!vertex_cache || !cached) {
        free(vertex_cache);
        free(cached);
        return out_of_memory(s);
    }
    for (size_t p = 0; p < count && result == 0; p++) {
        size_t base = out->vertex_count;

        result = unpack_vertices(s, &packets[p], p, unit, animated, blend_cache, vertex_cache, cached, out);
        if (result == 0)
            result = recover_strips(s, &packets[p], p, base, attributes, &texture, out);
    }
    free(vertex_cache);
    free(cached);
    if (result < 0)
        return -1;
    return deduplicate_vertices(out, s);
}

/* ---------------------------------------------------------------------------------------------- glTF writing */

struct glb_writer {
    Text accessors;
    Text views;
    struct bytes bin;
    size_t accessor_count;
    int failed;
};

/* An accessor of COUNT elements of TYPE (DATA, SIZE bytes) in its own buffer view. Returns its index. */
static size_t add_accessor(struct glb_writer *w, const void *data, size_t size, int component_type, size_t count,
                           const char *type, const float *max, const float *min, int normalized)
{
    size_t index = w->accessor_count++;
    size_t offset;

    while (w->bin.count % 4 != 0 && !w->failed)
        w->failed |= bytes_push(&w->bin, 0) < 0;
    offset = w->bin.count;
    if (bytes_append(&w->bin, data, size) < 0)
        w->failed = 1;
    text_format(&w->views, "%s{\"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu}", index ? ", " : "", offset,
                size);
    text_format(&w->accessors, "%s{\"bufferView\": %zu, \"componentType\": %d, \"count\": %zu, \"type\": \"%s\"",
                index ? ", " : "", index, component_type, count, type);
    if (max && min)
        text_format(&w->accessors, ", \"max\": [%.9g, %.9g, %.9g], \"min\": [%.9g, %.9g, %.9g]", max[0], max[1],
                    max[2], min[0], min[1], min[2]);
    if (normalized)
        text_add(&w->accessors, ", \"normalized\": true");
    text_add(&w->accessors, "}");
    return index;
}

/* A primitive in the glb: its own vertices, in the order its indices first
 * use them. */
static int write_primitive(struct glb_writer *w, Text *json, const struct mesh *mesh, const struct primitive *p,
                           int32_t *mapping)
{
    const struct ints *source = &p->indices;
    int32_t *order = malloc((source->count ? source->count : 1) * sizeof *order);
    int32_t *indices = malloc((source->count ? source->count : 1) * sizeof *indices);
    uint8_t *data = malloc((source->count ? source->count : 1) * 12);
    size_t count = 0;
    int32_t max_index = 0;
    size_t accessor;

    if (!order || !indices || !data) {
        free(order);
        free(indices);
        free(data);
        return -1;
    }
    for (size_t i = 0; i < mesh->vertex_count; i++)
        mapping[i] = -1;
    for (size_t i = 0; i < source->count; i++) {
        int32_t index = source->items[i];

        if (mapping[index] == -1) {
            mapping[index] = (int32_t)count;
            order[count++] = index;
        }
        indices[i] = mapping[index];
    }

    text_add(json, "{\"attributes\": {");
    if (p->attributes & ATTRIBUTE_POSITION) {
        float max[3], min[3];

        for (size_t i = 0; i < count; i++) {
            float position[3];

            transform(RATCHET_TO_GLTF, mesh->vertices[order[i]].pos, position);
            for (int k = 0; k < 3; k++)
                set_u32(data + i * 12 + k * 4, float_to_bits(position[k]));
        }
        for (int k = 0; k < 3; k++) {
            max[k] = FLT_MIN;       /* the smallest positive float, not the lowest: as wrench starts it */
            min[k] = FLT_MAX;
            for (size_t i = 0; i < count; i++) {
                float value = get_f32(data + i * 12 + k * 4);

                max[k] = value < max[k] ? max[k] : value;
                min[k] = min[k] < value ? min[k] : value;
            }
        }
        accessor = add_accessor(w, data, count * 12, GLTF_FLOAT, count, "VEC3", count ? max : NULL, min, 0);
        text_format(json, "\"POSITION\": %zu", accessor);
    }
    if (p->attributes & ATTRIBUTE_TEXCOORD_0) {
        for (size_t i = 0; i < count; i++) {
            for (int k = 0; k < 2; k++)
                set_u32(data + i * 8 + k * 4, float_to_bits(mesh->vertices[order[i]].tex_coord[k]));
        }
        accessor = add_accessor(w, data, count * 8, GLTF_FLOAT, count, "VEC2", NULL, NULL, 0);
        text_format(json, ", \"TEXCOORD_0\": %zu", accessor);
    }
    if (p->attributes & ATTRIBUTE_NORMAL) {
        for (size_t i = 0; i < count; i++) {
            for (int k = 0; k < 3; k++)
                set_u32(data + i * 12 + k * 4, float_to_bits(mesh->vertices[order[i]].normal[k]));
        }
        accessor = add_accessor(w, data, count * 12, GLTF_FLOAT, count, "VEC3", NULL, NULL, 0);
        text_format(json, ", \"NORMAL\": %zu", accessor);
    }
    if (p->attributes & ATTRIBUTE_JOINTS_0) {
        for (size_t i = 0; i < count; i++) {
            for (int k = 0; k < 3; k++)
                data[i * 4 + k] = (uint8_t)mesh->vertices[order[i]].skin.joints[k];
            data[i * 4 + 3] = 0;
        }
        accessor = add_accessor(w, data, count * 4, GLTF_UNSIGNED_BYTE, count, "VEC4", NULL, NULL, 0);
        text_format(json, ", \"JOINTS_0\": %zu", accessor);
    }
    if (p->attributes & ATTRIBUTE_WEIGHTS_0) {
        for (size_t i = 0; i < count; i++) {
            for (int k = 0; k < 3; k++)
                data[i * 4 + k] = mesh->vertices[order[i]].skin.weights[k];
            data[i * 4 + 3] = 0;
        }
        accessor = add_accessor(w, data, count * 4, GLTF_UNSIGNED_BYTE, count, "VEC4", NULL, NULL, 1);
        text_format(json, ", \"WEIGHTS_0\": %zu", accessor);
    }
    text_add(json, "}");

    /* the indices in the smallest type that holds them (wrench's limits) */
    for (size_t i = 0; i < source->count; i++)
        max_index = indices[i] > max_index ? indices[i] : max_index;
    for (size_t i = 0; i < source->count; i++) {
        if (max_index < 255)
            data[i] = (uint8_t)indices[i];
        else if (max_index < 65535)
            set_u16(data + i * 2, (uint16_t)indices[i]);
        else
            set_u32(data + i * 4, (uint32_t)indices[i]);
    }
    if (max_index < 255)
        accessor = add_accessor(w, data, source->count, GLTF_UNSIGNED_BYTE, source->count, "SCALAR", NULL, NULL, 0);
    else if (max_index < 65535)
        accessor = add_accessor(w, data, source->count * 2, GLTF_UNSIGNED_SHORT, source->count, "SCALAR", NULL, NULL,
                                0);
    else
        accessor = add_accessor(w, data, source->count * 4, GLTF_UNSIGNED_INT, source->count, "SCALAR", NULL, NULL, 0);
    text_format(json, ", \"indices\": %zu", accessor);
    if (p->has_material)
        text_format(json, ", \"material\": %d", (int)p->material);
    if (p->has_mode)
        text_format(json, ", \"mode\": %d", (int)p->mode);
    text_add(json, "}");
    free(order);
    free(indices);
    free(data);
    return 0;
}

/* A LOD's node and mesh in the glb: nodes NAME, meshes NAME_mesh. */
struct lod_mesh {
    char name[32];
    struct mesh mesh;
};

/* Texture indices become materials:
 * 0 .. TEXTURE_COUNT - 1 their own; none, chrome and glass (-1, -2, -3) new ones after those as they come; anything
 * else (outside the materials made so far) "error". Writes the materials' JSON. */
static void assign_materials(struct lod_mesh *lods, size_t lod_count, int texture_count, Text *json)
{
    static const char *const special_names[3] = {"none", "chrome", "glass"};
    static const char *const special_colours[3] = {"1, 0, 1, 1", "0.5, 0.5, 0.5, 1", "1, 1, 1, 1"};
    int32_t special[3] = {-1, -1, -1};
    int32_t error = -1;
    int32_t material_count = texture_count;

    for (int i = 0; i < texture_count; i++)
        text_format(json, "%s{\"name\": \"material_%d\", \"pbrMetallicRoughness\": {\"baseColorTexture\": "
                    "{\"index\": %d}}, \"alphaMode\": \"MASK\", \"doubleSided\": true}", i ? ", " : "", i, i);
    for (size_t l = 0; l < lod_count; l++) {
        for (size_t p = 0; p < lods[l].mesh.primitive_count; p++) {
            struct primitive *primitive = &lods[l].mesh.primitives[p];
            int32_t index = -primitive->material - 1;

            if (!primitive->has_material || primitive->material < -3 || primitive->material > -1)
                continue;
            if (special[index] < 0) {
                special[index] = material_count++;
                text_format(json, "%s{\"name\": \"%s\", \"pbrMetallicRoughness\": {\"baseColorFactor\": [%s]}}",
                            special[index] ? ", " : "", special_names[index], special_colours[index]);
            }
            primitive->material = special[index];
        }
    }
    for (size_t l = 0; l < lod_count; l++) {
        for (size_t p = 0; p < lods[l].mesh.primitive_count; p++) {
            struct primitive *primitive = &lods[l].mesh.primitives[p];

            if (!primitive->has_material || (primitive->material >= 0 && primitive->material < material_count))
                continue;
            if (error < 0) {
                error = material_count++;
                text_format(json, "%s{\"name\": \"error\", \"pbrMetallicRoughness\": {\"baseColorFactor\": "
                            "[1, 0, 1, 1]}}", error ? ", " : "");
            }
            primitive->material = error;
        }
    }
}

/* The packets of LOD (*COUNT): 0 the high LOD, 1 the low LOD, then each bangle's high and low LOD. */
static const struct moby_packet *lod_packets(const struct moby_class *moby, size_t lod, size_t *count)
{
    const struct moby_bangle *bangle;

    if (lod < 2) {
        *count = lod ? moby->low_count : moby->high_count;
        return lod ? moby->low_lod : moby->high_lod;
    }
    bangle = &moby->bangles[(lod - 2) / 2];
    *count = lod % 2 ? bangle->low_count : bangle->high_count;
    return lod % 2 ? bangle->low_lod : bangle->high_lod;
}

/* The largest texture index the class's packets use, plus one (at most 16). */
static int used_texture_count(const struct moby_class *moby)
{
    int count = 0;

    for (size_t lod = 0; lod < 2 + 2 * moby->bangle_count; lod++) {
        size_t packet_count;
        const struct moby_packet *packets = lod_packets(moby, lod, &packet_count);

        for (size_t p = 0; p < packet_count; p++) {
            for (size_t t = 0; t < packets[p].vif.texture_count; t++) {
                int32_t texture = (int32_t)get_u32(packets[p].vif.textures + t * TEXTURE_SIZE + 0x20);

                if (texture >= count)
                    count = texture + 1;
            }
        }
    }
    return count < MAX_MATERIALS ? count : MAX_MATERIALS;
}

uint8_t *moby_mesh_export(const struct moby_class *moby, int texture_count, size_t *size, char *error,
                          size_t error_size)
{
    struct status status = {error, error_size, 0};
    struct status *s = &status;
    size_t lod_count = 2 + 2 * moby->bangle_count, max_vertices = 1;
    struct lod_mesh *lods = calloc(lod_count, sizeof *lods);
    struct glb_writer writer;
    Text json = {NULL, 0, 0, false}, materials = {NULL, 0, 0, false};
    int32_t *mapping = NULL;
    uint8_t *glb = NULL;
    float scale = bits_to_float(moby->scale);
    int animated = moby->joint_list_count > 0;

    *size = 0;
    memset(&writer, 0, sizeof writer);
    if (!lods) {
        out_of_memory(s);
        return NULL;
    }
    if (texture_count < 0)
        texture_count = used_texture_count(moby);
    if (texture_count > MAX_MATERIALS)
        texture_count = MAX_MATERIALS;
    for (size_t l = 0; l < lod_count && !s->failed; l++) {
        size_t count;
        const struct moby_packet *packets = lod_packets(moby, l, &count);

        if (l < 2)
            snprintf(lods[l].name, sizeof lods[l].name, "%s", l ? "moby_low_lod" : "moby");
        else
            snprintf(lods[l].name, sizeof lods[l].name, "bangle_%u%s", (unsigned)((l - 2) / 2),
                     l % 2 ? "_low_lod" : "");
        if (recover_lod(s, packets, count, scale, animated, &lods[l].mesh) < 0)
            break;
        if (lods[l].mesh.vertex_count > max_vertices)
            max_vertices = lods[l].mesh.vertex_count;
    }
    if (s->failed)
        goto done;

    assign_materials(lods, lod_count, texture_count, &materials);
    mapping = malloc(max_vertices * sizeof *mapping);
    if (!mapping) {
        out_of_memory(s);
        goto done;
    }
    text_add(&json, "{\"asset\": {\"generator\": \"rac-moby (wrench's moby class unpack)\", \"version\": \"2.0\"}, "
             "\"scene\": 0, \"scenes\": [{\"nodes\": [");
    for (size_t l = 0; l < lod_count; l++)
        text_format(&json, "%s%zu", l ? ", " : "", l);
    text_add(&json, "]}], \"nodes\": [");
    for (size_t l = 0; l < lod_count; l++)
        text_format(&json, "%s{\"mesh\": %zu, \"name\": \"%s\"}", l ? ", " : "", l, lods[l].name);
    text_add(&json, "], \"meshes\": [");
    for (size_t l = 0; l < lod_count; l++) {
        text_format(&json, "%s{\"name\": \"%s_mesh\", \"primitives\": [", l ? ", " : "", lods[l].name);
        for (size_t p = 0; p < lods[l].mesh.primitive_count; p++) {
            if (p)
                text_add(&json, ", ");
            if (write_primitive(&writer, &json, &lods[l].mesh, &lods[l].mesh.primitives[p], mapping) < 0) {
                out_of_memory(s);
                goto done;
            }
        }
        text_add(&json, "]}");
    }
    text_add(&json, "]");
    if (materials.length) {
        text_format(&json, ", \"materials\": [%s]", materials.text);
        if (texture_count > 0) {
            text_add(&json, ", \"textures\": [");
            for (int i = 0; i < texture_count; i++)
                text_format(&json, "%s{\"source\": %d}", i ? ", " : "", i);
            text_add(&json, "], \"images\": [");
            for (int i = 0; i < texture_count; i++)
                text_format(&json, "%s{\"uri\": \"%d.png\"}", i ? ", " : "", i);
            text_add(&json, "]");
        }
    }
    if (writer.accessor_count)
        text_format(&json, ", \"accessors\": [%s], \"bufferViews\": [%s]", writer.accessors.text, writer.views.text);
    while (writer.bin.count % 4 != 0 && !writer.failed)
        writer.failed |= bytes_push(&writer.bin, 0) < 0;
    text_format(&json, ", \"buffers\": [{\"byteLength\": %zu}]}", writer.bin.count);
    while (json.length % 4 != 0)
        text_add(&json, " ");
    if (json.failed || materials.failed || writer.accessors.failed || writer.views.failed || writer.failed) {
        out_of_memory(s);
        goto done;
    }

    *size = 12 + 8 + json.length + 8 + writer.bin.count;
    glb = malloc(*size);
    if (!glb) {
        *size = 0;
        out_of_memory(s);
        goto done;
    }
    memcpy(glb, "glTF", 4);
    set_u32(glb + 4, 2);
    set_u32(glb + 8, (uint32_t)*size);
    set_u32(glb + 12, (uint32_t)json.length);
    memcpy(glb + 16, "JSON", 4);
    memcpy(glb + 20, json.text, json.length);
    set_u32(glb + 20 + json.length, (uint32_t)writer.bin.count);
    memcpy(glb + 24 + json.length, "BIN\0", 4);
    if (writer.bin.count)
        memcpy(glb + 28 + json.length, writer.bin.items, writer.bin.count);

done:
    for (size_t l = 0; l < lod_count; l++)
        free_mesh(&lods[l].mesh);
    free(lods);
    free(mapping);
    text_free(&json);
    text_free(&materials);
    text_free(&writer.accessors);
    text_free(&writer.views);
    free(writer.bin.items);
    return glb;
}
