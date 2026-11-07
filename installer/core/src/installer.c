#include "installer.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disc.h"
#include "files.h"

bool install_fail(InstallError *error, const char *code, const char *format, ...)
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
        return install_fail(error, "internal", "out of memory");
    }
    memcpy(payload->directory, directory, length + 1);
    char path[4096];
    snprintf(path, sizeof path, "%s/edition.json", directory);
    size_t size;
    char *text = file_read_all(path, &size);
    if (text == NULL) {
        return install_fail(error, "internal", "%s cannot be read", path);
    }
    char parse_error[128];
    payload->edition = json_parse(text, size, parse_error, sizeof parse_error);
    free(text);
    if (payload->edition == NULL || payload->edition->type != JSON_OBJECT) {
        return install_fail(error, "internal", "%s: %s", path, payload->edition == NULL ? parse_error : "not an object");
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
void payload_disc_label(const Payload *payload, const char *game, const char *region, char *label, size_t capacity)
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
        return install_fail(error, "internal", "edition.json lists no economies or switches");
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
        return install_fail(error, "missing_disc", "%s does not exist", path);
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
        return install_fail(error, "image_incomplete", "%s ends before its last sector: the image is incomplete", path);
    }
    char hex[65];
    bool earlier;
    DiscHashResult hashed = hash_cache_disc_hash(cache, path, "hash", job, hex, &earlier);
    if (hashed == DISC_CANCELLED) {
        return install_fail(error, "cancelled", "cancelled");
    }
    if (hashed != DISC_HASHED) {
        return install_fail(error, "missing_disc", "%s cannot be read", path);
    }
    const char *expected = game != NULL ? json_get_string(json_get(payload->edition, "discs"), game) : NULL;
    bool known = expected != NULL && strcmp(expected, hex) == 0;
    if (earlier) {
        job_log(job, "%s: checked earlier", path);
    }
    if (known) {
        char label[256];
        payload_disc_label(payload, game, NULL, label, sizeof label);
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

/* The disc the edition is built on (the one disc_names marks required), or NULL. */
const char *payload_base_disc(const Payload *payload)
{
    const Json *names = json_get(payload->edition, "disc_names");
    for (size_t i = 0; names != NULL && names->type == JSON_OBJECT && i < names->count; i++) {
        const Json *required = json_get(&names->items[i], "required");
        if (required != NULL && required->type == JSON_TRUE) {
            return names->keys[i];
        }
    }
    return NULL;
}

/* VALUE as Python's repr() writes it, for the messages that quote what the builder sent: a string in quotes, a
   list of them in brackets. Anything else as JSON. */
static void add_python_repr(Text *text, const Json *value)
{
    if (value->type == JSON_ARRAY) {
        text_add(text, "[");
        for (size_t i = 0; i < value->count; i++) {
            text_add(text, i > 0 ? ", " : "");
            add_python_repr(text, &value->items[i]);
        }
        text_add(text, "]");
        return;
    }
    if (value->type != JSON_STRING) {
        text_add_json(text, value);
        return;
    }
    /* single quotes, unless the string holds one and no double quote */
    char quote = strchr(value->string, '\'') != NULL && strchr(value->string, '"') == NULL ? '"' : '\'';
    text_append(text, &quote, 1);
    for (const char *c = value->string; *c != 0; c++) {
        if (*c == '\\' || *c == quote) {
            text_append(text, "\\", 1);
            text_append(text, c, 1);
        } else if (*c == '\n') {
            text_add(text, "\\n");
        } else if (*c == '\r') {
            text_add(text, "\\r");
        } else if (*c == '\t') {
            text_add(text, "\\t");
        } else if ((unsigned char)*c < 0x20 || *c == 0x7f) {
            text_format(text, "\\x%02x", (unsigned)(unsigned char)*c);
        } else {
            text_append(text, c, 1);
        }
    }
    text_append(text, &quote, 1);
}

static bool is_folder(const char *path)
{
    FileInfo info;
    return file_info(path, &info) && info.is_directory;
}

static bool exists(const char *path)
{
    FileInfo info;
    return file_info(path, &info);
}

/* An error opening PATH for writing, as the protocol's not_writable, worded as Python's OSError. */
static bool not_writable(InstallError *error, const char *path)
{
    return install_fail(error, "not_writable", "%s: %s", path, strerror(errno));
}

/* Whether a file can be made in FOLDER: one is made there and deleted. */
static bool takes_files(const char *folder, InstallError *error)
{
    char probe[4096 + 64];          /* a folder of up to 4096 bytes, and the probe's name */
    size_t length = strlen(folder);
    bool rooted = length > 0 && (folder[length - 1] == '/' || folder[length - 1] == '\\');
#ifdef _WIN32
    const char *separator = rooted ? "" : "\\";
#else
    const char *separator = rooted ? "" : "/";
#endif
    for (unsigned attempt = 0;; attempt++) {
        snprintf(probe, sizeof probe, "%s%s.rac-installer-%u.tmp", folder, separator, attempt);
        FILE *file = file_open(probe, "wbx");
        if (file != NULL) {
            fclose(file);
            file_remove(probe);
            return true;
        }
        if (errno != EEXIST || attempt == 99) {
            return not_writable(error, probe);
        }
    }
}

/* Fails with not_writable for an output the build could not write (a folder, one of the discs, a file in use or
   read-only, a folder that cannot be made or written), and with no_space for a drive with less than NEEDED free. */
static bool check_output(const Payload *payload, const Json *discs, const Json *output, uint64_t needed,
                         InstallError *error)
{
    const char *mode = json_get_string(output, "mode");
    if (mode == NULL) {
        return install_fail(error, "internal", "KeyError: 'mode'");
    }
    char folder[4096];
    if (strcmp(mode, "new") == 0) {
        const char *path = json_get_string(output, "path");
        if (path == NULL) {
            return install_fail(error, "internal", "KeyError: 'path'");
        }
        char target[4096];
        path_clean(path, target, sizeof target);
        if (is_folder(target)) {
            return install_fail(error, "not_writable", "%s is a folder: the image needs a file name", target);
        }
        char resolved[4096];
        char disc_resolved[4096];
        if (file_resolve(target, resolved, sizeof resolved)) {
            for (size_t i = 0; i < discs->count; i++) {
                const char *disc = discs->items[i].string;
                if (disc != NULL && file_resolve(disc, disc_resolved, sizeof disc_resolved) &&
                    strcmp(resolved, disc_resolved) == 0) {
                    return install_fail(error, "not_writable", "%s is one of the discs the edition is built from", target);
                }
            }
        }
        path_parent(target, folder, sizeof folder);
        if (exists(target)) {
            /* in use (an emulator has it open) or read-only */
            FILE *file = file_open(target, "ab");
            if (file == NULL) {
                return not_writable(error, target);
            }
            fclose(file);
        } else if (is_folder(folder)) {
            /* a name the system takes, in a folder that takes files */
            FILE *file = file_open(target, "wbx");
            if (file == NULL) {
                return not_writable(error, target);
            }
            fclose(file);
            file_remove(target);
        }
    } else if (strcmp(mode, "in_place") == 0) {
        char base[4096];
        path_clean(json_get_string(discs, payload_base_disc(payload)), base, sizeof base);
        FILE *file = file_open(base, "r+b");
        if (file == NULL) {
            return not_writable(error, base);
        }
        fclose(file);
        path_parent(base, folder, sizeof folder);
    } else {
        return install_fail(error, "internal", "unknown output mode %s", mode);
    }
    /* the folders the build would make: the nearest one there must take them */
    while (!exists(folder)) {
        char parent[4096];
        path_parent(folder, parent, sizeof parent);
        if (strcmp(parent, folder) == 0) {
            /* a drive that is not there */
            return install_fail(error, "not_writable", "%s does not exist", folder);
        }
        memcpy(folder, parent, strlen(parent) + 1);
    }
    if (!is_folder(folder)) {
        return install_fail(error, "not_writable", "%s is a file, not a folder", folder);
    }
    if (!takes_files(folder, error)) {
        return false;
    }
    uint64_t free_bytes;
    if (!file_free_space(folder, &free_bytes)) {
        return not_writable(error, folder);
    }
    if (free_bytes < needed) {
        return install_fail(error, "no_space", "%s has %llu MB free; the build needs %llu MB", folder,
                    (unsigned long long)(free_bytes >> 20), (unsigned long long)(needed >> 20));
    }
    return true;
}

bool json_list_has(const Json *list, const char *string)
{
    for (size_t i = 0; list != NULL && list->type == JSON_ARRAY && i < list->count; i++) {
        if (list->items[i].type == JSON_STRING && strcmp(list->items[i].string, string) == 0) {
            return true;
        }
    }
    return false;
}

bool command_plan(const Payload *payload, const Json *discs, const Json *options, const Json *output, Text *answer,
                  InstallError *error)
{
    const Json *edition = payload->edition;
    const Json *switches = json_get(edition, "switches");
    const Json *economies = json_get(edition, "economies");
    const char *base = payload_base_disc(payload);
    if (switches == NULL || switches->type != JSON_ARRAY || economies == NULL || economies->type != JSON_OBJECT ||
        economies->count == 0 || base == NULL) {
        return install_fail(error, "internal", "edition.json lists no switches, economies or base disc");
    }
    if (discs == NULL || discs->type != JSON_OBJECT) {
        return install_fail(error, "internal", "bad command: plan needs discs");
    }
    if (json_get(discs, base) == NULL) {
        char label[256];
        payload_disc_label(payload, base, NULL, label, sizeof label);
        return install_fail(error, "missing_disc", "the edition is built from %s", label);
    }
    for (size_t i = 0; i < discs->count; i++) {
        FileInfo info;
        const char *path = discs->items[i].string;
        if (path == NULL || !file_info(path, &info) || info.is_directory) {
            return install_fail(error, "missing_disc", "%s does not exist", path != NULL ? path : "null");
        }
    }

    /* the options: features left out by choice, and the economy, else the edition's first */
    const Json *without = json_get(options, "without");
    Json unknown = {.type = JSON_ARRAY};
    Json unknown_items[64];
    unknown.items = unknown_items;
    for (size_t i = 0; without != NULL && without->type == JSON_ARRAY && i < without->count; i++) {
        bool known = false;
        for (size_t s = 0; without->items[i].type == JSON_STRING && s < switches->count; s++) {
            const char *id = json_get_string(&switches->items[s], "id");
            known = known || (id != NULL && strcmp(id, without->items[i].string) == 0);
        }
        if (!known && unknown.count < 64) {
            unknown_items[unknown.count++] = without->items[i];
        }
    }
    const Json *economy = json_get(options, "economy");
    bool economy_known = true;
    if (economy != NULL && economy->type != JSON_NULL && !(economy->type == JSON_STRING && economy->string[0] == 0)) {
        economy_known = economy->type == JSON_STRING && json_get(economies, economy->string) != NULL;
    }
    if (unknown.count > 0 || !economy_known) {
        Text named = {0};
        add_python_repr(&named, unknown.count > 0 ? &unknown : economy);
        /* Python's f-string shows the economy itself, unquoted */
        const char *shown = unknown.count == 0 && economy->type == JSON_STRING ? economy->string : named.text;
        install_fail(error, "internal", "unknown options: %s", shown != NULL ? shown : "");
        text_free(&named);
        return false;
    }

    /* each switch in, unless chosen out or built from a disc the build does not have */
    text_add(answer, "{\"in\": [");
    size_t added = 0;
    for (size_t s = 0; s < switches->count; s++) {
        const char *id = json_get_string(&switches->items[s], "id");
        const char *disc = json_get_string(&switches->items[s], "disc");
        if (!json_list_has(without, id) && json_get(discs, disc != NULL ? disc : "") != NULL) {
            text_add(answer, added++ > 0 ? ", " : "");
            text_add_json_string(answer, id);
        }
    }
    text_add(answer, "], \"left_out\": [");
    added = 0;
    for (size_t s = 0; s < switches->count; s++) {
        const char *id = json_get_string(&switches->items[s], "id");
        const char *disc = json_get_string(&switches->items[s], "disc");
        bool chosen = json_list_has(without, id);
        if (chosen || json_get(discs, disc != NULL ? disc : "") == NULL) {
            text_add(answer, added++ > 0 ? ", {\"id\": " : "{\"id\": ");
            text_add_json_string(answer, id);
            if (chosen) {
                text_add(answer, ", \"because\": \"chosen\"}");
            } else {
                text_add(answer, ", \"because\": ");
                Text because = {0};
                text_format(&because, "no_%s", disc != NULL ? disc : "");
                text_add_json_string(answer, because.text);
                text_free(&because);
                text_add(answer, "}");
            }
        }
    }

    /* updating in place also writes the restore file, beside a full copy made first */
    const char *mode = json_get_string(output, "mode");
    int64_t growth = json_get_integer(edition, "growth", 0) * (mode != NULL && strcmp(mode, "in_place") == 0 ? 2 : 1);
    FileInfo base_info;
    file_info(json_get_string(discs, base), &base_info);
    uint64_t needed = base_info.size + (uint64_t)growth;
    text_format(answer, "], \"bytes_needed\": %llu}", (unsigned long long)needed);
    if (output != NULL && output->type != JSON_NULL && !check_output(payload, discs, output, needed, error)) {
        return false;
    }
    return true;
}
