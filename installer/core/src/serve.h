/*
 * The builder's protocol (installer/README.md, "The protocol"): JSON commands in, JSON events out, a line each.
 * A build or an identify runs in a thread of its own, so discs are checked side by side and a cancel can reach any
 * of them; the other commands answer in order.
 *
 * A Server takes command lines and sends event lines through a function, so a builder that links the core (the
 * PS3's) speaks the same protocol in-process; serve() is --serve, the same over standard input and output.
 */
#ifndef RAC_SERVE_H
#define RAC_SERVE_H

#include <stddef.h>
#include <stdio.h>

#include "hash_cache.h"
#include "installer.h"

/* Sends one event LINE (LENGTH bytes, no line break), whole; called from any of the server's threads, one at a
   time. */
typedef void ServerSend(void *context, const char *line, size_t length);

typedef struct Server Server;

/* A server for PAYLOAD's edition, which sends its "ready" event at once; NULL when out of memory. */
Server *server_new(const Payload *payload, HashCache *cache, ServerSend *send, void *context);

/* One command LINE (LENGTH bytes, no line break). Blank lines are ignored. */
void server_command(Server *server, const char *line, size_t length);

/* Waits until the identifies and builds still running have answered, then frees SERVER. */
void server_free(Server *server);

/* Serves commands from INPUT until it ends, writing events to OUTPUT; returns the process's exit code. */
int serve(const Payload *payload, HashCache *cache, FILE *input, FILE *output);

#endif
