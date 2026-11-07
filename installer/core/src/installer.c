#include "installer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disc.h"
#include "files.h"

static bool fail(InstallError *error, const char *code, const char *format, ...)
{
    error->code = code;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error->message, sizeof error->message, format, arguments);
    va_end(arguments);
    return false;
}

bool payload_open(Payload *payload, const char *directory, InstallError *error)
{
    memset(payload, 0, sizeof *payload);
    size_t length = strlen(directory);
    payload->directory = malloc(length + 1);
    if (payload->directory == NULL) {
        return fail(error, "internal", "out of memory");
    }
    memcpy(payload->directory, directory, length + 1);
    char path[4096];
    snprintf(path, sizeof path, "%s/edition.json", directory);
    size_t size;
    char *text = file_read_all(path, &size);
    if (text == NULL) {
        return fail(error, "internal", "%s cannot be read", path);
    }
    char parse_error[128];
    payload->edition = json_parse(text, size, parse_error, sizeof parse_error);
    free(text);
    if (payload->edition == NULL || payload->edition->type != JSON_OBJECT) {
        return fail(error, "internal", "%s: %s", path, payload->edition == NULL ? parse_error : "not an object");
    }
    return true;
}

void payload_close(Payload *payload)
{
    free(payload->directory);
    json_free(payload->edition);
    memset(payload, 0, sizeof *payload);
}

const char *payload_commit(const Payload *payload)
{
    return json_get_string(payload->edition, "commit");
}

/* The edition's name for GAME's disc ("Going Commando"), or NULL when the edition uses no disc of that game. */
static const char *disc_name(const Payload *payload, const char *game)
{
    return game != NULL ? json_get_string(json_get(json_get(payload->edition, "disc_names"), game), "name") : NULL;
}

/* "Ratchet & Clank (PAL)": the game's disc as the edition names it, in REGION or else the edition's own. */
static void disc_label(const Payload *payload, const char *game, const char *region, char *label, size_t capacity)
{
    const Json *names = json_get(json_get(payload->edition, "disc_names"), game);
    snprintf(label, capacity, "%s (%s)", json_get_string(names, "name"),
             region != NULL ? region : json_get_string(names, "region"));
}

bool command_editions(const Payload *payload, Text *answer, InstallError *error)
{
    const Json *edition = payload->edition;
    const Json *economies = json_get(edition, "economies");
    const Json *switches = json_get(edition, "switches");
    if (economies == NULL || economies->type != JSON_OBJECT || switches == NULL || switches->type != JSON_ARRAY) {
        return fail(error, "internal", "edition.json lists no economies or switches");
    }
    text_add(answer, "[{\"id\": ");
    text_add_json_string(answer, json_get_string(edition, "edition"));
    text_add(answer, ", \"name\": ");
    text_add_json_string(answer, json_get_string(edition, "name"));
    text_add(answer, ", \"game\": ");
    text_add_json_string(answer, json_get_string(edition, "game"));
    text_add(answer, ", \"version\": ");
    text_add_json_string(answer, payload_commit(payload));
    text_add(answer, ", \"discs\": ");
    text_add_json(answer, json_get(edition, "disc_names"));
    text_add(answer, ", \"options\": {\"economy\": [");
    for (size_t i = 0; i < economies->count; i++) {
        text_add(answer, i > 0 ? ", " : "");
        text_add_json_string(answer, economies->keys[i]);
    }
    text_add(answer, "], \"switches\": [");
    for (size_t i = 0; i < switches->count; i++) {
        const char *disc = json_get_string(&switches->items[i], "disc");
        const Json *required = json_get(json_get(json_get(edition, "disc_names"), disc != NULL ? disc : ""), "required");
        text_add(answer, i > 0 ? ", {\"id\": " : "{\"id\": ");
        text_add_json_string(answer, json_get_string(&switches->items[i], "id"));
        text_add(answer, ", \"needs\": ");
        /* a feature built from a disc the edition always has needs nothing more */
        text_add_json_string(answer, required != NULL && required->type == JSON_TRUE ? NULL : disc);
        text_add(answer, "}");
    }
    text_add(answer, "]}}]");
    return true;
}

bool command_identify(const Payload *payload, HashCache *cache, const char *path, Job *job, Text *answer,
                      InstallError *error)
{
    FileInfo info;
    if (!file_info(path, &info) || info.is_directory) {
        return fail(error, "missing_disc", "%s does not exist", path);
    }
    DiscKind kind;
    if (!disc_kind(path, &kind) || !kind.is_ps2_disc) {
        job_log(job, "%s: not an image of a PS2 disc", path);
        text_add(answer, "{\"disc\": null, \"game\": null, \"name\": null, \"region\": null, \"sha256\": null, "
                         "\"known\": false}");
        return true;
    }
    /* the game and its version by the boot file, as the edition's boot_files name them */
    const Json *boot = json_get(json_get(payload->edition, "boot_files"), kind.boot_file);
    const char *game = json_get_string(boot, "game");
    const char *region = json_get_string(boot, "region");
    const char *name = disc_name(payload, game);
    if (!kind.complete) {
        return fail(error, "image_incomplete", "%s ends before its last sector: the image is incomplete", path);
    }
    char hex[65];
    bool earlier;
    DiscHashResult hashed = hash_cache_disc_hash(cache, path, "hash", job, hex, &earlier);
    if (hashed == DISC_CANCELLED) {
        return fail(error, "cancelled", "cancelled");
    }
    if (hashed != DISC_HASHED) {
        return fail(error, "missing_disc", "%s cannot be read", path);
    }
    const char *expected = game != NULL ? json_get_string(json_get(payload->edition, "discs"), game) : NULL;
    bool known = expected != NULL && strcmp(expected, hex) == 0;
    if (earlier) {
        job_log(job, "%s: checked earlier", path);
    }
    if (known) {
        char label[256];
        disc_label(payload, game, NULL, label, sizeof label);
        job_log(job, "sha256 %s matches %s", hex, label);
    } else if (name != NULL) {
        job_log(job, "sha256 %s: %s (%s), not the version this edition is built from", hex, name, region);
    } else {
        job_log(job, "sha256 %s: not a disc of this edition", hex);
    }
    text_add(answer, "{\"disc\": ");
    text_add_json_string(answer, known ? game : NULL);
    text_add(answer, ", \"game\": ");
    text_add_json_string(answer, game);
    text_add(answer, ", \"name\": ");
    text_add_json_string(answer, name);
    text_add(answer, ", \"region\": ");
    text_add_json_string(answer, region);
    text_add(answer, ", \"sha256\": ");
    text_add_json_string(answer, hex);
    text_add(answer, known ? ", \"known\": true}" : ", \"known\": false}");
    return true;
}
