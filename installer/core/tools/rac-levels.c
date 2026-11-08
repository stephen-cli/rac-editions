/*
 * rac-levels: the edition's patch of the levels' data (levels.h), from the command line.
 *
 *   rac-levels IMAGE RAC1 [RAC2] [--without ID ...]
 *   rac-levels --check RAC1 [RAC2] [--without ID ...]
 *
 * IMAGE is an edition's image as tools/install.py leaves it before its patch stage; RAC1 and RAC2 the player's
 * discs (without RAC2, no Charge Boots). Kalebo III's sign is made from RAC1 (sign.h). --without names a switch
 * left out (charge_boots). An image that ends with a blank tail of ISO_TAIL_SECTORS sectors (one that grew)
 * keeps it after anything the patch appends.
 *
 * --check writes nothing: it patches every level of RAC1 in memory, as a build that streams the image does
 * (levels_produce), and fails when a level's new archive does not fit in the sectors the disc gives it.
 */
#include "levels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "files.h"
#include "json.h"
#include "sign.h"

#define MAX_WITHOUT 16

static void print_progress(Job *job, const char *stage, uint64_t done, uint64_t total)
{
    (void)job;
    fprintf(stderr, "%s %llu/%llu\r", stage, (unsigned long long)done, (unsigned long long)total);
}

static void print_log(Job *job, const char *text)
{
    (void)job;
    printf("%s\n", text);
    fflush(stdout);
}

static int usage(void)
{
    fputs("usage: rac-levels IMAGE RAC1 [RAC2] [--without ID ...]\n"
          "       rac-levels --check RAC1 [RAC2] [--without ID ...]\n",
          stderr);
    return 2;
}

/* Whether the image's last ISO_TAIL_SECTORS sectors are blank. */
static bool ends_blank(IsoImage *image)
{
    size_t size = (size_t)ISO_TAIL_SECTORS * ISO_SECTOR;
    uint8_t *tail = image->sectors >= ISO_TAIL_SECTORS ? malloc(size) : NULL;
    bool blank = tail && file_read_at(image->file, (uint64_t)(image->sectors - ISO_TAIL_SECTORS) * ISO_SECTOR, tail,
                                      size);

    for (size_t k = 0; blank && k < size; k++)
        blank = tail[k] == 0;
    free(tail);
    return blank;
}

/* The patch on the image PATH. */
static bool patch_image(const char *path, const BuildFacts *facts, const uint8_t *sign, size_t sign_size, Job *job,
                        InstallError *error)
{
    IsoImage image;
    uint32_t sectors;
    bool ok;

    if (!iso_image_open(&image, path)) {
        fprintf(stderr, "rac-levels: cannot open %s\n", path);
        return false;
    }
    /* the blank tail an image that grew ends with stays its last part: what the patch appends goes before it */
    if (ends_blank(&image)) {
        image.sectors -= ISO_TAIL_SECTORS;
        image.grew = true;
    }
    sectors = image.sectors;
    ok = patch_levels(&image, facts, sign, sign_size, job, error);
    fprintf(stderr, "\n");
    if (image.sectors == sectors)
        image.grew = false;         /* nothing appended: the image keeps its tail and volume size as they are */
    if (!iso_image_finish(&image) && ok) {
        fprintf(stderr, "rac-levels: %s could not be written\n", path);
        return false;
    }
    if (!ok)
        fprintf(stderr, "rac-levels: %s (%s)\n", error->message, error->code);
    return ok;
}

/* Every level of the disc RAC1 patched in memory, span by span; false at the first that does not fit. */
static bool check_levels(const char *rac1, const BuildFacts *facts, const uint8_t *sign, size_t sign_size, Job *job,
                         InstallError *error)
{
    FILE *disc = file_open(rac1, "rb");
    LevelsPlan *plan = disc ? levels_plan(disc, facts, sign, sign_size, job, error) : NULL;
    bool ok = plan != NULL;

    if (!disc)
        fprintf(stderr, "rac-levels: cannot read %s\n", rac1);
    for (int number = 0; ok && number < LEVEL_COUNT; number++) {
        uint32_t first, count;
        size_t size, archive_size = 0;
        uint8_t *span;

        levels_span(plan, number, &first, &count);
        size = (size_t)count * ISO_SECTOR;
        span = malloc(size);
        ok = span && file_read_at(disc, (uint64_t)first * ISO_SECTOR, span, size);
        if (!ok)
            fprintf(stderr, "rac-levels: level %d's span cannot be read\n", number);
        else
            ok = levels_produce(plan, number, span, size, &archive_size, error);
        free(span);
    }
    fprintf(stderr, "\n");
    if (ok)
        printf("every level fits in its sectors\n");
    else if (error->code)           /* else the failure was printed where it happened */
        fprintf(stderr, "rac-levels: %s (%s)\n", error->message, error->code);
    levels_plan_free(plan);
    if (disc)
        fclose(disc);
    return ok;
}

int main(int argc, char **argv)
{
    const char *paths[3], *without[MAX_WITHOUT + 1];
    size_t path_count = 0, without_count = 0, sign_size = 0;
    Text discs = {NULL, 0, 0, false};
    Job job = {.progress = print_progress, .log = print_log};
    InstallError error = {NULL, {0}};
    Json *disc_json;
    uint8_t *sign;
    struct timespec started, finished;
    bool check = false, ok;
    const char *const *disc_paths;

    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "--without") == 0 && k + 1 < argc && without_count < MAX_WITHOUT)
            without[without_count++] = argv[++k];
        else if (strcmp(argv[k], "--check") == 0)
            check = true;
        else if (argv[k][0] != '-' && path_count < 3)
            paths[path_count++] = argv[k];
        else
            return usage();
    }
    without[without_count] = NULL;
    /* with --check, the paths are the discs; else the image, then the discs */
    disc_paths = check ? paths : paths + 1;
    if (check ? path_count < 1 || path_count > 2 : path_count < 2)
        return usage();

    text_add(&discs, "{\"rac1\": ");
    text_add_json_string(&discs, disc_paths[0]);
    if (path_count == (check ? 2u : 3u)) {
        text_add(&discs, ", \"rac2\": ");
        text_add_json_string(&discs, disc_paths[1]);
    }
    text_add(&discs, "}");
    disc_json = discs.failed ? NULL : json_parse(discs.text, discs.length, NULL, 0);
    if (!disc_json) {
        fputs("rac-levels: out of memory\n", stderr);
        return 1;
    }
    sign = make_sign(disc_paths[0], &sign_size, &error);
    if (!sign) {
        fprintf(stderr, "rac-levels: %s (%s)\n", error.message, error.code);
        return 1;
    }

    BuildFacts facts = {NULL, disc_json, without, NULL};
    timespec_get(&started, TIME_UTC);
    ok = check ? check_levels(disc_paths[0], &facts, sign, sign_size, &job, &error)
               : patch_image(paths[0], &facts, sign, sign_size, &job, &error);
    if (ok) {
        timespec_get(&finished, TIME_UTC);
        printf("%s in %.2f s\n", check ? "checked" : "patched",
               (double)(finished.tv_sec - started.tv_sec) + (double)(finished.tv_nsec - started.tv_nsec) / 1e9);
    }
    json_free(disc_json);
    text_free(&discs);
    free(sign);
    return ok ? 0 : 1;
}
