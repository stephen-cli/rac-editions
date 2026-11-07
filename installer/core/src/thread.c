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
