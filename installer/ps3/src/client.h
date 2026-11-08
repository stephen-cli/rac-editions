/*
 * The installer core, in-process: commands go to its Server as protocol lines, and its events, which its threads send
 * whenever they have them, wait in a queue until the builder's frame loop takes them.
 */
#ifndef RAC_PS3_CLIENT_H
#define RAC_PS3_CLIENT_H

#include <stdbool.h>

#include "json.h"

typedef struct Client Client;

/* The core serving the payload in PAYLOAD_FOLDER, keeping disc hashes in DATA_FOLDER; NULL with ERROR filled when
   the payload cannot be read. */
Client *client_open(const char *payload_folder, const char *data_folder, char *error, size_t capacity);

/* Waits for the core's commands still running, then closes it. */
void client_close(Client *client);

/* Sends COMMAND, a protocol command as JSON text. */
void client_send(Client *client, const char *command);

/* The oldest event not yet taken, parsed (the caller frees it), or NULL when there is none. */
Json *client_next_event(Client *client);

/* The edition's boot files, to tell cheaply whether an image could be one of its discs: whether BOOT_FILE (as
   disc_kind names it, "SCES_509.16") is listed. */
bool client_knows_boot_file(Client *client, const char *boot_file);

#endif
