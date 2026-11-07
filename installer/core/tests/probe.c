/*
 * Runs one piece of the core on real inputs, to check it against the same step of tools/install.py:
 *
 *   probe segment IMAGE SEGMENT ADDRESS OUT     the boot executable of IMAGE with SEGMENT added at ADDRESS (hex)
 *   probe replace IMAGE NAME FILE               NAME in IMAGE replaced by FILE's bytes, then the image finished
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf.h"
#include "files.h"
#include "iso.h"

static int segment(const char *image_path, const char *segment_path, const char *address_text, const char *out_path)
{
    FILE *image = file_open(image_path, "rb");
    if (image == NULL) {
        fprintf(stderr, "%s cannot be opened\n", image_path);
        return 1;
    }
    size_t exe_size = 0;
    uint8_t *exe = iso_read_file(image, "SCES_509.16;1", &exe_size);
    fclose(image);
    size_t segment_size = 0;
    char *segment_bytes = file_read_all(segment_path, &segment_size);
    if (exe == NULL || segment_bytes == NULL) {
        fprintf(stderr, "the executable or the segment cannot be read\n");
        return 1;
    }
    size_t result_size = 0;
    uint8_t *result = elf_add_segment(exe, exe_size, (uint32_t)strtoul(address_text, NULL, 16),
                                      (const uint8_t *)segment_bytes, segment_size, &result_size);
    if (result == NULL) {
        fprintf(stderr, "no room for another program header\n");
        return 1;
    }
    FILE *out = file_open(out_path, "wb");
    if (out == NULL || fwrite(result, 1, result_size, out) != result_size || fclose(out) != 0) {
        fprintf(stderr, "%s cannot be written\n", out_path);
        return 1;
    }
    free(result);
    free(segment_bytes);
    free(exe);
    return 0;
}

static int replace(const char *image_path, const char *name, const char *file_path)
{
    size_t size = 0;
    char *bytes = file_read_all(file_path, &size);
    IsoImage image;
    if (bytes == NULL || !iso_image_open(&image, image_path)) {
        fprintf(stderr, "the image or the file cannot be opened\n");
        return 1;
    }
    bool replaced = iso_image_replace_file(&image, name, bytes, size);
    bool finished = iso_image_finish(&image);
    free(bytes);
    if (!replaced || !finished) {
        fprintf(stderr, "%s could not be replaced\n", name);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 6 && strcmp(argv[1], "segment") == 0) {
        return segment(argv[2], argv[3], argv[4], argv[5]);
    }
    if (argc == 5 && strcmp(argv[1], "replace") == 0) {
        return replace(argv[2], argv[3], argv[4]);
    }
    fprintf(stderr, "usage: probe segment IMAGE SEGMENT ADDRESS OUT | replace IMAGE NAME FILE\n");
    return 2;
}
