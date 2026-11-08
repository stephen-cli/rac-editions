#include "thread.h"

#include <stdlib.h>

typedef struct {
    void (*run)(void *argument);
    void *argument;
} ThreadStart;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <windows.h>

struct Lock {
    CRITICAL_SECTION section;
};

Lock *lock_new(void)
{
    Lock *lock = malloc(sizeof *lock);
    if (lock != NULL) {
        InitializeCriticalSection(&lock->section);
    }
    return lock;
}

void lock_take(Lock *lock)
{
    EnterCriticalSection(&lock->section);
}

void lock_release(Lock *lock)
{
    LeaveCriticalSection(&lock->section);
}

void lock_free(Lock *lock)
{
    if (lock != NULL) {
        DeleteCriticalSection(&lock->section);
        free(lock);
    }
}

static unsigned __stdcall thread_main(void *argument)
{
    ThreadStart start = *(ThreadStart *)argument;
    free(argument);
    start.run(start.argument);
    return 0;
}

bool thread_start_detached(void (*run)(void *argument), void *argument)
{
    ThreadStart *start = malloc(sizeof *start);
    if (start == NULL) {
        return false;
    }
    start->run = run;
    start->argument = argument;
    uintptr_t handle = _beginthreadex(NULL, 0, thread_main, start, 0, NULL);
    if (handle == 0) {
        free(start);
        return false;
    }
    CloseHandle((HANDLE)handle);
    return true;
}

void thread_sleep(unsigned milliseconds)
{
    Sleep(milliseconds);
}

struct Thread {
    HANDLE handle;
};

Thread *thread_start(void (*run)(void *argument), void *argument)
{
    Thread *thread = malloc(sizeof *thread);
    ThreadStart *start = malloc(sizeof *start);
    if (thread == NULL || start == NULL) {
        free(thread);
        free(start);
        return NULL;
    }
    start->run = run;
    start->argument = argument;
    thread->handle = (HANDLE)_beginthreadex(NULL, 0, thread_main, start, 0, NULL);
    if (thread->handle == 0) {
        free(start);
        free(thread);
        return NULL;
    }
    return thread;
}

void thread_join(Thread *thread)
{
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    free(thread);
}

unsigned thread_cpus(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (unsigned)info.dwNumberOfProcessors : 1;
}

#elif defined(__lv2ppu__)
/* The PS3's lv2: PPU threads and mutexes. A thread ends with sysThreadExit, and a joinable one is reaped by
   sysThreadJoin. */
#include <sys/mutex.h>
#include <sys/systime.h>
#include <sys/thread.h>

/* Each thread's stack: the patch steps keep a few kilobytes of locals, and the PS3's 256 MB has room. */
#define THREAD_STACK (512 * 1024)
#define THREAD_PRIORITY 1500

struct Lock {
    sys_mutex_t mutex;
};

Lock *lock_new(void)
{
    Lock *lock = malloc(sizeof *lock);
    if (lock == NULL) {
        return NULL;
    }
    sys_mutex_attr_t attributes;
    sysMutexAttrInitialize(attributes);
    if (sysMutexCreate(&lock->mutex, &attributes) != 0) {
        free(lock);
        return NULL;
    }
    return lock;
}

void lock_take(Lock *lock)
{
    sysMutexLock(lock->mutex, 0);
}

void lock_release(Lock *lock)
{
    sysMutexUnlock(lock->mutex);
}

void lock_free(Lock *lock)
{
    if (lock != NULL) {
        sysMutexDestroy(lock->mutex);
        free(lock);
    }
}

static void thread_main(void *argument)
{
    ThreadStart start = *(ThreadStart *)argument;
    free(argument);
    start.run(start.argument);
    sysThreadExit(0);
}

/* RUN(ARGUMENT) in a new PPU thread, joinable or not; false when it could not be started. */
static bool start_ppu_thread(void (*run)(void *argument), void *argument, bool joinable, sys_ppu_thread_t *id)
{
    ThreadStart *start = malloc(sizeof *start);
    if (start == NULL) {
        return false;
    }
    start->run = run;
    start->argument = argument;
    char name[] = "rac-installer";
    if (sysThreadCreate(id, thread_main, start, THREAD_PRIORITY, THREAD_STACK, joinable ? THREAD_JOINABLE : 0,
                        name) != 0) {
        free(start);
        return false;
    }
    return true;
}

bool thread_start_detached(void (*run)(void *argument), void *argument)
{
    sys_ppu_thread_t id;
    return start_ppu_thread(run, argument, false, &id);
}

void thread_sleep(unsigned milliseconds)
{
    sysUsleep(milliseconds * 1000u);
}

struct Thread {
    sys_ppu_thread_t id;
};

Thread *thread_start(void (*run)(void *argument), void *argument)
{
    Thread *thread = malloc(sizeof *thread);
    if (thread == NULL) {
        return NULL;
    }
    if (!start_ppu_thread(run, argument, true, &thread->id)) {
        free(thread);
        return NULL;
    }
    return thread;
}

void thread_join(Thread *thread)
{
    u64 result;
    sysThreadJoin(thread->id, &result);
    free(thread);
}

/* The Cell's PPU runs two hardware threads; the SPUs do not run C like this. */
unsigned thread_cpus(void)
{
    return 2;
}

#else
#include <pthread.h>
#include <time.h>
#include <unistd.h>

struct Lock {
    pthread_mutex_t mutex;
};

Lock *lock_new(void)
{
    Lock *lock = malloc(sizeof *lock);
    if (lock != NULL && pthread_mutex_init(&lock->mutex, NULL) != 0) {
        free(lock);
        return NULL;
    }
    return lock;
}

void lock_take(Lock *lock)
{
    pthread_mutex_lock(&lock->mutex);
}

void lock_release(Lock *lock)
{
    pthread_mutex_unlock(&lock->mutex);
}

void lock_free(Lock *lock)
{
    if (lock != NULL) {
        pthread_mutex_destroy(&lock->mutex);
        free(lock);
    }
}

static void *thread_main(void *argument)
{
    ThreadStart start = *(ThreadStart *)argument;
    free(argument);
    start.run(start.argument);
    return NULL;
}

bool thread_start_detached(void (*run)(void *argument), void *argument)
{
    ThreadStart *start = malloc(sizeof *start);
    if (start == NULL) {
        return false;
    }
    start->run = run;
    start->argument = argument;
    pthread_t thread;
    if (pthread_create(&thread, NULL, thread_main, start) != 0) {
        free(start);
        return false;
    }
    pthread_detach(thread);
    return true;
}

void thread_sleep(unsigned milliseconds)
{
    struct timespec pause = {.tv_sec = milliseconds / 1000, .tv_nsec = (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&pause, NULL);
}

struct Thread {
    pthread_t handle;
};

Thread *thread_start(void (*run)(void *argument), void *argument)
{
    Thread *thread = malloc(sizeof *thread);
    ThreadStart *start = malloc(sizeof *start);
    if (thread == NULL || start == NULL) {
        free(thread);
        free(start);
        return NULL;
    }
    start->run = run;
    start->argument = argument;
    if (pthread_create(&thread->handle, NULL, thread_main, start) != 0) {
        free(start);
        free(thread);
        return NULL;
    }
    return thread;
}

void thread_join(Thread *thread)
{
    pthread_join(thread->handle, NULL);
    free(thread);
}

unsigned thread_cpus(void)
{
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (unsigned)count : 1;
}
#endif
