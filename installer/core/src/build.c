#include "build.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytes.h"
#include "disc.h"
#include "elf.h"
#include "files.h"
#include "fill.h"
#include "helpdesk.h"
#include "levels.h"
#include "sha256.h"
#include "sign.h"
#include "thread.h"

#define COPY_CHUNK (16u << 20)       /* the copy's reads: progress events stay few, a cancel is quick */
#define RESTORE_CHUNK (1u << 20)
static const char RESTORE_MAGIC[8] = {'R', 'C', 'E', 'D', 'R', 'E', 'S', 'T'};

/* A write to PATH that failed with ERRNO_VALUE, as the protocol's error: no_space for a full drive, else
   not_writable. */
static bool write_failed(InstallError *error, const char *path, int errno_value)
{
    if (errno_value == ENOSPC) {
        return install_fail(error, "no_space", "%s: the drive is full", path);
    }
    return install_fail(error, "not_writable", "%s: %s", path, strerror(errno_value != 0 ? errno_value : EIO));
}

static bool cancelled(Job *job, InstallError *error)
{
    return job_cancelled(job) && !install_fail(error, "cancelled", "cancelled");
}

/* The payload's file NAME, whole and zero-terminated, or NULL. */
static char *payload_file(const Payload *payload, const char *name, size_t *size)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", payload->directory, name);
    return file_read_all(path, size);
}

static Json *payload_json(const Payload *payload, const char *name, InstallError *error)
{
    size_t size = 0;
    char *text = payload_file(payload, name, &size);
    char parse_error[128] = "cannot be read";
    Json *value = text != NULL ? json_parse(text, size, parse_error, sizeof parse_error) : NULL;
    free(text);
    if (value == NULL) {
        install_fail(error, "internal", "%s: %s", name, parse_error);
    }
    return value;
}

/* The next line of a payload .tsv from *AT, split at its tabs into FIELDS (up to CAPACITY): its field count, 0 at
   the end. Empty lines and comments (#) are skipped, as tools/install.py's tsv() skips them. TEXT is changed. */
static size_t tsv_line(char **at, char **fields, size_t capacity)
{
    while (**at != 0) {
        char *line = *at;
        char *end = line + strcspn(line, "\r\n");
        *at = end + strspn(end, "\r\n");
        if (end == line || line[0] == '#') {
            continue;
        }
        *end = 0;
        size_t count = 0;
        char *field = line;
        while (count < capacity) {
            fields[count++] = field;
            char *tab = strchr(field, '\t');
            if (tab == NULL) {
                break;
            }
            *tab = 0;
            field = tab + 1;
        }
        return count;
    }
    return 0;
}

/* HEX's bytes ("0a1b...") into OUT; false when it is not hex of that length. */
static bool from_hex(const char *hex, uint8_t *out, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        unsigned value;
        if (sscanf(hex + 2 * i, "%2x", &value) != 1) {
            return false;
        }
        out[i] = (uint8_t)value;
    }
    return true;
}

/* Whether FEATURE (an overlay.tsv feature) belongs to one of the switches OFF (NULL-ended) leaves out. */
static bool feature_left_out(const Json *switches, const char *const *off, const char *feature)
{
    for (size_t s = 0; off[s] != NULL; s++) {
        for (size_t k = 0; k < switches->count; k++) {
            const char *id = json_get_string(&switches->items[k], "id");
            if (id != NULL && strcmp(id, off[s]) == 0 &&
                json_list_has(json_get(&switches->items[k], "features"), feature)) {
                return true;
            }
        }
    }
    return false;
}

/* The convert stage's progress: a step after each hole, the executable and its words. */
typedef struct {
    Job *job;
    uint64_t done;
    uint64_t total;
} Converting;

static bool converted(Converting *converting, InstallError *error)
{
    converting->done++;
    if (cancelled(converting->job, error)) {
        return false;
    }
    job_progress(converting->job, "convert", converting->done, converting->total);
    return true;
}

/* The mod segment with its holes filled (but LEFT_OUT_HOLES'), the features word and the left-out switches' bytes;
   in a new buffer the caller frees. */
static uint8_t *mod_segment(const Payload *payload, const Json *discs, const Json *switches, const char *const *off,
                            uint32_t features, Converting *converting, size_t *size, InstallError *error)
{
    uint8_t *segment = (uint8_t *)payload_file(payload, "modseg.bin", size);
    Json *holes = payload_json(payload, "holes.json", error);
    Filler *filler = filler_new(discs);
    bool made = segment != NULL && holes != NULL && holes->type == JSON_ARRAY && filler != NULL;
    if (!made && holes != NULL) {
        install_fail(error, "internal", "the payload's modseg.bin or holes.json cannot be read");
    }
    for (size_t i = 0; made && i < holes->count; i++) {
        const Json *hole = &holes->items[i];
        const char *symbol = json_get_string(hole, "symbol");
        bool left_out = false;
        for (size_t s = 0; off[s] != NULL && symbol != NULL; s++) {
            for (size_t k = 0; k < switches->count; k++) {
                const char *id = json_get_string(&switches->items[k], "id");
                left_out = left_out || (id != NULL && strcmp(id, off[s]) == 0 &&
                                        json_list_has(json_get(&switches->items[k], "holes"), symbol));
            }
        }
        if (!left_out) {
            int64_t offset = json_get_integer(hole, "offset", -1);
            int64_t hole_size = json_get_integer(hole, "size", -1);
            char reason[1024];
            if (symbol == NULL || offset < 0 || hole_size < 0 || (uint64_t)(offset + hole_size) > *size) {
                made = install_fail(error, "internal", "holes.json: hole %zu is outside the segment", i);
            } else if (!filler_fill(filler, symbol, json_get(hole, "fill"), segment + offset, (size_t)hole_size,
                                    reason, sizeof reason)) {
                made = install_fail(error, "internal", "%s", reason);
            }
        }
        made = made && converted(converting, error);
    }
    filler_free(filler);
    json_free(holes);

    /* ee_features: the bits of the features in; then each left-out switch's own bytes */
    int64_t features_word = json_get_integer(payload->edition, "features_word", -1);
    if (made && (features_word < 0 || (uint64_t)features_word + 4 > *size)) {
        made = install_fail(error, "internal", "edition.json's features_word is outside the segment");
    }
    if (made) {
        write_u32_le(segment + features_word, features);
    }
    Json *switch_bytes = made ? payload_json(payload, "switches.json", error) : NULL;
    made = made && switch_bytes != NULL;
    for (size_t s = 0; made && off[s] != NULL; s++) {
        const Json *writes = json_get(switch_bytes, off[s]);
        for (size_t w = 0; writes != NULL && writes->type == JSON_ARRAY && w < writes->count; w++) {
            const Json *write = &writes->items[w];
            const char *hex = write->type == JSON_ARRAY && write->count == 2 ? write->items[1].string : NULL;
            int64_t at = write->type == JSON_ARRAY && write->count == 2 && write->items[0].is_integer
                             ? write->items[0].integer : -1;
            size_t length = hex != NULL ? strlen(hex) / 2 : 0;
            if (hex == NULL || at < 0 || (uint64_t)at + length > *size || !from_hex(hex, segment + at, length)) {
                made = install_fail(error, "internal", "switches.json: %s's bytes do not fit the segment", off[s]);
                break;
            }
        }
    }
    json_free(switch_bytes);
    if (!made) {
        free(segment);
        return NULL;
    }
    return segment;
}

/* A chunk of the copy's source, read on a thread of its own while the chunk before it is written. */
typedef struct {
    FILE *in;
    uint8_t *into;
    size_t want, got;
} CopyRead;

static void copy_read(void *argument)
{
    CopyRead *read = argument;
    read->got = fread(read->into, 1, read->want, read->in);
}

/* SOURCE copied to TARGET, with progress in stage "write". Each chunk is written while the next is read: on one
   drive (a PS3's), reading and writing in turn left it idle half the time. */
static bool copy_image(const char *source, const char *target, Job *job, InstallError *error)
{
    FILE *in = file_open(source, "rb");
    if (in == NULL) {
        return install_fail(error, "missing_disc", "%s cannot be read", source);
    }
    FILE *out = file_open(target, "wb");
    if (out == NULL) {
        int saved = errno;
        fclose(in);
        return write_failed(error, target, saved);
    }
    uint64_t total = file_size(in);
    file_seek(in, 0);
    uint8_t *chunks[2] = {malloc(COPY_CHUNK), malloc(COPY_CHUNK)};
    bool copied = (chunks[0] != NULL && chunks[1] != NULL) || install_fail(error, "internal", "out of memory");
    CopyRead current = {in, chunks[0], total < COPY_CHUNK ? (size_t)total : COPY_CHUNK, 0};
    if (copied) {
        copy_read(&current);
    }
    uint64_t done = 0;
    while (copied && done < total) {
        if (cancelled(job, error)) {
            copied = false;
            break;
        }
        if (current.got != current.want) {
            copied = install_fail(error, "missing_disc", "%s cannot be read", source);
            break;
        }
        uint64_t after = done + current.got;
        CopyRead next = {in, current.into == chunks[0] ? chunks[1] : chunks[0],
                         total - after < COPY_CHUNK ? (size_t)(total - after) : COPY_CHUNK, 0};
        /* the next chunk read beside the write, or after it when no thread can be had */
        Thread *reading = next.want > 0 ? thread_start(copy_read, &next) : NULL;
        bool written = fwrite(current.into, 1, current.got, out) == current.got;
        int saved = errno;
        if (reading != NULL) {
            thread_join(reading);
        } else if (next.want > 0 && written) {
            copy_read(&next);
        }
        if (!written) {
            copied = write_failed(error, target, saved);
            break;
        }
        done = after;
        job_progress(job, "write", done, total);
        current = next;
    }
    free(chunks[0]);
    free(chunks[1]);
    fclose(in);
    if (fclose(out) != 0 && copied) {
        copied = write_failed(error, target, errno);
    }
    return copied;
}

/* ---- A new image in one pass ----
 *
 * The same bytes as a copy of the base disc patched in place (install's other way), but each byte written once,
 * front to back, and hashed as it is written: no copy read back for the patches, no image read back for its hash.
 * The base disc is read in order; the level spans are patched as the stream reaches them (levels_produce); the
 * small changes (the overlay words, the executable's directory record, the HelpDesk table words, the volume size)
 * are laid on the bytes as they pass; the executable, the HelpDesk lines and the tail follow the retail sectors.
 * Every level must fit its own sectors; levels_produce fails the build otherwise.
 */

/* A small change to the image: SIZE bytes at OFFSET. BEFORE_LEVELS: there before a level's patch reads its span
   (the overlay words and the executable, as install writes them before patch_levels), else after (the HelpDesk's
   table words and the volume size). */
typedef struct {
    uint64_t offset;
    size_t size;
    bool before_levels;
    uint8_t word[8];                /* the bytes, when SIZE is 8 or less */
    const uint8_t *bytes;           /* else the caller's, which outlive the stream */
} Change;

typedef struct {
    Change *items;
    size_t count, capacity;
    bool failed;
} Changes;

static void change(Changes *changes, uint64_t offset, const void *bytes, size_t size, bool before_levels)
{
    if (changes->count == changes->capacity) {
        size_t capacity = changes->capacity > 0 ? changes->capacity * 2 : 256;
        Change *items = realloc(changes->items, capacity * sizeof *items);
        if (items == NULL) {
            changes->failed = true;
            return;
        }
        changes->items = items;
        changes->capacity = capacity;
    }
    Change *at = &changes->items[changes->count++];
    memset(at, 0, sizeof *at);
    at->offset = offset;
    at->size = size;
    at->before_levels = before_levels;
    if (size <= sizeof at->word) {
        memcpy(at->word, bytes, size);
    } else {
        at->bytes = bytes;
    }
}

/* The changes of BEFORE_LEVELS' kind laid on BYTES, the image's SIZE bytes from START. */
static void apply_changes(const Changes *changes, bool before_levels, uint64_t start, uint8_t *bytes, size_t size)
{
    uint64_t end = start + size;
    for (size_t i = 0; i < changes->count; i++) {
        const Change *at = &changes->items[i];
        if (at->before_levels != before_levels || at->offset >= end || at->offset + at->size <= start) {
            continue;
        }
        uint64_t from = at->offset > start ? at->offset : start;
        uint64_t to = at->offset + at->size < end ? at->offset + at->size : end;
        const uint8_t *source = at->size <= sizeof at->word ? at->word : at->bytes;
        memcpy(bytes + (from - start), source + (from - at->offset), (size_t)(to - from));
    }
}

/* What the stream has written: its file, its hash so far, how far it is. */
typedef struct {
    FILE *out;
    const char *path;
    Sha256 hash;
    uint64_t written, total;
    Job *job;
} Stream;

typedef struct {
    FILE *out;
    const uint8_t *bytes;
    size_t size;
    bool written;
    int error;
} StreamWrite;

static void stream_write(void *argument)
{
    StreamWrite *write = argument;
    write->written = fwrite(write->bytes, 1, write->size, write->out) == write->size;
    write->error = errno;
}

/* BYTES written and hashed, the write on a thread of its own while the hash runs. */
static bool stream_put(Stream *stream, const uint8_t *bytes, size_t size, InstallError *error)
{
    StreamWrite write = {stream->out, bytes, size, false, 0};
    Thread *writing = thread_start(stream_write, &write);
    if (writing == NULL) {
        stream_write(&write);
    }
    sha256_add(&stream->hash, bytes, size);
    if (writing != NULL) {
        thread_join(writing);
    }
    if (!write.written) {
        return write_failed(error, stream->path, write.error);
    }
    stream->written += size;
    job_progress(stream->job, "write", stream->written, stream->total);
    return true;
}

/* COUNT zero bytes written and hashed. */
static bool stream_zeros(Stream *stream, uint64_t count, uint8_t *scratch, size_t scratch_size, InstallError *error)
{
    memset(scratch, 0, scratch_size);
    while (count > 0) {
        size_t part = count < scratch_size ? (size_t)count : scratch_size;
        if (!stream_put(stream, scratch, part, error)) {
            return false;
        }
        count -= part;
    }
    return true;
}

/* Progress that goes nowhere: a level's patch, in the stream, whose progress is the stream's own. */
static void quiet_progress(Job *job, const char *stage, uint64_t done, uint64_t total)
{
    (void)job;
    (void)stage;
    (void)done;
    (void)total;
}

/* The NULL-ended HelpDesk uses the switches out (OFF) take with them, into LINES (256 at most). */
static void left_out_lines(const Json *switches, const char *const *off, const char **lines)
{
    size_t line_count = 0;
    for (size_t s = 0; off[s] != NULL; s++) {
        for (size_t k = 0; k < switches->count; k++) {
            const Json *helpdesk = json_get(&switches->items[k], "helpdesk");
            const char *id = json_get_string(&switches->items[k], "id");
            for (size_t u = 0; id != NULL && strcmp(id, off[s]) == 0 && helpdesk != NULL &&
                               helpdesk->type == JSON_ARRAY && u < helpdesk->count && line_count < 255;
                 u++) {
                lines[line_count++] = helpdesk->items[u].string;
            }
        }
    }
    lines[line_count] = NULL;
}

/* The patch stage's shares, out of PATCH_TOTAL: the levels (the level pass takes most of a build's time, their data
   decoded, patched and encoded again; in a stream, their plan), then the HelpDesk lines. */
#define PATCH_TOTAL 100
#define PATCH_LEVELS 87
#define PATCH_HELPDESK 13

/* A step of the patch stage: its own progress, done of total, shown as its share of the stage's. */
typedef struct {
    Job job;
    uint64_t start;                 /* the stage's progress before this step */
    uint64_t share;                 /* the step's part of the stage's total */
    uint64_t stage_total;
} PatchStep;

static void patch_step_progress(Job *job, const char *stage, uint64_t done, uint64_t total);
static void patch_step_log(Job *job, const char *text);

/* OUTPUT made in one pass from the base disc BASE (its path), with the executable EXE (EXE_SIZE bytes, named
   EXE_NAME on the disc) and CHANGES (the overlay words, which the stream adds to); its SHA-256 into HEX. */
static bool stream_image(const BuildFacts *facts, const char *base, const char *output, const char *exe_name,
                         const uint8_t *exe, size_t exe_size, Changes *changes, Job *job, char hex[65],
                         InstallError *error)
{
    FILE *source = file_open(base, "rb");
    if (source == NULL) {
        return install_fail(error, "missing_disc", "%s cannot be read", base);
    }
    uint64_t retail_bytes = file_size(source);
    uint32_t retail = (uint32_t)(retail_bytes / ISO_SECTOR);
    bool made = true;

    /* the executable: in its own sectors when it fits, else after the retail ones, its record pointed there */
    IsoEntry entry;
    uint8_t *in_place = NULL;
    uint32_t exe_sectors = 0;
    if (!iso_find(source, exe_name, &entry)) {
        made = install_fail(error, "internal", "%s is not on the disc", exe_name);
    } else if (exe_size <= entry.size) {
        in_place = calloc(entry.size > 0 ? entry.size : 1, 1);
        made = in_place != NULL || install_fail(error, "internal", "out of memory");
        if (made) {
            memcpy(in_place, exe, exe_size);
            change(changes, (uint64_t)entry.extent * ISO_SECTOR, in_place, entry.size, true);
        }
    } else {
        exe_sectors = (uint32_t)((exe_size + ISO_SECTOR - 1) / ISO_SECTOR);
        uint8_t field[8];
        write_u32_both(field, retail);
        change(changes, entry.record_offset + 2, field, sizeof field, true);
        write_u32_both(field, (uint32_t)exe_size);
        change(changes, entry.record_offset + 10, field, sizeof field, true);
    }

    /* the levels' plan and the HelpDesk lines, with their places after the executable */
    job_progress(job, "patch", 0, PATCH_TOTAL);
    size_t sign_size = 0;
    uint8_t *sign = made ? make_sign(base, &sign_size, error) : NULL;
    made = made && sign != NULL;
    PatchStep plan_step = {{.progress = patch_step_progress, .log = patch_step_log, .parent = job}, 0, PATCH_LEVELS,
                           PATCH_TOTAL};
    LevelsPlan *plan = made ? levels_plan(source, facts, sign, sign_size, &plan_step.job, error) : NULL;
    made = made && plan != NULL;
    /* from here a level's patch is a step of the stream: its log lines pass, its progress is the stream's */
    plan_step.job.progress = quiet_progress;
    HelpdeskLines lines = {0};
    uint32_t line_sectors = 0;
    if (made) {
        size_t size = 0;
        char *recipes = payload_file(facts->payload, "helpdesk.json", &size);
        PatchStep step = {{.progress = patch_step_progress, .log = patch_step_log, .parent = job}, PATCH_LEVELS,
                          PATCH_HELPDESK, PATCH_TOTAL};
        made = (recipes != NULL || install_fail(error, "internal", "the payload's helpdesk.json cannot be read")) &&
               helpdesk_make(base, (const uint8_t *)recipes, size, facts->left_out_lines, &step.job, &lines, error);
        free(recipes);
    }
    if (made) {
        line_sectors = helpdesk_place(&lines, retail + exe_sectors);
        for (size_t i = 0; i < lines.count; i++) {
            uint8_t word[4];
            write_u32_le(word, lines.lines[i].sector);
            change(changes, lines.lines[i].entry, word, sizeof word, false);
        }
        job_progress(job, "patch", PATCH_TOTAL, PATCH_TOTAL);
    }
    bool grew = exe_sectors > 0 || line_sectors > 0;
    uint32_t sectors = retail + exe_sectors + line_sectors + (grew ? ISO_TAIL_SECTORS : 0);
    if (made && grew) {
        uint8_t field[8];
        write_u32_both(field, sectors);
        change(changes, ISO_VOLUME_SIZE_OFFSET, field, sizeof field, false);
    }
    made = made && (!changes->failed || install_fail(error, "internal", "out of memory"));

    /* the stream: the retail sectors, a level's span at a time where one starts, then what follows them */
    Stream stream = {NULL, output, {{0}, 0, {0}, 0}, 0, (uint64_t)sectors * ISO_SECTOR, job};
    sha256_start(&stream.hash);
    uint8_t *chunk = made ? malloc(COPY_CHUNK) : NULL;
    made = made && (chunk != NULL || install_fail(error, "internal", "out of memory"));
    if (made) {
        stream.out = file_open(output, "wb");
        made = stream.out != NULL || write_failed(error, output, errno);
    }
    uint64_t at = 0;
    int level = 0;
    while (made && at < retail_bytes) {
        if (cancelled(job, error)) {
            made = false;
            break;
        }
        uint32_t first = 0, count = 0;
        if (level < LEVEL_COUNT) {
            levels_span(plan, level, &first, &count);
        }
        uint64_t span_start = level < LEVEL_COUNT ? (uint64_t)first * ISO_SECTOR : retail_bytes;
        if (span_start < at) {
            made = install_fail(error, "internal", "level %d starts before level %d ends", level, level - 1);
            break;
        }
        if (level < LEVEL_COUNT && at == span_start) {
            size_t size = (size_t)count * ISO_SECTOR;
            uint8_t *span = malloc(size);
            size_t archive_size = 0;
            made = (span != NULL || install_fail(error, "internal", "out of memory")) &&
                   (file_read_at(source, at, span, size) ||
                    install_fail(error, "missing_disc", "%s cannot be read", base));
            if (made) {
                apply_changes(changes, true, at, span, size);
                made = levels_produce(plan, level, span, size, &archive_size, error);
            }
            if (made) {
                apply_changes(changes, false, at, span, size);
                made = stream_put(&stream, span, size, error);
            }
            free(span);
            at += size;
            level++;
            continue;
        }
        uint64_t end = at + COPY_CHUNK < span_start ? at + COPY_CHUNK : span_start;
        end = end < retail_bytes ? end : retail_bytes;
        size_t size = (size_t)(end - at);
        made = file_read_at(source, at, chunk, size) || install_fail(error, "missing_disc", "%s cannot be read", base);
        if (made) {
            apply_changes(changes, true, at, chunk, size);
            apply_changes(changes, false, at, chunk, size);
            made = stream_put(&stream, chunk, size, error);
        }
        at = end;
    }
    made = made && (level == LEVEL_COUNT || install_fail(error, "internal", "level %d lies past the disc's end", level));
    if (made && exe_sectors > 0) {
        made = stream_put(&stream, exe, exe_size, error) &&
               stream_zeros(&stream, (uint64_t)exe_sectors * ISO_SECTOR - exe_size, chunk, ISO_SECTOR, error);
    }
    for (size_t i = 0; made && i < lines.count; i++) {
        made = stream_put(&stream, lines.lines[i].vag, lines.lines[i].size, error);
    }
    if (made && grew) {
        made = stream_zeros(&stream, (uint64_t)ISO_TAIL_SECTORS * ISO_SECTOR, chunk, COPY_CHUNK, error);
    }
    made = made && (stream.written == stream.total ||
                    install_fail(error, "internal", "the image came to %llu bytes, not %llu",
                                 (unsigned long long)stream.written, (unsigned long long)stream.total));
    if (stream.out != NULL && fclose(stream.out) != 0 && made) {
        made = write_failed(error, output, errno);
    }
    if (made) {
        uint8_t digest[32];
        sha256_finish(&stream.hash, digest);
        sha256_hex(digest, hex);
    }
    free(chunk);
    helpdesk_free(&lines);
    levels_plan_free(plan);
    free(sign);
    free(in_place);
    fclose(source);
    return made;
}

/* The edition's image at OUTPUT (a new file), as tools/install.py's install() makes it. With STREAMED_HEX it is
   written in one pass (stream_image), and its SHA-256 goes there; else the base disc is copied and patched. */
static bool install(const Payload *payload, HashCache *cache, const Json *discs, const Json *options,
                    const char *output, BuildPatch *patch, void *context, char *streamed_hex, Job *job,
                    InstallError *error)
{
    const Json *edition = payload->edition;
    const Json *switches = json_get(edition, "switches");
    const Json *expected = json_get(edition, "discs");
    for (size_t i = 0; i < discs->count; i++) {
        const char *name = discs->keys[i];
        const char *path = discs->items[i].string;
        char hex[65];
        bool earlier = false;
        DiscHashResult hashed = hash_cache_disc_hash(cache, path, "verify", job, hex, &earlier);
        if (hashed == DISC_CANCELLED) {
            return install_fail(error, "cancelled", "cancelled");
        }
        const char *wanted = json_get_string(expected, name);
        if (hashed != DISC_HASHED || wanted == NULL || strcmp(hex, wanted) != 0) {
            return install_fail(error, "unknown_disc", "%s: not the disc this edition is built from (%s)", path, name);
        }
        char label[256];
        payload_disc_label(payload, name, NULL, label, sizeof label);
        if (earlier) {
            job_log(job, "%s: checked earlier, sha256 %s", label, hex);
        } else {
            job_log(job, "sha256 %s matches %s", hex, label);
        }
    }

    /* the switches in and out, as plan() decides them, and what the ones out take with them */
    const Json *without = json_get(options, "without");
    const char *off[64];
    size_t off_count = 0;
    uint32_t features = 0;
    Text off_text = {0};
    for (size_t s = 0; s < switches->count && off_count < 63; s++) {
        const char *id = json_get_string(&switches->items[s], "id");
        const char *disc = json_get_string(&switches->items[s], "disc");
        if (!json_list_has(without, id) && json_get(discs, disc != NULL ? disc : "") != NULL) {
            features += (uint32_t)json_get_integer(&switches->items[s], "bit", 0);
        } else {
            text_add(&off_text, off_count > 0 ? ", " : "");
            text_add(&off_text, id);
            off[off_count++] = id;
        }
    }
    off[off_count] = NULL;
    job_log(job, "features left out: %s", off_count > 0 ? off_text.text : "none");
    text_free(&off_text);

    /* the economy's left-out words, else the edition's first economy's, and the switches' out */
    const Json *economies = json_get(edition, "economies");
    const Json *economy = json_get(options, "economy");
    const Json *economy_words = economy != NULL && economy->type == JSON_STRING && economy->string[0] != 0
                                    ? json_get(economies, economy->string) : &economies->items[0];

    Json *holes = payload_json(payload, "holes.json", error);
    if (holes == NULL) {
        return false;
    }
    Converting converting = {job, 0, holes->count + 2};
    json_free(holes);
    job_progress(job, "convert", 0, converting.total);
    size_t segment_size = 0;
    uint8_t *segment = mod_segment(payload, discs, switches, off, features, &converting, &segment_size, error);
    if (segment == NULL) {
        return false;
    }

    /* the base disc's boot executable, with the segment as its second loadable segment */
    const char *base = json_get_string(discs, payload_base_disc(payload));
    char base_clean[4096];
    path_clean(base, base_clean, sizeof base_clean);
    DiscKind kind;
    char exe_name[40];
    uint8_t *original = NULL;
    size_t original_size = 0;
    if (disc_kind(base, &kind) && kind.is_ps2_disc) {
        snprintf(exe_name, sizeof exe_name, "%s;1", kind.boot_file);
        FILE *disc = file_open(base, "rb");
        original = disc != NULL ? iso_read_file(disc, exe_name, &original_size) : NULL;
        if (disc != NULL) {
            fclose(disc);
        }
    }
    size_t exe_size = 0;
    uint8_t *exe = original != NULL
                       ? elf_add_segment(original, original_size,
                                         (uint32_t)json_get_integer(edition, "modseg_base", 0), segment, segment_size,
                                         &exe_size)
                       : NULL;
    free(original);
    free(segment);
    if (exe == NULL) {
        return install_fail(error, "internal", "the boot executable of %s cannot take the mod segment", base);
    }
    if (!converted(&converting, error)) {
        free(exe);
        return false;
    }
    size_t tsv_size = 0;
    char *exe_words = payload_file(payload, "exe.tsv", &tsv_size);
    bool made = exe_words != NULL || install_fail(error, "internal", "the payload's exe.tsv cannot be read");
    char *at = exe_words;
    char *fields[4];
    size_t count;
    while (made && (count = tsv_line(&at, fields, 4)) > 0) {
        unsigned long long offset = strtoull(fields[0], NULL, 16);
        if (count < 2 || offset + 4 > exe_size) {
            made = install_fail(error, "internal", "exe.tsv: %s is outside the executable", fields[0]);
        } else {
            write_u32_le(exe + offset, (uint32_t)strtoul(fields[1], NULL, 16));
        }
    }
    free(exe_words);
    made = made && converted(&converting, error);

    /* a new image with the edition's own patch stage: written in one pass, hashed as it goes */
    if (made && streamed_hex != NULL) {
        Changes changes = {0};
        char *overlay_words = payload_file(payload, "overlay.tsv", &tsv_size);
        size_t words = 0;
        made = overlay_words != NULL || install_fail(error, "internal", "the payload's overlay.tsv cannot be read");
        at = overlay_words;
        while (made && (count = tsv_line(&at, fields, 4)) > 0) {
            if (count < 3) {
                made = install_fail(error, "internal", "overlay.tsv: a line without a feature, offset and word");
            } else if (!json_list_has(economy_words, fields[0]) && !feature_left_out(switches, off, fields[0])) {
                uint8_t word[4];
                write_u32_le(word, (uint32_t)strtoul(fields[2], NULL, 16));
                change(&changes, strtoull(fields[1], NULL, 16), word, sizeof word, true);
                words++;
            }
        }
        free(overlay_words);
        if (made) {
            job_log(job, "writing %s from %s in one pass", output, base_clean);
            job_log(job, "the boot executable with the mod segment (%zu bytes); %zu level-code words", exe_size, words);
            char folder[4096];
            path_parent(output, folder, sizeof folder);
            file_make_directories(folder);
            const char *lines[256];
            left_out_lines(switches, off, lines);
            BuildFacts facts = {payload, discs, off, lines};
            made = stream_image(&facts, base, output, exe_name, exe, exe_size, &changes, job, streamed_hex, error);
        }
        free(changes.items);
        free(exe);
        return made;
    }

    /* the image: a copy of the base disc, the executable replaced, the level programs' words written */
    if (made) {
        job_log(job, "copying %s -> %s", base_clean, output);
        char folder[4096];
        path_parent(output, folder, sizeof folder);
        file_make_directories(folder);
        made = copy_image(base, output, job, error);
    }
    IsoImage image;
    if (made && !iso_image_open(&image, output)) {
        made = write_failed(error, output, errno);
    } else if (made) {
        bool replaced = iso_image_replace_file(&image, exe_name, exe, exe_size);
        char *overlay_words = payload_file(payload, "overlay.tsv", &tsv_size);
        size_t words = 0;
        made = overlay_words != NULL || install_fail(error, "internal", "the payload's overlay.tsv cannot be read");
        at = overlay_words;
        while (made && (count = tsv_line(&at, fields, 4)) > 0) {
            if (count < 3) {
                made = install_fail(error, "internal", "overlay.tsv: a line without a feature, offset and word");
            } else if (!json_list_has(economy_words, fields[0]) && !feature_left_out(switches, off, fields[0])) {
                iso_image_write_word(&image, strtoull(fields[1], NULL, 16), (uint32_t)strtoul(fields[2], NULL, 16));
                words++;
            }
        }
        free(overlay_words);
        if (made && !replaced && !image.failed) {
            made = install_fail(error, "internal", "%s is not on the copied disc", exe_name);
        }
        if (made && !image.failed) {
            job_log(job, "the boot executable with the mod segment (%zu bytes); %zu level-code words", exe_size, words);
        }
        if (made && patch != NULL && !image.failed) {
            /* the HelpDesk uses the switches out own */
            const char *lines[256];
            left_out_lines(switches, off, lines);
            BuildFacts facts = {payload, discs, off, lines};
            made = patch(context, &image, &facts, job, error);
        }
        bool finished = iso_image_finish(&image);
        if (made && !finished) {
            made = write_failed(error, output, image.error);
        }
    }
    free(exe);
    return made;
}


static void patch_step_progress(Job *job, const char *stage, uint64_t done, uint64_t total)
{
    PatchStep *step = (PatchStep *)job;
    uint64_t within = total > 0 ? step->share * (done < total ? done : total) / total : step->share;
    job_progress(job->parent, stage, step->start + within, step->stage_total);
}

static void patch_step_log(Job *job, const char *text)
{
    job_log(job->parent, "%s", text);
}


bool build_patch(void *context, IsoImage *image, const BuildFacts *facts, Job *job, InstallError *error)
{
    (void)context;
    const char *base = json_get_string(facts->discs, payload_base_disc(facts->payload));
    job_progress(job, "patch", 0, PATCH_TOTAL);
    /* Kalebo III's sign reads GADGETRON again: its class rebuilt from the PAL disc's own GALACTRON sign */
    size_t sign_size = 0;
    uint8_t *sign = make_sign(base, &sign_size, error);
    if (sign == NULL) {
        return false;
    }
    PatchStep levels = {{.progress = patch_step_progress, .log = patch_step_log, .parent = job}, 0, PATCH_LEVELS,
                        PATCH_TOTAL};
    bool patched_levels = patch_levels(image, facts, sign, sign_size, &levels.job, error);
    free(sign);
    if (!patched_levels) {
        return false;
    }
    PatchStep step = {{.progress = patch_step_progress, .log = patch_step_log, .parent = job}, PATCH_LEVELS,
                      PATCH_HELPDESK, PATCH_TOTAL};
    size_t size = 0;
    char *recipes = payload_file(facts->payload, "helpdesk.json", &size);
    if (recipes == NULL) {
        return install_fail(error, "internal", "the payload's helpdesk.json cannot be read");
    }
    bool patched = patch_helpdesk(image, base, (const uint8_t *)recipes, size, facts->left_out_lines, &step.job,
                                  error);
    free(recipes);
    if (patched) {
        job_progress(job, "patch", PATCH_TOTAL, PATCH_TOTAL);
    }
    return patched;
}

/* RESTORE: the original's length, then every sector the edition changes within it, with its original bytes.
   Layout: "RCEDREST", u64 original length, then records of u32 sector and 2048 bytes. */
static bool write_restore(const char *original, const char *edition_image, const char *restore, Job *job,
                          InstallError *error)
{
    FILE *a = file_open(original, "rb");
    FILE *b = file_open(edition_image, "rb");
    FILE *out = file_open(restore, "wb");
    int saved = errno;
    uint8_t *old = malloc(RESTORE_CHUNK);
    uint8_t *new_bytes = malloc(RESTORE_CHUNK);
    bool written = a != NULL && b != NULL && out != NULL && old != NULL && new_bytes != NULL;
    if (!written) {
        if (out == NULL) {
            write_failed(error, restore, saved);
        } else {
            install_fail(error, "internal", "%s or %s cannot be read", original, edition_image);
        }
    }
    uint64_t total = written ? file_size(a) : 0;
    if (written) {
        file_seek(a, 0);
        uint8_t header[16];
        memcpy(header, RESTORE_MAGIC, 8);
        write_u32_le(header + 8, (uint32_t)total);
        write_u32_le(header + 12, (uint32_t)(total >> 32));
        written = fwrite(header, 1, 16, out) == 16 || write_failed(error, restore, errno);
    }
    uint64_t done = 0;
    uint32_t sector = 0;
    while (written && done < total) {
        size_t got = fread(old, 1, RESTORE_CHUNK, a);
        if (got == 0) {
            break;
        }
        size_t new_got = fread(new_bytes, 1, got, b);
        if (cancelled(job, error)) {
            written = false;
            break;
        }
        for (size_t k = 0; written && k < got; k += ISO_SECTOR) {
            size_t length = got - k < ISO_SECTOR ? got - k : ISO_SECTOR;
            size_t new_length = new_got > k ? (new_got - k < length ? new_got - k : length) : 0;
            if (new_length == length && memcmp(old + k, new_bytes + k, length) == 0) {
                continue;
            }
            uint8_t number[4];
            write_u32_le(number, sector + (uint32_t)(k / ISO_SECTOR));
            written = (fwrite(number, 1, 4, out) == 4 && fwrite(old + k, 1, length, out) == length) ||
                      write_failed(error, restore, errno);
        }
        sector += (uint32_t)(got / ISO_SECTOR);
        done += got;
        job_progress(job, "write", done, total);
    }
    free(old);
    free(new_bytes);
    if (a != NULL) {
        fclose(a);
    }
    if (b != NULL) {
        fclose(b);
    }
    if (out != NULL && fclose(out) != 0 && written) {
        written = write_failed(error, restore, errno);
    }
    return written;
}

bool command_build(const Payload *payload, HashCache *cache, const Json *discs, const Json *options,
                   const Json *output, BuildPatch *patch, void *context, Job *job, Text *answer, InstallError *error)
{
    Text planned = {0};
    bool ok = command_plan(payload, discs, options, output, &planned, error);
    text_free(&planned);
    if (!ok) {
        return false;
    }
    if (output == NULL || output->type != JSON_OBJECT) {
        return install_fail(error, "internal", "bad command: build needs an output");
    }
    const char *base = json_get_string(discs, payload_base_disc(payload));
    char base_clean[4096];
    path_clean(base, base_clean, sizeof base_clean);
    bool in_place = strcmp(json_get_string(output, "mode"), "in_place") == 0;
    char target[4096];
    char work[4096 + 16];
    if (in_place) {
        snprintf(target, sizeof target, "%s", base_clean);
        snprintf(work, sizeof work, "%s.building", base_clean);
    } else {
        path_clean(json_get_string(output, "path"), target, sizeof target);
        snprintf(work, sizeof work, "%s", target);
    }
    /* a new image with the edition's patch stage is written in one pass, which hashes it too */
    char hex[65];
    bool streamed = !in_place && patch == build_patch;
    ok = install(payload, cache, discs, options, work, patch, context, streamed ? hex : NULL, job, error);
    if (ok && in_place) {
        char restore[4096 + 16];
        snprintf(restore, sizeof restore, "%s.restore", base_clean);
        ok = write_restore(base_clean, work, restore, job, error);
        if (ok && !file_replace(work, base_clean)) {
            ok = write_failed(error, base_clean, errno);
        }
    }
    if (!ok) {
        file_remove(work);
        return false;
    }
    DiscHashResult hashed = streamed ? DISC_HASHED : disc_hash(target, job, "verify", hex);
    if (hashed != DISC_HASHED) {
        return install_fail(error, hashed == DISC_CANCELLED ? "cancelled" : "internal",
                            hashed == DISC_CANCELLED ? "cancelled" : "%s cannot be read", target);
    }
    text_add(answer, "{\"path\": ");
    text_add_json_string(answer, target);
    text_add(answer, ", \"sha256\": ");
    text_add_json_string(answer, hex);
    text_add(answer, "}");
    return true;
}
