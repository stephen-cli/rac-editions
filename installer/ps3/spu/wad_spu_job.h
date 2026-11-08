/*
 * What the PPU (src/wad_spu.c) and the SPU program (spu/wad_job.c) share: a level's compression job, in main memory,
 * and each chunk's result. Both sides lay these out the same way: fixed-width fields, 16-byte aligned.
 */
#ifndef RAC_WAD_SPU_JOB_H
#define RAC_WAD_SPU_JOB_H

#include <stdint.h>

/* A chunk's packets' slot: wad_chunk_bound() (0x5404 bytes) on whole 16-byte lines; src/wad_spu.c checks it. */
#define WAD_SPU_SLOT 0x5410

typedef struct {
    uint64_t src;           /* the level's bytes */
    uint64_t slots;         /* COUNT slots of WAD_SPU_SLOT bytes, for the chunks' packets */
    uint64_t lengths;       /* COUNT WadSpuLength */
    uint32_t size;          /* the level's bytes */
    uint32_t count;         /* its chunks */
    uint32_t threads;       /* the SPU threads sharing them */
    uint32_t pad[7];
} WadSpuJob;                /* 64 bytes */

typedef struct {
    uint32_t bytes;         /* the chunk's packets, or 0 when the SPU had no memory for its matcher */
    uint32_t done;          /* 1 once written */
    uint32_t pad[2];
} WadSpuLength;             /* 16 bytes */

#endif
