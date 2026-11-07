/*
 * rac-installer, the installer core's command line.
 *
 *   rac-installer --serve --payload DIR [--data DIR]   the builder's protocol on standard input and output
 *   rac-installer identify IMAGE --payload DIR [--data DIR]
 *   rac-installer kind IMAGE                            the image's boot file, and whether it is complete
 *   rac-installer hash IMAGE                            the image's SHA-256
 *
 * --payload is an edition's release payload (its tools/release.py writes it); --data is a folder kept between runs,
 * with the discs' hashes, so a disc is hashed once.
 */
#include <stdio.h>
#include <string.h>

#include "disc.h"
#include "hash_cache.h"
#include "installer.h"
#include "serve.h"

static void print_progress(Job *job, const char *stage, uint64_t done, uint64_t total)
{
    (void)job;
    fprintf(stderr, "\r%s %3u%%", stage, (unsigned)(total ? done * 100 / total : 100));
    if (done == total) {
        fprintf(stderr, "\n");
    }
}

static void print_log(Job *job, const char *text)
{
    (void)job;
    fprintf(stderr, "%s\n", text);
}

static int command_kind(const char *path)
{
    DiscKind kind;
    if (!disc_kind(path, &kind)) {
        fprintf(stderr, "%s: cannot be opened\n", path);
        return 1;
    }
    if (!kind.is_ps2_disc) {
        printf("%s: not an image of a PS2 disc\n", path);
        return 0;
    }
    printf("boot %s, %s\n", kind.boot_file, kind.complete ? "complete" : "incomplete");
    return 0;
}

static int command_hash(const char *path)
{
    Job job = {.progress = print_progress};
    char hex[65];
    if (disc_hash(path, &job, "hash", hex) != DISC_HASHED) {
        fprintf(stderr, "%s: cannot be read\n", path);
        return 1;
    }
    printf("%s\n", hex);
    return 0;
}

static int usage(void)
{
    fprintf(stderr, "usage: rac-installer --serve --payload DIR [--data DIR]\n"
                    "       rac-installer identify IMAGE --payload DIR [--data DIR]\n"
                    "       rac-installer kind IMAGE | hash IMAGE\n");
    return 2;
}

int main(int argc, char **argv)
{
    const char *payload_directory = NULL;
    const char *data_directory = NULL;
    const char *command = NULL;
    const char *image = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--serve") == 0) {
            command = "serve";
        } else if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc) {
            payload_directory = argv[++i];
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data_directory = argv[++i];
        } else if (command == NULL && argv[i][0] != '-') {
            command = argv[i];
        } else if (image == NULL && argv[i][0] != '-') {
            image = argv[i];
        } else {
            return usage();
        }
    }
    if (command == NULL) {
        return usage();
    }
    if (strcmp(command, "kind") == 0 && image != NULL) {
        return command_kind(image);
    }
    if (strcmp(command, "hash") == 0 && image != NULL) {
        return command_hash(image);
    }
    if (payload_directory == NULL || (strcmp(command, "serve") != 0 && strcmp(command, "identify") != 0)) {
        return usage();
    }

    Payload payload;
    InstallError error = {0};
    if (!payload_open(&payload, payload_directory, &error)) {
        fprintf(stderr, "rac-installer: %s (%s)\n", error.message, error.code);
        payload_close(&payload);
        return 1;
    }
    HashCache *cache = hash_cache_open(data_directory);
    int code = 0;
    if (strcmp(command, "serve") == 0) {
        code = serve(&payload, cache, stdin, stdout);
    } else if (image != NULL) {
        Job job = {.progress = print_progress, .log = print_log};
        Text answer = {0};
        if (command_identify(&payload, cache, image, &job, &answer, &error)) {
            printf("%s\n", answer.text);
        } else {
            fprintf(stderr, "rac-installer: %s (%s)\n", error.message, error.code);
            code = 1;
        }
        text_free(&answer);
    } else {
        code = usage();
    }
    hash_cache_close(cache);
    payload_close(&payload);
    return code;
}
