/*
 * The installer's commands, as the builder's protocol names them (installer/README.md, "The protocol"): each
 * answers a JSON value, or fails with one of the protocol's error codes and a message.
 */
#ifndef RAC_INSTALLER_H
#define RAC_INSTALLER_H

#include <stdbool.h>
#include <stddef.h>

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

/* Fails: ERROR set to CODE and the formatted message; returns false. */
bool install_fail(InstallError *error, const char *code, const char *format, ...);

/* The disc the edition is built on (the one disc_names marks required), or NULL. */
const char *payload_base_disc(const Payload *payload);

/* "Ratchet & Clank (PAL)": GAME's disc as the edition names it, in REGION or else the edition's own. */
void payload_disc_label(const Payload *payload, const char *game, const char *region, char *label, size_t capacity);

/* Whether the JSON array LIST holds the string STRING. */
bool json_list_has(const Json *list, const char *string);

/* The edition's version: its release's tag ("v0.3.0"), or the rac-editions commit between releases. It also names
   this installer's version in the protocol's ready event. */
const char *payload_version(const Payload *payload);

/* editions: the editions this installer has payloads for, as the builder shows them. */
bool command_editions(const Payload *payload, Text *answer, InstallError *error);

/* identify: which disc the image at PATH is, by its boot file, then by its hash against the edition's. */
bool command_identify(const Payload *payload, HashCache *cache, const char *path, Job *job, Text *answer,
                      InstallError *error);

/* plan: what a build with DISCS (an object of disc ids to paths) and OPTIONS (economy, without) has in, what it
   leaves out and why, and the bytes it needs; with OUTPUT (mode new with a path, or in_place), also whether the
   image can be written there. OPTIONS and OUTPUT may be NULL. */
bool command_plan(const Payload *payload, const Json *discs, const Json *options, const Json *output, Text *answer,
                  InstallError *error);

#endif
