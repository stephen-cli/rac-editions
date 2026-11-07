/*
 * Runs one piece of the core on real inputs, to check it against the same step of tools/install.py:
 *
 *   probe segment IMAGE SEGMENT ADDRESS OUT     the boot executable of IMAGE with SEGMENT added at ADDRESS (hex)
 *   probe replace IMAGE NAME FILE               NAME in IMAGE replaced by FILE's bytes, then the image finished
 *   probe holes PAYLOAD RAC1 RAC2 OUT           every hole of PAYLOAD filled from the discs, to OUT/SYMBOL.bin
 *   probe build PAYLOAD COMMAND.json DATA       a build command's discs, options and output, built without the
 *                                               patch stage; its events printed as JSON lines
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "elf.h"
#include "files.h"
#include "fill.h"
#include "iso.h"
#include "json.h"

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

/* Every hole of PAYLOAD's holes.json filled from the discs, each to OUT/SYMBOL.bin; a hole that cannot be made is
   named on stderr. */
static int holes(const char *payload, const char *rac1, const char *rac2, const char *out_folder)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/holes.json", payload);
    size_t size = 0;
    char *text = file_read_all(path, &size);
    Json *list = text != NULL ? json_parse(text, size, NULL, 0) : NULL;
    free(text);
    Text discs_text = {0};
    text_add(&discs_text, "{\"rac1\": ");
    text_add_json_string(&discs_text, rac1);
    text_add(&discs_text, ", \"rac2\": ");
    text_add_json_string(&discs_text, rac2);
    text_add(&discs_text, "}");
    Json *discs = json_parse(discs_text.text, discs_text.length, NULL, 0);
    text_free(&discs_text);
    Filler *filler = filler_new(discs);
    if (list == NULL || list->type != JSON_ARRAY || discs == NULL || filler == NULL) {
        fprintf(stderr, "%s cannot be read\n", path);
        return 1;
    }
    int failures = 0;
    for (size_t i = 0; i < list->count; i++) {
        const Json *hole = &list->items[i];
        const char *symbol = json_get_string(hole, "symbol");
        size_t hole_size = (size_t)json_get_integer(hole, "size", 0);
        uint8_t *bytes = malloc(hole_size > 0 ? hole_size : 1);
        char error[1024];
        if (bytes == NULL || !filler_fill(filler, symbol, json_get(hole, "fill"), bytes, hole_size, error,
                                          sizeof error)) {
            fprintf(stderr, "%s\n", bytes == NULL ? "out of memory" : error);
            failures++;
        } else {
            snprintf(path, sizeof path, "%s/%s.bin", out_folder, symbol);
            FILE *out = file_open(path, "wb");
            if (out == NULL || fwrite(bytes, 1, hole_size, out) != hole_size || fclose(out) != 0) {
                fprintf(stderr, "%s cannot be written\n", path);
                failures++;
            }
        }
        free(bytes);
    }
    filler_free(filler);
    json_free(discs);
    json_free(list);
    return failures == 0 ? 0 : 1;
}

static void print_progress(Job *job, const char *stage, uint64_t done, uint64_t total)
{
    (void)job;
    printf("{\"event\": \"progress\", \"stage\": \"%s\", \"done\": %llu, \"total\": %llu}\n", stage,
           (unsigned long long)done, (unsigned long long)total);
}

static void print_log(Job *job, const char *text)
{
    (void)job;
    Text line = {0};
    text_add(&line, "{\"event\": \"log\", \"text\": ");
    text_add_json_string(&line, text);
    text_add(&line, "}");
    printf("%s\n", line.text);
    text_free(&line);
}

/* A build without the patch stage: PAYLOAD, a JSON command's discs, options and output (in COMMAND_FILE), its
   events and its answer printed as JSON lines. */
static int build(const char *payload_directory, const char *command_file, const char *data)
{
    size_t size = 0;
    char *text = file_read_all(command_file, &size);
    Json *command = text != NULL ? json_parse(text, size, NULL, 0) : NULL;
    free(text);
    Payload payload;
    InstallError error = {0};
    if (command == NULL || !payload_open(&payload, payload_directory, &error)) {
        fprintf(stderr, "the command or the payload cannot be read: %s\n", error.message);
        return 1;
    }
    HashCache *cache = hash_cache_open(data);
    Job job = {.progress = print_progress, .log = print_log};
    Text answer = {0};
    bool built = command_build(&payload, cache, json_get(command, "discs"), json_get(command, "options"),
                               json_get(command, "output"), NULL, NULL, &job, &answer, &error);
    if (built) {
        printf("{\"event\": \"result\", \"value\": %s}\n", answer.text);
    } else {
        Text line = {0};
        text_add(&line, "{\"event\": \"error\", \"code\": ");
        text_add_json_string(&line, error.code);
        text_add(&line, ", \"message\": ");
        text_add_json_string(&line, error.message);
        text_add(&line, "}");
        printf("%s\n", line.text);
        text_free(&line);
    }
    text_free(&answer);
    hash_cache_close(cache);
    payload_close(&payload);
    json_free(command);
    return built ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 5 && strcmp(argv[1], "build") == 0) {
        return build(argv[2], argv[3], argv[4]);
    }
    if (argc == 6 && strcmp(argv[1], "holes") == 0) {
        return holes(argv[2], argv[3], argv[4], argv[5]);
    }
    if (argc == 6 && strcmp(argv[1], "segment") == 0) {
        return segment(argv[2], argv[3], argv[4], argv[5]);
    }
    if (argc == 5 && strcmp(argv[1], "replace") == 0) {
        return replace(argv[2], argv[3], argv[4]);
    }
    fprintf(stderr, "usage: probe segment IMAGE SEGMENT ADDRESS OUT | replace IMAGE NAME FILE\n"
                    "       probe holes PAYLOAD RAC1 RAC2 OUT\n"
                    "       probe build PAYLOAD COMMAND.json DATA\n");
    return 2;
}
