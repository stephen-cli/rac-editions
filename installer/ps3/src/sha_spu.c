/*
 * SHA-256 on an SPU: the PS3 build links the core with --wrap=sha256_add, so a large addition (disc.c hashes 16 MiB
 * at a time: the discs, and the new image at the end of a build) comes here. Its whole 64-byte blocks go to one SPU
 * (spu/sha_job.c, embedded as sha_job_elf), which runs the core's own sha256.c on them; the rest stays on the PPU.
 * The hash is the same either way. A hash is a chain, block after block, so it takes one SPU, not all of them.
 */
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "spu.h"
#include "../spu/sha_spu_job.h"

void __real_sha256_add(Sha256 *hash, const void *data, size_t size);
void __wrap_sha256_add(Sha256 *hash, const void *data, size_t size);

/* spu/sha_job.c's ELF, embedded by the Makefile */
extern const uint8_t sha_job_elf[];
static SpuProgram program = {.name = "sha256", .elf = sha_job_elf};

#define SPU_MIN (1u << 20)          /* below this, starting an SPU costs more than it saves */

void __wrap_sha256_add(Sha256 *hash, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    /* the SPU takes whole blocks, from a 16-byte line: a part block first, as the PPU has it */
    if (size >= SPU_MIN && hash->block_used > 0) {
        size_t fill = 64 - hash->block_used;
        __real_sha256_add(hash, bytes, fill);
        bytes += fill;
        size -= fill;
    }
    size_t whole = size & ~(size_t)63;
    if (size < SPU_MIN || ((uintptr_t)bytes & 15) != 0 || whole > UINT32_MAX) {
        __real_sha256_add(hash, bytes, size);
        return;
    }
    ShaSpuJob *job = memalign(128, sizeof *job);
    bool hashed = false;
    if (job != NULL) {
        memset(job, 0, sizeof *job);
        memcpy(job->state, hash->state, sizeof job->state);
        job->data = (uint64_t)(uintptr_t)bytes;
        job->size = (uint32_t)whole;
        hashed = spu_run(&program, 1, job);
        if (hashed) {
            memcpy(hash->state, job->state, sizeof hash->state);
            hash->length += whole;
        }
    }
    free(job);
    if (!hashed) {
        __real_sha256_add(hash, bytes, size);
        return;
    }
    __real_sha256_add(hash, bytes + whole, size - whole);
}
