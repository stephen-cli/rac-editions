/*
 * rac-levels: the edition's patch of the levels' data (levels.h) on an image, from the command line.
 *
 *   rac-levels IMAGE RAC1 [RAC2] [--without ID ...]
 *
 * IMAGE is an edition's image as tools/install.py leaves it before its patch stage; RAC1 and RAC2 the player's
 * discs (without RAC2, no Charge Boots). Kalebo III's sign is made from RAC1 (sign.h). --without names a switch
 * left out (charge_boots). An image that ends with a blank tail of ISO_TAIL_SECTORS sectors (one that grew)
 * keeps it after anything the patch appends.
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
    fputs("usage: rac-levels IMAGE RAC1 [RAC2] [--without ID ...]\n", stderr);
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

int main(int argc, char **argv)
{
    const char *paths[3], *without[MAX_WITHOUT + 1];
    size_t path_count = 0, without_count = 0, sign_size = 0;
    Text discs = {NULL, 0, 0, false};
    Job job = {.progress = print_progress, .log = print_log};
    InstallError error = {NULL, {0}};
    IsoImage image;
    Json *disc_json;
    uint8_t *sign;
    uint32_t sectors;
    struct timespec started, finished;
    bool ok;

    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "--without") == 0 && k + 1 < argc && without_count < MAX_WITHOUT)
            without[without_count++] = argv[++k];
        else if (argv[k][0] != '-' && path_count < 3)
            paths[path_count++] = argv[k];
        else
            return usage();
    }
    without[without_count] = NULL;
    if (path_count < 2)
        return usage();

    text_add(&discs, "{\"rac1\": ");
    text_add_json_string(&discs, paths[1]);
    if (path_count == 3) {
        text_add(&discs, ", \"rac2\": ");
        text_add_json_string(&discs, paths[2]);
    }
    text_add(&discs, "}");
    disc_json = discs.failed ? NULL : json_parse(discs.text, discs.length, NULL, 0);
    if (!disc_json) {
        fputs("rac-levels: out of memory\n", stderr);
        return 1;
    }
    sign = make_sign(paths[1], &sign_size, &error);
    if (!sign) {
        fprintf(stderr, "rac-levels: %s (%s)\n", error.message, error.code);
        return 1;
    }
    if (!iso_image_open(&image, paths[0])) {
        fprintf(stderr, "rac-levels: cannot open %s\n", paths[0]);
        return 1;
    }
    /* the blank tail an image that grew ends with stays its last part: what the patch appends goes before it */
    if (ends_blank(&image)) {
        image.sectors -= ISO_TAIL_SECTORS;
        image.grew = true;
    }
    sectors = image.sectors;

    BuildFacts facts = {NULL, disc_json, without, NULL};
    timespec_get(&started, TIME_UTC);
    ok = patch_levels(&image, &facts, sign, sign_size, &job, &error);
    fprintf(stderr, "\n");
    if (image.sectors == sectors)
        image.grew = false;         /* nothing appended: the image keeps its tail and volume size as they are */
    if (!iso_image_finish(&image) && ok) {
        fprintf(stderr, "rac-levels: %s could not be written\n", paths[0]);
        ok = false;
    } else if (!ok) {
        fprintf(stderr, "rac-levels: %s (%s)\n", error.message, error.code);
    } else {
        timespec_get(&finished, TIME_UTC);
        printf("patched in %.2f s\n", (double)(finished.tv_sec - started.tv_sec) +
                                      (double)(finished.tv_nsec - started.tv_nsec) / 1e9);
    }
    json_free(disc_json);
    text_free(&discs);
    free(sign);
    return ok ? 0 : 1;
}
