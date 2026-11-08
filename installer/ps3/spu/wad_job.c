/*
 * The WAD compressor's chunks on an SPU (wad.h: wad_compress_chunk, the core's own code, built for the SPU). An SPU
 * thread of the group takes every THREADS'th chunk from its INDEX: it brings the chunk's input (with the history
 * before it, at most 0xFFFF bytes) into its local store by DMA, compresses it there, and sends the packets and their
 * length back to the slots the PPU set aside (src/wad_spu.c).
 */
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <stdint.h>
#include <string.h>

#include <sys/spu_thread.h>

#include "wad.h"
#include "wad_spu_job.h"

#define DMA_MAX 16384               /* the most bytes one DMA moves */
#define TAG 1

static WadSpuJob job __attribute__((aligned(128)));
/* a chunk's input: the history and the chunk, with room to start on the 16-byte line its address sits in */
static uint8_t input[0x10000 + 32] __attribute__((aligned(128)));
static uint8_t output[WAD_SPU_SLOT] __attribute__((aligned(128)));
static WadSpuLength length __attribute__((aligned(16)));

static void wait_dma(void)
{
    mfc_write_tag_mask(1 << TAG);
    mfc_read_tag_status_all();
}

/* SIZE bytes (a multiple of 16) between local store LS and main memory EA, both on 16-byte lines, by GET. */
static void move(void *ls, uint64_t ea, uint32_t size, int get)
{
    uint8_t *at = ls;
    while (size > 0) {
        uint32_t part = size < DMA_MAX ? size : DMA_MAX;
        if (get) {
            mfc_get(at, ea, part, TAG, 0, 0);
        } else {
            mfc_put(at, ea, part, TAG, 0, 0);
        }
        at += part;
        ea += part;
        size -= part;
    }
    wait_dma();
}

int main(uint64_t index, uint64_t job_ea)
{
    move(&job, job_ea, sizeof job, 1);
    for (uint32_t chunk = (uint32_t)index; chunk < job.count; chunk += job.threads) {
        uint32_t start = chunk * WAD_CHUNK;
        uint32_t end = start + WAD_CHUNK < job.size ? start + WAD_CHUNK : job.size;
        uint32_t from = start > 0xBFFF ? start - 0xBFFF : 0;
        /* from the 16-byte line the history starts in, to the line the chunk ends in */
        uint64_t first = (job.src + from) & ~15ull;
        uint32_t lead = (uint32_t)(job.src + from - first);
        uint32_t span = (lead + (end - from) + 15) & ~15u;
        move(input, first, span, 1);
        /* the core indexes its input from the whole level's start: SRC + FROM is where the history landed */
        const uint8_t *src = input + lead - from;
        uint32_t packets = wad_compress_chunk(src, job.size, chunk, output);
        if (packets > 0) {
            move(output, job.slots + (uint64_t)chunk * WAD_SPU_SLOT, (packets + 15) & ~15u, 0);
        }
        length.bytes = packets;
        length.done = 1;
        move(&length, job.lengths + (uint64_t)chunk * sizeof length, sizeof length, 0);
    }
    /* an lv2 SPU thread ends by telling lv2 so; returning from main would run newlib's exit, which stops the SPU
       with a code lv2 does not take for an end */
    spu_thread_exit(0);
    return 0;
}
