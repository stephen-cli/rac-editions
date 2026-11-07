/*
 * The installer's commands, as the builder's protocol names them (installer/README.md, "The protocol"): each
 * answers a JSON value, or fails with one of the protocol's error codes and a message.
 */
#ifndef RAC_INSTALLER_H
#define RAC_INSTALLER_H

#include <stdbool.h>

#include "hash_cache.h"
#include "job.h"
#include "json.h"

#define PROTOCOL_VERSION 1

typedef struct {
    /* unknown_disc, wrong_version, image_incomplete, missing_disc, no_space, not_writable, cancelled, internal */
    const char *code;
    char message[1024];
} InstallError;

/* A release payload: its folder and its edition.json. */
typedef struct {
    char *directory;
    Json *edition;
} Payload;

/* The payload in DIRECTORY; false with ERROR set when its edition.json cannot be read. */
bool payload_open(Payload *payload, const char *directory, InstallError *error);
void payload_close(Payload *payload);

/* The edition's commit, which names this installer's version in the protocol's ready event. */
const char *payload_commit(const Payload *payload);

/* editions: the editions this installer has payloads for, as the builder shows them. */
bool command_editions(const Payload *payload, Text *answer, InstallError *error);

/* identify: which disc the image at PATH is, by its boot file, then by its hash against the edition's. */
bool command_identify(const Payload *payload, HashCache *cache, const char *path, Job *job, Text *answer,
                      InstallError *error);

#endif
