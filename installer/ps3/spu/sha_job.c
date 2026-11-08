/*
 * SHA-256 on an SPU (the core's sha256.c, built for the SPU): whole blocks from main memory added to the state the
 * PPU sends (src/sha_spu.c). The blocks come in by DMA in two buffers, so the next arrives while one is hashed.
 */
#include <spu_mfcio.h>
#include <stdint.h>
#include <string.h>

#include <sys/spu_thread.h>

#include "sha256.h"
#include "sha_spu_job.h"

#define PIECE 16384                 /* the most bytes one DMA moves, and a multiple of 64 */

static ShaSpuJob job __attribute__((aligned(128)));
static uint8_t pieces[2][PIECE] __attribute__((aligned(128)));

static void fetch(int buffer, uint64_t ea, uint32_t size)
{
    mfc_get(pieces[buffer], ea, size, buffer, 0, 0);
}

static void wait_for(int buffer)
{
    mfc_write_tag_mask(1u << buffer);
    mfc_read_tag_status_all();
}

int main(uint64_t index, uint64_t job_ea)
{
    (void)index;
    mfc_get(&job, job_ea, sizeof job, 0, 0, 0);
    wait_for(0);
    Sha256 hash;
    sha256_start(&hash);
    memcpy(hash.state, job.state, sizeof hash.state);
    uint32_t at = 0;
    int buffer = 0;
    if (job.size > 0) {
        fetch(0, job.data, job.size < PIECE ? job.size : PIECE);
    }
    while (at < job.size) {
        uint32_t size = job.size - at < PIECE ? job.size - at : PIECE;
        uint32_t after = at + size;
        if (after < job.size) {
            fetch(!buffer, job.data + after, job.size - after < PIECE ? job.size - after : PIECE);
        }
        wait_for(buffer);
        sha256_add(&hash, pieces[buffer], size);
        at = after;
        buffer = !buffer;
    }
    memcpy(job.state, hash.state, sizeof job.state);
    mfc_put(&job, job_ea, sizeof job, 0, 0, 0);
    wait_for(0);
    spu_thread_exit(0);
    return 0;
}
