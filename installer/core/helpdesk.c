/*
 * The edition's HelpDesk lines (helpdesk.h), as tools/gen_helpdesk.py and tools/splice_helpdesk.py make and place
 * them, byte for byte.
 *
 * The disc's lines: the table of contents (sector 1500) has the help-audio group at byte 0x1AB8, a word per line,
 * the first sector of its VAG file, 150 lines per language in language order, English first. The edition's lines
 * take language 1's block (slots 150-299), which no disc fills: the game reads slot id + 150 * language, so English
 * line 150 + k is that block's slot k. Each new line is joined from pieces of the English lines by its cut list and
 * encoded (vag.c), the lines side by side on the thread pool, then written at the end of the image in slot order.
 */
#include "helpdesk.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "disc.h"
#include "files.h"
#include "json.h"
#include "pool.h"
#include "vag.h"

#define HELP_AUDIO_TABLE 0x1AB8u    /* in the table of contents */
#define LINES_PER_LANGUAGE 150
#define FIRST_SLOT 150
#define LAST_SLOT 299
#define MAX_CLIPS 32

/* An English line of the disc, decoded. */
struct source {
    int line;
    int16_t *samples;
    size_t count;
};

/* A new line: its cut list, then the VAG file made from it. */
struct line {
    int slot;
    const char *name;
    struct vag_clip clips[MAX_CLIPS];
    size_t clip_count;
    uint8_t *vag;               /* padded to whole sectors */
    size_t size;
};

/* The lines to make, and the disc's lines they are cut from, each decoded once. */
struct helpdesk {
    struct source sources[LINES_PER_LANGUAGE];
    size_t source_count;
    struct line *lines;
    size_t line_count;
};

static uint32_t u32_be(const uint8_t *at)
{
    return (uint32_t)at[0] << 24 | (uint32_t)at[1] << 16 | (uint32_t)at[2] << 8 | at[3];
}

static uint32_t u32_le(const uint8_t *at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

static bool is_left_out(const char *use, const char *const *left_out)
{
    for (; left_out && *left_out; left_out++) {
        if (strcmp(use, *left_out) == 0)
            return true;
    }
    return false;
}

/* KEY of the clip as a number in *OUT; false when it is missing or no number. */
static bool get_number(const Json *clip, const char *key, double *out)
{
    const Json *value = json_get(clip, key);

    if (!value || value->type != JSON_NUMBER)
        return false;
    *out = value->number;
    return true;
}

/* The disc's English line LINE as a VAG file (its header and data), checked to be named NAME. TABLE is the disc's
 * help-audio table. */
static uint8_t *disc_line(FILE *disc, const uint8_t *table, int64_t line, const char *name, size_t *size,
                          InstallError *error)
{
    uint32_t sector;
    uint8_t head[VAG_HEADER], *vag;
    char found[17] = {0};

    if (line < 0 || line >= LINES_PER_LANGUAGE || (sector = u32_le(table + 4 * line)) == 0) {
        install_fail(error, "internal", "the disc has no English HelpDesk line %lld", (long long)line);
        return NULL;
    }
    if (!file_read_at(disc, (uint64_t)sector * ISO_SECTOR, head, sizeof head) || memcmp(head, "VAGp", 4) != 0) {
        install_fail(error, "internal", "HelpDesk line %d: no VAG file at sector %u", (int)line, sector);
        return NULL;
    }
    memcpy(found, head + 0x20, 16);
    if (strncmp(found, name, 16) != 0) {
        install_fail(error, "internal", "HelpDesk line %d on the disc is '%s', the cut list expects '%s'", (int)line,
                     found, name);
        return NULL;
    }
    *size = VAG_HEADER + u32_be(head + 12);
    vag = malloc(*size);
    if (!vag || !file_read_at(disc, (uint64_t)sector * ISO_SECTOR, vag, *size)) {
        free(vag);
        install_fail(error, "internal", "HelpDesk line %d: cannot read %zu bytes at sector %u", (int)line, *size,
                     sector);
        return NULL;
    }
    return vag;
}

/* The disc's English line LINE, named NAME, decoded: from the ones decoded so far, or read now. */
static const struct source *source_line(struct helpdesk *helpdesk, FILE *disc, const uint8_t *table, int64_t line,
                                        const char *name, InstallError *error)
{
    struct source *source;
    size_t vag_size = 0;
    uint8_t *vag;

    for (size_t s = 0; s < helpdesk->source_count; s++) {
        if (helpdesk->sources[s].line == line)
            return &helpdesk->sources[s];
    }
    /* a line outside the language's 150 is refused, so the sources never outnumber them */
    vag = disc_line(disc, table, line, name, &vag_size, error);
    if (!vag)
        return NULL;
    source = &helpdesk->sources[helpdesk->source_count];
    source->line = (int)line;
    source->samples = vag_decode(vag, vag_size, &source->count);
    free(vag);
    if (!source->samples) {
        install_fail(error, "internal", "HelpDesk line %d does not decode", (int)line);
        return NULL;
    }
    helpdesk->source_count++;
    return source;
}

/* The cut lists ALL (helpdesk.json's "lines") as lines to make, but for those whose "use" is in LEFT_OUT_USES, with
 * the disc's lines they take decoded. */
static bool read_cut_lists(struct helpdesk *helpdesk, const Json *all, const char *const *left_out_uses, FILE *disc,
                           const uint8_t *table, InstallError *error)
{
    for (size_t k = 0; k < all->count; k++) {
        const Json *entry = &all->items[k], *clips = json_get(entry, "clips");
        const char *use = json_get_string(entry, "use"), *name = json_get_string(entry, "name");
        int64_t slot = json_get_integer(entry, "slot", -1);
        struct line *line;

        if (!use || !name || !clips || clips->type != JSON_ARRAY || slot < FIRST_SLOT || slot > LAST_SLOT ||
            clips->count == 0 || clips->count > MAX_CLIPS) {
            install_fail(error, "internal", "helpdesk.json: line %zu is not a cut list it can make", k);
            return false;
        }
        if (is_left_out(use, left_out_uses))
            continue;
        line = &helpdesk->lines[helpdesk->line_count++];
        line->slot = (int)slot;
        line->name = name;
        for (size_t c = 0; c < clips->count; c++) {
            const Json *clip = &clips->items[c];
            struct vag_clip *to = &line->clips[line->clip_count++];
            const char *source_name = json_get_string(clip, "source");
            const struct source *source;

            if (!source_name || !get_number(clip, "at", &to->at) || !get_number(clip, "length", &to->length) ||
                !get_number(clip, "from", &to->from) || !get_number(clip, "fade_in", &to->fade_in) ||
                !get_number(clip, "fade_out", &to->fade_out)) {
                install_fail(error, "internal", "helpdesk.json: slot %d, clip %zu is incomplete", line->slot, c);
                return false;
            }
            source = source_line(helpdesk, disc, table, json_get_integer(clip, "line", -1), source_name, error);
            if (!source)
                return false;
            to->samples = source->samples;
            to->count = source->count;
        }
    }
    return true;
}

/* Line K of the lines ARG, made: joined from its clips and encoded (NULL when out of memory). */
static void make_line(void *arg, size_t k)
{
    struct line *line = (struct line *)arg + k;
    size_t count = 0;
    int16_t *samples = vag_render(line->clips, line->clip_count, &count);

    line->vag = samples ? vag_encode(samples, count, line->name, &line->size) : NULL;
    free(samples);
}

static int by_slot(const void *a, const void *b)
{
    return ((const struct line *)a)->slot - ((const struct line *)b)->slot;
}

/* The made lines at the end of IMAGE in slot order, each entered in its help-audio table; progress from DONE of
 * TOTAL steps. */
static bool write_lines(IsoImage *image, struct helpdesk *helpdesk, Job *job, size_t done, size_t total,
                        InstallError *error)
{
    qsort(helpdesk->lines, helpdesk->line_count, sizeof *helpdesk->lines, by_slot);
    for (size_t k = 0; k < helpdesk->line_count; k++) {
        const struct line *line = &helpdesk->lines[k];
        uint64_t entry = (uint64_t)DISC_TOC_SECTOR * ISO_SECTOR + HELP_AUDIO_TABLE + 4u * (uint32_t)line->slot;
        uint8_t old[4];
        uint32_t sector;

        if (job_cancelled(job)) {
            install_fail(error, "cancelled", "cancelled");
            return false;
        }
        if (!file_read_at(image->file, entry, old, sizeof old) || u32_le(old) != 0) {
            install_fail(error, "internal", "HelpDesk slot %d is already filled: the image is not a fresh copy",
                         line->slot);
            return false;
        }
        sector = iso_image_append(image, line->vag, line->size);
        iso_image_write_word(image, entry, sector);
        job_log(job, "  slot %d: %d_%s.vag, %zu bytes at sector %u", line->slot, line->slot, line->name,
                line->size, sector);
        job_progress(job, "patch", ++done, total);
    }
    if (image->failed) {
        install_fail(error, image->error == ENOSPC ? "no_space" : "not_writable", "the image could not be written");
        return false;
    }
    job_log(job, "%zu HelpDesk lines in slots %d-%d", helpdesk->line_count, helpdesk->lines[0].slot,
            helpdesk->lines[helpdesk->line_count - 1].slot);
    return true;
}

bool patch_helpdesk(IsoImage *image, const char *rac1, const uint8_t *recipes, size_t size,
                    const char *const *left_out_uses, Job *job, InstallError *error)
{
    char parse_error[128];
    Json *json = json_parse((const char *)recipes, size, parse_error, sizeof parse_error);
    const Json *all = json ? json_get(json, "lines") : NULL;
    struct helpdesk helpdesk = {.source_count = 0};
    uint8_t table[4 * LINES_PER_LANGUAGE];
    FILE *disc = NULL;
    unsigned threads = pool_cpus();
    size_t total;
    bool ok = false;

    if (!all || all->type != JSON_ARRAY) {
        install_fail(error, "internal", "helpdesk.json: %s", json ? "no lines" : parse_error);
        goto out;
    }
    helpdesk.lines = calloc(all->count + 1, sizeof *helpdesk.lines);
    disc = file_open(rac1, "rb");
    if (!helpdesk.lines || !disc) {
        install_fail(error, "internal", disc ? "out of memory" : "cannot read the PAL disc %s", rac1);
        goto out;
    }
    if (!file_read_at(disc, (uint64_t)DISC_TOC_SECTOR * ISO_SECTOR + HELP_AUDIO_TABLE, table, sizeof table)) {
        install_fail(error, "internal", "cannot read the disc's help-audio table");
        goto out;
    }
    if (!read_cut_lists(&helpdesk, all, left_out_uses, disc, table, error))
        goto out;
    if (helpdesk.line_count == 0) {
        ok = true;
        goto out;
    }

    total = 2 * helpdesk.line_count;
    job_progress(job, "patch", 0, total);
    pool_run(&threads, make_line, helpdesk.lines, helpdesk.line_count);
    for (size_t k = 0; k < helpdesk.line_count; k++) {
        if (!helpdesk.lines[k].vag) {
            install_fail(error, "internal", "out of memory making HelpDesk line %d", helpdesk.lines[k].slot);
            goto out;
        }
    }
    job_progress(job, "patch", helpdesk.line_count, total);
    ok = write_lines(image, &helpdesk, job, helpdesk.line_count, total, error);

out:
    for (size_t s = 0; s < helpdesk.source_count; s++)
        free(helpdesk.sources[s].samples);
    for (size_t k = 0; helpdesk.lines && k < helpdesk.line_count; k++)
        free(helpdesk.lines[k].vag);
    free(helpdesk.lines);
    if (disc)
        fclose(disc);
    json_free(json);
    return ok;
}
