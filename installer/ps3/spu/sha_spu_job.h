/*
 * What the PPU (src/sha_spu.c) and the SPU program (spu/sha_job.c) share: SHA-256's state, and whole 64-byte
 * blocks in main memory for the SPU to add to it. 16-byte aligned, fixed-width fields.
 */
#ifndef RAC_SHA_SPU_JOB_H
#define RAC_SHA_SPU_JOB_H

#include <stdint.h>

typedef struct {
    uint32_t state[8];      /* in, then out */
    uint64_t data;          /* the blocks, on a 16-byte line */
    uint32_t size;          /* their bytes: a multiple of 64 */
    uint32_t pad[5];
} ShaSpuJob;                /* 64 bytes */

#endif
