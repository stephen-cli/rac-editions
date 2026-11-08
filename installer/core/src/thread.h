/*
 * Threads and locks, for --serve: a build and each identify run in a thread of their own, so two discs are checked
 * side by side and a cancel can reach any of them. Windows threads, POSIX threads, and the PS3 lv2's PPU threads.
 */
#ifndef RAC_THREAD_H
#define RAC_THREAD_H

#include <stdbool.h>

typedef struct Lock Lock;

/* A new lock, or NULL. */
Lock *lock_new(void);
void lock_take(Lock *lock);
void lock_release(Lock *lock);
void lock_free(Lock *lock);

/* RUN(ARGUMENT) in a new thread that nobody waits for; false when it could not be started. */
bool thread_start_detached(void (*run)(void *argument), void *argument);

typedef struct Thread Thread;

/* RUN(ARGUMENT) in a new thread that thread_join waits for, or NULL when it could not be started. */
Thread *thread_start(void (*run)(void *argument), void *argument);
/* Waits until THREAD's RUN has returned, then frees THREAD. */
void thread_join(Thread *thread);

/* The machine's processors: how many threads a pool uses by default. */
unsigned thread_cpus(void);

/* The calling thread paused for about MILLISECONDS. */
void thread_sleep(unsigned milliseconds);

#endif
