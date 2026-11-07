/*
 * Little- and big-endian fields in byte buffers. The discs, the executable and the payload are little-endian;
 * ISO 9660 also stores its 32-bit fields big-endian beside them. The installer runs on little-endian PCs and on
 * the big-endian PS3, so every multi-byte field is read and written through these, never through a cast pointer.
 */
#ifndef RAC_BYTES_H
#define RAC_BYTES_H

#include <stdint.h>

static inline uint16_t read_u16_le(const uint8_t *at)
{
    return (uint16_t)(at[0] | at[1] << 8);
}

static inline uint32_t read_u32_le(const uint8_t *at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

static inline void write_u16_le(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
}

static inline void write_u32_le(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    at[2] = (uint8_t)(value >> 16);
    at[3] = (uint8_t)(value >> 24);
}

static inline void write_u32_be(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
}

/* A 32-bit field as ISO 9660 stores it: little-endian, then big-endian (8 bytes). */
static inline void write_u32_both(uint8_t *at, uint32_t value)
{
    write_u32_le(at, value);
    write_u32_be(at + 4, value);
}

#endif
