/*
 * Kalebo III's sign (sign.h), as tools/gen_sign.py makes it, byte for byte.
 *
 * The sign is moby class 1891, which only Kalebo III holds: a half ring of nine 3D letters, placed twice back to
 * back to make the ring the level turns. The PAL release spells GALACTRON and has no other copy of the word on the
 * disc, so the class is rebuilt from the player's PAL class (read from Kalebo III's data on the disc, its mesh
 * exported as wrench's unpack exports it, moby_mesh.h), nothing from any other disc:
 *
 *   - Each letter is a flat plate, extruded radially: a front face (texture 1), a back face and side walls
 *     (texture 0), flat-shaded, each quad mapped onto a whole texture. GALACTRON and GADGETRON share six places:
 *     G A _ _ _ T R O N. On the NTSC disc those six are the PAL letters turned about the ring's axis (measured:
 *     T R O N by -1.3623 degrees, G A by +0.7377), and its second G is the first one turned by -51.5149 degrees.
 *     All three moves are applied to the PAL letters as they are.
 *   - L A C become D G E. The D and the E are built from the L: its stem and its foot's height (0.184 of the
 *     letter), the top bar's height that the T shares (0.831 to 1), and the L's front and back planes. The E is the
 *     L's stem with three bars, the D the stem with two bars and a bowl, cut where the strokes meet as the NTSC
 *     letters are (no T-junctions). Placed by turning the L about the axis (E -35.3284 degrees, D +0.7380, where the
 *     NTSC letters' stems are).
 *   - The shapes the PAL disc does not have are measured from the NTSC letters in letter units (height 1), relative
 *     to the stem's right edge: the E's top and bottom bars run 0.767 and its middle bar 0.432, at heights 0.435 to
 *     0.601; the D's bowl is ten 18-degree segments between two half ellipses, the outer one centred 0.437 right of
 *     the stem with a half width of 0.532 and the full height, the inner one 0.405 and 0.367 between the bars.
 *
 * The new mesh goes into both of the class's LODs through a glb, as moby_mesh_replace takes it (keeping the rest of
 * the class and retail's packet conventions). The class's bounding spheres (header +0x30 and its one sequence's) are
 * the smallest sphere around the vertices, which is how retail's agree to 0.03%. The class is then read back the way
 * OpenRAC's moby reader reads a class's high LOD, and checked.
 *
 * The bytes are the reference's (CPython 3.10 on Windows) on every compiler:
 *   - every double operation is the reference's, in its order (build without floating-point contraction: gcc
 *     -ffp-contract=off, MSVC's default); values go to the glb as f32 by C's conversion, round to nearest even, as
 *     struct.pack("<f") does;
 *   - sines and cosines are constants, the reference's (MSVC's CRT) exact values, since libms differ in the last
 *     bit: the fixed turns, the D's bowl, and the L's plate angle (an atan2 of the disc's normals);
 *   - atan2 otherwise only sorts the strips into letters, where the nearest vertex is 0.37 degrees from a cut;
 *   - math.hypot and math.dist are CPython's vector_norm, round(x, n) is the double nearest the correctly rounded
 *     decimal, and sum() adds left to right;
 *   - the smallest sphere is Welzl's algorithm over the vertices in the order the reference visits them: the distinct
 *     position tuples sorted, then random.Random(0).shuffle (MT19937 seeded by init_by_array([0]), _randbelow by
 *     getrandbits rejection).
 */
#include "sign.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytes.h"
#include "json.h"
#include "levels.h"
#include "moby.h"
#include "moby_mesh.h"

#define SIGN_LEVEL 16               /* Kalebo III, the only level with the sign */
#define SIGN_CLASS 1891

/* The PAL sign's letters, by the angle (degrees, atan2(z, x) in wrench's glb axes, y up) of their strips' mean
   vertex; the word reads towards decreasing angles. Triangle counts check that the input is the PAL sign. */
#define LETTERS 9
#define CUTS (LETTERS - 1)
enum { LETTER_N, LETTER_O, LETTER_R, LETTER_T, LETTER_C, LETTER_A2, LETTER_L, LETTER_A1, LETTER_G1 };
static const double PAL_CUTS[CUTS] = {-58.0, -39.5, -23.8, -8.4, 9.4, 27.4, 40.7, 58.5};
static const char *const PAL_LETTERS[LETTERS] = {"N", "O", "R", "T", "C", "A2", "L", "A1", "G1"};
static const size_t PAL_TRIANGLES[LETTERS] = {64, 128, 136, 36, 140, 104, 28, 104, 156};

/* CPython's radToDeg, 180 / pi: math.degrees(x) is x * RAD_TO_DEG. */
#define RAD_TO_DEG 0x1.ca5dc1a63c1f8p+5

/* A turn about the ring's axis by DEGREES: the cosine and sine of math.radians(DEGREES) (DEGREES * (pi / 180), one
   product), as the reference has them. */
struct turn {
    double degrees;
    double cos, sin;
};

/* The turns that put the PAL letters where the NTSC letters are. */
static const struct turn TURN_TRON = {-1.3623, 0x1.ffdaf3d27f9d3p-1, -0x1.8584fa5c2b373p-6};
static const struct turn TURN_GA = {0.7377, 0x1.fff522d364dfep-1, 0x1.a5e2cf2dbd11ap-7};
static const struct turn TURN_SECOND_G = {-51.5149, 0x1.3e9f8f5352decp-1, -0x1.90c73b55de705p-1};
static const struct turn TURN_E = {-35.3284, 0x1.a1b73af3cf13dp-1, -0x1.2811f8041bda1p-1};
static const struct turn TURN_D = {0.7380, 0x1.fff5209033f01p-1, 0x1.a60eba71c3cb3p-7};

/* The L's plate faces 32.34375062027619 degrees: its ten front-face normals all point one way, the mean of their
   angles. The angle is an atan2, which libms round differently in the last bit, so it is the reference's; the one
   measured here must agree to PLATE_TOLERANCE degrees. PLATE_IN turns the world into the plate's frame, PLATE_OUT
   back. */
#define PLATE_ANGLE 0x1.02c0005340871p+5
#define PLATE_TOLERANCE 1e-9
static const struct turn PLATE_IN = {-PLATE_ANGLE, 0x1.b090a54f8fccap-1, -0x1.11eb3590458d4p-1};
static const struct turn PLATE_OUT = {PLATE_ANGLE, 0x1.b090a54f8fccap-1, 0x1.11eb3590458d4p-1};

/* Letter units (height 1), from the stem's right edge; see the top. */
#define E_BAR 0.767
#define E_MIDDLE_BAR 0.432
#define E_MIDDLE_BOTTOM 0.435
#define E_MIDDLE_TOP 0.601
#define D_OUTER_CENTRE 0.437
#define D_OUTER_HALF_WIDTH 0.532
#define D_INNER_CENTRE 0.405
#define D_INNER_HALF_WIDTH 0.367
#define D_SEGMENTS 10

/* sin and cos of pi * k / D_SEGMENTS, k = 0 .. D_SEGMENTS: the bowl's segment ends, as the reference has them. */
static const double D_SIN[D_SEGMENTS + 1] = {
    0x0.0p+0, 0x1.3c6ef372fe94fp-2, 0x1.2cf2304755a5ep-1, 0x1.9e3779b97f4a8p-1, 0x1.e6f0e134454ffp-1,
    0x1.0000000000000p+0, 0x1.e6f0e13445500p-1, 0x1.9e3779b97f4a8p-1, 0x1.2cf2304755a5fp-1, 0x1.3c6ef372fe951p-2,
    0x1.1a62633145c07p-53,
};
static const double D_COS[D_SEGMENTS + 1] = {
    0x1.0000000000000p+0, 0x1.e6f0e134454ffp-1, 0x1.9e3779b97f4a8p-1, 0x1.2cf2304755a5ep-1, 0x1.3c6ef372fe950p-2,
    0x1.1a62633145c07p-54, -0x1.3c6ef372fe94ep-2, -0x1.2cf2304755a5dp-1, -0x1.9e3779b97f4a7p-1,
    -0x1.e6f0e134454ffp-1, -0x1.0000000000000p+0,
};

/* Per packet; the retail sign has one at most. A packet entry's +6 is its texture blocks' quadwords, 4 each. */
#define MAX_TEXTURE_BLOCKS 2
#define SPHERE_MARGIN 4.0           /* added to the smallest sphere's radius, in vertex units */
#define VERTEX_UNIT 1024.0          /* a vertex's s16 position over the class's scale */
#define WELZL_SLACK 1e-6

#define MATERIAL_NAME 24

/* ------------------------------------------------------------------------------------------------- errors */

static bool out_of_memory(InstallError *error)
{
    return install_fail(error, "internal", "out of memory making the Gadgetron sign");
}

static bool not_the_sign(InstallError *error, const char *why)
{
    return install_fail(error, "internal", "Kalebo III's class %d is not the PAL GALACTRON sign: %s", SIGN_CLASS, why);
}

/* ---------------------------------------------------------------------------------------- CPython's numbers */

/* Dekker's exact product: A * B - fl(A * B), with no fused multiply-add. */
static void split(double x, double *hi, double *lo)
{
    double t = 134217729.0 * x;     /* 2^27 + 1 */

    *hi = t - (t - x);
    *lo = x - *hi;
}

static double product_error(double a, double b, double product)
{
    double ah, al, bh, bl;

    split(a, &ah, &al);
    split(b, &bh, &bl);
    return ((ah * bh - product) + ah * bl + al * bh) + al * bl;
}

/*
 * Python's round(X, digits) for SCALE = 10^digits: the double nearest the decimal that X rounds to (half to even, on
 * X's exact value), X's sign kept. X * SCALE is exactly T + ERR; T - floor(T) is exact, so the comparisons with a
 * half are exact too (a rounded sum has its exact value's sign). K / SCALE is the double nearest K * 10^-digits, as
 * CPython's strtod of the rounded decimal is.
 */
static double python_round(double x, double scale)
{
    double t = x * scale, err = product_error(x, scale, t), k = floor(t), frac = t - k, half;

    if (frac + err < 0.0) {
        k -= 1.0;
        frac = t - k;
    }
    half = (frac - 0.5) + err;
    if (half > 0.0 || (half == 0.0 && fmod(k, 2.0) != 0.0))
        k += 1.0;
    return copysign(k / scale, x);
}

/* CPython 3.10's vector_norm (Modules/mathmodule.c): the length of N non-negative, finite values, MAX the largest,
   with the scaling, the exact squares and the final correction step it has. */
static double vector_norm(size_t n, const double *values, double max)
{
    const double t27 = 134217729.0;
    double x, scale, oldcsum, csum = 1.0, frac1 = 0.0, frac2 = 0.0, frac3 = 0.0, t, hi, lo, h;
    int max_e;

    if (max == 0.0 || n <= 1)
        return max;
    frexp(max, &max_e);
    if (max_e < -1023) {
        for (size_t i = 0; i < n; i++) {
            x = values[i] / max;
            x = x * x;
            oldcsum = csum;
            csum += x;
            frac1 += (oldcsum - csum) + x;
        }
        return max * sqrt(csum - 1.0 + frac1);
    }
    scale = ldexp(1.0, -max_e);
    for (size_t i = 0; i < n; i++) {
        x = values[i] * scale;
        t = x * t27;
        hi = t - (t - x);
        lo = x - hi;

        x = hi * hi;
        oldcsum = csum;
        csum += x;
        frac1 += (oldcsum - csum) + x;

        x = 2.0 * hi * lo;
        oldcsum = csum;
        csum += x;
        frac2 += (oldcsum - csum) + x;

        frac3 += lo * lo;
    }
    h = sqrt(csum - 1.0 + (frac1 + frac2 + frac3));

    x = h;
    t = x * t27;
    hi = t - (t - x);
    lo = x - hi;

    x = -hi * hi;
    oldcsum = csum;
    csum += x;
    frac1 += (oldcsum - csum) + x;

    x = -2.0 * hi * lo;
    oldcsum = csum;
    csum += x;
    frac2 += (oldcsum - csum) + x;

    x = -lo * lo;
    oldcsum = csum;
    csum += x;
    frac3 += (oldcsum - csum) + x;

    x = csum - 1.0 + (frac1 + frac2 + frac3);
    return (h + x / (2.0 * h)) / scale;
}

/* math.hypot(a, b) */
static double python_hypot(double a, double b)
{
    double values[2] = {fabs(a), fabs(b)}, max = 0.0;

    for (int i = 0; i < 2; i++) {
        if (values[i] > max)
            max = values[i];
    }
    return vector_norm(2, values, max);
}

/* math.dist(p, q) in three dimensions */
static double python_dist(const double p[3], const double q[3])
{
    double values[3], max = 0.0;

    for (int i = 0; i < 3; i++) {
        values[i] = fabs(p[i] - q[i]);
        if (values[i] > max)
            max = values[i];
    }
    return vector_norm(3, values, max);
}

/* ------------------------------------------------------------------------------------------------- strips */

/* A triangle strip of the mesh, in wrench's glb axes: positions y up, and normals z up (a normal (x, y, z) points
   along the position direction (x, z, y)). A turn about the ring's axis by t degrees adds t to atan2(z, x) of
   positions and to atan2(y, x) of normals. */
struct strip {
    char material[MATERIAL_NAME];
    size_t count;               /* vertices */
    double (*pos)[3];
    double (*nrm)[3];
    double (*uv)[2];
    uint32_t *indices;
    size_t index_count;
};

struct strips {
    struct strip *items;
    size_t count;
    size_t capacity;
};

static void strip_free(struct strip *strip)
{
    free(strip->pos);
    free(strip->nrm);
    free(strip->uv);
    free(strip->indices);
    memset(strip, 0, sizeof *strip);
}

static void strips_free(struct strips *list)
{
    for (size_t i = 0; i < list->count; i++)
        strip_free(&list->items[i]);
    free(list->items);
    memset(list, 0, sizeof *list);
}

/* A new strip of MATERIAL at the list's end, with room for COUNT vertices and INDEX_COUNT indices; NULL when out of
   memory or the name is too long. Valid until the next strip is added. */
static struct strip *add_strip(struct strips *list, const char *material, size_t count, size_t index_count)
{
    struct strip *strip;
    size_t length = strlen(material);

    if (length >= MATERIAL_NAME)
        return NULL;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? 2 * list->capacity : 64;
        struct strip *items = realloc(list->items, capacity * sizeof *items);

        if (!items)
            return NULL;
        list->items = items;
        list->capacity = capacity;
    }
    strip = &list->items[list->count];
    memset(strip, 0, sizeof *strip);
    memcpy(strip->material, material, length + 1);
    strip->count = count;
    strip->index_count = index_count;
    strip->pos = malloc((count ? count : 1) * sizeof *strip->pos);
    strip->nrm = malloc((count ? count : 1) * sizeof *strip->nrm);
    strip->uv = malloc((count ? count : 1) * sizeof *strip->uv);
    strip->indices = malloc((index_count ? index_count : 1) * sizeof *strip->indices);
    if (!strip->pos || !strip->nrm || !strip->uv || !strip->indices) {
        strip_free(strip);
        return NULL;
    }
    list->count++;
    return strip;
}

/* Triangles the strip draws: index triples of three different vertices. */
static size_t strip_triangles(const struct strip *strip)
{
    size_t triangles = 0;

    for (size_t k = 0; k + 2 < strip->index_count; k++) {
        uint32_t a = strip->indices[k], b = strip->indices[k + 1], c = strip->indices[k + 2];

        if (a != b && b != c && a != c)
            triangles++;
    }
    return triangles;
}

static void turn_pos(const double p[3], const struct turn *turn, double out[3])
{
    double x = p[0] * turn->cos - p[2] * turn->sin, y = p[1], z = p[0] * turn->sin + p[2] * turn->cos;

    out[0] = x;
    out[1] = y;
    out[2] = z;
}

static void turn_nrm(const double n[3], const struct turn *turn, double out[3])
{
    double x = n[0] * turn->cos - n[1] * turn->sin, y = n[0] * turn->sin + n[1] * turn->cos, z = n[2];

    out[0] = x;
    out[1] = y;
    out[2] = z;
}

/* STRIP turned by TURN, at the end of OUT. */
static bool add_turned(struct strips *out, const struct strip *strip, const struct turn *turn, InstallError *error)
{
    struct strip *copy = add_strip(out, strip->material, strip->count, strip->index_count);

    if (!copy)
        return out_of_memory(error);
    for (size_t i = 0; i < strip->count; i++) {
        turn_pos(strip->pos[i], turn, copy->pos[i]);
        turn_nrm(strip->nrm[i], turn, copy->nrm[i]);
        copy->uv[i][0] = strip->uv[i][0];
        copy->uv[i][1] = strip->uv[i][1];
    }
    memcpy(copy->indices, strip->indices, strip->index_count * sizeof *strip->indices);
    return true;
}

/* ---------------------------------------------------------------------------------------------- glb reading */

#define GLTF_UNSIGNED_BYTE 5121
#define GLTF_UNSIGNED_SHORT 5123
#define GLTF_UNSIGNED_INT 5125
#define GLTF_FLOAT 5126
#define TRIANGLE_STRIP 5

static bool bad_glb(InstallError *error, const char *why)
{
    return install_fail(error, "internal", "the PAL sign's mesh: %s", why);
}

static const Json *json_item(const Json *array, int64_t index)
{
    if (!array || array->type != JSON_ARRAY || index < 0 || (uint64_t)index >= array->count)
        return NULL;
    return &array->items[index];
}

/* An accessor of the glb (DATA, SIZE bytes; BIN its binary chunk's offset): its element count and where element k's
   components start and step. */
struct accessor {
    size_t count;
    int components;
    int component_type;
    size_t at;
    size_t stride;
};

static bool read_accessor(const Json *root, int64_t index, size_t size, size_t bin, int components,
                          struct accessor *a, InstallError *error)
{
    static const struct {
        const char *name;
        int components;
    } types[] = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}};
    const Json *accessor = json_item(json_get(root, "accessors"), index), *view;
    const char *type;
    int64_t count, component_size, view_offset, offset, stride;
    size_t element;

    if (!accessor || !(view = json_item(json_get(root, "bufferViews"), json_get_integer(accessor, "bufferView", -1))))
        return bad_glb(error, "an accessor or its buffer view is missing");
    type = json_get_string(accessor, "type");
    a->components = 0;
    for (size_t i = 0; type && i < sizeof types / sizeof types[0]; i++) {
        if (strcmp(type, types[i].name) == 0)
            a->components = types[i].components;
    }
    a->component_type = (int)json_get_integer(accessor, "componentType", 0);
    component_size = a->component_type == GLTF_UNSIGNED_BYTE ? 1 : a->component_type == GLTF_UNSIGNED_SHORT ? 2 :
                     a->component_type == GLTF_UNSIGNED_INT || a->component_type == GLTF_FLOAT ? 4 : 0;
    count = json_get_integer(accessor, "count", -1);
    if (a->components != components || component_size == 0 || count < 0)
        return bad_glb(error, "an accessor is not the type expected");
    element = (size_t)(component_size * a->components);
    stride = json_get_integer(view, "byteStride", (int64_t)element);
    view_offset = json_get_integer(view, "byteOffset", 0);
    offset = json_get_integer(accessor, "byteOffset", 0);
    if (stride < (int64_t)element || view_offset < 0 || offset < 0 || (uint64_t)view_offset > size ||
        (uint64_t)offset > size)
        return bad_glb(error, "an accessor's layout is not one wrench writes");
    a->count = (size_t)count;
    a->stride = (size_t)stride;
    a->at = bin + (size_t)view_offset + (size_t)offset;
    /* the last element must end inside the file */
    if (a->count && (a->at > size || size - a->at < element || (size - a->at - element) / a->stride < a->count - 1))
        return bad_glb(error, "an accessor runs past the file");
    return true;
}

static double component(const uint8_t *data, const struct accessor *a, size_t k, int c)
{
    const uint8_t *at = data + a->at + k * a->stride;

    switch (a->component_type) {
    case GLTF_FLOAT: {
        uint32_t bits = read_u32_le(at + 4 * c);
        float value;

        memcpy(&value, &bits, sizeof value);
        return value;
    }
    case GLTF_UNSIGNED_INT:
        return read_u32_le(at + 4 * c);
    case GLTF_UNSIGNED_SHORT:
        return read_u16_le(at + 2 * c);
    default:
        return at[c];
    }
}

/* The node 'moby' of a wrench glb as strips: one per primitive (each one triangle strip). */
static bool read_mesh(const uint8_t *data, size_t size, struct strips *out, InstallError *error)
{
    Json *root;
    const Json *nodes, *materials, *primitives = NULL, *mesh = NULL;
    size_t json_size, bin;
    bool ok = false;

    if (size < 28 || memcmp(data, "glTF", 4) != 0 || memcmp(data + 16, "JSON", 4) != 0)
        return bad_glb(error, "not a glb");
    json_size = read_u32_le(data + 12);
    if (json_size > size - 28)
        return bad_glb(error, "the JSON chunk runs past the file");
    bin = 20 + json_size + 8;
    root = json_parse((const char *)data + 20, json_size, NULL, 0);
    if (!root)
        return bad_glb(error, "its JSON does not parse");
    nodes = json_get(root, "nodes");
    for (size_t i = 0; nodes && nodes->type == JSON_ARRAY && i < nodes->count && !mesh; i++) {
        const char *name = json_get_string(&nodes->items[i], "name");

        if (name && strcmp(name, "moby") == 0 && json_get(&nodes->items[i], "mesh"))
            mesh = json_item(json_get(root, "meshes"), json_get_integer(&nodes->items[i], "mesh", -1));
    }
    materials = json_get(root, "materials");
    if (mesh)
        primitives = json_get(mesh, "primitives");
    if (!primitives || primitives->type != JSON_ARRAY) {
        bad_glb(error, "no node 'moby' with a mesh");
        goto done;
    }
    for (size_t p = 0; p < primitives->count; p++) {
        const Json *primitive = &primitives->items[p], *attributes = json_get(primitive, "attributes");
        const char *material = json_get_string(json_item(materials, json_get_integer(primitive, "material", -1)),
                                               "name");
        struct accessor pos, nrm, uv, indices;
        struct strip *strip;

        if (json_get_integer(primitive, "mode", 4) != TRIANGLE_STRIP) {
            bad_glb(error, "expected one triangle strip per primitive");
            goto done;
        }
        if (!material) {
            bad_glb(error, "a primitive has no named material");
            goto done;
        }
        if (!read_accessor(root, json_get_integer(attributes, "POSITION", -1), size, bin, 3, &pos, error) ||
            !read_accessor(root, json_get_integer(attributes, "NORMAL", -1), size, bin, 3, &nrm, error) ||
            !read_accessor(root, json_get_integer(attributes, "TEXCOORD_0", -1), size, bin, 2, &uv, error) ||
            !read_accessor(root, json_get_integer(primitive, "indices", -1), size, bin, 1, &indices, error))
            goto done;
        if (pos.component_type != GLTF_FLOAT || nrm.component_type != GLTF_FLOAT || uv.component_type != GLTF_FLOAT ||
            indices.component_type == GLTF_FLOAT || nrm.count != pos.count || uv.count != pos.count) {
            bad_glb(error, "a primitive's attributes are not the ones wrench writes");
            goto done;
        }
        strip = add_strip(out, material, pos.count, indices.count);
        if (!strip) {
            out_of_memory(error);
            goto done;
        }
        for (size_t k = 0; k < pos.count; k++) {
            for (int c = 0; c < 3; c++) {
                strip->pos[k][c] = component(data, &pos, k, c);
                strip->nrm[k][c] = component(data, &nrm, k, c);
            }
            strip->uv[k][0] = component(data, &uv, k, 0);
            strip->uv[k][1] = component(data, &uv, k, 1);
        }
        for (size_t k = 0; k < indices.count; k++) {
            strip->indices[k] = (uint32_t)component(data, &indices, k, 0);
            if (strip->indices[k] >= pos.count) {
                bad_glb(error, "an index is past its primitive's vertices");
                goto done;
            }
        }
    }
    ok = true;

done:
    json_free(root);
    return ok;
}

/* ------------------------------------------------------------------------------------------------- letters */

/* Python's bisect.bisect: how many cuts are at or below ANGLE. */
static int letter_at(double angle)
{
    int letter = 0;

    while (letter < CUTS && PAL_CUTS[letter] <= angle)
        letter++;
    return letter;
}

/* Each strip's letter, by its mean vertex's angle; every vertex must lie in the same letter, and each letter must
   have the PAL sign's triangles. */
static bool letters(const struct strips *glb, int *letter_of, InstallError *error)
{
    size_t triangles[LETTERS] = {0};

    for (size_t s = 0; s < glb->count; s++) {
        const struct strip *strip = &glb->items[s];
        double sum = 0.0;

        if (strip->index_count == 0)
            return not_the_sign(error, "a strip has no vertices");
        for (size_t k = 0; k < strip->index_count; k++) {
            const double *p = strip->pos[strip->indices[k]];

            sum += atan2(p[2], p[0]) * RAD_TO_DEG;
        }
        letter_of[s] = letter_at(sum / (double)strip->index_count);
        for (size_t k = 0; k < strip->index_count; k++) {
            const double *p = strip->pos[strip->indices[k]];

            if (letter_at(atan2(p[2], p[0]) * RAD_TO_DEG) != letter_of[s])
                return not_the_sign(error, "a strip crosses from one letter into another");
        }
        triangles[letter_of[s]] += strip_triangles(strip);
    }
    for (int l = 0; l < LETTERS; l++) {
        if (triangles[l] != PAL_TRIANGLES[l]) {
            char why[64];

            snprintf(why, sizeof why, "letter %s has %zu triangles, not %zu", PAL_LETTERS[l], triangles[l],
                     PAL_TRIANGLES[l]);
            return not_the_sign(error, why);
        }
    }
    return true;
}

/* The strips of LETTER, turned by TURN, at the end of OUT. */
static bool add_letter(struct strips *out, const struct strips *glb, const int *letter_of, int letter,
                       const struct turn *turn, InstallError *error)
{
    for (size_t s = 0; s < glb->count; s++) {
        if (letter_of[s] == letter && !add_turned(out, &glb->items[s], turn, error))
            return false;
    }
    return true;
}

/* -------------------------------------------------------------------------------------------- the L's plate */

/*
 * A letter's frame: u along the plate in reading order, v up (0 to 1), w out of the ring. The plate faces the angle
 * of its front faces' normals (PLATE_ANGLE for the L).
 */
static void plate_local(const double p[3], double out[3])
{
    double t[3];

    turn_pos(p, &PLATE_IN, t);
    out[0] = -t[2];
    out[1] = t[1];
    out[2] = t[0];
}

static void plate_world(const double local[3], double out[3])
{
    const double p[3] = {local[2], local[1], -local[0]};

    turn_pos(p, &PLATE_OUT, out);
}

static void plate_normal(const double n[3], double out[3])
{
    const double p[3] = {n[2], n[1], -n[0]};
    double t[3];

    turn_pos(p, &PLATE_OUT, t);
    out[0] = t[0];
    out[1] = t[2];
    out[2] = t[1];
}

/* The angle the L's front faces (texture 1, normals level within 0.01) face, measured, against PLATE_ANGLE. */
static bool check_plate(const struct strips *glb, const int *letter_of, InstallError *error)
{
    double sum = 0.0;
    size_t count = 0;

    for (size_t s = 0; s < glb->count; s++) {
        const struct strip *strip = &glb->items[s];

        if (letter_of[s] != LETTER_L || strcmp(strip->material, "material_1") != 0)
            continue;
        for (size_t k = 0; k < strip->count; k++) {
            if (fabs(strip->nrm[k][2]) < 0.01) {
                sum += atan2(strip->nrm[k][1], strip->nrm[k][0]) * RAD_TO_DEG;
                count++;
            }
        }
    }
    if (count == 0 || !(fabs(sum / (double)count - PLATE_ANGLE) <= PLATE_TOLERANCE))
        return not_the_sign(error, "the L does not face where the PAL sign's does");
    return true;
}

/* w = a + b u through the (u, w) POINTS, least squares: LINE = {a, b}. */
static void fit_line(const double (*points)[2], size_t n, double line[2])
{
    double su = 0.0, sw = 0.0, suu = 0.0, suw = 0.0, b;

    for (size_t i = 0; i < n; i++)
        su += points[i][0];
    for (size_t i = 0; i < n; i++)
        sw += points[i][1];
    for (size_t i = 0; i < n; i++)
        suu += points[i][0] * points[i][0];
    for (size_t i = 0; i < n; i++)
        suw += points[i][0] * points[i][1];
    b = ((double)n * suw - su * sw) / ((double)n * suu - su * su);
    line[0] = (sw - b * su) / (double)n;
    line[1] = b;
}

/* VALUE added to the set VALUES (COUNT so far) unless an equal one is there; then VALUES sorted. */
static void add_unique(double *values, size_t *count, double value)
{
    for (size_t i = 0; i < *count; i++) {
        if (values[i] == value)
            return;
    }
    values[(*count)++] = value;
}

static void sort_values(double *values, size_t count)
{
    for (size_t i = 1; i < count; i++) {
        double value = values[i];
        size_t j = i;

        for (; j > 0 && values[j - 1] > value; j--)
            values[j] = values[j - 1];
        values[j] = value;
    }
}

/* The L's measures, in its plate's frame: the front and back planes (w along u), its height (bottom, top), the
   stem's left and right edges, the foot's height, and the T's top bar's lower edge. */
struct measures {
    double front[2], back[2];
    double bottom, top;
    double s0, s1;
    double foot, bar;
};

static bool measure_l(const struct strips *glb, const int *letter_of, struct measures *m, InstallError *error)
{
    double (*pts)[3] = NULL, (*fit)[2] = NULL, *stem = NULL, t_heights[4], max_w, min_w, mid_w;
    size_t count = 0, fit_count, stem_count = 0, t_count = 0;
    bool has_foot = false, ok = false;

    for (size_t s = 0; s < glb->count; s++)
        count += letter_of[s] == LETTER_L ? glb->items[s].count : 0;
    if (count == 0)
        return not_the_sign(error, "the L has no vertices");
    pts = malloc(count * sizeof *pts);
    fit = malloc(count * sizeof *fit);
    stem = malloc(count * sizeof *stem);
    if (!pts || !fit || !stem) {
        out_of_memory(error);
        goto done;
    }
    count = 0;
    for (size_t s = 0; s < glb->count; s++) {
        for (size_t k = 0; letter_of[s] == LETTER_L && k < glb->items[s].count; k++)
            plate_local(glb->items[s].pos[k], pts[count++]);
    }

    max_w = min_w = pts[0][2];
    m->top = m->bottom = pts[0][1];
    for (size_t i = 1; i < count; i++) {
        if (pts[i][2] > max_w)
            max_w = pts[i][2];
        if (pts[i][2] < min_w)
            min_w = pts[i][2];
        if (pts[i][1] > m->top)
            m->top = pts[i][1];
        if (pts[i][1] < m->bottom)
            m->bottom = pts[i][1];
    }
    mid_w = (max_w + min_w) / 2;
    fit_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (pts[i][2] > mid_w) {
            fit[fit_count][0] = pts[i][0];
            fit[fit_count++][1] = pts[i][2];
        }
    }
    if (fit_count == 0)
        goto unexpected;
    fit_line((const double (*)[2])fit, fit_count, m->front);
    fit_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (pts[i][2] < mid_w) {
            fit[fit_count][0] = pts[i][0];
            fit[fit_count++][1] = pts[i][2];
        }
    }
    if (fit_count == 0)
        goto unexpected;
    fit_line((const double (*)[2])fit, fit_count, m->back);

    for (size_t i = 0; i < count; i++) {
        if (pts[i][1] > m->top - 0.01 && pts[i][2] > mid_w)
            add_unique(stem, &stem_count, python_round(pts[i][0], 1e3));
    }
    sort_values(stem, stem_count);
    if (stem_count == 0)
        goto unexpected;
    m->s0 = stem[0];
    m->s1 = stem[stem_count - 1];
    for (size_t i = 0; i < count; i++) {
        if (pts[i][0] > m->s1 + 0.05 && (!has_foot || pts[i][1] > m->foot)) {
            m->foot = pts[i][1];
            has_foot = true;
        }
    }
    for (size_t s = 0; s < glb->count; s++) {
        for (size_t k = 0; letter_of[s] == LETTER_T && k < glb->items[s].count && t_count < 4; k++)
            add_unique(t_heights, &t_count, python_round(glb->items[s].pos[k][1], 1e3));
    }
    sort_values(t_heights, t_count);
    if (t_count != 3 || !(fabs(m->top - 1) < 0.01 && fabs(m->bottom) < 0.01 && stem_count == 2) || !has_foot)
        goto unexpected;
    m->bar = t_heights[1];          /* the top bar's lower edge, 0.831 */
    ok = true;
    goto done;

unexpected:
    not_the_sign(error, "unexpected L or T");
done:
    free(pts);
    free(fit);
    free(stem);
    return ok;
}

/* ---------------------------------------------------------------------------------------------- extrusion */

/* A quad in the plate's (u, v), its corners counter-clockwise. */
struct quad {
    double c[4][2];
};

static struct quad rect(double u0, double u1, double v0, double v1)
{
    struct quad q = {{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}};

    return q;
}

static struct quad quad4(const double a[2], const double b[2], const double c[2], const double d[2])
{
    struct quad q = {{{a[0], a[1]}, {b[0], b[1]}, {c[0], c[1]}, {d[0], d[1]}}};

    return q;
}

/* An edge between two corners, each rounded to 5 decimals: corners that round alike are the same corner. */
struct edge {
    double a[2], b[2];
};

static void edge_key(const double from[2], const double to[2], struct edge *edge)
{
    edge->a[0] = python_round(from[0], 1e5);
    edge->a[1] = python_round(from[1], 1e5);
    edge->b[0] = python_round(to[0], 1e5);
    edge->b[1] = python_round(to[1], 1e5);
}

static bool has_edge(const struct edge *edges, size_t count, const struct edge *edge)
{
    for (size_t i = 0; i < count; i++) {
        if (edges[i].a[0] == edge->a[0] && edges[i].a[1] == edge->a[1] && edges[i].b[0] == edge->b[0] &&
            edges[i].b[1] == edge->b[1])
            return true;
    }
    return false;
}

/* A face: a strip of its own through CORNERS (plate frame, in order around the face; strip order 0 1 3 2), mapped
   onto a whole texture, with the plate-frame NORMAL; turned by TURN into OUT. */
static bool add_face(struct strips *out, const char *material, const double corners[4][3], const double normal[3],
                     const struct turn *turn, InstallError *error)
{
    static const double uv[4][2] = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
    static const int order[4] = {0, 1, 3, 2};
    struct strip *strip = add_strip(out, material, 4, 4);
    double n[3], world[3];

    if (!strip)
        return out_of_memory(error);
    plate_normal(normal, n);
    for (int k = 0; k < 4; k++) {
        plate_world(corners[order[k]], world);
        turn_pos(world, turn, strip->pos[k]);
        turn_nrm(n, turn, strip->nrm[k]);
        strip->uv[k][0] = uv[order[k]][0];
        strip->uv[k][1] = uv[order[k]][1];
        strip->indices[k] = (uint32_t)k;
    }
    return true;
}

/*
 * Flat-shaded plate letters in the L's plate frame, cut from QUADS: a front (texture 1), a back and walls on every
 * edge no other quad shares (texture 0), as the sign's letters are made, each face turned by TURN into OUT. FRONT and
 * BACK are the planes' w along u.
 */
static bool extrude(const struct quad *quads, size_t count, const double front[2], const double back[2],
                    const struct turn *turn, struct strips *out, InstallError *error)
{
    static const double front_normal[3] = {0.0, 0.0, 1.0}, back_normal[3] = {0.0, 0.0, -1.0};
    struct edge *edges = malloc((count ? 4 * count : 1) * sizeof *edges);
    size_t edge_count = 0;

    if (!edges)
        return out_of_memory(error);
    for (size_t q = 0; q < count; q++) {
        double area = 0.0;

        for (int i = 0; i < 4; i++)
            area += quads[q].c[i][0] * quads[q].c[(i + 1) % 4][1] - quads[q].c[(i + 1) % 4][0] * quads[q].c[i][1];
        if (area <= 0) {
            free(edges);
            return install_fail(error, "internal", "the Gadgetron sign: a quad is not counter-clockwise");
        }
        for (int i = 0; i < 4; i++) {
            struct edge edge;

            edge_key(quads[q].c[i], quads[q].c[(i + 1) % 4], &edge);
            if (!has_edge(edges, edge_count, &edge))
                edges[edge_count++] = edge;
        }
    }
    for (size_t q = 0; q < count; q++) {
        const struct quad *quad = &quads[q];
        double f[4][3], b[4][3], reversed[4][3];

        for (int i = 0; i < 4; i++) {
            f[i][0] = b[i][0] = quad->c[i][0];
            f[i][1] = b[i][1] = quad->c[i][1];
            f[i][2] = front[0] + front[1] * quad->c[i][0];
            b[i][2] = back[0] + back[1] * quad->c[i][0];
        }
        for (int i = 0; i < 4; i++)
            memcpy(reversed[i], b[3 - i], sizeof reversed[i]);
        if (!add_face(out, "material_1", (const double (*)[3])f, front_normal, turn, error) ||
            !add_face(out, "material_0", (const double (*)[3])reversed, back_normal, turn, error))
            goto fail;
        for (int i = 0; i < 4; i++) {
            const double *a = quad->c[i], *c = quad->c[(i + 1) % 4];
            double du, dv, length, wall[4][3], normal[3];
            struct edge inside;

            edge_key(c, a, &inside);
            if (has_edge(edges, edge_count, &inside))
                continue;           /* shared with the next quad: inside the letter */
            du = c[0] - a[0];
            dv = c[1] - a[1];
            length = python_hypot(du, dv);
            memcpy(wall[0], f[i], sizeof wall[0]);
            memcpy(wall[1], b[i], sizeof wall[1]);
            memcpy(wall[2], b[(i + 1) % 4], sizeof wall[2]);
            memcpy(wall[3], f[(i + 1) % 4], sizeof wall[3]);
            normal[0] = dv / length;
            normal[1] = -du / length;
            normal[2] = 0.0;
            if (!add_face(out, "material_0", (const double (*)[3])wall, normal, turn, error))
                goto fail;
        }
    }
    free(edges);
    return true;

fail:
    free(edges);
    return false;
}

/* The D's bowl ends: the outer half ellipse's point K, and the inner one's. */
static void bowl_point(double centre, double half_width, double mid, double half_height, int k, double out[2])
{
    out[0] = centre + half_width * D_SIN[k];
    out[1] = mid - half_height * D_COS[k];
}

/* Strips grouped by texture, keeping their order within each: the packer starts a GS texture block wherever the
   texture changes, and each block takes VU1 memory beside the index stream (0x37 quadwords for both); interleaved,
   the first build had up to nine in a packet, where retail has at most five and the PAL sign one. */
static void sort_by_material(struct strips *list)
{
    for (size_t i = 1; i < list->count; i++) {
        struct strip strip = list->items[i];
        size_t j = i;

        for (; j > 0 && strcmp(list->items[j - 1].material, strip.material) > 0; j--)
            list->items[j] = list->items[j - 1];
        list->items[j] = strip;
    }
}

/* The GADGETRON word from the PAL sign's strips GLB. */
static bool build_word(const struct strips *glb, struct strips *out, InstallError *error)
{
    int *letter_of = malloc((glb->count ? glb->count : 1) * sizeof *letter_of);
    struct measures m;
    struct quad e[8], d[5 + D_SEGMENTS];
    double oc, ic, o_mid, o_half, i_mid, i_half, outer[D_SEGMENTS + 1][2], inner[D_SEGMENTS + 1][2];
    bool ok;

    if (!letter_of)
        return out_of_memory(error);
    memset(&m, 0, sizeof m);
    ok = letters(glb, letter_of, error) && add_letter(out, glb, letter_of, LETTER_T, &TURN_TRON, error) &&
         add_letter(out, glb, letter_of, LETTER_R, &TURN_TRON, error) &&
         add_letter(out, glb, letter_of, LETTER_O, &TURN_TRON, error) &&
         add_letter(out, glb, letter_of, LETTER_N, &TURN_TRON, error) &&
         add_letter(out, glb, letter_of, LETTER_G1, &TURN_GA, error) &&
         add_letter(out, glb, letter_of, LETTER_A1, &TURN_GA, error) &&
         add_letter(out, glb, letter_of, LETTER_G1, &TURN_SECOND_G, error) && check_plate(glb, letter_of, error) &&
         measure_l(glb, letter_of, &m, error);
    free(letter_of);
    if (!ok)
        return false;

    /* E: the stem cut where its three bars meet it. */
    e[0] = rect(m.s0, m.s1, m.bottom, m.foot);
    e[1] = rect(m.s0, m.s1, m.foot, E_MIDDLE_BOTTOM);
    e[2] = rect(m.s0, m.s1, E_MIDDLE_BOTTOM, E_MIDDLE_TOP);
    e[3] = rect(m.s0, m.s1, E_MIDDLE_TOP, m.bar);
    e[4] = rect(m.s0, m.s1, m.bar, m.top);
    e[5] = rect(m.s1, m.s1 + E_BAR, m.bottom, m.foot);
    e[6] = rect(m.s1, m.s1 + E_MIDDLE_BAR, E_MIDDLE_BOTTOM, E_MIDDLE_TOP);
    e[7] = rect(m.s1, m.s1 + E_BAR, m.bar, m.top);
    if (!extrude(e, 8, m.front, m.back, &TURN_E, out, error))
        return false;

    /* D: the stem, the bars to where the bowl starts, and the bowl. */
    oc = m.s1 + D_OUTER_CENTRE;
    ic = m.s1 + D_INNER_CENTRE;
    o_mid = (m.bottom + m.top) / 2;
    o_half = (m.top - m.bottom) / 2;
    i_mid = (m.foot + m.bar) / 2;
    i_half = (m.bar - m.foot) / 2;
    for (int k = 0; k <= D_SEGMENTS; k++) {
        bowl_point(oc, D_OUTER_HALF_WIDTH, o_mid, o_half, k, outer[k]);
        bowl_point(ic, D_INNER_HALF_WIDTH, i_mid, i_half, k, inner[k]);
    }
    {
        const double stem_bottom[2] = {m.s1, m.bottom}, stem_foot[2] = {m.s1, m.foot};
        const double stem_bar[2] = {m.s1, m.bar}, stem_top[2] = {m.s1, m.top};

        d[0] = rect(m.s0, m.s1, m.bottom, m.foot);
        d[1] = rect(m.s0, m.s1, m.foot, m.bar);
        d[2] = rect(m.s0, m.s1, m.bar, m.top);
        d[3] = quad4(stem_bottom, outer[0], inner[0], stem_foot);
        d[4] = quad4(stem_bar, inner[D_SEGMENTS], outer[D_SEGMENTS], stem_top);
    }
    for (int k = 0; k < D_SEGMENTS; k++)
        d[5 + k] = quad4(outer[k], outer[k + 1], inner[k + 1], inner[k]);
    if (!extrude(d, 5 + D_SEGMENTS, m.front, m.back, &TURN_D, out, error))
        return false;

    sort_by_material(out);
    return true;
}

/* ---------------------------------------------------------------------------------------------- glb writing */

struct bytes {
    uint8_t *data;
    size_t size;
    size_t capacity;
    bool failed;
};

static void bytes_append(struct bytes *b, const void *data, size_t size)
{
    if (b->failed)
        return;
    if (b->size + size > b->capacity) {
        size_t capacity = b->capacity ? 2 * b->capacity : 4096;
        uint8_t *grown;

        while (capacity < b->size + size)
            capacity *= 2;
        grown = realloc(b->data, capacity);
        if (!grown) {
            b->failed = true;
            return;
        }
        b->data = grown;
        b->capacity = capacity;
    }
    if (data)
        memcpy(b->data + b->size, data, size);
    else
        memset(b->data + b->size, 0, size);
    b->size += size;
}

static void bytes_f32(struct bytes *b, double value)
{
    float f = (float)value;
    uint32_t bits;
    uint8_t at[4];

    memcpy(&bits, &f, sizeof bits);
    write_u32_le(at, bits);
    bytes_append(b, at, 4);
}

/* The glb's buffer views and accessors so far, as JSON lists. */
struct glb_writer {
    struct bytes bin;
    Text views;
    Text accessors;
    size_t count;
};

/* A buffer view and accessor of COUNT elements of TYPE whose values start at START of the binary chunk. */
static size_t end_accessor(struct glb_writer *w, size_t start, int component_type, size_t count, const char *type)
{
    text_format(&w->views, "%s{\"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu}", w->count ? ", " : "", start,
                w->bin.size - start);
    text_format(&w->accessors, "%s{\"bufferView\": %zu, \"componentType\": %d, \"count\": %zu, \"type\": \"%s\"}",
                w->count ? ", " : "", w->count, component_type, count, type);
    return w->count++;
}

static size_t start_accessor(struct glb_writer *w)
{
    bytes_append(&w->bin, NULL, (4 - w->bin.size % 4) % 4);
    return w->bin.size;
}

/* STRIPS as one mesh 'moby' (node 'moby'), a triangle strip per primitive, the way wrench's unpack writes a moby
   class, which moby_mesh_replace reads; materials named as the strips' are, sorted. */
static uint8_t *write_glb(const struct strips *strips, size_t *size, InstallError *error)
{
    struct glb_writer w = {{0}, {0}, {0}, 0};
    Text primitives = {0}, json = {0};
    const char *materials[16];
    size_t material_count = 0;
    uint8_t *glb = NULL, head[20];

    for (size_t s = 0; s < strips->count; s++) {
        size_t m = 0;

        while (m < material_count && strcmp(materials[m], strips->items[s].material) != 0)
            m++;
        if (m == material_count) {
            if (material_count == sizeof materials / sizeof materials[0]) {
                install_fail(error, "internal", "the Gadgetron sign has too many materials");
                goto done;
            }
            materials[material_count++] = strips->items[s].material;
        }
    }
    for (size_t i = 1; i < material_count; i++) {
        const char *name = materials[i];
        size_t j = i;

        for (; j > 0 && strcmp(materials[j - 1], name) > 0; j--)
            materials[j] = materials[j - 1];
        materials[j] = name;
    }

    for (size_t s = 0; s < strips->count; s++) {
        const struct strip *strip = &strips->items[s];
        size_t position, normal, uv, indices, material = 0, start;

        while (strcmp(materials[material], strip->material) != 0)
            material++;
        start = start_accessor(&w);
        for (size_t k = 0; k < strip->count; k++) {
            for (int c = 0; c < 3; c++)
                bytes_f32(&w.bin, strip->pos[k][c]);
        }
        position = end_accessor(&w, start, GLTF_FLOAT, strip->count, "VEC3");
        start = start_accessor(&w);
        for (size_t k = 0; k < strip->count; k++) {
            for (int c = 0; c < 3; c++)
                bytes_f32(&w.bin, strip->nrm[k][c]);
        }
        normal = end_accessor(&w, start, GLTF_FLOAT, strip->count, "VEC3");
        start = start_accessor(&w);
        for (size_t k = 0; k < strip->count; k++) {
            bytes_f32(&w.bin, strip->uv[k][0]);
            bytes_f32(&w.bin, strip->uv[k][1]);
        }
        uv = end_accessor(&w, start, GLTF_FLOAT, strip->count, "VEC2");
        start = start_accessor(&w);
        for (size_t k = 0; k < strip->index_count; k++) {
            uint8_t at[2];

            if (strip->indices[k] > 0xFFFF) {
                install_fail(error, "internal", "the Gadgetron sign: a strip has too many vertices");
                goto done;
            }
            write_u16_le(at, (uint16_t)strip->indices[k]);
            bytes_append(&w.bin, at, 2);
        }
        indices = end_accessor(&w, start, GLTF_UNSIGNED_SHORT, strip->index_count, "SCALAR");
        text_format(&primitives, "%s{\"attributes\": {\"POSITION\": %zu, \"NORMAL\": %zu, \"TEXCOORD_0\": %zu}, "
                    "\"indices\": %zu, \"material\": %zu, \"mode\": %d}", s ? ", " : "", position, normal, uv,
                    indices, material, TRIANGLE_STRIP);
    }
    bytes_append(&w.bin, NULL, (4 - w.bin.size % 4) % 4);

    text_add(&json, "{\"asset\": {\"version\": \"2.0\", \"generator\": \"rac-editions installer core (sign.c)\"}, "
                    "\"scene\": 0, \"scenes\": [{\"nodes\": [0]}], \"nodes\": [{\"name\": \"moby\", \"mesh\": 0}], "
                    "\"meshes\": [{\"name\": \"moby\", \"primitives\": [");
    text_append(&json, primitives.text ? primitives.text : "", primitives.length);
    text_add(&json, "]}], \"materials\": [");
    for (size_t m = 0; m < material_count; m++) {
        text_add(&json, m ? ", {\"name\": " : "{\"name\": ");
        text_add_json_string(&json, materials[m]);
        text_add(&json, "}");
    }
    text_format(&json, "], \"buffers\": [{\"byteLength\": %zu}], \"bufferViews\": [", w.bin.size);
    text_append(&json, w.views.text ? w.views.text : "", w.views.length);
    text_add(&json, "], \"accessors\": [");
    text_append(&json, w.accessors.text ? w.accessors.text : "", w.accessors.length);
    text_add(&json, "]}");
    while (json.length % 4)
        text_add(&json, " ");
    if (json.failed || primitives.failed || w.views.failed || w.accessors.failed || w.bin.failed) {
        out_of_memory(error);
        goto done;
    }

    *size = 28 + json.length + w.bin.size;
    glb = malloc(*size);
    if (!glb) {
        out_of_memory(error);
        goto done;
    }
    memcpy(head, "glTF", 4);
    write_u32_le(head + 4, 2);
    write_u32_le(head + 8, (uint32_t)*size);
    write_u32_le(head + 12, (uint32_t)json.length);
    memcpy(head + 16, "JSON", 4);
    memcpy(glb, head, 20);
    memcpy(glb + 20, json.text, json.length);
    write_u32_le(glb + 20 + json.length, (uint32_t)w.bin.size);
    memcpy(glb + 24 + json.length, "BIN\0", 4);
    if (w.bin.size)
        memcpy(glb + 28 + json.length, w.bin.data, w.bin.size);

done:
    free(w.bin.data);
    text_free(&w.views);
    text_free(&w.accessors);
    text_free(&primitives);
    text_free(&json);
    return glb;
}

/* ------------------------------------------------------------------------------------------- reading back */

/*
 * The class's high LOD read back as OpenRAC's moby reader reads it (common/openrac_editor/moby_class.py): each
 * packet's vertices (positions in vertex units, the duplicates from the vertex cache) and its drawn triangles, with
 * its checks. A packet entry is {u32 VIF list, u16 its quadwords, u16 texture blocks' quadwords, u32 vertex table,
 * u8 vertex data's quadwords, u8 position and colour quadwords, u8 transfer vertices}; its VIF list UNPACKs the
 * texture coordinates, the index stream and the GS texture blocks to VU1 (addresses relative to TOPS).
 */
#define UNPACK_ST 0x75
#define UNPACK_INDICES 0x6e
#define UNPACK_GS_BLOCKS 0x6c
#define ST_ADDRESS 0x80c2
#define INDEX_ADDRESS 0x812d
#define NO_STORE 0xf4               /* a VU0 address meaning "do not keep this matrix" */
#define VERTEX_IDS 512              /* the vertex cache's 9-bit ids */

struct unpack {
    unsigned command;
    unsigned address;
    size_t count;
    size_t at;                  /* the payload, in the class */
};

/* What carries over between the packets of the LOD: the vertex cache, the VU0 matrix slots' joints and the
   current texture. */
struct list_state {
    bool cached[VERTEX_IDS];
    double cache[VERTEX_IDS][3];
    bool stored[256];
    int joint[256];
    bool has_texture;
    int32_t texture;
};

struct readback {
    double (*positions)[3];
    size_t count;
    size_t capacity;
    size_t triangles;
};

static bool span_ok(size_t size, size_t at, size_t length)
{
    return at <= size && length <= size - at;
}

static bool read_fail(InstallError *error, size_t packet, const char *why)
{
    return install_fail(error, "internal", "the Gadgetron sign does not read back: packet %zu: %s", packet, why);
}

/* The UNPACKs of the VIF list at AT (SIZE bytes); NOPs are skipped. At most 3. */
static bool vif_unpacks(const uint8_t *data, size_t size, size_t at, size_t length, struct unpack *out,
                        size_t *count, size_t packet, InstallError *error)
{
    static const struct {
        unsigned command;
        size_t element;
    } sizes[] = {{UNPACK_ST, 4}, {UNPACK_INDICES, 4}, {UNPACK_GS_BLOCKS, 16}};
    size_t pos = 0;

    *count = 0;
    if (!span_ok(size, at, length))
        return read_fail(error, packet, "its VIF list is outside the class");
    while (pos < length) {
        uint32_t word;
        unsigned command, n;
        size_t element = 0;

        if (length - pos < 4)
            return read_fail(error, packet, "its VIF list ends inside a command");
        word = read_u32_le(data + at + pos);
        command = word >> 24;
        n = (word >> 16) & 0xff ? (word >> 16) & 0xff : 256;
        pos += 4;
        if (command == 0)
            continue;
        for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            if (sizes[i].command == command)
                element = sizes[i].element;
        }
        if (element == 0)
            return read_fail(error, packet, "unsupported VIF command");
        if (*count == 3 || length - pos < element * n)
            return read_fail(error, packet, "unsupported VIF program");
        out[*count].command = command;
        out[*count].address = word & 0xffff;
        out[*count].count = n;
        out[(*count)++].at = at + pos;
        pos += element * n;
    }
    return true;
}

static bool add_position(struct readback *back, const double p[3])
{
    if (back->count == back->capacity) {
        size_t capacity = back->capacity ? 2 * back->capacity : 1024;
        double (*grown)[3] = realloc(back->positions, capacity * sizeof *grown);

        if (!grown)
            return false;
        back->positions = grown;
        back->capacity = capacity;
    }
    memcpy(back->positions[back->count++], p, sizeof *back->positions);
    return true;
}

/* A VU0 slot: the matrix of joint JOINT stored at ADDRESS. */
static bool slot_store(struct list_state *state, unsigned address, int joint)
{
    if (address % 4)
        return false;
    state->stored[address] = true;
    state->joint[address] = joint;
    return true;
}

/* Packet P of the high LOD, at ENTRY: its vertices into BACK and its drawn triangles counted. Its vertices are each
   on one joint (the sign's class has no joints, so joint 0): a blended vertex is refused. */
static bool read_packet(const uint8_t *data, size_t size, size_t entry, size_t p, int joint_count,
                        struct list_state *state, struct readback *back, InstallError *error)
{
    struct unpack u[3];
    size_t n, table, in_file, epilogue, records, id_count = 0, total_vertices, st_count, index_count, push_count = 0;
    uint32_t transfers, two, three, single, duplicates, total, vertex_offset, multipliers;
    unsigned transfer = data[entry + 15], data_size = data[entry + 12];
    const uint8_t *index_data;
    int8_t secrets[1 + 64];
    int32_t textures[64];
    size_t secret_count = 1, texture_count = 0, secret = 0, block = 0, at;
    uint16_t ids[VERTEX_IDS + 16];
    struct push {
        int index;
        bool kick;
        bool has_texture;
        int32_t texture;
    } *pushes = NULL;
    bool ok = false;

    if (!vif_unpacks(data, size, read_u32_le(data + entry), (size_t)read_u16_le(data + entry + 4) * 16, u, &n, p,
                     error))
        return false;
    if (n < 2 || u[0].command != UNPACK_ST || u[1].command != UNPACK_INDICES ||
        (n == 3 && u[2].command != UNPACK_GS_BLOCKS))
        return read_fail(error, p, "unsupported VIF program");
    st_count = u[0].count;
    index_count = u[1].count;
    index_data = data + u[1].at;
    if (u[0].address != ST_ADDRESS || u[1].address != INDEX_ADDRESS || index_data[3])
        return read_fail(error, p, "unexpected VIF addresses");
    secrets[0] = (int8_t)index_data[2];
    if (n == 3) {
        if (u[2].count % 4 || u[2].address != INDEX_ADDRESS + index_count || index_data[1] != index_count ||
            u[2].count / 4 > 64)
            return read_fail(error, p, "misplaced GS texture blocks");
        for (size_t b = 0; b < u[2].count / 4; b++) {
            textures[texture_count++] = (int32_t)read_u32_le(data + u[2].at + b * 64 + 32);   /* TEX0, patched at load */
            secrets[secret_count++] = (int8_t)data[u[2].at + b * 16 + 12];  /* extra indices, successive quadwords */
        }
    }
    if (data[entry + 13] != (transfer * 6 + 15) / 16 || data[entry + 14] != (transfer + 3) / 4)
        return read_fail(error, p, "the entry's sizes disagree");

    table = read_u32_le(data + entry + 8);
    if (!span_ok(size, table, 0x20))
        return read_fail(error, p, "its vertex table is outside the class");
    transfers = read_u32_le(data + table);
    two = read_u32_le(data + table + 0x04);
    three = read_u32_le(data + table + 0x08);
    single = read_u32_le(data + table + 0x0C);
    duplicates = read_u32_le(data + table + 0x10);
    total = read_u32_le(data + table + 0x14);
    vertex_offset = read_u32_le(data + table + 0x18);
    multipliers = read_u32_le(data + table + 0x1C);
    if (two > 512 || three > 512 || single > 512 || duplicates > 512 || transfers > 256)
        return read_fail(error, p, "implausible vertex counts");
    in_file = (size_t)two + three + single;
    if (total != in_file + duplicates || total != transfer)
        return read_fail(error, p, "the vertex counts disagree");
    if (two || three)
        return read_fail(error, p, "blended vertices, which the sign has none of");
    at = table + 0x20 + (size_t)transfers * 2;
    at += at % 4 ? 2 : 0;
    at += at % 8 ? 4 : 0;
    if (!span_ok(size, table + 0x20, (size_t)transfers * 2) || !span_ok(size, at, (size_t)duplicates * 2))
        return read_fail(error, p, "its vertex table is outside the class");
    if (multipliers < vertex_offset || (multipliers - vertex_offset) % 16 ||
        (multipliers - vertex_offset) / 16 < in_file + 1 || (multipliers - vertex_offset) / 16 > in_file + 6)
        return read_fail(error, p, "invalid epilogue vertex count");
    epilogue = (multipliers - vertex_offset) / 16 - in_file;
    if ((size_t)data_size * 16 < multipliers || data_size * 16 - multipliers != (transfer * 4 + 15) / 16 * 16)
        return read_fail(error, p, "the colour multipliers do not fit the vertex data");
    records = table + vertex_offset;
    if (!span_ok(size, records, (in_file + epilogue) * 16))
        return read_fail(error, p, "its vertices are outside the class");

    /* Each record carries the 9-bit cache id of the vertex seven places earlier; the last ones continue in the six
       halfwords at +4 of the final record. */
    for (size_t i = 7; i < in_file + epilogue; i++)
        ids[id_count++] = read_u16_le(data + records + i * 16) & 0x1ff;
    for (size_t i = 0; i < 6; i++)
        ids[id_count++] = read_u16_le(data + records + (in_file + epilogue) * 16 - 12 + 2 * i) & 0x1ff;
    if (id_count < in_file)
        return read_fail(error, p, "it lacks vertex ids");

    for (size_t i = 0; i < transfers; i++) {
        if (!slot_store(state, data[table + 0x20 + 2 * i + 1], data[table + 0x20 + 2 * i]))
            return read_fail(error, p, "unaligned VU0 address");
    }
    total_vertices = in_file + duplicates;
    for (size_t i = 0; i < in_file; i++) {
        const uint8_t *r = data + records + i * 16;
        double position[3];

        if (!slot_store(state, r[3], r[1] >> 1))
            return read_fail(error, p, "unaligned VU0 address");
        if (r[2] % 4 || r[2] >= NO_STORE || !state->stored[r[2]])
            return read_fail(error, p, "a vertex loads an unwritten or invalid VU0 slot");
        if (state->joint[r[2]] >= (joint_count > 1 ? joint_count : 1))
            return read_fail(error, p, "a vertex uses a joint past the class's joint count");
        for (int c = 0; c < 3; c++)
            position[c] = (int16_t)read_u16_le(r + 10 + 2 * c);
        if (!add_position(back, position))
            return out_of_memory(error);
    }
    for (size_t i = 0; i < in_file; i++) {
        state->cached[ids[i]] = true;
        memcpy(state->cache[ids[i]], back->positions[back->count - in_file + i], sizeof state->cache[0]);
    }
    for (size_t i = 0; i < duplicates; i++) {
        uint16_t copy = read_u16_le(data + at + 2 * i);

        if (copy & 0x7f || !state->cached[copy >> 7])
            return read_fail(error, p, "a duplicate vertex refers to an empty cache slot");
        if (!add_position(back, state->cache[copy >> 7]))
            return out_of_memory(error);
    }
    if (st_count < total_vertices)
        return read_fail(error, p, "fewer texture coordinates than vertices");

    /* The index stream: 1-based, bit 7 suppresses the drawing kick. A 0 switches to the next GS block's texture and
       takes the next extra index; an extra index of 0 ends the packet. The last three pushes (1, 1, 1) flush the
       VU1 pipeline and are never drawn. */
    pushes = malloc(index_count * 4 * sizeof *pushes);
    if (!pushes)
        return out_of_memory(error);
    for (size_t k = 4; k < index_count * 4; k++) {
        unsigned byte = index_data[k];

        if (byte == 0) {
            if (secret >= secret_count) {
                read_fail(error, p, "the index stream runs out of extra indices");
                goto done;
            }
            byte = (uint8_t)secrets[secret++];
            if (byte == 0)
                break;
            if (block >= texture_count) {
                read_fail(error, p, "a texture switch without a GS block");
                goto done;
            }
            state->has_texture = true;
            state->texture = textures[block++];
            byte |= 0x80;
        }
        if (!((byte & 0x7f) > 0 && (byte & 0x7f) <= total_vertices)) {
            read_fail(error, p, "a vertex index out of range");
            goto done;
        }
        pushes[push_count].index = (int)(byte & 0x7f) - 1;
        pushes[push_count].kick = !(byte & 0x80);
        pushes[push_count].has_texture = state->has_texture;
        pushes[push_count++].texture = state->texture;
        if (k + 1 == index_count * 4) {
            read_fail(error, p, "the index stream is not terminated");
            goto done;
        }
    }
    if (index_count * 4 <= 4) {
        read_fail(error, p, "the index stream is not terminated");
        goto done;
    }
    if (block != texture_count || push_count < 3 || pushes[push_count - 1].index || pushes[push_count - 2].index ||
        pushes[push_count - 3].index) {
        read_fail(error, p, "it does not end with its flush trailer");
        goto done;
    }
    push_count -= 3;
    for (size_t k = 2; k < push_count; k++) {
        if (!pushes[k].kick)
            continue;
        if (!pushes[k].has_texture || pushes[k].texture < -3 || pushes[k].texture >= 16) {
            read_fail(error, p, "an invalid texture index");
            goto done;
        }
        back->triangles++;
    }
    ok = true;

done:
    free(pushes);
    return ok;
}

/* The class core DATA's high LOD read back. */
static bool read_back(const uint8_t *data, size_t size, struct readback *back, InstallError *error)
{
    struct list_state *state;
    size_t table, high;
    uint32_t scale_bits;
    float scale;

    memset(back, 0, sizeof *back);
    if (size < 0x48)
        return install_fail(error, "internal", "the Gadgetron sign's class is too short");
    scale_bits = read_u32_le(data + 0x24);
    memcpy(&scale, &scale_bits, sizeof scale);
    table = read_u32_le(data);
    high = data[4];
    if (!isfinite(scale) || scale <= 0 || table == 0 || high == 0 || !span_ok(size, table, high * 16))
        return install_fail(error, "internal", "the Gadgetron sign's class has no high LOD to read back");
    state = calloc(1, sizeof *state);
    if (!state)
        return out_of_memory(error);
    for (size_t p = 0; p < high; p++) {
        if (!read_packet(data, size, table + 16 * p, p, data[8], state, back, error)) {
            free(state);
            return false;
        }
    }
    free(state);
    if (back->triangles == 0)
        return install_fail(error, "internal", "the Gadgetron sign's class has no drawn triangles");
    return true;
}

/* ---------------------------------------------------------------------------------- the bounding sphere */

/* A point of the sphere's input and its place in the input, for sorting. */
struct sorted_point {
    double p[3];
    size_t index;
};

/* Tuple order (x, then y, then z), then the input's order, so equal points keep their first. */
static int compare_points(const void *left, const void *right)
{
    const struct sorted_point *a = left, *b = right;

    for (int i = 0; i < 3; i++) {
        if (a->p[i] != b->p[i])
            return a->p[i] < b->p[i] ? -1 : 1;
    }
    return a->index < b->index ? -1 : a->index > b->index;
}

/* sorted(set(POINTS)): *ORDER, a new array of *UNIQUE indices into POINTS (COUNT), the distinct points in tuple
   order; of equal points (== on each coordinate, so -0.0 equals 0.0) the first. */
static bool sorted_order(const double (*points)[3], size_t count, size_t **order, size_t *unique)
{
    struct sorted_point *sorted = malloc((count ? count : 1) * sizeof *sorted);

    *order = malloc((count ? count : 1) * sizeof **order);
    if (!sorted || !*order) {
        free(sorted);
        free(*order);
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        memcpy(sorted[i].p, points[i], sizeof sorted[i].p);
        sorted[i].index = i;
    }
    qsort(sorted, count, sizeof *sorted, compare_points);
    *unique = 0;
    for (size_t i = 0; i < count; i++) {
        const double *last = *unique ? points[(*order)[*unique - 1]] : NULL;

        if (!last || last[0] != sorted[i].p[0] || last[1] != sorted[i].p[1] || last[2] != sorted[i].p[2])
            (*order)[(*unique)++] = sorted[i].index;
    }
    free(sorted);
    return true;
}

/* Python's random.Random: MT19937 (Modules/_randommodule.c). */
#define MT_N 624
#define MT_M 397

struct mt {
    uint32_t state[MT_N];
    int index;
};

static void mt_init_genrand(struct mt *mt, uint32_t s)
{
    mt->state[0] = s;
    for (int i = 1; i < MT_N; i++)
        mt->state[i] = 1812433253u * (mt->state[i - 1] ^ (mt->state[i - 1] >> 30)) + (uint32_t)i;
    mt->index = MT_N;
}

/* random.Random(SEED) for a seed below 2^32: init_by_array with the key [SEED]. */
static void mt_seed(struct mt *mt, uint32_t seed)
{
    uint32_t *s = mt->state;
    int i = 1, k;

    mt_init_genrand(mt, 19650218u);
    for (k = MT_N; k; k--) {
        s[i] = (s[i] ^ ((s[i - 1] ^ (s[i - 1] >> 30)) * 1664525u)) + seed;    /* + key[j] + j, j always 0 */
        i++;
        if (i >= MT_N) {
            s[0] = s[MT_N - 1];
            i = 1;
        }
    }
    for (k = MT_N - 1; k; k--) {
        s[i] = (s[i] ^ ((s[i - 1] ^ (s[i - 1] >> 30)) * 1566083941u)) - (uint32_t)i;
        i++;
        if (i >= MT_N) {
            s[0] = s[MT_N - 1];
            i = 1;
        }
    }
    s[0] = 0x80000000u;
}

static uint32_t mt_next(struct mt *mt)
{
    static const uint32_t mag01[2] = {0x0u, 0x9908b0dfu};
    uint32_t *s = mt->state, y;

    if (mt->index >= MT_N) {
        int k;

        for (k = 0; k < MT_N - MT_M; k++) {
            y = (s[k] & 0x80000000u) | (s[k + 1] & 0x7fffffffu);
            s[k] = s[k + MT_M] ^ (y >> 1) ^ mag01[y & 1];
        }
        for (; k < MT_N - 1; k++) {
            y = (s[k] & 0x80000000u) | (s[k + 1] & 0x7fffffffu);
            s[k] = s[k + (MT_M - MT_N)] ^ (y >> 1) ^ mag01[y & 1];
        }
        y = (s[MT_N - 1] & 0x80000000u) | (s[0] & 0x7fffffffu);
        s[MT_N - 1] = s[MT_M - 1] ^ (y >> 1) ^ mag01[y & 1];
        mt->index = 0;
    }
    y = s[mt->index++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    y ^= y >> 18;
    return y;
}

/* random's _randbelow(N) for 2 <= N < 2^32: getrandbits(N.bit_length()) until it is below N. */
static uint32_t mt_below(struct mt *mt, uint32_t n)
{
    int bits = 0;
    uint32_t r;

    while (bits < 32 && n >> bits)
        bits++;
    do
        r = mt_next(mt) >> (32 - bits);
    while (r >= n);
    return r;
}

/* random.Random(SEED).shuffle(ITEMS) */
static void shuffle(size_t *items, size_t count, uint32_t seed)
{
    struct mt mt;

    mt_seed(&mt, seed);
    for (size_t i = count - 1; i >= 1 && count > 1; i--) {
        size_t j = mt_below(&mt, (uint32_t)(i + 1)), t = items[i];

        items[i] = items[j];
        items[j] = t;
    }
}

struct sphere {
    double centre[3];
    double radius;
};

static double det3(const double m[3][3])
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/* The smallest sphere through the support points R (COUNT, at most 4): the circumsphere, the circle's in the plane
   of three, the midpoint of two; when the points are (nearly) degenerate, the sphere on the farthest pair. */
static struct sphere from_support(const double *const *r, size_t count)
{
    struct sphere s = {{0.0, 0.0, 0.0}, -1.0};
    double rows[3][3], rhs[3], dm;
    const double *a;

    if (count == 0)
        return s;
    if (count == 1) {
        memcpy(s.centre, r[0], sizeof s.centre);
        s.radius = 0.0;
        return s;
    }
    if (count == 2) {
        for (int i = 0; i < 3; i++)
            s.centre[i] = (r[0][i] + r[1][i]) / 2;
        s.radius = python_dist(s.centre, r[0]);
        return s;
    }
    a = r[0];
    for (size_t k = 1; k < count; k++) {
        double sum = 0.0;

        for (int i = 0; i < 3; i++) {
            rows[k - 1][i] = 2 * (r[k][i] - a[i]);
            sum += r[k][i] * r[k][i] - a[i] * a[i];
        }
        rhs[k - 1] = sum;
    }
    if (count == 3) {               /* the circle's centre: in the plane of the three points */
        double sum = 0.0;

        rows[2][0] = rows[0][1] * rows[1][2] - rows[0][2] * rows[1][1];
        rows[2][1] = rows[0][2] * rows[1][0] - rows[0][0] * rows[1][2];
        rows[2][2] = rows[0][0] * rows[1][1] - rows[0][1] * rows[1][0];
        for (int i = 0; i < 3; i++)
            sum += rows[2][i] * a[i];
        rhs[2] = sum;
    }
    dm = det3((const double (*)[3])rows);
    if (fabs(dm) < 1e-9) {
        const double *far[2] = {r[0], r[0]};
        double best = 0.0;
        bool first = true;

        for (size_t p = 0; p < count; p++) {
            for (size_t q = 0; q < count; q++) {
                double d = python_dist(r[p], r[q]);

                if (first || d > best) {
                    best = d;
                    far[0] = r[p];
                    far[1] = r[q];
                    first = false;
                }
            }
        }
        return from_support(far, 2);
    }
    for (int k = 0; k < 3; k++) {
        double mk[3][3];

        memcpy(mk, rows, sizeof mk);
        for (int i = 0; i < 3; i++)
            mk[i][k] = rhs[i];
        s.centre[k] = det3((const double (*)[3])mk) / dm;
    }
    s.radius = python_dist(s.centre, a);
    return s;
}

/* Welzl's smallest sphere around the first N of PTS with the support points R (COUNT) on it. */
static struct sphere welzl(const double (*pts)[3], const size_t *order, size_t n, const double **r, size_t count)
{
    struct sphere s = from_support(r, count);

    if (count == 4)
        return s;
    for (size_t i = 0; i < n; i++) {
        const double *p = pts[order[i]];

        if (python_dist(s.centre, p) > s.radius + WELZL_SLACK) {
            r[count] = p;
            s = welzl(pts, order, i, r, count + 1);
        }
    }
    return s;
}

/* The smallest sphere around POINTS (Welzl), visiting the distinct points as the reference does. */
static bool enclosing_sphere(const double (*points)[3], size_t count, struct sphere *sphere)
{
    const double *support[4];
    size_t *order, unique;

    if (!sorted_order(points, count, &order, &unique))
        return false;
    shuffle(order, unique, 0);
    *sphere = welzl(points, order, unique, support, 0);
    free(order);
    return true;
}

/* -------------------------------------------------------------------------------------------- the new class */

/*
 * The new class core DATA: its bounding spheres (the class's at +0x30 and each sequence's, which starts with one)
 * set to the smallest sphere around its vertices plus SPHERE_MARGIN, in vertex units; then checked: no packet with
 * more than MAX_TEXTURE_BLOCKS texture blocks, each packet's colour multipliers (vertex table +0x1C, 4 bytes per
 * transfer vertex) padded with zeros as on the disc (with 0x80 there, the level stopped presenting frames), the
 * triangles expected (TRIANGLES), and every vertex inside the sphere.
 */
static bool finish_class(uint8_t *data, size_t size, size_t triangles, InstallError *error)
{
    struct readback back;
    struct sphere sphere;
    uint8_t packed[16];
    size_t table_at, packets, blocks = 0;

    if (!read_back(data, size, &back, error))
        return false;
    if (!enclosing_sphere((const double (*)[3])back.positions, back.count, &sphere)) {
        free(back.positions);
        return out_of_memory(error);
    }
    free(back.positions);
    for (int i = 0; i < 4; i++) {
        float f = (float)(i < 3 ? sphere.centre[i] : sphere.radius + SPHERE_MARGIN);
        uint32_t bits;

        memcpy(&bits, &f, sizeof bits);
        write_u32_le(packed + 4 * i, bits);
    }
    memcpy(data + 0x30, packed, sizeof packed);
    if (!span_ok(size, 0x48, (size_t)data[0x0C] * 4))
        return install_fail(error, "internal", "the Gadgetron sign's sequence list is outside its class");
    for (size_t i = 0; i < data[0x0C]; i++) {
        uint32_t sequence = read_u32_le(data + 0x48 + 4 * i);

        if (sequence && !span_ok(size, sequence, sizeof packed))
            return install_fail(error, "internal", "the Gadgetron sign's sequence %zu is outside its class", i);
        if (sequence)
            memcpy(data + sequence, packed, sizeof packed);
    }

    table_at = read_u32_le(data);
    packets = (size_t)data[4] + data[5];
    if (!span_ok(size, table_at, packets * 16))
        return install_fail(error, "internal", "the Gadgetron sign's packet table is outside its class");
    for (size_t i = 0; i < packets; i++) {
        size_t b = read_u16_le(data + table_at + 16 * i + 6) / 4;

        blocks = b > blocks ? b : blocks;
    }
    if (blocks > MAX_TEXTURE_BLOCKS)
        return install_fail(error, "internal", "the Gadgetron sign: a packet has %zu texture blocks (at most %d)",
                            blocks, MAX_TEXTURE_BLOCKS);
    for (size_t i = 0; i < packets; i++) {
        size_t table = read_u32_le(data + table_at + 16 * i + 8), end, transfer, mult;

        if (!span_ok(size, table, 0x20))
            return install_fail(error, "internal", "the Gadgetron sign's packet %zu is outside its class", i);
        end = table + (size_t)data[table_at + 16 * i + 12] * 16;
        transfer = read_u32_le(data + table + 0x14);
        mult = read_u32_le(data + table + 0x1C);
        if (end > size || mult > end - table || 4 * transfer > end - table - mult)
            return install_fail(error, "internal", "the Gadgetron sign's packet %zu's colour multipliers do not fit "
                                "it", i);
        for (size_t at = table + mult + 4 * transfer; at < end; at++) {
            if (data[at])
                return install_fail(error, "internal", "the Gadgetron sign: packet %zu's colour multipliers are not "
                                    "padded with zeros", i);
        }
    }

    if (!read_back(data, size, &back, error))
        return false;
    if (back.triangles != triangles) {
        free(back.positions);
        return install_fail(error, "internal", "the Gadgetron sign reads back with %zu triangles, not %zu",
                            back.triangles, triangles);
    }
    for (size_t i = 0; i < back.count; i++) {
        if (python_dist(sphere.centre, back.positions[i]) > sphere.radius + SPHERE_MARGIN) {
            free(back.positions);
            return install_fail(error, "internal", "the Gadgetron sign: a vertex lies outside the bounding sphere");
        }
    }
    free(back.positions);
    return true;
}

uint8_t *make_sign(const char *rac1, size_t *size, InstallError *error)
{
    size_t blob_size = 0, texture_count = 0, glb_size = 0, new_size = 0, core_size = 0, triangles = 0;
    uint8_t *blob, *glb = NULL, *new_glb = NULL, *core = NULL;
    struct moby_class *moby = NULL, *check;
    struct strips retail = {0}, word = {0};
    char reason[256];

    blob = disc_class(rac1, SIGN_LEVEL, SIGN_CLASS, &blob_size, &texture_count, error);
    if (!blob)
        return NULL;
    moby = moby_read(blob, blob_size, MOBY_RAC, reason, sizeof reason);
    if (!moby) {
        install_fail(error, "internal", "Kalebo III's class %d: %s", SIGN_CLASS, reason);
        goto done;
    }
    glb = moby_mesh_export(moby, (int)texture_count, &glb_size, reason, sizeof reason);
    if (!glb) {
        install_fail(error, "internal", "Kalebo III's class %d: %s", SIGN_CLASS, reason);
        goto done;
    }
    if (!read_mesh(glb, glb_size, &retail, error) || !build_word(&retail, &word, error) ||
        !(new_glb = write_glb(&word, &new_size, error)))
        goto done;
    if (moby_mesh_replace(moby, new_glb, new_size, NULL, reason, sizeof reason) != 0 ||
        !(core = moby_write(moby, MOBY_RAC, &core_size, reason, sizeof reason))) {
        install_fail(error, "internal", "the Gadgetron sign: %s", reason);
        goto done;
    }
    check = moby_read(core, core_size, MOBY_RAC, reason, sizeof reason);
    if (!check) {
        install_fail(error, "internal", "the Gadgetron sign does not read back: %s", reason);
        free(core);
        core = NULL;
        goto done;
    }
    moby_free(check);
    for (size_t s = 0; s < word.count; s++)
        triangles += strip_triangles(&word.items[s]);
    if (!finish_class(core, core_size, triangles, error)) {
        free(core);
        core = NULL;
        goto done;
    }
    *size = core_size;

done:
    moby_free(moby);
    strips_free(&retail);
    strips_free(&word);
    free(new_glb);
    free(glb);
    free(blob);
    return core;
}
