#include "disc.h"

#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "iso.h"
#include "sha256.h"

/* Read in chunks this size: large enough that progress events stay few, small enough to cancel quickly. */
#define HASH_CHUNK (16u << 20)

DiscHashResult disc_hash(const char *path, Job *job, const char *stage, char hex[65])
{
    FILE *file = file_open(path, "rb");
    if (file == NULL) {
        return DISC_UNREADABLE;
    }
    uint64_t total = file_size(file);
    file_seek(file, 0);
    uint8_t *chunk = malloc(HASH_CHUNK);
    if (chunk == NULL) {
        fclose(file);
        return DISC_UNREADABLE;
    }
    Sha256 hash;
    sha256_start(&hash);
    uint64_t done = 0;
    DiscHashResult result = DISC_HASHED;
    for (;;) {
        if (job_cancelled(job)) {
            result = DISC_CANCELLED;
            break;
        }
        size_t got = fread(chunk, 1, HASH_CHUNK, file);
        if (got == 0) {
            if (ferror(file)) {
                result = DISC_UNREADABLE;
            }
            break;
        }
        sha256_add(&hash, chunk, got);
        done += got;
        job_progress(job, stage, done, total);
    }
    free(chunk);
    fclose(file);
    if (result == DISC_HASHED) {
        uint8_t digest[32];
        sha256_finish(&hash, digest);
        sha256_hex(digest, hex);
    }
    return result;
}

/* The BOOT2 line's executable in SYSTEM.CNF ("BOOT2 = cdrom0:\SCES_509.16;1" gives "SCES_509.16"). */
static void boot_file_of(const char *cnf, size_t size, char *boot_file, size_t capacity)
{
    boot_file[0] = 0;
    size_t line_start = 0;
    while (line_start < size) {
        size_t line_end = line_start;
        while (line_end < size && cnf[line_end] != '\n' && cnf[line_end] != '\r') {
            line_end++;
        }
        if (line_end - line_start >= 5 && memcmp(cnf + line_start, "BOOT2", 5) == 0) {
            size_t name_start = line_start;
            for (size_t i = line_start; i < line_end; i++) {
                if (cnf[i] == '\\') {
                    name_start = i + 1;
                }
            }
            size_t name_end = name_start;
            while (name_end < line_end && cnf[name_end] != ';') {
                name_end++;
            }
            size_t length = name_end - name_start;
            if (length >= capacity) {
                length = capacity - 1;
            }
            memcpy(boot_file, cnf + name_start, length);
            boot_file[length] = 0;
            return;
        }
        line_start = line_end + 1;
    }
}

bool disc_kind(const char *path, DiscKind *kind)
{
    memset(kind, 0, sizeof *kind);
    FILE *image = file_open(path, "rb");
    if (image == NULL) {
        return false;
    }
    size_t cnf_size = 0;
    uint8_t *cnf = iso_read_file(image, "SYSTEM.CNF;1", &cnf_size);
    if (cnf != NULL) {
        kind->is_ps2_disc = true;
        boot_file_of((const char *)cnf, cnf_size, kind->boot_file, sizeof kind->boot_file);
        free(cnf);
        uint64_t volume_bytes = (uint64_t)iso_volume_sectors(image) * ISO_SECTOR;
        kind->complete = file_size(image) >= volume_bytes;
    }
    fclose(image);
    return true;
}
