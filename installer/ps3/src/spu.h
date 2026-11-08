/*
 * The PS3's SPUs, for the core's heavy work (wad_spu.c: a level's compression; sha_spu.c: SHA-256): started once,
 * then a program (an SPU ELF embedded in the app) run on a group of SPU threads until each has ended.
 */
#ifndef RAC_PS3_SPU_H
#define RAC_PS3_SPU_H

#include <stdbool.h>
#include <stdint.h>

#include <lv2/spu.h>

#define SPU_THREADS 6               /* the SPUs a game has */

/* An SPU program: its ELF, and the image made from it the first time it runs. */
typedef struct {
    const char *name;
    const uint8_t *elf;
    sysSpuImage image;
    bool imported, failed;
} SpuProgram;

/* PROGRAM on THREADS SPU threads (at most SPU_THREADS), each given its index and JOB's address; true once all
   have ended. False when the SPUs, the program or the group could not be had: the caller does the work itself. */
bool spu_run(SpuProgram *program, uint32_t threads, void *job);

#endif
