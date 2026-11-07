/*
 * --serve: the builder's protocol on standard input and output (installer/README.md, "The protocol"). One JSON
 * command per line in, one JSON event per line out. A build or an identify runs in a thread of its own, so discs are
 * checked side by side and a cancel can reach any of them; the other commands answer in order. When the input ends,
 * the commands still running answer before the installer exits.
 */
#ifndef RAC_SERVE_H
#define RAC_SERVE_H

#include <stdio.h>

#include "hash_cache.h"
#include "installer.h"

/* Serves commands from INPUT until it ends, writing events to OUTPUT; returns the process's exit code. */
int serve(const Payload *payload, HashCache *cache, FILE *input, FILE *output);

#endif
