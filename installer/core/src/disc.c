#include "disc.h"

#include <stdlib.h>
#include <string.h>

#include "bytes.h"
#include "files.h"
#include "iso.h"
#include "sha256.h"
#include "thread.h"
#include "wad.h"

/* Read in chunks this size: large enough that progress events stay few, small enough to cancel quickly. */
#define HASH_CHUNK (16u << 20)

/* A chunk of the file being hashed, read on a thread of its own while the chunk before it is hashed. */
typedef struct {
    FILE *file;
    uint8_t *into;
    size_t got;
    bool failed;
} HashRead;

static void hash_read(void *argument)
{
    HashRead *read = argument;
    read->got = fread(read->into, 1, HASH_CHUNK, read->file);
    read->failed = read->got == 0 && ferror(read->file);
}

/* Each chunk is hashed while the next is read, so the time is the slower of the two, not both. */
DiscHashResult disc_hash(const char *path, Job *job, const char *stage, char hex[65])
{
    FILE *file = file_open(path, "rb");
    if (file == NULL) {
        return DISC_UNREADABLE;
    }
    uint64_t total = file_size(file);
    file_seek(file, 0);
    uint8_t *chunks[2] = {malloc(HASH_CHUNK), malloc(HASH_CHUNK)};
    if (chunks[0] == NULL || chunks[1] == NULL) {
        free(chunks[0]);
        free(chunks[1]);
        fclose(file);
        return DISC_UNREADABLE;
    }
    Sha256 hash;
    sha256_start(&hash);
    uint64_t done = 0;
    DiscHashResult result = DISC_HASHED;
    HashRead current = {file, chunks[0], 0, false};
    hash_read(&current);
    for (;;) {
        if (job_cancelled(job)) {
            result = DISC_CANCELLED;
            break;
        }
        if (current.got == 0) {
            if (current.failed) {
                result = DISC_UNREADABLE;
            }
            break;
        }
        /* the next chunk read beside the hashing, or after it when no thread can be had */
        HashRead next = {file, current.into == chunks[0] ? chunks[1] : chunks[0], 0, false};
        Thread *reading = thread_start(hash_read, &next);
        sha256_add(&hash, current.into, current.got);
        if (reading != NULL) {
            thread_join(reading);
        } else {
            hash_read(&next);
        }
        done += current.got;
        job_progress(job, stage, done, total);
        current = next;
    }
    free(chunks[0]);
    free(chunks[1]);
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

/* The file the table of contents names at ENTRY ({sector, sectors}), read whole from the R&C1 disc at PATH. */
uint8_t *disc_read_toc_file(const char *path, uint32_t entry, size_t *size)
{
    FILE *file = file_open(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    uint8_t place[8];
    uint8_t *bytes = NULL;
    if (file_read_at(file, (uint64_t)DISC_TOC_SECTOR * ISO_SECTOR + entry, place, sizeof place)) {
        uint64_t sector = read_u32_le(place);
        size_t length = (size_t)read_u32_le(place + 4) * ISO_SECTOR;
        bytes = malloc(length > 0 ? length : 1);
        if (bytes != NULL && !file_read_at(file, sector * ISO_SECTOR, bytes, length)) {
            free(bytes);
            bytes = NULL;
        }
        *size = length;
    }
    fclose(file);
    return bytes;
}

/* A WAD stream decompressed into a new buffer the caller frees; NULL when it is not one the game reads. */
uint8_t *disc_unwad(const uint8_t *stream, size_t size, size_t *out_size)
{
    for (size_t capacity = 1 << 22; capacity <= (size_t)64 << 20; capacity *= 2) {
        uint8_t *out = malloc(capacity);
        if (out == NULL) {
            return NULL;
        }
        enum wad_error result = wad_decompress(stream, size, out, capacity, out_size);
        if (result == WAD_OK) {
            return out;
        }
        free(out);
        if (result != WAD_TOO_LARGE) {
            return NULL;
        }
    }
    return NULL;
}
