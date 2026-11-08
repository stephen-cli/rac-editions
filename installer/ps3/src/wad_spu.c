/*
 * The WAD compressor on the SPUs: the PS3 build links the core with --wrap=wad_compress, so a level's compression
 * (levels.c) comes here. The chunks are shared among an SPU thread group (spu/wad_job.c, embedded as wad_job_elf),
 * then laid out by the core's wad_layout as wad_compress would: the stream is the same, byte for byte. When the SPUs
 * cannot be had, the core's own wad_compress runs on the PPU, as before.
 */
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lv2/systime.h>

#include "spu.h"
#include "wad.h"
#include "../spu/wad_spu_job.h"

size_t __real_wad_compress(const uint8_t *src, size_t size, uint8_t *dst, wad_runner *run, void *context);
size_t __wrap_wad_compress(const uint8_t *src, size_t size, uint8_t *dst, wad_runner *run, void *context);

/* spu/wad_job.c's ELF, embedded by the Makefile */
extern const uint8_t wad_job_elf[];
static SpuProgram program = {.name = "wad chunks", .elf = wad_job_elf};

size_t __wrap_wad_compress(const uint8_t *src, size_t size, uint8_t *dst, wad_runner *run, void *context)
{
    size_t count = wad_chunks(size);
    if (size == 0 || size > UINT32_MAX || wad_chunk_bound() > WAD_SPU_SLOT) {
        return __real_wad_compress(src, size, dst, run, context);
    }
    WadSpuJob *job = memalign(128, sizeof *job);
    uint8_t *slots = memalign(128, count * WAD_SPU_SLOT);
    WadSpuLength *lengths = memalign(128, count * sizeof *lengths);
    uint8_t **chunks = malloc(count * sizeof *chunks);
    size_t *chunk_lengths = malloc(count * sizeof *chunk_lengths);
    size_t stream = 0;
    if (job != NULL && slots != NULL && lengths != NULL && chunks != NULL && chunk_lengths != NULL) {
        memset(job, 0, sizeof *job);
        memset(lengths, 0, count * sizeof *lengths);
        job->src = (uint64_t)(uintptr_t)src;
        job->slots = (uint64_t)(uintptr_t)slots;
        job->lengths = (uint64_t)(uintptr_t)lengths;
        job->size = (uint32_t)size;
        job->count = (uint32_t)count;
        job->threads = count < SPU_THREADS ? (uint32_t)count : SPU_THREADS;
        s64 started = sysGetSystemTime();
        bool whole = spu_run(&program, job->threads, job);
        printf("spu: %u chunks on %u SPUs in %lld ms\n", job->count, job->threads,
               (long long)(sysGetSystemTime() - started) / 1000);
        for (size_t c = 0; whole && c < count; c++) {
            whole = lengths[c].done == 1 && lengths[c].bytes != 0;
            chunks[c] = slots + c * WAD_SPU_SLOT;
            chunk_lengths[c] = lengths[c].bytes;
        }
        if (whole) {
            stream = wad_layout(chunks, chunk_lengths, count, dst);
        } else {
            printf("spu: a level's chunks did not all come back; compressing it on the PPU\n");
        }
    }
    free(job);
    free(slots);
    free(lengths);
    free(chunks);
    free(chunk_lengths);
    return stream != 0 ? stream : __real_wad_compress(src, size, dst, run, context);
}
