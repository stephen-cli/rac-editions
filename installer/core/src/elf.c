#include "elf.h"

#include <stdlib.h>
#include <string.h>

#include "bytes.h"

#define PAGE 0x1000u
#define PT_LOAD 1u
#define PF_RWX 7u
/* The ELF header's fields the segment needs: e_phoff, e_phentsize, e_phnum. */
#define E_PHOFF 28
#define E_PHENTSIZE 42
#define E_PHNUM 44

uint8_t *elf_add_segment(const uint8_t *image, size_t image_size, uint32_t address, const uint8_t *data,
                         size_t data_size, size_t *result_size)
{
    if (image_size < 52) {
        return NULL;
    }
    uint32_t header_table = read_u32_le(image + E_PHOFF);
    uint16_t header_size = read_u16_le(image + E_PHENTSIZE);
    uint16_t header_count = read_u16_le(image + E_PHNUM);
    size_t new_header = (size_t)header_table + (size_t)header_count * header_size;
    if (header_size < 32 || new_header + header_size > image_size) {
        return NULL;
    }
    for (size_t i = 0; i < header_size; i++) {
        if (image[new_header + i] != 0) {
            return NULL;            /* no room for another program header */
        }
    }
    /* the data's file offset: the first at or after the file's end that matches ADDRESS modulo the page size */
    size_t offset = image_size + (size_t)((address - image_size) % PAGE);
    size_t end = offset + data_size;
    size_t padded = end + (16 - end % 16) % 16;
    uint8_t *result = calloc(padded, 1);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, image, image_size);
    memcpy(result + offset, data, data_size);
    uint8_t *entry = result + new_header;
    write_u32_le(entry + 0, PT_LOAD);
    write_u32_le(entry + 4, (uint32_t)offset);
    write_u32_le(entry + 8, address);                       /* p_vaddr */
    write_u32_le(entry + 12, address);                      /* p_paddr */
    write_u32_le(entry + 16, (uint32_t)data_size);          /* p_filesz */
    write_u32_le(entry + 20, (uint32_t)data_size);          /* p_memsz */
    write_u32_le(entry + 24, PF_RWX);
    write_u32_le(entry + 28, PAGE);                         /* p_align */
    write_u16_le(result + E_PHNUM, (uint16_t)(header_count + 1));
    *result_size = padded;
    return result;
}
