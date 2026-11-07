/*
 * The SHA-256 of every disc image hashed before, by its resolved path, size and modification time, so a disc checked
 * when it was added is not hashed again for the build. Kept in DATA/hashes.json between runs, in the same form as
 * tools/install.py's (a JSON object of "PATH|SIZE|MTIME_NS" to the hex digest), so either reads the other's.
 */
#ifndef RAC_HASH_CACHE_H
#define RAC_HASH_CACHE_H

#include <stdbool.h>

#include "disc.h"
#include "job.h"

typedef struct HashCache HashCache;

/* The cache kept in DATA_DIRECTORY (made when first saved); NULL keeps nothing between runs. */
HashCache *hash_cache_open(const char *data_directory);
void hash_cache_close(HashCache *cache);

/* PATH's SHA-256 and whether it was checked earlier: from the cache then (one progress event, done == total), else
   hashed with progress events in STAGE after the log line "checking PATH (N.N GB)". */
DiscHashResult hash_cache_disc_hash(HashCache *cache, const char *path, const char *stage, Job *job, char hex[65],
                                    bool *earlier);

#endif
