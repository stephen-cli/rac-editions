/*
 * The games' WAD compression (wad.h): a decompressor that reads a stream as the game does, and a compressor that
 * writes streams the game reads.
 *
 * The compressor works in chunks, each a job that may run beside the others (threads on a PC, the SPUs on a PS3):
 * it finds matches through the latest positions of each 3-byte sequence, takes the match that saves the most bytes,
 * and puts off a match for one byte when the next byte's saves more. One pass then lays the chunks' packets out in
 * the game's 0x2000-byte blocks, each ended by the block's end marker.
 */
#include "wad.h"

#include <stdlib.h>
#include <string.h>

#define WINDOW 0xBFFF               /* the farthest match: a far match's distance */
#define MEDIUM_REACH 16384          /* the farthest short or medium match */
#define SHORT_REACH 2048
#define MAX_LEN 288                 /* a medium match */
#define MAX_FAR_LEN 264
#define MIN_RUN 4                   /* a literal run */
#define MAX_RUN 273
#define MAX_FIRST_RUN 238           /* the literals a stream may begin with */
#define MARKER 3                    /* the block's end marker: 12 00 00 */
#define PAD 0xEE                    /* after the marker, as retail pads */


/* ------------------------------------------------------------------------------------------------ decompress */

const char *wad_error_text(enum wad_error error)
{
    switch (error) {
    case WAD_OK: return "ok";
    case WAD_NOT_WAD: return "not a WAD stream";
    case WAD_TRUNCATED: return "a packet runs past the stream's end";
    case WAD_TWO_RUNS: return "a literal run after literals";
    case WAD_BAD_MATCH: return "a match before the start of the output";
    case WAD_TOO_LARGE: return "more output than the buffer holds";
    case WAD_CROSSES_BLOCK: return "a packet crosses the end of its 0x2000-byte block";
    }
    return "unknown error";
}

enum wad_error wad_decompress(const uint8_t *src, size_t size, uint8_t *dst, size_t capacity, size_t *out_size)
{
    size_t end, pos = WAD_HEADER, n = 0;
    int run_allowed = 1, after_marker = 0;

    *out_size = 0;
    if (size < WAD_HEADER || memcmp(src, "WAD", 3) != 0)
        return WAD_NOT_WAD;
    end = (size_t)src[3] | (size_t)src[4] << 8 | (size_t)src[5] << 16 | (size_t)src[6] << 24;
    if (end < WAD_HEADER || end > size)
        return WAD_NOT_WAD;

    while (pos < end) {
        size_t block = (pos - WAD_HEADER) / WAD_BLOCK;
        size_t block_end = WAD_HEADER + (block + 1) * WAD_BLOCK;
        size_t limit = block_end < end ? block_end : end;
        enum wad_error past = block_end < end ? WAD_CROSSES_BLOCK : WAD_TRUNCATED;
        size_t start = pos, count, distance, trailing;
        unsigned tag;

        /* a block is left only by its end marker: the game reads on into the scratchpad's other half */
        if (pos == block_end - WAD_BLOCK && pos > WAD_HEADER && !after_marker)
            return WAD_CROSSES_BLOCK;
        after_marker = 0;
#define TAKE(k) do { if (pos + (k) > limit) return past; } while (0)

        TAKE(1);
        tag = src[pos++];
        if (start == WAD_HEADER && tag > 0x11) {
            count = tag - 0x11u;
            TAKE(count);
            if (n + count > capacity)
                return WAD_TOO_LARGE;
            memcpy(dst + n, src + pos, count);
            n += count;
            pos += count;
            run_allowed = 0;
            continue;
        }
        if (tag < 0x10) {
            if (!run_allowed)
                return WAD_TWO_RUNS;
            if (tag) {
                count = tag + 3u;
            } else {
                TAKE(1);
                count = src[pos++] + 18u;
            }
            TAKE(count);
            if (n + count > capacity)
                return WAD_TOO_LARGE;
            memcpy(dst + n, src + pos, count);
            n += count;
            pos += count;
            run_allowed = 0;
            continue;
        }
        if (tag >= 0x40) {
            TAKE(1);
            distance = 1 + ((tag >> 2) & 7) + src[pos++] * 8u;
            count = (tag >> 5) + 1u;
        } else if (tag >= 0x20) {
            count = tag & 31;
            if (!count) {
                TAKE(1);
                count = src[pos++] + 31u;
            }
            count += 2;
            TAKE(2);
            distance = 1 + (src[pos] >> 2) + src[pos + 1] * 64u;
            pos += 2;
        } else {
            count = tag & 7;
            if (!count) {
                TAKE(1);
                count = src[pos++] + 7u;
            }
            TAKE(2);
            distance = (tag & 8) * 2048u + (src[pos] >> 2) + src[pos + 1] * 64u;
            pos += 2;
            if (!distance) {
                if (count != 1) {           /* the block's end: on to the next block */
                    pos = block_end;
                    run_allowed = 1;
                    after_marker = 1;
                    continue;
                }
                count = 0;                  /* a no-op, which may carry literals */
            } else {
                distance += 0x4000;
                count += 2;
            }
        }
        if (distance > n && count)
            return WAD_BAD_MATCH;
        if (n + count > capacity)
            return WAD_TOO_LARGE;
        for (size_t k = 0; k < count; k++, n++)
            dst[n] = dst[n - distance];
        trailing = src[pos - 2] & 3u;
        TAKE(trailing);
        if (n + trailing > capacity)
            return WAD_TOO_LARGE;
        memcpy(dst + n, src + pos, trailing);
        n += trailing;
        pos += trailing;
        run_allowed = trailing == 0;
#undef TAKE
    }
    /* a stream that fills its last block exactly gives the game no end to stop at but a marker */
    if (end > WAD_HEADER && (end - WAD_HEADER) % WAD_BLOCK == 0 && !after_marker)
        return WAD_CROSSES_BLOCK;
    *out_size = n;
    return WAD_OK;
}

/* -------------------------------------------------------------------------------------------------- compress */

/* The input is compressed in chunks of WAD_CHUNK bytes, each a job of its own that reads the WINDOW bytes before
 * it as history, so a chunk finds the matches a single pass would. The chunks are fixed by the input alone, never
 * by how many jobs run at once, so every machine writes the same stream. A job's memory is fixed: the matcher
 * keeps, per hash of three bytes, the WAYS latest positions. The jobs' packets are then laid out in the game's
 * blocks in one pass, which ends each block with its marker and puts a no-op between two literal runs where one
 * chunk ends with literals and the next begins with them. */

#define BUCKET_BITS 11
#define WAYS 16
#define NONE 0xFFFFu                /* a position no job reaches: a job's input is at most 0xFFFF bytes */

size_t wad_bound(size_t size)
{
    /* literal runs cost 5 bytes in 273 at worst (a header and a no-op), a block's unused end under 300 bytes in
     * 0x2000, a chunk's join a no-op: under 1/16 and 1/4096 together */
    return WAD_HEADER + size + size / 16 + size / 4096 + 4096;
}

size_t wad_chunks(size_t size)
{
    return (size + WAD_CHUNK - 1) / WAD_CHUNK;
}

struct writer {
    uint8_t *out;
    size_t len;
    int run_allowed;
    int blocks;                     /* the final stream: packets laid out in blocks; else a chunk's packets */
};

static void put(struct writer *w, unsigned byte)
{
    w->out[w->len++] = (uint8_t)byte;
}

/* Room in the current block for a packet of UNIT bytes and a marker after it, or the block ended here. */
static void fit(struct writer *w, size_t unit)
{
    if (!w->blocks || (w->len - WAD_HEADER) % WAD_BLOCK + unit + MARKER <= WAD_BLOCK)
        return;
    put(w, 0x12);
    put(w, 0);
    put(w, 0);
    while ((w->len - WAD_HEADER) % WAD_BLOCK)
        put(w, PAD);
    w->run_allowed = 1;
}

/* A match of COUNT bytes at DISTANCE, then the TRAILING (0..3) literals at LIT. */
static void put_match(struct writer *w, size_t count, size_t distance, const uint8_t *lit, unsigned trailing)
{
    uint8_t packet[4];
    size_t size = 0;

    if (distance <= SHORT_REACH && count <= 8) {
        size_t d = distance - 1;
        packet[size++] = (uint8_t)((count - 1) << 5 | (d & 7) << 2 | trailing);
        packet[size++] = (uint8_t)(d >> 3);
    } else if (distance <= MEDIUM_REACH) {
        size_t d = distance - 1;
        if (count <= 33) {
            packet[size++] = (uint8_t)(0x20 | (count - 2));
        } else {
            packet[size++] = 0x20;
            packet[size++] = (uint8_t)(count - 33);
        }
        packet[size++] = (uint8_t)((d & 63) << 2 | trailing);
        packet[size++] = (uint8_t)(d >> 6);
    } else {
        size_t d = distance - 0x4000;
        unsigned high = 0;
        if (d >= 0x4000) {
            high = 8;
            d -= 0x4000;
        }
        if (count <= 9) {
            packet[size++] = (uint8_t)(0x10 | high | (count - 2));
        } else {
            packet[size++] = (uint8_t)(0x10 | high);
            packet[size++] = (uint8_t)(count - 9);
        }
        packet[size++] = (uint8_t)((d & 63) << 2 | trailing);
        packet[size++] = (uint8_t)(d >> 6);
    }
    fit(w, size + trailing);
    memcpy(w->out + w->len, packet, size);
    w->len += size;
    if (trailing)
        memcpy(w->out + w->len, lit, trailing);
    w->len += trailing;
    w->run_allowed = trailing == 0;
}

/* The no-op, with TRAILING (0..3) literals at LIT. */
static void put_noop(struct writer *w, const uint8_t *lit, unsigned trailing)
{
    fit(w, MARKER + trailing);
    put(w, 0x11);
    put(w, trailing);
    put(w, 0);
    if (trailing)
        memcpy(w->out + w->len, lit, trailing);
    w->len += trailing;
    w->run_allowed = trailing == 0;
}

/* COUNT (4 or more) literals as literal runs, each after a no-op where a run may not follow. */
static void put_literals(struct writer *w, const uint8_t *lit, size_t count)
{
    while (count) {
        size_t run = count;
        if (run > MAX_RUN)
            run = count - MAX_RUN < MIN_RUN ? count - MIN_RUN : MAX_RUN;
        if (!w->run_allowed)
            put_noop(w, lit, 0);
        fit(w, run + (run <= 18 ? 1 : 2));
        if (run <= 18) {
            put(w, (unsigned)(run - 3));
        } else {
            put(w, 0);
            put(w, (unsigned)(run - 18));
        }
        memcpy(w->out + w->len, lit, run);
        w->len += run;
        w->run_allowed = 0;
        lit += run;
        count -= run;
    }
}

/* The literals a stream begins with: one packet for up to 238 of them. */
static void put_first(struct writer *w, const uint8_t *lit, size_t count)
{
    if (count > MAX_FIRST_RUN) {
        put_literals(w, lit, count);
        return;
    }
    put(w, (unsigned)(count + 0x11));
    memcpy(w->out + w->len, lit, count);
    w->len += count;
    w->run_allowed = 0;
}

struct match {
    size_t at, count, distance;
    int gain;                       /* bytes saved over literals */
};

static int cost(size_t count, size_t distance)
{
    if (distance <= SHORT_REACH && count <= 8)
        return 2;
    if (distance <= MEDIUM_REACH)
        return count <= 33 ? 3 : 4;
    return count <= 9 ? 3 : 4;
}

/* The latest WAYS positions of each hash of three bytes, from BASE, where the job's input begins. */
struct matcher {
    size_t base;
    uint16_t at[1u << BUCKET_BITS][WAYS];
    uint8_t next[1u << BUCKET_BITS];
};

static unsigned hash3(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    return (unsigned)((v * 2654435761u) >> (32 - BUCKET_BITS));
}

static void insert(struct matcher *m, const uint8_t *src, size_t end, size_t at)
{
    unsigned h;
    if (at + 3 > end)
        return;
    h = hash3(src + at);
    m->at[h][m->next[h]] = (uint16_t)(at - m->base);
    m->next[h] = (uint8_t)((m->next[h] + 1) % WAYS);
}

/* The match at AT that saves the most, from the positions before AT, no longer than to END. */
static struct match find(const struct matcher *m, const uint8_t *src, size_t end, size_t at)
{
    struct match best = {at, 0, 0, 0};
    const uint8_t *here = src + at;
    size_t left = end - at;
    const uint16_t *ways;

    if (left < 3)
        return best;
    ways = m->at[hash3(here)];
    for (int k = 0; k < WAYS; k++) {
        size_t candidate, distance, longest, count = 0;
        const uint8_t *there;
        int gain;
        if (ways[k] == NONE)
            continue;
        candidate = m->base + ways[k];
        if (candidate >= at || at - candidate > WINDOW)
            continue;
        distance = at - candidate;
        there = src + candidate;
        longest = distance <= MEDIUM_REACH ? MAX_LEN : MAX_FAR_LEN;
        if (longest > left)
            longest = left;
        while (count < longest && there[count] == here[count])
            count++;
        if (count < 3)
            continue;
        gain = (int)count - cost(count, distance);
        if (gain > best.gain || (gain == best.gain && gain > 0 && distance < best.distance)) {
            best.count = count;
            best.distance = distance;
            best.gain = gain;
        }
    }
    return best;
}

/* The match before COUNT literals at LIT: up to 3 of them carried by the match, more as runs. */
static void put_pending(struct writer *w, const struct match *m, const uint8_t *lit, size_t count)
{
    if (count <= 3) {
        put_match(w, m->count, m->distance, lit, (unsigned)count);
    } else {
        put_match(w, m->count, m->distance, NULL, 0);
        put_literals(w, lit, count);
    }
}

size_t wad_chunk_bound(void)
{
    return wad_bound(WAD_CHUNK) - WAD_HEADER;
}

size_t wad_compress_chunk(const uint8_t *src, size_t size, size_t chunk, uint8_t *out)
{
    size_t start = chunk * WAD_CHUNK, end = start + WAD_CHUNK, at, inserted, literals;
    struct writer w = {out, 0, 1, 0};
    struct match pending = {0, 0, 0, 0}, next = {0, 0, 0, 0};
    int have_pending = 0, have_next = 0, first = chunk == 0, last;
    struct matcher *m = malloc(sizeof *m);

    if (!m)
        return 0;
    if (end > size)
        end = size;
    last = end == size;
    memset(m->at, 0xFF, sizeof m->at);
    memset(m->next, 0, sizeof m->next);
    inserted = start > WINDOW ? start - WINDOW : 0;     /* the history, indexed first */
    m->base = inserted;
    at = start;

    while (at < end) {
        struct match found;
        while (inserted < at)
            insert(m, src, end, inserted++);
        found = have_next ? next : find(m, src, end, at);
        have_next = 0;
        if (found.gain < 1) {
            at++;
            continue;
        }
        if (at + 1 < end) {         /* a better match a byte later is worth a literal */
            while (inserted < at + 1)
                insert(m, src, end, inserted++);
            next = find(m, src, end, at + 1);
            if (next.gain > found.gain) {
                have_next = 1;
                at++;
                continue;
            }
        }
        if (have_pending) {
            size_t from = pending.at + pending.count;
            put_pending(&w, &pending, src + from, at - from);
        } else if (at > start) {
            literals = at - start;
            if (first)
                put_first(&w, src, literals);
            else if (literals <= 3)
                put_noop(&w, src + start, (unsigned)literals);
            else
                put_literals(&w, src + start, literals);
        }
        pending = found;
        have_pending = 1;
        at += found.count;
    }

    literals = end - (have_pending ? pending.at + pending.count : start);
    if (!have_pending) {
        if (first)
            put_first(&w, src, literals);
        else if (literals <= 3)
            put_noop(&w, src + start, (unsigned)literals);
        else
            put_literals(&w, src + start, literals);
    } else if (!last || literals == 0 || literals >= 3) {
        put_pending(&w, &pending, src + pending.at + pending.count, literals);
    } else if (pending.count - (3 - literals) >= 3) {
        /* the stream's end: the last packet copies exactly what it holds, as the trailing literals' copy writes
         * three */
        pending.count -= 3 - literals;
        put_pending(&w, &pending, src + pending.at + pending.count, 3);
    } else {
        put_literals(&w, src + pending.at, pending.count + literals);
    }
    free(m);
    return w.len;
}

/* A chunk's packet at P: its size, and whether a literal run may follow it (RUN_AFTER) and it is one (*RUN).
 * FIRST: the stream's first packet. */
static size_t packet_size(const uint8_t *p, int first, int *run, int *run_after)
{
    unsigned tag = p[0], trailing;
    size_t head;

    if ((first && tag > 0x11) || tag < 0x10) {
        *run = 1;
        *run_after = 0;
        if (first && tag > 0x11)
            return 1 + (tag - 0x11u);
        return tag ? 1 + tag + 3u : 2 + p[1] + 18u;
    }
    if (tag >= 0x40)
        head = 2;
    else if (tag >= 0x20)
        head = (tag & 31 ? 1 : 2) + 2;
    else
        head = (tag & 7 ? 1 : 2) + 2;
    trailing = p[head - 2] & 3u;
    *run = 0;
    *run_after = trailing == 0;
    return head + trailing;
}

size_t wad_layout(uint8_t *const *chunks, const size_t *lengths, size_t count, uint8_t *dst)
{
    struct writer w = {dst, WAD_HEADER, 1, 1};

    for (size_t c = 0; c < count; c++) {
        const uint8_t *p = chunks[c];
        for (size_t at = 0; at < lengths[c];) {
            int run, run_after;
            size_t size = packet_size(p + at, c == 0 && at == 0, &run, &run_after);
            if (run && !w.run_allowed)
                put_noop(&w, NULL, 0);
            fit(&w, size);
            memcpy(w.out + w.len, p + at, size);
            w.len += size;
            at += size;
            w.run_allowed = run_after;
        }
    }
    memcpy(dst, "WAD", 3);
    dst[3] = (uint8_t)w.len;
    dst[4] = (uint8_t)(w.len >> 8);
    dst[5] = (uint8_t)(w.len >> 16);
    dst[6] = (uint8_t)(w.len >> 24);
    memset(dst + 7, 0, WAD_HEADER - 7);
    return w.len;
}

struct serial_job {
    const uint8_t *src;
    size_t size;
    uint8_t **chunks;
    size_t *lengths;
};

static void compress_job(void *arg, size_t chunk)
{
    struct serial_job *job = arg;
    job->lengths[chunk] = wad_compress_chunk(job->src, job->size, chunk, job->chunks[chunk]);
}

static void run_serially(void *context, wad_job *job, void *arg, size_t count)
{
    (void)context;
    for (size_t k = 0; k < count; k++)
        job(arg, k);
}

size_t wad_compress(const uint8_t *src, size_t size, uint8_t *dst, wad_runner *run, void *context)
{
    size_t count = wad_chunks(size), bound = wad_chunk_bound(), len = 0;
    uint8_t *pool = malloc(count * bound + 1);
    uint8_t **chunks = malloc((count + 1) * sizeof *chunks);
    size_t *lengths = malloc((count + 1) * sizeof *lengths);
    struct serial_job job = {src, size, chunks, lengths};

    if (pool && chunks && lengths) {
        int ok = 1;
        for (size_t c = 0; c < count; c++)
            chunks[c] = pool + c * bound;
        (run ? run : run_serially)(context, compress_job, &job, count);
        for (size_t c = 0; c < count; c++)
            ok &= lengths[c] != 0;
        if (ok)
            len = wad_layout(chunks, lengths, count, dst);
    }
    free(pool);
    free(chunks);
    free(lengths);
    return len;
}
