#include "job.h"

#include <stdarg.h>
#include <stdio.h>

void job_progress(Job *job, const char *stage, uint64_t done, uint64_t total)
{
    if (job != NULL && job->progress != NULL) {
        job->progress(job, stage, done, total);
    }
}

void job_log(Job *job, const char *format, ...)
{
    if (job == NULL || job->log == NULL) {
        return;
    }
    char line[2048];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof line, format, arguments);
    va_end(arguments);
    job->log(job, line);
}
