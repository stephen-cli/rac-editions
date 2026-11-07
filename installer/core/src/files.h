/*
 * Files by UTF-8 path with 64-bit offsets, the same on Windows and POSIX systems (the PS3 among them): disc images
 * are over 4 GB, and on Windows a path with characters outside the system's code page opens only by its UTF-16 name.
 */
#ifndef RAC_FILES_H
#define RAC_FILES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint64_t size;
    int64_t modified_ns;        /* last modification, in nanoseconds since 1970 */
    bool is_directory;
} FileInfo;

/* PATH opened with a stdio MODE ("rb", "r+b", "wb"), or NULL with errno set. */
FILE *file_open(const char *path, const char *mode);

/* Whether PATH exists; fills INFO when it does. */
bool file_info(const char *path, FileInfo *info);

bool file_seek(FILE *file, uint64_t offset);

/* Exactly SIZE bytes at OFFSET, or false (an error, or the file ends first). */
bool file_read_at(FILE *file, uint64_t offset, void *buffer, size_t size);

bool file_write_at(FILE *file, uint64_t offset, const void *buffer, size_t size);

/* The size of an open file, from its end. */
uint64_t file_size(FILE *file);

/* PATH's whole contents in a new zero-terminated buffer the caller frees, and their size; NULL when unreadable. */
char *file_read_all(const char *path, size_t *size);

/* PATH made absolute with links followed, as Python's Path.resolve() gives it (the hash cache's keys need the same
   spelling); false when it does not exist. */
bool file_resolve(const char *path, char *resolved, size_t capacity);

/* FROM renamed to TO, replacing TO if it exists. */
bool file_replace(const char *from, const char *to);

/* DIRECTORY and the folders above it that are missing. */
bool file_make_directories(const char *directory);

#endif
