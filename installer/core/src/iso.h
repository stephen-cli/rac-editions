/*
 * The disc-image operations the installer needs on a player's own PS2 image (common/iso_image.py in C).
 *
 * The PAL disc of Ratchet & Clank names only three files in its ISO 9660 root directory (the boot executable
 * SCES_509.16 among them); the game reaches everything else by absolute sector, through the table of contents it
 * reads at startup. So an edition changes the image in place: words written where they are, the boot executable
 * replaced, and anything larger than what it replaces placed in new sectors after the end of the image, with its
 * directory record pointed there and the volume size grown.
 */
#ifndef RAC_ISO_H
#define RAC_ISO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ISO_SECTOR 2048u
#define ISO_PVD_SECTOR 16u
/* The primary volume descriptor's volume space size, in sectors, both byte orders. */
#define ISO_VOLUME_SIZE_OFFSET (ISO_PVD_SECTOR * ISO_SECTOR + 80u)

typedef struct {
    uint64_t record_offset;     /* the directory record's byte offset in the image */
    uint32_t extent;            /* the file's first sector */
    uint32_t size;              /* its size in bytes */
} IsoEntry;

/* NAME's record in the root directory ("SYSTEM.CNF;1"); false when the image is no ISO 9660 image or has none. */
bool iso_find(FILE *image, const char *name, IsoEntry *entry);

/* The record of the file at PATH ("G/LEVEL1.WAD"): folders by name, then the file by its name without ";1". */
bool iso_find_path(FILE *image, const char *path, IsoEntry *entry);

/* NAME's bytes in a new buffer the caller frees, and their size; NULL when there is no such file. */
uint8_t *iso_read_file(FILE *image, const char *name, size_t *size);

/* The volume's size in sectors, from the primary volume descriptor; 0 when it cannot be read. */
uint32_t iso_volume_sectors(FILE *image);

/*
 * The game's stream reader reads past the end of a file by up to a few sectors (seen: one sector past the text
 * table). In the middle of the disc that lands on the next file; at the end of the image it is an error, so an image
 * that grows keeps this many blank sectors after its last file.
 */
#define ISO_TAIL_SECTORS 32u

/* An image opened for writing, which tracks the sectors added at its end. */
typedef struct {
    FILE *file;
    uint32_t sectors;           /* the image's length in sectors, with what was appended */
    bool grew;
    bool failed;                /* a write failed: the image is not to be used */
    int error;                  /* errno of the first failed write (ENOSPC for a full drive) */
} IsoImage;

/* PATH opened for writing in place; false when it cannot be. */
bool iso_image_open(IsoImage *image, const char *path);
void iso_image_write(IsoImage *image, uint64_t offset, const void *bytes, size_t size);
void iso_image_write_word(IsoImage *image, uint64_t offset, uint32_t word);
/* BYTES in new sectors at the end, padded to a whole sector; returns the first sector. */
uint32_t iso_image_append(IsoImage *image, const void *bytes, size_t size);
/* NAME's bytes replaced: in its own sectors when they fit (zero-padded to its old size), else in new sectors at the
   end with its directory record moved there (extent and size, both byte orders). False when there is no NAME. */
bool iso_image_replace_file(IsoImage *image, const char *name, const void *bytes, size_t size);
/* Closes the image: when it grew, TAIL_SECTORS blank sectors after the last added file, and the new volume size.
   False when any write failed. */
bool iso_image_finish(IsoImage *image);

#endif
