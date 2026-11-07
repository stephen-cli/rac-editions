/*
 * Moby classes (moby.h): wrench's read_class and write_class in C, written from wrench's moby engine
 * (src/engine/moby_low.cpp, moby_packet.cpp, moby_vertex.cpp, moby_animation.cpp and core/vif.cpp) by chaoticgd,
 * GPL-3.0, and --rebind from the Executive Edition's src/mobyconvert.cpp in wrench's fork.
 *
 * The output is wrench's byte for byte, so the writer keeps wrench's ways where they differ from the input:
 *  - the GIF usage table, packet table entries, bangle header, frame sizes and vertex counts are rebuilt, and the
 *    bangle header comes out zero;
 *  - the vertex index each vertex carries for the one 7 before it loses its bit 8 (wrench masks it with 0xff);
 *  - a collision vector's fourth short comes out 0, and its shorts pass through floats (/ 1024, * 1024);
 *  - duplicate vertex indices lose their low 7 bits;
 *  - a regular frame's data is not padded to its stated size, and a sequence's flags come from its first frame;
 *  - R&C1's trigger data is aligned to 16 bytes and counted from the class, R&C2's from its sequence;
 *  - an R&C2 corncob is dropped from an R&C1 class, which keeps its own short at 0x2e instead;
 *  - bangle packets past the packet table's room are written over whatever follows it, as wrench does;
 *  - a vertex table read from R&C2 gets neutral R&C1 colour multipliers (0x80) with zero padding (the fork's
 *    change to write_vertex_table).
 * The class is read from offset 0 of its buffer, as wrench reads it: all offsets count from there.
 */
#include "moby.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLASS_HEADER 0x48
#define VERTEX_SIZE 0x10
#define TEXTURE_SIZE 0x40
#define VU0_UNUSED 0xf4             /* a vertex's VU0 address field when it loads or stores nothing */
#define ST_UNPACK_ADDR 0xc2         /* VU1 quadword addresses of the UNPACKs */
#define INDEX_UNPACK_ADDR 0x12d

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

static void set_error(char *error, size_t error_size, const char *format, va_list args)
{
    if (error && error_size)
        vsnprintf(error, error_size, format, args);
}

/* ------------------------------------------------------------------------------------------------ memory */

void *moby_alloc(struct moby_class *moby, size_t size)
{
    void *block;

    if (moby->allocation_count == moby->allocation_capacity) {
        size_t capacity = moby->allocation_capacity ? moby->allocation_capacity * 2 : 64;
        void **grown = realloc(moby->allocations, capacity * sizeof *grown);

        if (!grown)
            return NULL;
        moby->allocations = grown;
        moby->allocation_capacity = capacity;
    }
    block = calloc(1, size ? size : 1);
    if (block)
        moby->allocations[moby->allocation_count++] = block;
    return block;
}

void moby_free(struct moby_class *moby)
{
    if (!moby)
        return;
    for (size_t i = 0; i < moby->allocation_count; i++)
        free(moby->allocations[i]);
    free(moby->allocations);
    free(moby);
}

/* ------------------------------------------------------------------------------------------------ reader */

/* The input, with wrench's bounds checks: a failed check sets FAILED, keeps the first reason, and makes every later
 * read fail, so the callers test FAILED where they would go on. */
struct reader {
    const uint8_t *data;
    int64_t size;
    struct moby_class *moby;
    char *error;
    size_t error_size;
    int failed;
};

static void read_fail(struct reader *r, const char *format, ...)
{
    va_list args;

    if (r->failed)
        return;
    r->failed = 1;
    va_start(args, format);
    set_error(r->error, r->error_size, format, args);
    va_end(args);
}

/* COUNT items of ELEMENT bytes at OFFSET: a pointer into the input, or NULL when they are not all in it. */
static const uint8_t *span(struct reader *r, int64_t offset, int64_t count, int64_t element, const char *subject)
{
    if (r->failed)
        return NULL;
    if (offset < 0 || count < 0 || offset > r->size || count * element > r->size - offset) {
        read_fail(r, "the %s at 0x%llx (%lld bytes) is outside the class (0x%llx bytes)", subject,
             (long long)offset, (long long)(count * element), (long long)r->size);
        return NULL;
    }
    return r->data + offset;
}

static uint8_t read_u8(struct reader *r, int64_t offset, const char *subject)
{
    const uint8_t *at = span(r, offset, 1, 1, subject);

    return at ? at[0] : 0;
}

static uint16_t read_u16(struct reader *r, int64_t offset, const char *subject)
{
    const uint8_t *at = span(r, offset, 1, 2, subject);

    return at ? get_u16(at) : 0;
}

static uint32_t read_u32(struct reader *r, int64_t offset, const char *subject)
{
    const uint8_t *at = span(r, offset, 1, 4, subject);

    return at ? get_u32(at) : 0;
}

static void *alloc_items(struct reader *r, size_t count, size_t element)
{
    void *block;

    if (r->failed || count == 0)
        return NULL;
    block = moby_alloc(r->moby, count * element);
    if (!block)
        read_fail(r, "out of memory");
    return block;
}

/* A copy of COUNT items of ELEMENT bytes at OFFSET (none for COUNT 0, though OFFSET is still checked). */
static struct moby_bytes read_bytes(struct reader *r, int64_t offset, int64_t count, int64_t element,
                                    const char *subject)
{
    struct moby_bytes bytes = {NULL, 0};
    const uint8_t *at = span(r, offset, count, element, subject);

    if (!at || count == 0)
        return bytes;
    bytes.data = alloc_items(r, (size_t)count, (size_t)element);
    if (bytes.data) {
        bytes.size = (size_t)(count * element);
        memcpy(bytes.data, at, bytes.size);
    }
    return bytes;
}

/* ------------------------------------------------------------------------------------------------ writer */

/* The output, grown as wrench grows it: appends, alignment padding, and writes into what is there. */
struct writer {
    uint8_t *data;
    size_t size;
    size_t capacity;
    int failed;
    char *error;
    size_t error_size;
};

static void write_fail(struct writer *w, const char *format, ...)
{
    va_list args;

    if (w->failed)
        return;
    w->failed = 1;
    va_start(args, format);
    set_error(w->error, w->error_size, format, args);
    va_end(args);
}

/* COUNT more bytes of FILL at the end: their offset. */
static size_t grow(struct writer *w, size_t count, uint8_t fill)
{
    size_t at = w->size;

    if (w->failed)
        return at;
    if (w->size + count > w->capacity) {
        size_t capacity = w->capacity ? w->capacity : 0x1000;
        uint8_t *grown;

        while (capacity < w->size + count)
            capacity *= 2;
        grown = realloc(w->data, capacity);
        if (!grown) {
            write_fail(w, "out of memory");
            return at;
        }
        w->data = grown;
        w->capacity = capacity;
    }
    memset(w->data + at, fill, count);
    w->size += count;
    return at;
}

static size_t append(struct writer *w, const void *bytes, size_t count)
{
    size_t at = grow(w, count, 0);

    if (!w->failed && count)
        memcpy(w->data + at, bytes, count);
    return at;
}

static void append_u8(struct writer *w, uint8_t value)
{
    append(w, &value, 1);
}

static void append_u16(struct writer *w, uint16_t value)
{
    uint8_t bytes[2];

    set_u16(bytes, value);
    append(w, bytes, 2);
}

static size_t append_u32(struct writer *w, uint32_t value)
{
    uint8_t bytes[4];

    set_u32(bytes, value);
    return append(w, bytes, 4);
}

static void pad(struct writer *w, size_t align)
{
    if (w->size % align)
        grow(w, align - w->size % align, 0);
}

/* Zeros up to OFFSET, when the output is shorter. */
static void pad_to(struct writer *w, size_t offset)
{
    if (w->size < offset)
        grow(w, offset - w->size, 0);
}

/* COUNT bytes at OFFSET, which must already be written: otherwise the write fails. */
static int in_output(struct writer *w, size_t offset, size_t count)
{
    if (w->failed)
        return 0;
    if (offset + count > w->size) {
        write_fail(w, "a write at 0x%zx past the output's end (0x%zx)", offset, w->size);
        return 0;
    }
    return 1;
}

static void put(struct writer *w, size_t offset, const void *bytes, size_t count)
{
    if (in_output(w, offset, count))
        memcpy(w->data + offset, bytes, count);
}

static void put_u16(struct writer *w, size_t offset, uint16_t value)
{
    if (in_output(w, offset, 2))
        set_u16(w->data + offset, value);
}

static void put_u32(struct writer *w, size_t offset, uint32_t value)
{
    if (in_output(w, offset, 4))
        set_u32(w->data + offset, value);
}

/* ------------------------------------------------------------------------------------------------- VIF */

/* An UNPACK's data in the input. */
struct unpack {
    int64_t offset;
    int64_t size;
};

/* A VIF code's packet size in bytes, 0 for a code wrench cannot decode. */
static int64_t vif_packet_size(uint32_t code, int *is_unpack)
{
    uint32_t cmd = code >> 24 & 0x7f;
    int64_t num = code >> 16 & 0xff;
    int64_t size;

    *is_unpack = 0;
    if (num == 0)
        num = 256;
    switch (cmd) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:   /* NOP .. MARK */
    case 0x10: case 0x11: case 0x13: case 0x14: case 0x15: case 0x17:   /* FLUSHE .. MSCNT */
        return 4;
    case 0x20:                      /* STMASK */
        return 8;
    case 0x30: case 0x31:           /* STROW, STCOL */
        return 20;
    case 0x4a:                      /* MPG */
        return 4 + num * 8;
    case 0x50: case 0x51:           /* DIRECT, DIRECTHL */
        size = code & 0xffff;
        return 4 + (size ? size : 65536) * 16;
    default:
        if ((cmd & 0x60) != 0x60)
            return 0;
        *is_unpack = 1;
        {
            uint32_t vn = code >> 26 & 3;
            uint32_t vl = code >> 24 & 3;
            int64_t bytes = num * (((32 >> vl) * (vn + 1)) / 8);

            return 4 + (bytes + 3) / 4 * 4;
        }
    }
}

/* The UNPACKs of the VIF command list at OFFSET (SIZE bytes), as read_vif_command_list and filter_vif_unpacks find
 * them: the first MAX in UNPACKS, and how many there are. A list ends at a code wrench cannot decode, or at a
 * packet that overruns the list, which as an UNPACK counts with no data. */
static int vif_unpacks(struct reader *r, int64_t offset, int64_t size, struct unpack *unpacks, int max)
{
    const uint8_t *list = span(r, offset, size, 1, "VIF command list");
    int count = 0;

    if (!list)
        return 0;
    for (int64_t at = 0; at + 4 <= size;) {
        int is_unpack;
        int64_t packet_size = vif_packet_size(get_u32(list + at), &is_unpack);
        int whole;

        if (packet_size == 0)
            break;
        whole = packet_size <= 0x10000 && at + packet_size <= size;
        if (is_unpack) {
            if (count < max) {
                unpacks[count].offset = offset + at + 4;
                unpacks[count].size = whole ? packet_size - 4 : 0;
            }
            count++;
        }
        if (!whole)
            break;
        at += packet_size;
    }
    return count;
}

/* The index stream (and, with TEXTURES, the texture registers) of a packet's VIF list. METAL: the texture index must
 * be chrome (-2) or glass (-3), not below -1 as in a regular packet. */
static void read_vif(struct reader *r, struct moby_vif *vif, const struct unpack *indices,
                     const struct unpack *textures, int metal)
{
    const uint8_t *header = span(r, indices->offset, 1, 4, "index header");
    size_t texture_count;

    if (!header)
        return;
    if (indices->size < 4) {
        read_fail(r, "a moby packet's index UNPACK has no header");
        return;
    }
    vif->index_header_first_byte = header[0];
    if (header[3] != 0) {
        read_fail(r, "a moby packet has a bad index buffer");
        return;
    }
    texture_count = textures ? (size_t)(textures->size / TEXTURE_SIZE) : 0;
    vif->secret_indices = alloc_items(r, 1 + texture_count, 1);
    if (r->failed)
        return;
    vif->secret_indices[0] = (int8_t)header[2];
    vif->secret_count = 1;
    vif->indices = read_bytes(r, indices->offset + 4, indices->size - 4, 1, "index data");
    if (!textures)
        return;
    if (textures->size % TEXTURE_SIZE != 0) {
        read_fail(r, "a moby packet has a bad texture unpack");
        return;
    }
    vif->textures = alloc_items(r, texture_count, TEXTURE_SIZE);
    if (r->failed)
        return;
    memcpy(vif->textures, r->data + textures->offset, texture_count * TEXTURE_SIZE);
    vif->texture_count = texture_count;
    for (size_t i = 0; i < texture_count; i++) {
        /* the extra indices sit at +0xc of the first quadwords, one per texture */
        int32_t texture = (int32_t)get_u32(vif->textures + i * TEXTURE_SIZE + 0x20);

        vif->secret_indices[vif->secret_count++] = (int8_t)r->data[textures->offset + (int64_t)i * 0x10 + 0xc];
        if (metal ? texture != -2 && texture != -3 : texture < -1) {
            read_fail(r, "a moby packet has a bad texture index (%d)", (int)texture);
            return;
        }
    }
}

/* ---------------------------------------------------------------------------------------- packet entries */

/* A packet table entry: 16 bytes. */
struct packet_entry {
    uint32_t vif_list_offset;
    uint16_t vif_list_size;         /* in quadwords */
    uint16_t texture_unpack_offset; /* no third UNPACK when 0 */
    uint32_t vertex_offset;
    uint8_t vertex_data_size;       /* in quadwords, header included */
    uint8_t unknown_d;              /* (0xf + transfer vertices * 6) / 0x10 */
    uint8_t unknown_e;              /* (3 + transfer vertices) / 4 */
    uint8_t transfer_vertex_count;
};

static struct packet_entry get_entry(const uint8_t *at)
{
    struct packet_entry entry;

    entry.vif_list_offset = get_u32(at);
    entry.vif_list_size = get_u16(at + 4);
    entry.texture_unpack_offset = get_u16(at + 6);
    entry.vertex_offset = get_u32(at + 8);
    entry.vertex_data_size = at[0xc];
    entry.unknown_d = at[0xd];
    entry.unknown_e = at[0xe];
    entry.transfer_vertex_count = at[0xf];
    return entry;
}

/* ------------------------------------------------------------------------------------------ vertex tables */

static void set_vertex_index(uint8_t *vertex, uint16_t index)
{
    set_u16(vertex, (uint16_t)((get_u16(vertex) & ~0x1ff) | (index & 0x1ff)));
}

/* The vertex table of a regular packet's ENTRY, checked against the entry's counts, with each vertex given back its
 * own index (the file stores it 7 vertices later). */
static void read_vertex_table(struct reader *r, struct moby_vertex_table *table, const struct packet_entry *entry,
                              int rac1)
{
    int64_t header_offset = entry->vertex_offset;
    int32_t transfer_vertex_count = entry->transfer_vertex_count;
    int32_t vertex_data_size = entry->vertex_data_size;
    uint32_t transfers, two_way, three_way, main, duplicates, transfer_vertices, table_offset, unknown_e;
    int64_t array_offset = header_offset;
    int64_t vertex_offset;
    int32_t in_file, epilogue;
    const uint8_t *duplicate_data, *last;

    if (rac1) {
        const uint8_t *header = span(r, header_offset, 1, 0x20, "moby vertex header");

        if (!header)
            return;
        transfers = get_u32(header);
        two_way = get_u32(header + 4);
        three_way = get_u32(header + 8);
        main = get_u32(header + 0xc);
        duplicates = get_u32(header + 0x10);
        transfer_vertices = get_u32(header + 0x14);
        table_offset = get_u32(header + 0x18);
        unknown_e = get_u32(header + 0x1c);
        array_offset += 0x20;
    } else {
        const uint8_t *header = span(r, header_offset, 1, 0x10, "moby vertex header");

        if (!header)
            return;
        transfers = get_u16(header);
        two_way = get_u16(header + 2);
        three_way = get_u16(header + 4);
        main = get_u16(header + 6);
        duplicates = get_u16(header + 8);
        transfer_vertices = get_u16(header + 0xa);
        table_offset = get_u16(header + 0xc);
        unknown_e = get_u16(header + 0xe);
        array_offset += 0x10;
    }
    if (table_offset / 0x10 > (uint32_t)vertex_data_size) {
        read_fail(r, "bad vertex table offset or size");
        return;
    }
    if ((uint32_t)transfer_vertex_count != transfer_vertices) {
        read_fail(r, "conflicting vertex counts");
        return;
    }
    if (entry->unknown_d != (0xf + transfer_vertex_count * 6) / 0x10 ||
        entry->unknown_e != (3 + transfer_vertex_count) / 4) {
        read_fail(r, "weird values in a packet table entry at 0xd and 0xe");
        return;
    }

    vertex_offset = header_offset + table_offset;
    in_file = (int32_t)(two_way + three_way + main);
    {
        struct moby_bytes vertices = read_bytes(r, vertex_offset, in_file, VERTEX_SIZE, "vertex table");

        table->vertices = vertices.data;
        table->vertex_count = vertices.size / VERTEX_SIZE;
    }
    vertex_offset += (int64_t)in_file * 0x10;

    {
        struct moby_bytes list = read_bytes(r, array_offset, transfers, 2, "matrix transfers");

        table->transfers = list.data;
        table->transfer_count = list.size / 2;
    }
    array_offset += (int64_t)transfers * 2;
    if (array_offset % 4 != 0)
        array_offset += 2;
    if (array_offset % 8 != 0)
        array_offset += 4;
    duplicate_data = span(r, array_offset, duplicates, 2, "duplicate vertices");
    table->duplicates = alloc_items(r, duplicates, sizeof(uint16_t));
    if (r->failed)
        return;
    for (uint32_t i = 0; i < duplicates; i++)
        table->duplicates[i] = (uint16_t)(get_u16(duplicate_data + i * 2) >> 7);
    table->duplicate_count = duplicates;

    table->two_way_count = (int32_t)two_way;
    table->three_way_count = (int32_t)three_way;
    table->main_count = (int32_t)main;
    table->unknown_e = (uint16_t)unknown_e;
    if (rac1) {
        int32_t unknown_e_size = (int32_t)((uint32_t)vertex_data_size * 0x10 - unknown_e);

        table->unknown_e_data = read_bytes(r, header_offset + unknown_e, unknown_e_size, 1,
                                           "vertex table unknown_e data");
        if (r->failed)
            return;
    }

    /* Each vertex stores the index of the vertex 7 before it; the last 7 indices follow in the padding vertices
     * and then in the last padding vertex's second half (see write_vertex_table). */
    for (size_t i = 7; i < table->vertex_count; i++)
        set_vertex_index(table->vertices + (i - 7) * VERTEX_SIZE, get_u16(table->vertices + i * VERTEX_SIZE));
    if (rac1)
        epilogue = (int32_t)((unknown_e - table_offset) / 0x10 - (uint32_t)in_file);
    else
        epilogue = (int32_t)((uint32_t)vertex_data_size - table_offset / 0x10 - (uint32_t)in_file);
    if (epilogue >= 7) {
        read_fail(r, "bad moby vertex table");
        return;
    }
    {
        int32_t first = 7 - in_file > 0 ? 7 - in_file : 0;

        vertex_offset += (int64_t)first * 0x10;
        for (int32_t i = first; i < epilogue; i++) {
            const uint8_t *vertex = span(r, vertex_offset, 1, VERTEX_SIZE, "vertex table");

            if (!vertex)
                return;
            vertex_offset += 0x10;
            set_vertex_index(table->vertices + (size_t)(in_file + i - 7) * VERTEX_SIZE, get_u16(vertex));
        }
    }
    last = span(r, vertex_offset - 0x10, 1, VERTEX_SIZE, "vertex table");
    if (!last)
        return;
    {
        int32_t first = 7 - in_file - epilogue > 0 ? 7 - in_file - epilogue : 0;

        for (int32_t i = first; i < 6; i++) {
            int64_t dest = (int64_t)in_file + epilogue + i - 7;

            if (dest >= 0 && (uint64_t)dest < table->vertex_count)
                set_vertex_index(table->vertices + dest * VERTEX_SIZE, get_u16(last + 4 + i * 2));
        }
    }
}

/* TABLE in the output, each vertex carrying the index of the one 7 before it. Returns the transfer vertex count. */
static uint32_t write_vertex_table(struct writer *w, const struct moby_vertex_table *table, int rac1)
{
    size_t header_offset = grow(w, rac1 ? 0x20 : 0x10, 0);
    size_t count = table->vertex_count;
    uint16_t epilogue_indices[7];
    size_t epilogue_count = 0;
    uint32_t transfer_vertices, table_offset, unknown_e;
    uint8_t *vertices;
    size_t out_count = count;
    size_t epilogue;
    uint8_t last[VERTEX_SIZE] = {0};

    append(w, table->transfers, table->transfer_count * 2);
    pad(w, 8);
    for (size_t i = 0; i < table->duplicate_count; i++)
        append_u16(w, (uint16_t)(table->duplicates[i] << 7));
    pad(w, 0x10);
    table_offset = (uint32_t)(w->size - header_offset);

    /* The vertex index stored in each vertex is the one 7 vertices before's, so the last 7 indices go into the
     * padding vertices, and when those run out, into the second half of the last one (there is always one). */
    for (size_t i = count < 7 ? 7 - count : 0; i > 0; i--)
        epilogue_indices[epilogue_count++] = 0;
    for (size_t i = count > 7 ? count - 7 : 0; i < count; i++)
        epilogue_indices[epilogue_count++] = get_u16(table->vertices + i * VERTEX_SIZE) & 0x1ff;
    vertices = malloc((count + 8) * VERTEX_SIZE);
    if (!vertices) {
        write_fail(w, "out of memory");
        return 0;
    }
    if (count)
        memcpy(vertices, table->vertices, count * VERTEX_SIZE);
    for (size_t i = count; i-- > 7;) {
        uint8_t *vertex = vertices + i * VERTEX_SIZE;

        /* wrench takes 8 bits of the earlier vertex's 9-bit index here */
        set_u16(vertex, (uint16_t)((get_u16(vertex) & ~0x1ff) | (get_u16(vertex - 7 * VERTEX_SIZE) & 0xff)));
    }
    for (size_t i = 0; i < count && i < 7; i++)
        set_u16(vertices + i * VERTEX_SIZE, (uint16_t)(get_u16(vertices + i * VERTEX_SIZE) & ~0x1ff));
    for (epilogue = 0; out_count % 4 != 2 && epilogue < epilogue_count; epilogue++) {
        uint8_t *vertex = vertices + out_count++ * VERTEX_SIZE;

        memset(vertex, 0, VERTEX_SIZE);
        if (count + epilogue >= 7)
            set_u16(vertex, epilogue_indices[epilogue]);
    }
    if (epilogue >= epilogue_count) {
        free(vertices);
        write_fail(w, "no room for a vertex table's last indices");
        return 0;
    }
    if (count + epilogue >= 7)
        set_u16(last, epilogue_indices[epilogue]);
    /* the rest go into its second half: 6 slots, and at most 7 - 1 indices remain */
    for (size_t slot = 0; slot < (VERTEX_SIZE - 4) / 2 && epilogue + 1 + slot < epilogue_count; slot++) {
        if (count + epilogue + 1 + slot >= 7)
            set_u16(last + 4 + slot * 2, epilogue_indices[epilogue + 1 + slot]);
    }
    memcpy(vertices + out_count++ * VERTEX_SIZE, last, VERTEX_SIZE);
    append(w, vertices, out_count * VERTEX_SIZE);
    free(vertices);

    transfer_vertices = (uint32_t)table->two_way_count + (uint32_t)table->three_way_count +
                        (uint32_t)table->main_count + (uint32_t)table->duplicate_count;
    unknown_e = table->unknown_e;
    if (rac1) {
        uint8_t header[0x20];

        unknown_e = (uint32_t)(w->size - header_offset);
        if (table->unknown_e_data.size == 0) {
            /* No R&C1 colour multipliers in a table from another game: neutral ones (0x80 = 1.0, as every one on
             * the R&C1 disc is), with the padding to the next quadword zero (R&C1's vertex pipeline also runs the
             * trailing records, and with 0x80 there a level drawing the class stops presenting frames). */
            size_t size = ((size_t)transfer_vertices * 4 + 15) / 16 * 16;
            size_t at = grow(w, size, 0);

            if (!w->failed)
                memset(w->data + at, 0x80, (size_t)transfer_vertices * 4);
        } else {
            append(w, table->unknown_e_data.data, table->unknown_e_data.size);
        }
        set_u32(header, (uint32_t)table->transfer_count);
        set_u32(header + 4, (uint32_t)table->two_way_count);
        set_u32(header + 8, (uint32_t)table->three_way_count);
        set_u32(header + 0xc, (uint32_t)table->main_count);
        set_u32(header + 0x10, (uint32_t)table->duplicate_count);
        set_u32(header + 0x14, transfer_vertices);
        set_u32(header + 0x18, table_offset);
        set_u32(header + 0x1c, unknown_e);
        put(w, header_offset, header, sizeof header);
    } else {
        uint8_t header[0x10];

        set_u16(header, (uint16_t)table->transfer_count);
        set_u16(header + 2, (uint16_t)table->two_way_count);
        set_u16(header + 4, (uint16_t)table->three_way_count);
        set_u16(header + 6, (uint16_t)table->main_count);
        set_u16(header + 8, (uint16_t)table->duplicate_count);
        set_u16(header + 0xa, (uint16_t)transfer_vertices);
        set_u16(header + 0xc, (uint16_t)table_offset);
        set_u16(header + 0xe, (uint16_t)unknown_e);
        put(w, header_offset, header, sizeof header);
    }
    return transfer_vertices;
}

/* ------------------------------------------------------------------------------------------------ packets */

static void put_entry(struct writer *w, size_t offset, const struct packet_entry *entry)
{
    uint8_t bytes[16];

    set_u32(bytes, entry->vif_list_offset);
    set_u16(bytes + 4, entry->vif_list_size);
    set_u16(bytes + 6, entry->texture_unpack_offset);
    set_u32(bytes + 8, entry->vertex_offset);
    bytes[0xc] = entry->vertex_data_size;
    bytes[0xd] = entry->unknown_d;
    bytes[0xe] = entry->unknown_e;
    bytes[0xf] = entry->transfer_vertex_count;
    put(w, offset, bytes, sizeof bytes);
}

/* COUNT regular packets from the packet table at TABLE_OFFSET: the ST, index and texture UNPACKs of each one's VIF
 * list, and its vertex table. */
static struct moby_packet *read_packets(struct reader *r, int64_t table_offset, int64_t count, int rac1)
{
    const uint8_t *table = span(r, table_offset, count, 16, "moby packet table");
    struct moby_packet *packets = alloc_items(r, (size_t)count, sizeof *packets);

    if (r->failed)
        return NULL;
    for (int64_t i = 0; i < count; i++) {
        struct packet_entry entry = get_entry(table + i * 16);
        struct moby_packet *packet = &packets[i];
        struct unpack unpacks[3];
        int unpack_count = vif_unpacks(r, entry.vif_list_offset, (int64_t)entry.vif_list_size * 0x10, unpacks, 3);
        struct moby_bytes sts;

        if (r->failed)
            return NULL;
        if (unpack_count < 2) {
            read_fail(r, "a moby packet's VIF list has %d UNPACKs", unpack_count);
            return NULL;
        }
        sts = read_bytes(r, unpacks[0].offset, unpacks[0].size / 4, 4, "moby st unpack");
        packet->sts = sts.data;
        packet->st_count = sts.size / 4;
        read_vif(r, &packet->vif, &unpacks[1], unpack_count >= 3 ? &unpacks[2] : NULL, 0);
        read_vertex_table(r, &packet->table, &entry, rac1);
        if (r->failed)
            return NULL;
    }
    return packets;
}

/* COUNT metal packets from the packet table at TABLE_OFFSET: the index and texture UNPACKs of each one's VIF list,
 * and its vertices after a 16-byte header. */
static struct moby_metal_packet *read_metal_packets(struct reader *r, int64_t table_offset, int64_t count)
{
    const uint8_t *table = span(r, table_offset, count, 16, "moby metal packet table");
    struct moby_metal_packet *packets = alloc_items(r, (size_t)count, sizeof *packets);

    if (r->failed)
        return NULL;
    for (int64_t i = 0; i < count; i++) {
        struct packet_entry entry = get_entry(table + i * 16);
        struct moby_metal_packet *packet = &packets[i];
        struct unpack unpacks[2];
        int unpack_count = vif_unpacks(r, entry.vif_list_offset, (int64_t)entry.vif_list_size * 0x10, unpacks, 2);
        const uint8_t *header;
        struct moby_bytes vertices;

        if (r->failed)
            return NULL;
        if (unpack_count < 1) {
            read_fail(r, "a metal packet's VIF list has no UNPACK");
            return NULL;
        }
        read_vif(r, &packet->vif, &unpacks[0], unpack_count >= 2 ? &unpacks[1] : NULL, 1);
        header = span(r, entry.vertex_offset, 1, 16, "metal vertex table header");
        if (!header)
            return NULL;
        vertices = read_bytes(r, (int64_t)entry.vertex_offset + 0x10, (int32_t)get_u32(header), VERTEX_SIZE,
                              "metal vertex table");
        packet->vertices = vertices.data;
        packet->vertex_count = vertices.size / VERTEX_SIZE;
        packet->unknown_4 = get_u32(header + 4);
        packet->unknown_8 = get_u32(header + 8);
        packet->unknown_c = get_u32(header + 0xc);
        if (r->failed)
            return NULL;
    }
    return packets;
}

/* A growable list of 16-byte GIF usage entries: the textures (12 bytes, 0xff after the last) and the offset of a
 * texture UNPACK's quadword. */
struct gif_usage {
    struct writer list;
};

static void append_unpack(struct writer *w, uint32_t code, const void *data, size_t size)
{
    append_u32(w, code);
    append(w, data, size);
    pad(w, 4);
}

/* The index and texture UNPACKs of a packet's VIF list, and its GIF usage entry (with GIF). The texture UNPACK's
 * offset from the list's end, in quadwords (0 without textures). */
static uint16_t write_vif(struct writer *w, struct gif_usage *gif, const struct moby_vif *vif)
{
    size_t index_size = 4 + vif->indices.size;
    uint8_t *indices = malloc(index_size);
    uint32_t index_num = (uint32_t)(index_size / 4);
    size_t texture_unpack;
    uint8_t *textures;

    if (!indices) {
        write_fail(w, "out of memory");
        return 0;
    }
    indices[0] = vif->index_header_first_byte;
    indices[1] = vif->texture_count ? (uint8_t)index_num : 0;
    indices[2] = vif->secret_count ? (uint8_t)vif->secret_indices[0] : 0;
    indices[3] = 0;
    if (vif->indices.size)
        memcpy(indices + 4, vif->indices.data, vif->indices.size);
    /* UNPACK V4_8 (signed, with TOPS) to INDEX_UNPACK_ADDR */
    append_unpack(w, 0x6e000000u | (index_num & 0xff) << 16 | 0x8000u | INDEX_UNPACK_ADDR, indices, index_size);
    free(indices);
    if (!vif->texture_count)
        return 0;

    while (w->size % 0x10 != 0xc && !w->failed)
        append_u8(w, 0);
    if (vif->secret_count < vif->texture_count) {
        write_fail(w, "a packet has fewer extra indices than textures");
        return 0;
    }
    textures = malloc(vif->texture_count * TEXTURE_SIZE);
    if (!textures) {
        write_fail(w, "out of memory");
        return 0;
    }
    memcpy(textures, vif->textures, vif->texture_count * TEXTURE_SIZE);
    for (size_t i = 1; i < vif->secret_count; i++) {
        size_t at = (i - 1) * 0x10 + 0xc;

        if (at >= vif->texture_count * TEXTURE_SIZE) {
            free(textures);
            write_fail(w, "a packet has more extra indices than its textures hold");
            return 0;
        }
        textures[at] = (uint8_t)vif->secret_indices[i];
    }
    texture_unpack = w->size;
    /* UNPACK V4_32 (signed, with TOPS) right after the indices */
    append_unpack(w, 0x6c000000u | (uint32_t)(vif->texture_count * 4 & 0xff) << 16 | 0x8000u |
                     ((INDEX_UNPACK_ADDR + index_num) & 0x3ff),
                  textures, vif->texture_count * TEXTURE_SIZE);
    if (gif) {
        uint8_t entry[16];

        if (vif->texture_count > 12) {
            free(textures);
            write_fail(w, "a packet has more than 12 textures");
            return 0;
        }
        memset(entry, 0xff, 12);
        for (size_t i = 0; i < vif->texture_count; i++)
            entry[i] = textures[i * TEXTURE_SIZE + 0x20];   /* the low byte of TEX0's texture index */
        set_u32(entry + 12, (uint32_t)(texture_unpack - 0xc));
        append(&gif->list, entry, sizeof entry);
        if (gif->list.failed)
            write_fail(w, "out of memory");
    }
    free(textures);
    pad(w, 0x10);
    return (uint16_t)((w->size - texture_unpack + 0x4) / 0x10);
}

/* COUNT regular packets: each one's VIF list and vertex table, and its entry at TABLE_OFFSET onwards. */
static void write_packets(struct writer *w, struct gif_usage *gif, size_t table_offset,
                          const struct moby_packet *packets, size_t count, int rac1)
{
    for (size_t i = 0; i < count && !w->failed; i++) {
        const struct moby_packet *packet = &packets[i];
        struct packet_entry entry;
        size_t vif_list, vertex_header;
        uint32_t transfer_vertices;

        pad(w, 0x10);
        vif_list = w->size;
        entry.vif_list_offset = (uint32_t)vif_list;
        /* UNPACK V2_16 (signed, with TOPS, masked) to ST_UNPACK_ADDR */
        append_unpack(w, 0x75000000u | (uint32_t)(packet->st_count & 0xff) << 16 | 0x8000u | ST_UNPACK_ADDR,
                      packet->sts, packet->st_count * 4);
        entry.texture_unpack_offset = write_vif(w, gif, &packet->vif);
        pad(w, 0x10);
        entry.vif_list_size = (uint16_t)((w->size - vif_list) / 0x10);

        vertex_header = w->size;
        transfer_vertices = write_vertex_table(w, &packet->table, rac1);
        entry.vertex_offset = (uint32_t)vertex_header;
        pad(w, 0x10);
        entry.vertex_data_size = (uint8_t)((w->size - vertex_header) / 0x10);
        entry.unknown_d = (uint8_t)((0xf + transfer_vertices * 6) / 0x10);
        entry.unknown_e = (uint8_t)((3 + transfer_vertices) / 4);
        entry.transfer_vertex_count = (uint8_t)transfer_vertices;

        pad(w, 0x10);
        put_entry(w, table_offset, &entry);
        table_offset += 0x10;
    }
}

/* COUNT metal packets: each one's VIF list and vertices, and its entry at TABLE_OFFSET onwards. */
static void write_metal_packets(struct writer *w, size_t table_offset, const struct moby_metal_packet *packets,
                                size_t count)
{
    for (size_t i = 0; i < count && !w->failed; i++) {
        const struct moby_metal_packet *packet = &packets[i];
        struct packet_entry entry;
        size_t vif_list, vertex_header;
        uint8_t header[16];
        uint32_t vertex_count = (uint32_t)packet->vertex_count;

        pad(w, 0x10);
        vif_list = w->size;
        entry.vif_list_offset = (uint32_t)vif_list;
        entry.texture_unpack_offset = write_vif(w, NULL, &packet->vif);
        pad(w, 0x10);
        entry.vif_list_size = (uint16_t)((w->size - vif_list) / 0x10);

        pad(w, 0x10);
        vertex_header = w->size;
        set_u32(header, vertex_count);
        set_u32(header + 4, packet->unknown_4);
        set_u32(header + 8, packet->unknown_8);
        set_u32(header + 0xc, packet->unknown_c);
        append(w, header, sizeof header);
        append(w, packet->vertices, packet->vertex_count * VERTEX_SIZE);
        entry.vertex_offset = (uint32_t)vertex_header;
        entry.vertex_data_size = (uint8_t)((w->size - vertex_header) / 0x10);
        entry.unknown_d = (uint8_t)((0xf + vertex_count * 6) / 0x10);
        entry.unknown_e = (uint8_t)((3 + vertex_count) / 4);
        entry.transfer_vertex_count = (uint8_t)vertex_count;
        put_entry(w, table_offset, &entry);
        table_offset += 0x10;
    }
}

/* ---------------------------------------------------------------------------------------------- sequences */

/* A special frame's fifth part: COUNT flag bytes, each followed by 0..2 bytes for each of its three 2-bit fields
 * (3 counts as 0). */
static void read_fifth_part(struct reader *r, int64_t *offset, int count, struct moby_bytes *part)
{
    int64_t start = *offset;

    /* wrench appends each byte as it reads it: the bytes are contiguous, so read them in one span */
    for (int i = 0; i < count; i++) {
        uint8_t flags = read_u8(r, (*offset)++, "special anim data flag");
        int sizes[3] = {flags & 3, flags >> 2 & 3, flags >> 4 & 3};

        for (int k = 0; k < 3; k++) {
            int size = sizes[k] == 3 ? 0 : sizes[k];

            for (int j = 0; j < size; j++)
                read_u8(r, (*offset)++, "special anim data fifth part");
        }
        if (r->failed)
            return;
    }
    *part = read_bytes(r, start, *offset - start, 1, "special anim data fifth part");
}

/* The sequence at SEQUENCE_OFFSET: its header, triggers and frames, regular or special (any frame pointer with a
 * high nibble makes them all special), and its trigger data. */
static void read_sequence(struct reader *r, struct moby_sequence *sequence, int64_t sequence_offset,
                          int32_t joint_count, enum moby_game game)
{
    const uint8_t *header = span(r, sequence_offset, 1, 0x1c, "moby sequence header");
    const uint8_t *frame_table;
    int64_t after_frames, after_triggers;
    uint8_t frame_count, trigger_count;
    uint32_t triggers;

    if (!header)
        return;
    sequence->present = 1;
    memcpy(sequence->bounding_sphere, header, 16);
    frame_count = header[0x10];
    sequence->sound_count = header[0x11];
    trigger_count = header[0x12];
    sequence->unknown_13 = header[0x13];
    triggers = get_u32(header + 0x14);
    sequence->animation_info = get_u32(header + 0x18);

    frame_table = span(r, sequence_offset + 0x1c, frame_count, 4, "moby sequence table");
    if (!frame_table)
        return;
    for (int i = 0; i < frame_count; i++) {
        if (get_u32(frame_table + i * 4) & 0xf0000000u)
            sequence->has_special_data = 1;
    }
    after_frames = sequence_offset + 0x1c + frame_count * 4;
    sequence->triggers = read_bytes(r, after_frames, trigger_count, 4, "moby sequence trigger list");
    after_triggers = after_frames + trigger_count * 4;
    sequence->frames = alloc_items(r, frame_count, sizeof *sequence->frames);
    if (r->failed)
        return;
    sequence->frame_count = frame_count;

    if (!sequence->has_special_data) {
        for (int i = 0; i < frame_count; i++) {
            struct moby_frame *frame = &sequence->frames[i];
            int64_t frame_offset = get_u32(frame_table + i * 4) & 0x0fffffffu;
            const uint8_t *frame_header = span(r, frame_offset, 1, 0x10, "moby frame header");
            uint16_t thing_1_count, thing_2_count;
            int64_t data;

            if (!frame_header)
                return;
            frame->unknown_0 = get_u32(frame_header);
            frame->unknown_4 = get_u16(frame_header + 4);
            frame->unknown_c = get_u16(frame_header + 0xc);
            thing_1_count = get_u16(frame_header + 0xa);
            thing_2_count = get_u16(frame_header + 0xe);
            data = frame_offset + 0x10;
            frame->joint_data = read_bytes(r, data, joint_count, 8, "frame joint data");
            data += joint_count * 8;
            frame->thing_1 = read_bytes(r, data, thing_1_count, 8, "frame thing 1");
            data += thing_1_count * 8;
            frame->thing_2 = read_bytes(r, data, thing_2_count, 8, "frame thing 2");
        }
    } else {
        uint32_t packed = read_u32(r, after_triggers, "special anim data offsets");
        uint32_t second_offset = 4 + (packed & 0x3ff);
        uint32_t third_offset = 4 + (packed >> 10 & 0x7ff);
        uint32_t fourth_offset = 4 + (packed >> 21);
        int64_t things = after_triggers + 4 + joint_count * 6;
        uint8_t thing_1_count, thing_2_count;
        int32_t fourth_size = (joint_count + 7) / 8;

        sequence->special_joint_data = read_bytes(r, after_triggers + 4, joint_count * 3, 2, "special joint data");
        thing_1_count = read_u8(r, things, "special anim data thing 1 count");
        thing_2_count = read_u8(r, things + 1, "special anim data thing 2 count");
        sequence->special_thing_1 = read_bytes(r, things + 2, thing_1_count, 8, "special anim data thing 1");
        sequence->special_thing_2 = read_bytes(r, things + 2 + thing_1_count * 8, thing_2_count, 8,
                                               "special anim data thing 2");
        for (int i = 0; i < frame_count; i++) {
            struct moby_frame *frame = &sequence->frames[i];
            int64_t frame_offset = get_u32(frame_table + i * 4) & 0x0fffffffu;
            int64_t offset;

            frame->inverse_unknown_0 = read_u16(r, frame_offset, "special anim data unknown 0");
            frame->special_unknown_4 = read_u16(r, frame_offset + 2, "special anim data unknown 1");
            frame->first_part = read_bytes(r, frame_offset + 4, (int64_t)(second_offset - 4), 1,
                                           "special anim data first part");
            /* the sizes are unsigned differences, as in wrench: a negative one wraps and fails the read */
            frame->second_part = read_bytes(r, frame_offset + second_offset, (int64_t)(uint32_t)(third_offset -
                                            second_offset), 1, "special anim data second part");
            frame->third_part = read_bytes(r, frame_offset + third_offset, (int64_t)(uint32_t)(fourth_offset -
                                           third_offset), 1, "special anim data third part");
            frame->fourth_part = read_bytes(r, frame_offset + fourth_offset, fourth_size, 1,
                                            "special anim data fourth part");
            offset = frame_offset + fourth_offset + fourth_size;
            read_fifth_part(r, &offset, thing_1_count, &frame->fifth_part_1);
            read_fifth_part(r, &offset, thing_2_count, &frame->fifth_part_2);
            if (r->failed)
                return;
        }
    }

    if (triggers != 0) {
        /* R&C1 counts the trigger data from the class, R&C2 from the sequence */
        int64_t at = game == MOBY_RAC ? (int64_t)triggers : sequence_offset + triggers;
        const uint8_t *data = span(r, at, 1, 32, "moby sequence trigger data");

        if (!data)
            return;
        sequence->has_trigger_data = 1;
        memcpy(sequence->trigger_data, data, 32);
    }
}

/* SEQUENCE in the output, its header filled in last. Returns the sequence's offset. */
static size_t write_sequence(struct writer *w, const struct moby_sequence *sequence, int32_t joint_count,
                             enum moby_game game)
{
    uint8_t header[0x1c] = {0};
    size_t header_offset, frame_pointers;

    pad(w, 0x10);
    header_offset = grow(w, sizeof header, 0);
    memcpy(header, sequence->bounding_sphere, 16);
    header[0x10] = (uint8_t)sequence->frame_count;
    header[0x11] = sequence->sound_count;
    header[0x12] = (uint8_t)(sequence->triggers.size / 4);
    header[0x13] = sequence->unknown_13;
    frame_pointers = grow(w, sequence->frame_count * 4, 0);
    append(w, sequence->triggers.data, sequence->triggers.size);

    if (sequence->has_special_data) {
        uint32_t second_offset = 0, third_offset, fourth_offset;
        size_t thing_1_count = sequence->special_thing_1.size / 8;
        size_t thing_2_count = sequence->special_thing_2.size / 8;

        if (sequence->frame_count) {
            /* wrench sizes the parts from the first frame */
            const struct moby_frame *frame = &sequence->frames[0];

            second_offset = (uint32_t)frame->first_part.size;
            third_offset = second_offset + (uint32_t)frame->second_part.size;
            fourth_offset = third_offset + (uint32_t)frame->third_part.size;
        } else {
            third_offset = 0;
            fourth_offset = 0;
        }
        if (second_offset > 0x3ff || third_offset > 0x7ff || fourth_offset > 0x7ff) {
            write_fail(w, "an animation frame is too big");
            return header_offset;
        }
        append_u32(w, second_offset | third_offset << 10 | fourth_offset << 21);
        pad(w, 2);
        append(w, sequence->special_joint_data.data, sequence->special_joint_data.size);
        if (thing_1_count >= 256 || thing_2_count >= 256) {
            write_fail(w, "an animation frame is too big");
            return header_offset;
        }
        append_u8(w, (uint8_t)thing_1_count);
        append_u8(w, (uint8_t)thing_2_count);
        append(w, sequence->special_thing_1.data, sequence->special_thing_1.size);
        append(w, sequence->special_thing_2.data, sequence->special_thing_2.size);
    }

    if (sequence->has_trigger_data) {
        size_t trigger_data;

        if (game == MOBY_RAC)
            pad(w, 0x10);
        trigger_data = append(w, sequence->trigger_data, 32);
        set_u32(header + 0x14, (uint32_t)(game == MOBY_RAC ? trigger_data : trigger_data - header_offset));
    }
    set_u32(header + 0x18, sequence->animation_info);

    for (size_t i = 0; i < sequence->frame_count; i++) {
        const struct moby_frame *frame = &sequence->frames[i];

        if (!sequence->has_special_data) {
            size_t thing_1_count = frame->thing_1.size / 8;
            size_t thing_2_count = frame->thing_2.size / 8;
            size_t data_size = ((size_t)joint_count + thing_1_count + thing_2_count) * 8;
            uint8_t frame_header[0x10];
            size_t at;

            data_size = (data_size + 0xf) & ~(size_t)0xf;
            if (data_size / 0x10 >= 65536 || thing_1_count >= 65536 || thing_2_count >= 65536) {
                write_fail(w, "a frame's data is too big");
                return header_offset;
            }
            set_u32(frame_header, frame->unknown_0);
            set_u16(frame_header + 4, frame->unknown_4);
            set_u16(frame_header + 6, (uint16_t)(data_size / 0x10));
            set_u16(frame_header + 8, (uint16_t)(joint_count * 8));
            set_u16(frame_header + 0xa, (uint16_t)thing_1_count);
            set_u16(frame_header + 0xc, frame->unknown_c);
            set_u16(frame_header + 0xe, (uint16_t)thing_2_count);
            pad(w, 0x10);
            at = append(w, frame_header, sizeof frame_header);
            put_u32(w, frame_pointers, (uint32_t)at);
            append(w, frame->joint_data.data, frame->joint_data.size);
            append(w, frame->thing_1.data, frame->thing_1.size);
            append(w, frame->thing_2.data, frame->thing_2.size);
        } else {
            pad(w, 4);
            put_u32(w, frame_pointers, (uint32_t)w->size | 0xf0000000u);
            append_u16(w, frame->inverse_unknown_0);
            append_u16(w, frame->special_unknown_4);
            append(w, frame->first_part.data, frame->first_part.size);
            append(w, frame->second_part.data, frame->second_part.size);
            append(w, frame->third_part.data, frame->third_part.size);
            append(w, frame->fourth_part.data, frame->fourth_part.size);
            append(w, frame->fifth_part_1.data, frame->fifth_part_1.size);
            append(w, frame->fifth_part_2.data, frame->fifth_part_2.size);
        }
        frame_pointers += 4;
    }
    put(w, header_offset, header, sizeof header);
    return header_offset;
}

/* ------------------------------------------------------------------------------------ the class's other parts */

/* The bangles at BANGLES: 15 index entries (high LOD and low LOD packet ranges in the packet table), each used one
 * with its two vectors. */
static void read_bangles(struct reader *r, struct moby_class *moby, int64_t bangles, int64_t packet_table, int rac1)
{
    const uint8_t *indices = span(r, bangles + 4, 15, 4, "bangle indices");

    if (!indices)
        return;
    moby->bangles = alloc_items(r, 15, sizeof *moby->bangles);
    for (int i = 0; i < 15 && !r->failed; i++) {
        struct moby_bangle *bangle = &moby->bangles[moby->bangle_count];
        const uint8_t *index = indices + i * 4;

        memset(bangle, 0, sizeof *bangle);
        if (index[0] != 0) {
            bangle->high_lod = read_packets(r, packet_table + index[0] * 0x10, index[1], rac1);
            bangle->high_count = index[1];
        }
        if (index[2] != 0) {
            bangle->low_lod = read_packets(r, packet_table + index[2] * 0x10, index[3], rac1);
            bangle->low_count = index[3];
        }
        if (bangle->high_count || bangle->low_count) {
            const uint8_t *vectors = span(r, bangles + 64 + i * 16, 1, 16, "bangle vectors");

            if (!vectors)
                return;
            memcpy(bangle->vectors, vectors, 16);
            moby->bangle_count++;
        }
    }
}

/* The bangles' packets, and the bangles' index entries and vectors at BANGLES. PACKET is the first table entry
 * after the metal packets'. */
static void write_bangles(struct writer *w, struct gif_usage *gif, size_t bangles, size_t packet_table, int packet,
                          const struct moby_class *moby, int rac1)
{
    size_t count = moby->bangle_count;
    size_t area_size = 64 + count * 16;
    uint8_t *area = calloc(1, area_size);
    uint8_t *indices = calloc(count, 4);

    if (!area || !indices) {
        free(area);
        free(indices);
        write_fail(w, "out of memory");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        const struct moby_bangle *bangle = &moby->bangles[i];
        uint8_t *index = indices + i * 4;

        index[0] = (uint8_t)packet;
        index[1] = (uint8_t)bangle->high_count;
        write_packets(w, gif, packet_table + (size_t)packet * 16, bangle->high_lod, bangle->high_count, rac1);
        packet += (int)bangle->high_count;
        if (bangle->low_count) {
            if (packet >= 256 || bangle->low_count >= 256) {
                write_fail(w, "the bangles have too many packets");
                break;
            }
            index[2] = (uint8_t)packet;
            index[3] = (uint8_t)bangle->low_count;
            write_packets(w, gif, packet_table + (size_t)packet * 16, bangle->low_lod, bangle->low_count, rac1);
            packet += (int)bangle->low_count;
        }
    }
    /* the header (zero), the indices after it, then the vectors at +64, in wrench's order */
    memcpy(area + 4, indices, count * 4);
    for (size_t i = 0; i < count; i++)
        memcpy(area + 64 + i * 16, moby->bangles[i].vectors, 16);
    put(w, bangles, area, area_size);
    free(area);
    free(indices);
}

/* The corncob at CORNCOB (R&C2): 16 kernel offsets in quadwords (0xff for none), each kernel a vector and, when
 * the vector is not zero, its vertices. */
static void read_corncob(struct reader *r, struct moby_class *moby, int64_t corncob)
{
    const uint8_t *header = span(r, corncob, 1, 16, "moby corncob");

    if (!header)
        return;
    moby->has_corncob = 1;
    for (int i = 0; i < 16; i++) {
        struct moby_corn_kernel *kernel = &moby->kernels[i];
        int64_t kernel_offset = corncob + header[i] * 0x10;
        const uint8_t *vec;

        if (header[i] == 0xff)
            continue;
        vec = span(r, kernel_offset, 1, 16, "corn vec4");
        if (!vec)
            return;
        kernel->present = 1;
        memcpy(kernel->vec, vec, 16);
        if (get_u32(vec) || get_u32(vec + 4) || get_u32(vec + 8) || get_u32(vec + 12)) {
            int16_t count = (int16_t)read_u16(r, kernel_offset + 0x16, "corn vertex count");

            kernel->vertices = read_bytes(r, kernel_offset + 0x10, count, 8, "corn vertices");
        }
        if (r->failed)
            return;
    }
}

/* The corncob in the output. Returns its offset. */
static size_t write_corncob(struct writer *w, const struct moby_class *moby)
{
    size_t header_offset = grow(w, 16, 0);
    uint8_t header[16];

    for (int i = 0; i < 16; i++) {
        const struct moby_corn_kernel *kernel = &moby->kernels[i];
        size_t kernel_offset;

        if (!kernel->present) {
            header[i] = 0xff;
            continue;
        }
        pad(w, 0x10);
        kernel_offset = append(w, kernel->vec, 16);
        append(w, kernel->vertices.data, kernel->vertices.size);
        if (kernel->vertices.size)
            put_u16(w, kernel_offset + 0x16, (uint16_t)(kernel->vertices.size / 8));
        header[i] = (uint8_t)((kernel_offset - header_offset) / 0x10);
    }
    put(w, header_offset, header, sizeof header);
    return header_offset;
}

/* The collision at OFFSET: a 16-byte header with the three parts' sizes, then the parts. */
static void read_collision(struct reader *r, struct moby_collision *collision, int64_t offset)
{
    const uint8_t *header = span(r, offset, 1, 16, "moby collision header");
    int32_t first_size, third_size, second_size;
    const uint8_t *second;
    int64_t at;

    if (!header)
        return;
    collision->present = 1;
    collision->unknown_0 = get_u16(header);
    collision->unknown_2 = get_u16(header + 2);
    first_size = (int32_t)get_u32(header + 4);
    third_size = (int32_t)get_u32(header + 8);
    second_size = (int32_t)get_u32(header + 0xc);
    at = offset + 0x10;
    collision->first_part = read_bytes(r, at, first_size, 1, "moby collision data");
    at += first_size;
    if (second_size % 8 != 0) {
        read_fail(r, "bad moby collision");
        return;
    }
    second = span(r, at, second_size / 2, 2, "moby collision second part");
    if (!second)
        return;
    collision->second_part = alloc_items(r, (size_t)(second_size / 8) * 3, sizeof(float));
    if (r->failed)
        return;
    collision->second_count = (size_t)(second_size / 8);
    for (size_t i = 0; i < collision->second_count; i++) {
        for (int k = 0; k < 3; k++)
            collision->second_part[i * 3 + k] = (int16_t)get_u16(second + i * 8 + k * 2) / 1024.f;
    }
    at += second_size;
    collision->third_part = read_bytes(r, at, third_size, 1, "moby collision third part");
}

/* COLLISION in the output. Returns its offset. */
static size_t write_collision(struct writer *w, const struct moby_collision *collision)
{
    uint8_t header[16];
    size_t offset;

    set_u16(header, collision->unknown_0);
    set_u16(header + 2, collision->unknown_2);
    set_u32(header + 4, (uint32_t)collision->first_part.size);
    set_u32(header + 8, (uint32_t)collision->third_part.size);
    set_u32(header + 0xc, (uint32_t)(collision->second_count * 8));
    pad(w, 0x10);
    offset = append(w, header, sizeof header);
    append(w, collision->first_part.data, collision->first_part.size);
    for (size_t i = 0; i < collision->second_count; i++) {
        for (int k = 0; k < 3; k++)
            append_u16(w, (uint16_t)(int16_t)(collision->second_part[i * 3 + k] * 1024.f));
        append_u16(w, 0);
    }
    append(w, collision->third_part.data, collision->third_part.size);
    return offset;
}

/* The joint lists at OFFSET: a count, an offset per list, and each list's two counts, bytes and 0xff end. */
static void read_joints(struct reader *r, struct moby_class *moby, int64_t offset)
{
    int32_t count = (int32_t)read_u32(r, offset, "joint list count");

    if (r->failed || count <= 0)
        return;
    /* every list needs its own offset in the class, so a count past the class's end fails at the first missing one */
    if (!span(r, offset + 4, count, 4, "joint lists"))
        return;
    moby->joints = alloc_items(r, (size_t)count, sizeof *moby->joints);
    for (int32_t i = 0; i < count && !r->failed; i++) {
        struct moby_joint_list *list = &moby->joints[i];
        int64_t at = (int32_t)read_u32(r, offset + (int64_t)(i + 1) * 4, "joint list");
        int16_t one = (int16_t)read_u16(r, at, "joint count 1");
        int16_t two = (int16_t)read_u16(r, at + 2, "joint count 2");

        at += 4;
        list->thing_one = read_bytes(r, at, one, 1, "joint thing ones");
        at += one;
        list->thing_two = read_bytes(r, at, two, 1, "joint thing twos");
        at += two;
        if (read_u8(r, at, "joint list terminator") != 0xff)
            read_fail(r, "bad joint data");
        moby->joint_list_count++;
    }
}

/* The joint lists in the output. Returns their offset. */
static size_t write_joints(struct writer *w, const struct moby_class *moby)
{
    size_t base, lists;

    pad(w, 0x10);
    base = append_u32(w, (uint32_t)moby->joint_list_count);
    lists = grow(w, moby->joint_list_count * 4, 0);
    for (size_t i = 0; i < moby->joint_list_count; i++) {
        const struct moby_joint_list *list = &moby->joints[i];

        pad(w, 4);
        put_u32(w, lists + i * 4, (uint32_t)w->size);
        append_u16(w, (uint16_t)list->thing_one.size);
        append_u16(w, (uint16_t)list->thing_two.size);
        append(w, list->thing_one.data, list->thing_one.size);
        append(w, list->thing_two.data, list->thing_two.size);
        append_u8(w, 0xff);
    }
    return base;
}

/* -------------------------------------------------------------------------------------------------- class */

static int format_is_rac1(enum moby_game game, int force_rac1_format)
{
    return game == MOBY_RAC || force_rac1_format;
}

struct moby_class *moby_read(const uint8_t *data, size_t size, enum moby_game game, char *error, size_t error_size)
{
    struct moby_class *moby = calloc(1, sizeof *moby);
    struct reader reader = {data, (int64_t)size, moby, error, error_size, 0};
    struct reader *r = &reader;
    const uint8_t *header;
    const uint8_t *sequence_offsets;
    uint8_t sequence_count, sound_count, shadow, bangles;
    int32_t collision, skeleton, common_trans, joints, sound_defs, packet_table;
    int16_t corncob;
    int rac1;

    if (!moby) {
        read_fail(r, "out of memory");
        return NULL;
    }
    header = span(r, 0, 1, CLASS_HEADER, "moby class header");
    if (!header) {
        moby_free(moby);
        return NULL;
    }
    packet_table = (int32_t)get_u32(header);
    moby->joint_count = header[8];
    moby->unknown_9 = header[9];
    moby->rac1_byte_a = header[0xa];
    moby->rac1_byte_b = header[0xb];
    sequence_count = header[0xc];
    sound_count = header[0xd];
    moby->lod_trans = header[0xe];
    shadow = header[0xf];
    collision = (int32_t)get_u32(header + 0x10);
    skeleton = (int32_t)get_u32(header + 0x14);
    common_trans = (int32_t)get_u32(header + 0x18);
    joints = (int32_t)get_u32(header + 0x1c);
    moby->scale = get_u32(header + 0x24);
    sound_defs = (int32_t)get_u32(header + 0x28);
    bangles = header[0x2c];
    moby->mip_dist = header[0x2d];
    corncob = (int16_t)get_u16(header + 0x2e);
    memcpy(moby->bounding_sphere, header + 0x30, 16);
    moby->glow_rgba = (int32_t)get_u32(header + 0x40);
    moby->mode_bits = (int16_t)get_u16(header + 0x44);
    moby->type = header[0x46];
    moby->mode_bits2 = header[0x47];
    if (game == MOBY_GC && moby->rac1_byte_b != 0)
        moby->force_rac1_format = 1;
    rac1 = format_is_rac1(game, moby->force_rac1_format);

    /* the header ends at the first sequence, or before it at the bangles or the corncob */
    moby->header_end_offset = CLASS_HEADER;
    sequence_offsets = span(r, CLASS_HEADER, sequence_count, 4, "sequence offsets");
    for (int i = 0; sequence_offsets && i < sequence_count; i++) {
        int32_t offset = (int32_t)get_u32(sequence_offsets + i * 4);

        if (offset != 0) {
            moby->header_end_offset = offset;
            break;
        }
    }
    if (bangles != 0 && bangles * 0x10 < moby->header_end_offset)
        moby->header_end_offset = bangles * 0x10;
    if (game == MOBY_RAC) {
        moby->rac1_short_2e = (uint16_t)corncob;
    } else if (corncob != 0) {
        read_corncob(r, moby, corncob * 0x10);
        if (corncob * 0x10 < moby->header_end_offset)
            moby->header_end_offset = corncob * 0x10;
    }

    moby->sequences = alloc_items(r, sequence_count, sizeof *moby->sequences);
    if (!r->failed)
        moby->sequence_count = sequence_count;
    for (int i = 0; i < sequence_count && !r->failed; i++) {
        int32_t offset = (int32_t)get_u32(sequence_offsets + i * 4);

        if (offset != 0)
            read_sequence(r, &moby->sequences[i], offset, moby->joint_count, game);
    }
    if (collision != 0)
        read_collision(r, &moby->collision, collision);
    if (skeleton != 0) {
        moby->shadow = read_bytes(r, (int64_t)skeleton - shadow * 16, shadow * 16, 1, "shadow");
        moby->skeleton = read_bytes(r, skeleton, moby->joint_count, 0x40, "skeleton");
        moby->has_skeleton = 1;
    }
    if (common_trans != 0) {
        moby->common_trans = read_bytes(r, common_trans, moby->joint_count, 0x10, "skeleton trans");
        moby->has_common_trans = 1;
    }
    read_joints(r, moby, joints);
    moby->sound_defs = read_bytes(r, sound_defs, sound_count, 0x20, "moby sound defs");
    if (packet_table != 0) {
        uint8_t high = header[4], low = header[5], metal = header[6], metal_begin = header[7];

        moby->packet_table_offset = packet_table;
        moby->high_lod = read_packets(r, packet_table, high, rac1);
        moby->high_count = r->failed ? 0 : high;
        moby->low_lod = read_packets(r, (int64_t)packet_table + high * 0x10, low, rac1);
        moby->low_count = r->failed ? 0 : low;
        moby->metal = read_metal_packets(r, (int64_t)packet_table + metal_begin * 0x10, metal);
        moby->metal_count = r->failed ? 0 : metal;
        moby->has_packet_table = 1;
        if (bangles != 0)
            read_bangles(r, moby, bangles * 0x10, packet_table, rac1);
    }
    if (r->failed) {
        moby_free(moby);
        return NULL;
    }
    return moby;
}

uint8_t *moby_write(const struct moby_class *moby, enum moby_game game, size_t *size, char *error, size_t error_size)
{
    struct writer writer = {NULL, 0, 0, 0, error, error_size};
    struct writer *w = &writer;
    struct gif_usage gif = {{NULL, 0, 0, 0, NULL, 0}};
    uint8_t header[CLASS_HEADER] = {0};
    int rac1 = format_is_rac1(game, moby->force_rac1_format);
    size_t sequence_list, bangles = 0, packet_table, entries;
    uint8_t high = (uint8_t)moby->high_count, low = (uint8_t)moby->low_count, metal = (uint8_t)moby->metal_count;

    *size = 0;
    if (moby->joint_count > 0x6f)
        write_fail(w, "the class has more than 0x6f joints");
    if (moby->sound_defs.size / 0x20 >= 256)
        write_fail(w, "the class has too many sounds");
    if (moby->sequence_count >= 256)
        write_fail(w, "the class has too many sequences (max is 255)");
    if (moby->high_count >= 256 || moby->low_count >= 256 || moby->metal_count >= 256)
        write_fail(w, "the class has too many packets");
    if (moby->has_skeleton && moby->skeleton.size / 0x40 >= 255)
        write_fail(w, "the class has too many joints");
    grow(w, CLASS_HEADER, 0);

    if (rac1) {
        header[0xa] = moby->rac1_byte_a;
        header[0xb] = moby->rac1_byte_b;
    }
    header[8] = moby->joint_count;
    header[9] = moby->unknown_9;
    header[0xe] = moby->lod_trans;
    header[0xf] = (uint8_t)(moby->shadow.size / 16);
    set_u32(header + 0x24, moby->scale);
    header[0xd] = (uint8_t)(moby->sound_defs.size / 0x20);
    header[0x2d] = moby->mip_dist;
    memcpy(header + 0x30, moby->bounding_sphere, 16);
    set_u32(header + 0x40, (uint32_t)moby->glow_rgba);
    set_u16(header + 0x44, (uint16_t)moby->mode_bits);
    header[0x46] = moby->type;
    header[0x47] = moby->mode_bits2;
    header[0xc] = (uint8_t)moby->sequence_count;

    sequence_list = grow(w, moby->sequence_count * 4, 0);
    pad_to(w, moby->header_end_offset > 0 ? (size_t)moby->header_end_offset : 0);
    if (moby->bangle_count) {
        pad(w, 0x10);
        bangles = grow(w, 4 + 15 * 4 + moby->bangle_count * 16, 0);
        header[0x2c] = (uint8_t)(bangles / 0x10);
    }
    if (game == MOBY_RAC) {
        set_u16(header + 0x2e, moby->rac1_short_2e);
    } else if (moby->has_corncob) {
        pad(w, 0x10);
        set_u16(header + 0x2e, (uint16_t)(write_corncob(w, moby) / 0x10));
    }
    pad(w, 0x10);
    for (size_t i = 0; i < moby->sequence_count && !w->failed; i++) {
        uint32_t offset = 0;

        if (moby->sequences[i].present)
            offset = (uint32_t)write_sequence(w, &moby->sequences[i], moby->joint_count, game);
        put_u32(w, sequence_list + i * 4, offset);
    }
    pad(w, 0x10);
    pad_to(w, moby->packet_table_offset > 0 ? (size_t)moby->packet_table_offset : 0);
    entries = moby->high_count + moby->low_count + moby->metal_count + moby->bangle_count;
    packet_table = grow(w, entries * 16, 0);
    if (moby->bangle_count)
        grow(w, moby->bangle_count * 16, 0);
    if (moby->has_packet_table)
        set_u32(header, (uint32_t)packet_table);
    if (moby->collision.present)
        set_u32(header + 0x10, (uint32_t)write_collision(w, &moby->collision));
    if (moby->has_skeleton) {
        pad(w, 0x10);
        append(w, moby->shadow.data, moby->shadow.size);
        set_u32(header + 0x14, (uint32_t)w->size);
        append(w, moby->skeleton.data, moby->skeleton.size);
    }
    if (moby->has_common_trans) {
        pad(w, 0x10);
        set_u32(header + 0x18, (uint32_t)append(w, moby->common_trans.data, moby->common_trans.size));
    }
    set_u32(header + 0x1c, (uint32_t)write_joints(w, moby));
    if (moby->sound_defs.size) {
        pad(w, 0x10);
        set_u32(header + 0x28, (uint32_t)append(w, moby->sound_defs.data, moby->sound_defs.size));
    }

    /* the mesh: high LOD, low LOD and metal packets in the table's order, then the bangles' */
    header[4] = high;
    header[5] = low;
    header[6] = metal;
    header[7] = (uint8_t)(high + low);
    write_packets(w, &gif, packet_table, moby->high_lod, moby->high_count, rac1);
    write_packets(w, &gif, packet_table + moby->high_count * 16, moby->low_lod, moby->low_count, rac1);
    write_metal_packets(w, packet_table + (moby->high_count + moby->low_count) * 16, moby->metal, moby->metal_count);
    if (moby->bangle_count)
        write_bangles(w, &gif, bangles, packet_table, header[7] + metal, moby, rac1);
    if (gif.list.size && !w->failed) {
        gif.list.data[gif.list.size - 1] |= 0x80;     /* the last entry's terminator bit */
        set_u32(header + 0x20, (uint32_t)append(w, gif.list.data, gif.list.size));
    }
    free(gif.list.data);
    put(w, 0, header, sizeof header);
    if (w->failed) {
        free(w->data);
        return NULL;
    }
    *size = w->size;
    return w->data;
}

/* ------------------------------------------------------------------------------------------------- rebind */

#define REBIND_NO_ADDRESS (-1)
#define REBIND_NO_MEMORY (-2)

/* A VU0 address above every one the packets' transfers and vertices use, or REBIND_NO_ADDRESS when none is left. */
static int unused_vu0_address(const struct moby_packet *packets, size_t count)
{
    int top = 0;

    for (size_t p = 0; p < count; p++) {
        const struct moby_vertex_table *table = &packets[p].table;

        for (size_t i = 0; i < table->transfer_count; i++) {
            if (table->transfers[i * 2 + 1] > top)
                top = table->transfers[i * 2 + 1];
        }
        for (size_t v = 0; v < table->vertex_count; v++) {
            const uint8_t *vertex = table->vertices + v * VERTEX_SIZE;

            for (int i = 2; i < 8; i++) {
                if (vertex[i] != VU0_UNUSED && (i < 4 || i >= 6) && vertex[i] > top)
                    top = vertex[i];
            }
        }
    }
    return top + 4 < VU0_UNUSED ? top + 4 : REBIND_NO_ADDRESS;
}

/* The weight of TO for a vertex at height Z between Z_TOP (none) and Z_BOTTOM (all), in 1..255. */
static int weight_to(int z, int z_top, int z_bottom)
{
    float t = (float)(z_top - z) / (float)(z_top - z_bottom);
    int weight;

    t = t < 0.f ? 0.f : 1.f < t ? 1.f : t;
    weight = (int)roundf(t * 256.f);
    return weight < 1 ? 1 : 255 < weight ? 255 : weight;
}

/* moby_rebind for COUNT regular packets: how many vertices changed, or REBIND_NO_ADDRESS or REBIND_NO_MEMORY. VU0's
 * matrices are followed through the packets: which joint each address holds, -1 for a blend. */
static int rebind_packets(struct moby_class *moby, struct moby_packet *packets, size_t count, int from, int to,
                          int z_top, int z_bottom)
{
    int spare = unused_vu0_address(packets, count);
    int z_mid = (z_top + z_bottom) / 2;
    int changed = 0;
    int vu0[256];

    if (spare < 0)
        return REBIND_NO_ADDRESS;
    for (int i = 0; i < 256; i++)
        vu0[i] = -1;
    for (size_t p = 0; p < count; p++) {
        struct moby_vertex_table *table = &packets[p].table;
        uint8_t *transfers = moby_alloc(moby, (table->transfer_count + 1) * 2);
        size_t two, three;

        if (!transfers)
            return REBIND_NO_MEMORY;
        for (size_t i = 0; i < table->transfer_count; i++)
            vu0[table->transfers[i * 2 + 1]] = table->transfers[i * 2];
        if (table->transfer_count)
            memcpy(transfers, table->transfers, table->transfer_count * 2);
        transfers[table->transfer_count * 2] = (uint8_t)to;
        transfers[table->transfer_count * 2 + 1] = (uint8_t)spare;
        table->transfers = transfers;
        table->transfer_count++;
        vu0[spare] = to;
        two = (size_t)table->two_way_count;
        three = two + (size_t)table->three_way_count;
        for (size_t i = 0; i < table->vertex_count; i++) {
            uint8_t *vertex = table->vertices + i * VERTEX_SIZE;
            uint16_t low_halfword = get_u16(vertex);
            int scratchpad_joint = low_halfword >> 9;
            int z = (int16_t)get_u16(vertex + 0xe);

            if (i < two) {
                /* loads bytes 2 and 3, weights 4 and 5, transfer store 6, blend store 7 */
                int j1, j2;

                if (vertex[6] != VU0_UNUSED)
                    vu0[vertex[6]] = scratchpad_joint;
                j1 = vu0[vertex[2]];
                j2 = vu0[vertex[3]];
                if ((j1 == from && j2 == to) || (j1 == to && j2 == from)) {
                    int weight = weight_to(z, z_top, z_bottom);

                    vertex[4] = (uint8_t)(j1 == to ? weight : 256 - weight);
                    vertex[5] = (uint8_t)(256 - vertex[4]);
                    changed++;
                } else if (z < z_mid && (j1 == from || j2 == from)) {
                    vertex[j1 == from ? 2 : 3] = (uint8_t)spare;
                    changed++;
                }
                if (vertex[7] != VU0_UNUSED)
                    vu0[vertex[7]] = -1;
            } else if (i < three) {
                /* loads bytes 2 and 3 and the high bits of the low halfword times two, blend store 7 */
                if (z < z_mid) {
                    if (vu0[vertex[2]] == from) {
                        vertex[2] = (uint8_t)spare;
                        changed++;
                    }
                    if (vu0[vertex[3]] == from) {
                        vertex[3] = (uint8_t)spare;
                        changed++;
                    }
                    if (vu0[scratchpad_joint * 2] == from) {
                        set_u16(vertex, (uint16_t)((low_halfword & 0x1ff) | (spare / 2) << 9));
                        changed++;
                    }
                }
                if (vertex[7] != VU0_UNUSED)
                    vu0[vertex[7]] = -1;
            } else {
                /* transfer store byte 3 (done first), load 2 */
                if (vertex[3] != VU0_UNUSED)
                    vu0[vertex[3]] = scratchpad_joint;
                if (z < z_mid && vu0[vertex[2]] == from) {
                    vertex[2] = (uint8_t)spare;
                    changed++;
                }
            }
        }
    }
    return changed;
}

/* moby_rebind for the metal packets: how many vertices changed. Metal (multipass shine) vertices name their joints
 * directly: joints in bytes 8..a, joint count in byte b, weights in bytes c..e. */
static int rebind_metal_packets(struct moby_class *moby, int from, int to, int z_top, int z_bottom)
{
    int z_mid = (z_top + z_bottom) / 2;
    int changed = 0;

    for (size_t p = 0; p < moby->metal_count; p++) {
        struct moby_metal_packet *packet = &moby->metal[p];

        for (size_t v = 0; v < packet->vertex_count; v++) {
            uint8_t *vertex = packet->vertices + v * VERTEX_SIZE;
            uint8_t *joints = vertex + 8;
            uint8_t *weights = vertex + 0xc;
            uint8_t count = vertex[0xb];
            int z = (int16_t)get_u16(vertex + 4);

            if (count == 2 && ((joints[0] == from && joints[1] == to) || (joints[0] == to && joints[1] == from))) {
                int weight = weight_to(z, z_top, z_bottom);

                weights[0] = (uint8_t)(joints[0] == to ? weight : 256 - weight);
                weights[1] = (uint8_t)(256 - weights[0]);
                changed++;
            } else if (z < z_mid) {
                int has_to = 0;

                for (int i = 0; i < count && i < 3; i++)
                    has_to |= joints[i] == to;
                for (int i = 0; i < count && i < 3 && !has_to; i++) {
                    if (joints[i] == from) {
                        joints[i] = (uint8_t)to;
                        changed++;
                    }
                }
            }
        }
    }
    return changed;
}

int moby_rebind(struct moby_class *moby, int from, int to, int z_top, int z_bottom, struct moby_rebind_counts *counts,
                char *error, size_t error_size)
{
    int failure;

    counts->high = rebind_packets(moby, moby->high_lod, moby->high_count, from, to, z_top, z_bottom);
    counts->low = counts->high < 0 ? counts->high
                                   : rebind_packets(moby, moby->low_lod, moby->low_count, from, to, z_top, z_bottom);
    if (counts->low >= 0) {
        counts->metal = rebind_metal_packets(moby, from, to, z_top, z_bottom);
        return 0;
    }
    failure = counts->low;
    counts->high = counts->low = counts->metal = -1;
    if (error && error_size)
        snprintf(error, error_size, "%s", failure == REBIND_NO_MEMORY ? "out of memory" : "no free VU0 address");
    return -1;
}
