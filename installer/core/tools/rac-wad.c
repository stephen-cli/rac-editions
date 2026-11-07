/*
 * rac-wad: the games' WAD compression from the command line.
 *
 *   rac-wad compress IN -o OUT [-j N]  IN as a WAD stream, checked by decoding it back; N jobs at once (default:
 *                                      one per CPU; the stream is the same for any N)
 *   rac-wad decompress IN -o OUT       a WAD stream's bytes, read as the game reads them
 */
#include "pool.h"
#include "wad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------------------------------- files */

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

static int usage(void)
{
    fputs("usage: rac-wad compress IN -o OUT [-j N]\n       rac-wad decompress IN -o OUT\n", stderr);
    return 2;
}

int main(int argc, char **argv)
{
    uint8_t *in, *out;
    size_t in_size = 0, out_size = 0;
    unsigned threads = pool_cpus();

    if (argc == 7 && strcmp(argv[5], "-j") == 0 && atoi(argv[6]) > 0)
        threads = (unsigned)atoi(argv[6]);
    else if (argc != 5)
        return usage();
    if (strcmp(argv[3], "-o") != 0)
        return usage();
    in = read_all(argv[2], &in_size);
    if (!in) {
        fprintf(stderr, "rac-wad: cannot read %s\n", argv[2]);
        return 1;
    }

    if (strcmp(argv[1], "compress") == 0) {
        uint8_t *check;
        size_t check_size = 0;
        enum wad_error error;

        out = malloc(wad_bound(in_size));
        check = malloc(in_size + 1);
        if (!out || !check || !(out_size = wad_compress(in, in_size, out, pool_run, &threads))) {
            fputs("rac-wad: out of memory\n", stderr);
            return 1;
        }
        error = wad_decompress(out, out_size, check, in_size, &check_size);
        if (error != WAD_OK || check_size != in_size || memcmp(check, in, in_size) != 0) {
            fprintf(stderr, "rac-wad: the stream does not decode to the input (%s)\n", wad_error_text(error));
            return 1;
        }
        free(check);
    } else if (strcmp(argv[1], "decompress") == 0) {
        size_t capacity = 64u << 20;
        enum wad_error error;

        out = malloc(capacity);
        if (!out) {
            fputs("rac-wad: out of memory\n", stderr);
            return 1;
        }
        error = wad_decompress(in, in_size, out, capacity, &out_size);
        if (error != WAD_OK) {
            fprintf(stderr, "rac-wad: %s: %s\n", argv[2], wad_error_text(error));
            return 1;
        }
    } else {
        return usage();
    }

    if (!write_all(argv[4], out, out_size)) {
        fprintf(stderr, "rac-wad: cannot write %s\n", argv[4]);
        return 1;
    }
    free(in);
    free(out);
    return 0;
}
