/*
 * rac-vag: the HelpDesk lines' audio from the command line (vag.h).
 *
 *   rac-vag JOBS [-j N]
 *
 * JOBS is a text file, a command per line, tools/gen_helpdesk.py's: "src ID PATH" a source line's VAG file;
 * "line NAME OUT" a new line, written as a VAG file to OUT; "clip ID AT LENGTH FROM FADE_IN FADE_OUT" a piece of
 * the line before it, from source ID (times as Python writes floats, so they read back exactly); "preview WAV"
 * the line before it also decoded, as the game plays it, to a WAV file. The new lines are made N at once (default:
 * one per CPU).
 */
#include "pool.h"
#include "vag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SOURCES 160
#define MAX_LINES 64
#define MAX_CLIPS 32
#define PATH_MAX_LEN 1024

struct source {
    int id;
    int16_t *samples;
    size_t count;
};

struct line {
    char name[17];
    char out[PATH_MAX_LEN];
    char preview[PATH_MAX_LEN];
    struct vag_clip clips[MAX_CLIPS];
    int ids[MAX_CLIPS];
    size_t clip_count;
    int failed;
};

struct jobs {
    struct source sources[MAX_SOURCES];
    size_t source_count;
    struct line lines[MAX_LINES];
    size_t line_count;
};

static uint8_t *read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long length;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (length = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        data = malloc((size_t)length + 1);
        if (data && fread(data, 1, (size_t)length, f) != (size_t)length) {
            free(data);
            data = NULL;
        }
        *size = (size_t)length;
    }
    fclose(f);
    return data;
}

static int write_all(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(data, 1, size, f) == size;

    if (f && fclose(f) != 0)
        ok = 0;
    return ok;
}

static void put_u32_le(uint8_t *at, uint32_t v)
{
    at[0] = (uint8_t)v;
    at[1] = (uint8_t)(v >> 8);
    at[2] = (uint8_t)(v >> 16);
    at[3] = (uint8_t)(v >> 24);
}

/* SAMPLES as a 16-bit mono WAV file, as Python's wave module writes it. */
static int write_wav(const char *path, const int16_t *samples, size_t count)
{
    size_t size = 44 + 2 * count;
    uint8_t *wav = malloc(size);
    int ok;

    if (!wav)
        return 0;
    memcpy(wav, "RIFF", 4);
    put_u32_le(wav + 4, (uint32_t)(size - 8));
    memcpy(wav + 8, "WAVEfmt ", 8);
    put_u32_le(wav + 16, 16);
    wav[20] = 1, wav[21] = 0, wav[22] = 1, wav[23] = 0;     /* PCM, one channel */
    put_u32_le(wav + 24, VAG_RATE);
    put_u32_le(wav + 28, VAG_RATE * 2);
    wav[32] = 2, wav[33] = 0, wav[34] = 16, wav[35] = 0;    /* two bytes a sample, 16 bits */
    memcpy(wav + 36, "data", 4);
    put_u32_le(wav + 40, (uint32_t)(2 * count));
    for (size_t k = 0; k < count; k++) {
        wav[44 + 2 * k] = (uint8_t)((uint16_t)samples[k] & 0xFF);
        wav[45 + 2 * k] = (uint8_t)((uint16_t)samples[k] >> 8);
    }
    ok = write_all(path, wav, size);
    free(wav);
    return ok;
}

/* A new line: rendered from its clips, encoded, written; and its preview. */
static void make_line(void *arg, size_t k)
{
    struct line *line = &((struct jobs *)arg)->lines[k];
    size_t count = 0, size = 0;
    int16_t *samples = vag_render(line->clips, line->clip_count, &count);
    uint8_t *vag = samples ? vag_encode(samples, count, line->name, &size) : NULL;

    line->failed = !vag || !write_all(line->out, vag, size);
    if (!line->failed && line->preview[0]) {
        size_t heard = 0;
        int16_t *played = vag_decode(vag, size, &heard);
        line->failed = !played || !write_wav(line->preview, played, heard);
        free(played);
    }
    free(samples);
    free(vag);
}

/* REST of a command line after its word: a path, to the line's end. */
static int copy_path(char *to, const char *rest)
{
    size_t n = strcspn(rest, "\r\n");
    if (n == 0 || n >= PATH_MAX_LEN)
        return 0;
    memcpy(to, rest, n);
    to[n] = 0;
    return 1;
}

static int parse(struct jobs *jobs, FILE *f)
{
    char text[PATH_MAX_LEN + 64];
    struct line *line = NULL;

    while (fgets(text, sizeof text, f)) {
        int id, used = 0;
        char name[64];
        double v[5];
        if (sscanf(text, "src %d %n", &id, &used) == 1 && used) {
            struct source *s = &jobs->sources[jobs->source_count];
            char path[PATH_MAX_LEN];
            uint8_t *vag;
            size_t size = 0;
            if (jobs->source_count == MAX_SOURCES || !copy_path(path, text + used))
                return 0;
            vag = read_all(path, &size);
            s->id = id;
            s->samples = vag ? vag_decode(vag, size, &s->count) : NULL;
            free(vag);
            if (!s->samples) {
                fprintf(stderr, "rac-vag: cannot read the VAG file %s\n", path);
                return 0;
            }
            jobs->source_count++;
        } else if (sscanf(text, "line %63s %n", name, &used) == 1 && used) {
            if (jobs->line_count == MAX_LINES)
                return 0;
            line = &jobs->lines[jobs->line_count++];
            memset(line, 0, sizeof *line);
            memcpy(line->name, name, strlen(name) < 16 ? strlen(name) : 16);  /* a VAG name: 16 bytes at most */
            if (!copy_path(line->out, text + used))
                return 0;
        } else if (sscanf(text, "clip %d %lf %lf %lf %lf %lf", &id, &v[0], &v[1], &v[2], &v[3], &v[4]) == 6) {
            struct vag_clip *clip;
            size_t s;
            if (!line || line->clip_count == MAX_CLIPS)
                return 0;
            for (s = 0; s < jobs->source_count && jobs->sources[s].id != id; s++)
                ;
            if (s == jobs->source_count) {
                fprintf(stderr, "rac-vag: clip of source %d, which no src command gives\n", id);
                return 0;
            }
            clip = &line->clips[line->clip_count++];
            clip->samples = jobs->sources[s].samples;
            clip->count = jobs->sources[s].count;
            clip->at = v[0], clip->length = v[1], clip->from = v[2], clip->fade_in = v[3], clip->fade_out = v[4];
        } else if (sscanf(text, "preview %n", &used) == 0 && used) {
            if (!line || !copy_path(line->preview, text + used))
                return 0;
        } else if (strspn(text, " \t\r\n") != strlen(text)) {
            fprintf(stderr, "rac-vag: cannot read the job line: %s", text);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    static struct jobs jobs;
    unsigned threads = pool_cpus();
    FILE *f;
    int ok;

    if (argc == 4 && strcmp(argv[2], "-j") == 0 && atoi(argv[3]) > 0)
        threads = (unsigned)atoi(argv[3]);
    else if (argc != 2) {
        fputs("usage: rac-vag JOBS [-j N]\n", stderr);
        return 2;
    }
    f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "rac-vag: cannot read %s\n", argv[1]);
        return 1;
    }
    ok = parse(&jobs, f);
    fclose(f);
    if (!ok) {
        fprintf(stderr, "rac-vag: %s is not a job file it can read\n", argv[1]);
        return 1;
    }
    pool_run(&threads, make_line, &jobs, jobs.line_count);
    for (size_t k = 0; k < jobs.line_count; k++) {
        if (jobs.lines[k].failed) {
            fprintf(stderr, "rac-vag: line %s failed\n", jobs.lines[k].name);
            ok = 0;
        }
    }
    for (size_t s = 0; s < jobs.source_count; s++)
        free(jobs.sources[s].samples);
    return ok ? 0 : 1;
}
