/*
 * What a long command (hashing, a build) reports while it runs, and how it learns it was cancelled. The command
 * line prints; --serve sends the protocol's progress and log events; the PS3 builder draws its screens.
 */
#ifndef RAC_JOB_H
#define RAC_JOB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Job Job;

struct Job {
    /* STAGE is one of the protocol's: hash, verify, convert, write, patch. */
    void (*progress)(Job *job, const char *stage, uint64_t done, uint64_t total);
    void (*log)(Job *job, const char *text);
    /* Set from another thread to stop the job at its next check; a flag only, so volatile is enough. */
    volatile int cancelled;
    void *context;
    /* The job this one is a part of (a step of a build's stage): cancelling it cancels this one too. */
    Job *parent;
};

static inline bool job_cancelled(const Job *job)
{
    for (; job != NULL; job = job->parent) {
        if (job->cancelled != 0) {
            return true;
        }
    }
    return false;
}

void job_progress(Job *job, const char *stage, uint64_t done, uint64_t total);

/* A log line made like printf. */
void job_log(Job *job, const char *format, ...);

#endif
