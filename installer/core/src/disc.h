/*
 * The player's disc images: their hash, and their boot file, by which the edition's boot_files name the game and
 * version (identify, in installer.c), before it knows its own discs by hash.
 */
#ifndef RAC_DISC_H
#define RAC_DISC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "job.h"

typedef enum {
    DISC_HASHED,
    DISC_CANCELLED,
    DISC_UNREADABLE,
} DiscHashResult;

/* PATH's SHA-256 as 64 hex digits, with progress events in STAGE as it reads. */
DiscHashResult disc_hash(const char *path, Job *job, const char *stage, char hex[65]);

typedef struct {
    bool is_ps2_disc;           /* an ISO 9660 image with a SYSTEM.CNF */
    char boot_file[32];         /* SYSTEM.CNF's BOOT2 executable, as "SCES_509.16" */
    bool complete;              /* the image holds every sector its volume descriptor counts */
} DiscKind;

/* PATH's boot file, without hashing it; false when PATH cannot be opened. */
bool disc_kind(const char *path, DiscKind *kind);

/* R&C1's table of contents, which the boot program reads at startup: each file the game reaches by sector has
   an entry {u32 sector, u32 sectors} in it. */
#define DISC_TOC_SECTOR 1500

/* The file R&C1's table of contents names at byte ENTRY of it, read whole (its sectors) from the image at PATH, in a
   new buffer the caller frees; NULL when it cannot be read. */
uint8_t *disc_read_toc_file(const char *path, uint32_t entry, size_t *size);

/* The WAD stream STREAM (SIZE bytes) decompressed into a new buffer the caller frees, of up to 64 MB; NULL when it is
   no stream the game reads, or memory is short. */
uint8_t *disc_unwad(const uint8_t *stream, size_t size, size_t *out_size);

#endif
