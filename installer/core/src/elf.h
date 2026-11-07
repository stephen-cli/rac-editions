/* The boot executable's ELF, which the mod segment joins as one more loadable segment. */
#ifndef RAC_ELF_H
#define RAC_ELF_H

#include <stddef.h>
#include <stdint.h>

/*
 * The ELF IMAGE with DATA appended as one more PT_LOAD at ADDRESS, in a new buffer the caller frees, and its size;
 * NULL when the program header table has no room. Retail's program header table (one entry at 0x34) is followed by
 * zeros up to the first segment at 0x1000, so the new entry goes right after it. The data goes after the file's end
 * (the section headers), at an offset congruent to ADDRESS modulo the page size, as the ELF rules want, and the file
 * is padded to 16 bytes.
 */
uint8_t *elf_add_segment(const uint8_t *image, size_t image_size, uint32_t address, const uint8_t *data,
                         size_t data_size, size_t *result_size);

#endif
