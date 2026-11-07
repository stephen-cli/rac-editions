/*
 * The games' WAD compression, as the PAL executable's decompressor (func_0020C468) reads it.
 *
 * A stream is a 16-byte header ("WAD", the stream's size in bytes at +3, header included, little-endian, then
 * zeros) and LZO-like packets:
 *
 *   00 n         a literal run of n + 18 bytes (18..273)
 *   01..0F       a literal run of tag + 3 bytes (4..18)
 *   40..FF b     a short match: length (tag >> 5) + 1 (3..8), distance 1 + ((tag >> 2) & 7) + b * 8 (to 2048)
 *   20..3F [n] l h
 *                a medium match: length (tag & 31) + 2, or n + 33 when tag & 31 is 0 (3..288),
 *                distance 1 + (l >> 2) + h * 64 (to 16384)
 *   10..1F [n] l h
 *                a far match: length (tag & 7) + 2, or n + 9 when tag & 7 is 0 (3..264),
 *                distance 0x4000 + (tag & 8) * 2048 + (l >> 2) + h * 64 (0x4001..0xBFFF). Distance 0x4000 itself
 *                is a marker: with length 1 (11 00 00) a no-op, with any other length (12 00 00) the end of the
 *                block.
 *
 * A match's second-to-last byte carries in its low two bits 0..3 literals that follow it. A literal run may
 * not follow another literal run or a match with literals after it: the game traps (teq). A first byte above
 * 0x11 is a run of byte - 0x11 literals (1..238).
 *
 * The game streams the packets through the EE's 16 KB scratchpad in 0x2000-byte blocks, two at a time, and
 * checks for the stream's end only in its last block. Every other block ends with the block's end marker,
 * after which the rest of the block is padding, and every packet, with the literals after it, lies whole in
 * one block. The literals after a match are copied three at a time, so the stream's last packet has 0 or 3 of
 * them, or the copy writes past the decompressed data.
 */
#ifndef RAC_WAD_H
#define RAC_WAD_H

#include <stddef.h>
#include <stdint.h>

#define WAD_HEADER 16
#define WAD_BLOCK 0x2000
/* The input a compression job takes: a constant, so every machine writes the same stream. With the 0xBFFF bytes
 * of history before it, a job reads at most 0xFFFF bytes, so its positions fit in 16 bits. */
#define WAD_CHUNK 0x4000

/* The most bytes wad_compress writes for SIZE bytes of input. */
size_t wad_bound(size_t size);

/* Runs JOB(ARG, k) for every k below COUNT, in any order and as many at once as it likes, and returns when all
 * are done. CONTEXT is the runner's own (a thread pool, say). */
typedef void wad_job(void *arg, size_t k);
typedef void wad_runner(void *context, wad_job *job, void *arg, size_t count);

/* SRC (SIZE bytes) as a WAD stream in DST, which holds wad_bound(SIZE) bytes: its size, or 0 when there is too
 * little memory. The chunks are compressed through RUN (with CONTEXT), or one after another when RUN is NULL;
 * the stream is the same either way. */
size_t wad_compress(const uint8_t *src, size_t size, uint8_t *dst, wad_runner *run, void *context);

/* The parts of wad_compress, for a runner that moves the work itself (a PS3 SPU job): */
size_t wad_chunks(size_t size);         /* how many chunks SIZE bytes make */
size_t wad_chunk_bound(void);           /* the most bytes a chunk's packets take */
/* Chunk CHUNK of SRC's packets into OUT: their size, or 0 without memory for its matcher. The job reads SRC from
 * up to 0xBFFF bytes before the chunk to its end, at most 0xFFFF bytes, and keeps a matcher of 66 KB. */
size_t wad_compress_chunk(const uint8_t *src, size_t size, size_t chunk, uint8_t *out);
/* The stream in DST from the COUNT chunks' packets, laid out in the game's blocks: its size. */
size_t wad_layout(uint8_t *const *chunks, const size_t *lengths, size_t count, uint8_t *dst);

enum wad_error {
    WAD_OK,
    WAD_NOT_WAD,        /* no "WAD" header, or a size outside the input */
    WAD_TRUNCATED,      /* a packet runs past the stream's end */
    WAD_TWO_RUNS,       /* a literal run after literals: the game traps */
    WAD_BAD_MATCH,      /* a match before the start of the output */
    WAD_TOO_LARGE,      /* more output than the buffer holds */
    WAD_CROSSES_BLOCK,  /* a packet that does not lie whole in its 0x2000-byte block */
};

/* The decompressed bytes of the stream SRC (SIZE bytes) into DST (CAPACITY bytes), read as the game reads them;
 * *OUT_SIZE is how many. */
enum wad_error wad_decompress(const uint8_t *src, size_t size, uint8_t *dst, size_t capacity, size_t *out_size);

const char *wad_error_text(enum wad_error error);

#endif
