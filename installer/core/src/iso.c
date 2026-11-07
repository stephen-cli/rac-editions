#include "iso.h"

#include <stdlib.h>
#include <string.h>

#include "bytes.h"
#include "files.h"

/* The root directory's extent and size, from the primary volume descriptor's root record (at byte 156). */
static bool root_directory(FILE *image, uint32_t *extent, uint32_t *size)
{
    uint8_t descriptor[ISO_SECTOR];
    if (!file_read_at(image, (uint64_t)ISO_PVD_SECTOR * ISO_SECTOR, descriptor, sizeof descriptor)) {
        return false;
    }
    if (descriptor[0] != 1 || memcmp(descriptor + 1, "CD001", 5) != 0) {
        return false;
    }
    const uint8_t *root_record = descriptor + 156;
    *extent = read_u32_le(root_record + 2);
    *size = read_u32_le(root_record + 10);
    return true;
}

bool iso_find(FILE *image, const char *name, IsoEntry *entry)
{
    uint32_t extent;
    uint32_t size;
    if (!root_directory(image, &extent, &size)) {
        return false;
    }
    /* A PS2 disc's root directory takes a sector or two; a larger one is no image of a PS2 disc. */
    if (size == 0 || size > 64 * ISO_SECTOR) {
        return false;
    }
    uint8_t *directory = malloc(size);
    if (directory == NULL || !file_read_at(image, (uint64_t)extent * ISO_SECTOR, directory, size)) {
        free(directory);
        return false;
    }
    size_t name_length = strlen(name);
    bool found = false;
    size_t position = 0;
    while (position < size) {
        uint8_t length = directory[position];
        if (length == 0) {
            /* records never straddle a sector: a zero length pads to the next */
            position = (position / ISO_SECTOR + 1) * ISO_SECTOR;
            continue;
        }
        if (position + length > size || length < 33) {
            break;
        }
        const uint8_t *record = directory + position;
        uint8_t record_name_length = record[32];
        if (33u + record_name_length <= length && record_name_length == name_length &&
            memcmp(record + 33, name, name_length) == 0) {
            entry->record_offset = (uint64_t)extent * ISO_SECTOR + position;
            entry->extent = read_u32_le(record + 2);
            entry->size = read_u32_le(record + 10);
            found = true;
            break;
        }
        position += length;
    }
    free(directory);
    return found;
}

uint8_t *iso_read_file(FILE *image, const char *name, size_t *size)
{
    IsoEntry entry;
    if (!iso_find(image, name, &entry)) {
        return NULL;
    }
    uint8_t *bytes = malloc(entry.size > 0 ? entry.size : 1);
    if (bytes == NULL || !file_read_at(image, (uint64_t)entry.extent * ISO_SECTOR, bytes, entry.size)) {
        free(bytes);
        return NULL;
    }
    *size = entry.size;
    return bytes;
}

uint32_t iso_volume_sectors(FILE *image)
{
    uint8_t field[4];
    if (!file_read_at(image, ISO_VOLUME_SIZE_OFFSET, field, sizeof field)) {
        return 0;
    }
    return read_u32_le(field);
}

bool iso_image_open(IsoImage *image, const char *path)
{
    memset(image, 0, sizeof *image);
    image->file = file_open(path, "r+b");
    if (image->file == NULL) {
        return false;
    }
    image->sectors = (uint32_t)(file_size(image->file) / ISO_SECTOR);
    return true;
}

void iso_image_write(IsoImage *image, uint64_t offset, const void *bytes, size_t size)
{
    if (!file_write_at(image->file, offset, bytes, size)) {
        image->failed = true;
    }
}

void iso_image_write_word(IsoImage *image, uint64_t offset, uint32_t word)
{
    uint8_t bytes[4];
    write_u32_le(bytes, word);
    iso_image_write(image, offset, bytes, sizeof bytes);
}

uint32_t iso_image_append(IsoImage *image, const void *bytes, size_t size)
{
    uint32_t first = image->sectors;
    uint32_t count = (uint32_t)((size + ISO_SECTOR - 1) / ISO_SECTOR);
    iso_image_write(image, (uint64_t)first * ISO_SECTOR, bytes, size);
    size_t padding = (size_t)count * ISO_SECTOR - size;
    if (padding > 0) {
        static const uint8_t ZEROS[ISO_SECTOR] = {0};
        iso_image_write(image, (uint64_t)first * ISO_SECTOR + size, ZEROS, padding);
    }
    image->sectors += count;
    image->grew = true;
    return first;
}

bool iso_image_replace_file(IsoImage *image, const char *name, const void *bytes, size_t size)
{
    IsoEntry entry;
    if (!iso_find(image->file, name, &entry)) {
        return false;
    }
    if (size <= entry.size) {
        uint8_t *padded = calloc(entry.size > 0 ? entry.size : 1, 1);
        if (padded == NULL) {
            image->failed = true;
            return false;
        }
        memcpy(padded, bytes, size);
        iso_image_write(image, (uint64_t)entry.extent * ISO_SECTOR, padded, entry.size);
        free(padded);
        return true;
    }
    uint32_t first = iso_image_append(image, bytes, size);
    uint8_t field[8];
    write_u32_both(field, first);
    iso_image_write(image, entry.record_offset + 2, field, sizeof field);
    write_u32_both(field, (uint32_t)size);
    iso_image_write(image, entry.record_offset + 10, field, sizeof field);
    return true;
}

bool iso_image_finish(IsoImage *image)
{
    if (image->grew) {
        uint8_t *tail = calloc(ISO_TAIL_SECTORS, ISO_SECTOR);
        if (tail == NULL) {
            image->failed = true;
        } else {
            iso_image_write(image, (uint64_t)image->sectors * ISO_SECTOR, tail, (size_t)ISO_TAIL_SECTORS * ISO_SECTOR);
            free(tail);
        }
        image->sectors += ISO_TAIL_SECTORS;
        uint8_t field[8];
        write_u32_both(field, image->sectors);
        iso_image_write(image, ISO_VOLUME_SIZE_OFFSET, field, sizeof field);
    }
    if (fclose(image->file) != 0) {
        image->failed = true;
    }
    image->file = NULL;
    return !image->failed;
}
