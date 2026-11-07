/*
 * A pool of threads for jobs that can run side by side, such as wad.h's compression chunks: pool_run is a wad_runner.
 * The calling thread works too, so a pool of one thread runs every job on the caller, one after another.
 */
#ifndef RAC_POOL_H
#define RAC_POOL_H

#include <stddef.h>

#define POOL_MAX_THREADS 64

/*
 * JOB(ARG, K) for every K below COUNT, on *(unsigned *)CONTEXT threads at most (the caller among them, and never
 * more threads than jobs), returning when every job is done. Jobs are handed out in order of K, but finish in any
 * order. When a thread cannot be started, the threads that did start do its share.
 */
void pool_run(void *context, void (*job)(void *arg, size_t k), void *arg, size_t count);

/* The machine's processors: the threads a pool uses by default. */
unsigned pool_cpus(void);

#endif
