#include "pool.h"

#include "thread.h"

typedef struct {
    void (*job)(void *arg, size_t k);
    void *arg;
    size_t count;
    size_t next;                /* the next job to hand out, under LOCK */
    Lock *lock;
} Pool;

/* A lock rather than an atomic counter: C11's atomics are missing from MSVC's default mode and from PSL1GHT's gcc,
   and a job (16 KB of compression, say) takes far longer than taking the lock. */
static void pool_work(void *argument)
{
    Pool *pool = argument;
    for (;;) {
        lock_take(pool->lock);
        size_t k = pool->next;
        if (k < pool->count) {
            pool->next++;
        }
        lock_release(pool->lock);
        if (k >= pool->count) {
            return;
        }
        pool->job(pool->arg, k);
    }
}

void pool_run(void *context, void (*job)(void *arg, size_t k), void *arg, size_t count)
{
    unsigned threads = context != NULL ? *(const unsigned *)context : 1;
    if (threads > POOL_MAX_THREADS) {
        threads = POOL_MAX_THREADS;
    }
    if (threads > count) {
        threads = (unsigned)count;
    }
    Pool pool = {job, arg, count, 0, NULL};
    if (threads > 1) {
        pool.lock = lock_new();
    }
    if (pool.lock == NULL) {
        for (size_t k = 0; k < count; k++) {
            job(arg, k);
        }
        return;
    }
    Thread *started[POOL_MAX_THREADS];
    unsigned running = 0;
    for (unsigned t = 1; t < threads; t++) {
        Thread *thread = thread_start(pool_work, &pool);
        if (thread != NULL) {
            started[running++] = thread;
        }
    }
    pool_work(&pool);
    for (unsigned t = 0; t < running; t++) {
        thread_join(started[t]);
    }
    lock_free(pool.lock);
}

unsigned pool_cpus(void)
{
    return thread_cpus();
}
