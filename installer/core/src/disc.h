/*
 * The player's disc images: their hash, and their boot file, by which the edition's boot_files name the game and
 * version (identify, in installer.c), before it knows its own discs by hash.
 */
#ifndef RAC_DISC_H
#define RAC_DISC_H

#include <stdbool.h>

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

#endif
