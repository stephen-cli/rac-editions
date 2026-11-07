#include "hash_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "json.h"
#include "thread.h"

typedef struct {
    char *key;
    char hex[65];
} CacheEntry;

struct HashCache {
    char *file;                 /* DATA/hashes.json */
    char *data_directory;
    CacheEntry *entries;
    size_t count;
    size_t capacity;
    Lock *lock;                 /* identify and build threads use the cache at once */
};

static bool add_entry(HashCache *cache, const char *key, const char *hex)
{
    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].key, key) == 0) {
            memcpy(cache->entries[i].hex, hex, 65);
            return true;
        }
    }
    if (cache->count == cache->capacity) {
        size_t grown = cache->capacity ? cache->capacity * 2 : 16;
        CacheEntry *entries = realloc(cache->entries, grown * sizeof *entries);
        if (entries == NULL) {
            return false;
        }
        cache->entries = entries;
        cache->capacity = grown;
    }
    char *copy = malloc(strlen(key) + 1);
    if (copy == NULL) {
        return false;
    }
    strcpy(copy, key);
    cache->entries[cache->count].key = copy;
    memcpy(cache->entries[cache->count].hex, hex, 65);
    cache->count++;
    return true;
}

static char *joined(const char *directory, const char *name)
{
    size_t length = strlen(directory) + 1 + strlen(name) + 1;
    char *path = malloc(length);
    if (path != NULL) {
        snprintf(path, length, "%s/%s", directory, name);
    }
    return path;
}

HashCache *hash_cache_open(const char *data_directory)
{
    HashCache *cache = calloc(1, sizeof *cache);
    if (cache == NULL) {
        return NULL;
    }
    cache->lock = lock_new();
    if (data_directory == NULL) {
        return cache;
    }
    cache->data_directory = malloc(strlen(data_directory) + 1);
    if (cache->data_directory != NULL) {
        strcpy(cache->data_directory, data_directory);
    }
    cache->file = joined(data_directory, "hashes.json");
    size_t size = 0;
    char *text = cache->file != NULL ? file_read_all(cache->file, &size) : NULL;
    Json *known = text != NULL ? json_parse(text, size, NULL, 0) : NULL;
    free(text);
    /* a missing or unreadable cache only costs a second hash */
    if (known != NULL && known->type == JSON_OBJECT) {
        for (size_t i = 0; i < known->count; i++) {
            const Json *digest = &known->items[i];
            if (digest->type == JSON_STRING && strlen(digest->string) == 64) {
                add_entry(cache, known->keys[i], digest->string);
            }
        }
    }
    json_free(known);
    return cache;
}

void hash_cache_close(HashCache *cache)
{
    if (cache == NULL) {
        return;
    }
    for (size_t i = 0; i < cache->count; i++) {
        free(cache->entries[i].key);
    }
    free(cache->entries);
    free(cache->file);
    free(cache->data_directory);
    lock_free(cache->lock);
    free(cache);
}

/* The cache written to hashes.json.part, then put in place of hashes.json, so a reader never sees half a file. */
static void save(HashCache *cache)
{
    if (cache->file == NULL || cache->data_directory == NULL) {
        return;
    }
    Text text = {0};
    text_add(&text, "{");
    for (size_t i = 0; i < cache->count; i++) {
        text_add(&text, i > 0 ? ",\n " : "\n ");
        text_add_json_string(&text, cache->entries[i].key);
        text_add(&text, ": ");
        text_add_json_string(&text, cache->entries[i].hex);
    }
    text_add(&text, "\n}");
    size_t part_capacity = strlen(cache->file) + sizeof ".part";
    char *part = malloc(part_capacity);
    if (!text.failed && part != NULL && file_make_directories(cache->data_directory)) {
        snprintf(part, part_capacity, "%s.part", cache->file);
        FILE *file = file_open(part, "wb");
        if (file != NULL) {
            bool written = fwrite(text.text, 1, text.length, file) == text.length;
            written = fclose(file) == 0 && written;
            if (written) {
                file_replace(part, cache->file);
            }
        }
    }
    free(part);
    text_free(&text);
}

/* The cache's key for PATH: "RESOLVED|SIZE|MTIME_NS", as tools/install.py makes it. */
static bool key_of(const char *path, char *key, size_t capacity, uint64_t *size)
{
    char resolved[4096];
    FileInfo info;
    if (!file_resolve(path, resolved, sizeof resolved) || !file_info(path, &info)) {
        return false;
    }
    *size = info.size;
    int length = snprintf(key, capacity, "%s|%llu|%lld", resolved, (unsigned long long)info.size,
                          (long long)info.modified_ns);
    return length > 0 && (size_t)length < capacity;
}

DiscHashResult hash_cache_disc_hash(HashCache *cache, const char *path, const char *stage, Job *job, char hex[65],
                                    bool *earlier)
{
    char key[4200];
    uint64_t size = 0;
    bool has_key = key_of(path, key, sizeof key, &size);
    *earlier = false;
    if (has_key) {
        lock_take(cache->lock);
        for (size_t i = 0; i < cache->count; i++) {
            if (strcmp(cache->entries[i].key, key) == 0) {
                memcpy(hex, cache->entries[i].hex, 65);
                *earlier = true;
                break;
            }
        }
        lock_release(cache->lock);
    }
    if (*earlier) {
        job_progress(job, stage, size, size);
        return DISC_HASHED;
    }
    job_log(job, "checking %s (%.1f GB)", path, (double)size / (double)(1u << 30));
    DiscHashResult result = disc_hash(path, job, stage, hex);
    if (result == DISC_HASHED && has_key) {
        lock_take(cache->lock);
        if (add_entry(cache, key, hex)) {
            save(cache);
        }
        lock_release(cache->lock);
    }
    return result;
}
