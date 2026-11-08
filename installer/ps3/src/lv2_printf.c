/*
 * C99's size modifiers for the toolchain's printf. Its newlib is built without them: "%zu" prints "zu" and leaves
 * the argument behind, so a later "%s" takes a number for a pointer. The core writes sizes with %zu (into its logs
 * and into the JSON its mesh code reads back), so on the PS3 the printf family is wrapped (the linker's --wrap): a
 * format's z, t and j become l, which on the PPU (LP64, size_t is unsigned long) takes the same argument.
 * The wrap also keeps the app's log (lv2_log_open).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lv2/systime.h>
#include <sys/file.h>

#include "thread.h"

int __real_snprintf(char *buffer, size_t capacity, const char *format, ...);
int __real_vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments);
int __real_vfprintf(FILE *file, const char *format, va_list arguments);

int __wrap_vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments);
int __wrap_snprintf(char *buffer, size_t capacity, const char *format, ...);
int __wrap_vfprintf(FILE *file, const char *format, va_list arguments);
int __wrap_fprintf(FILE *file, const char *format, ...);
int __wrap_printf(const char *format, ...);

/* FORMAT with each conversion's z, t and j length modifier made l, into REWRITTEN (CAPACITY bytes); false when it
   does not fit. */
static int rewrite(const char *format, char *rewritten, size_t capacity)
{
    size_t out = 0;
    for (const char *at = format; *at != 0; at++) {
        if (out + 3 >= capacity) {
            return 0;
        }
        rewritten[out++] = *at;
        if (*at != '%') {
            continue;
        }
        at++;
        /* flags, width and precision, as they are */
        while (*at != 0 && strchr("-+ #0'123456789.*", *at) != NULL) {
            if (out + 3 >= capacity) {
                return 0;
            }
            rewritten[out++] = *at++;
        }
        if (*at == 'z' || *at == 't' || *at == 'j') {
            rewritten[out++] = 'l';
            at++;
        }
        if (*at == 0) {
            break;
        }
        rewritten[out++] = *at;
    }
    rewritten[out] = 0;
    return 1;
}

int __wrap_vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments)
{
    char rewritten[1024];
    if (rewrite(format, rewritten, sizeof rewritten)) {
        return __real_vsnprintf(buffer, capacity, rewritten, arguments);
    }
    size_t size = strlen(format) * 2 + 1;
    char *long_format = malloc(size);
    int written = long_format != NULL && rewrite(format, long_format, size)
                      ? __real_vsnprintf(buffer, capacity, long_format, arguments)
                      : __real_vsnprintf(buffer, capacity, format, arguments);
    free(long_format);
    return written;
}

int __wrap_snprintf(char *buffer, size_t capacity, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int written = __wrap_vsnprintf(buffer, capacity, format, arguments);
    va_end(arguments);
    return written;
}

/* The log: what is printed to stdout and stderr, also written to a file, each line after the milliseconds since
   the log was opened. On a console the TTY is out of reach, but the file can be read over FTP, even after a hang.
   Lines are gathered in memory and written by a thread of their own, four times a second: a file write waits
   while a build writes the new image (lv2 gave a frame's log line seconds), and the screen must not. */
#define LOG_PENDING (64 * 1024)
static int log_fd = -1;
static s64 log_opened;
static Lock *log_lock;
static char log_pending[LOG_PENDING];
static size_t log_pending_length;
static int log_line_started;

static void log_writer(void *unused)
{
    (void)unused;
    static char writing[LOG_PENDING];
    for (;;) {
        thread_sleep(250);
        lock_take(log_lock);
        size_t length = log_pending_length;
        memcpy(writing, log_pending, length);
        log_pending_length = 0;
        lock_release(log_lock);
        u64 written;
        if (length > 0) {
            sysLv2FsWrite(log_fd, writing, length, &written);
        }
    }
}

void lv2_log_open(const char *path)
{
    s32 fd;
    log_lock = lock_new();
    if (log_lock == NULL || sysLv2FsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, 0666, NULL, 0) != 0) {
        return;
    }
    log_opened = sysGetSystemTime();
    log_fd = fd;
    if (!thread_start_detached(log_writer, NULL)) {
        log_fd = -1;
    }
}

/* TEXT added to what the writer has yet to write; dropped when that is full. */
static void log_add(const char *text, size_t length)
{
    if (log_pending_length + length <= LOG_PENDING) {
        memcpy(log_pending + log_pending_length, text, length);
        log_pending_length += length;
    }
}

static void log_write(const char *text)
{
    lock_take(log_lock);
    for (const char *at = text; *at != 0;) {
        if (!log_line_started) {
            char stamp[24];
            int length = __real_snprintf(stamp, sizeof stamp, "%8lld ", (long long)((sysGetSystemTime() - log_opened) / 1000));
            log_add(stamp, (size_t)length);
            log_line_started = 1;
        }
        const char *end = strchr(at, '\n');
        size_t length = end != NULL ? (size_t)(end - at) + 1 : strlen(at);
        log_add(at, length);
        log_line_started = end == NULL;
        at += length;
    }
    lock_release(log_lock);
}

int __wrap_vfprintf(FILE *file, const char *format, va_list arguments)
{
    char rewritten[1024];
    const char *used = rewrite(format, rewritten, sizeof rewritten) ? rewritten : format;
    if (log_fd >= 0 && (file == stdout || file == stderr)) {
        char line[1024];
        va_list copied;
        va_copy(copied, arguments);
        __real_vsnprintf(line, sizeof line, used, copied);
        va_end(copied);
        log_write(line);
    }
    return __real_vfprintf(file, used, arguments);
}

/* GCC makes printf("a plain line\n") a puts, which the log is to see too. */
int __real_puts(const char *line);
int __wrap_puts(const char *line);

int __wrap_puts(const char *line)
{
    if (log_fd >= 0) {
        log_write(line);
        log_write("\n");
    }
    return __real_puts(line);
}

int __wrap_fprintf(FILE *file, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int written = __wrap_vfprintf(file, format, arguments);
    va_end(arguments);
    return written;
}

int __wrap_printf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int written = __wrap_vfprintf(stdout, format, arguments);
    va_end(arguments);
    return written;
}
