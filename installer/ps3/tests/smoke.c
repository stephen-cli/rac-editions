/*
 * The installer core on the PS3, headless: the edition built from the discs in /dev_hdd0/PS2ISO through the core's
 * in-process server, as the PS3 builder drives it, with every event printed to the TTY. It identifies each image
 * there, picks the discs the edition is built from, plans, and builds into /dev_hdd0/PS2ISO/SMOKE_TEST.iso, which it
 * leaves for the image's SHA-256 to be checked. The payload is in the app's USRDIR/payload.
 */
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/file.h>

#include "files.h"
#include "hash_cache.h"
#include "installer.h"
#include "json.h"
#include "serve.h"
#include "thread.h"

#define DISCS_FOLDER "/dev_hdd0/PS2ISO"
#define OUTPUT DISCS_FOLDER "/SMOKE_TEST.iso"

/* The answer to the command being waited for: its "result" or "error" event, as JSON. */
static Lock *answer_lock;
static char waiting_for[32];
static Json *answer;

static void receive(void *context, const char *line, size_t length)
{
    (void)context;
    printf("%.*s\n", (int)length, line);
    char error[128];
    Json *event = json_parse(line, length, error, sizeof error);
    const char *kind = json_get_string(event, "event");
    const Json *id = json_get(event, "id");
    Text id_text = {0};
    if (id != NULL) {
        text_add_json(&id_text, id);
    }
    bool final = kind != NULL && (strcmp(kind, "result") == 0 || strcmp(kind, "error") == 0);
    lock_take(answer_lock);
    if (final && id_text.text != NULL && strcmp(id_text.text, waiting_for) == 0 && answer == NULL) {
        answer = event;
        event = NULL;
    }
    lock_release(answer_lock);
    text_free(&id_text);
    json_free(event);
}

/* Sends COMMAND (JSON text whose id is the string ID) and waits for its answer, which the caller frees. */
static Json *ask(Server *server, const char *id, const char *command)
{
    lock_take(answer_lock);
    snprintf(waiting_for, sizeof waiting_for, "\"%s\"", id);
    answer = NULL;
    lock_release(answer_lock);
    printf("> %s\n", command);
    server_command(server, command, strlen(command));
    for (;;) {
        lock_take(answer_lock);
        Json *got = answer;
        lock_release(answer_lock);
        if (got != NULL) {
            return got;
        }
        thread_sleep(100);
    }
}

/* The folder holding the running EBOOT.BIN, from argv[0]. */
static void app_folder(const char *argv0, char *folder, size_t capacity)
{
    snprintf(folder, capacity, "%s", argv0 != NULL ? argv0 : "");
    char *slash = strrchr(folder, '/');
    if (slash != NULL) {
        *slash = 0;
    }
}

/* lv2's own file calls on a large image, without stdio: its size, a seek to the end, reads past 2 and 4 GB. */
static void probe_lv2(const char *path)
{
    s32 fd;
    s32 opened = sysLv2FsOpen(path, SYS_O_RDONLY, &fd, 0, NULL, 0);
    if (opened != 0) {
        printf("lv2: open failed %d\n", (int)opened);
        return;
    }
    sysFSStat status;
    sysLv2FsFStat(fd, &status);
    u64 position = 0;
    s32 sought = sysLv2FsLSeek64(fd, 0, SEEK_END, &position);
    printf("lv2: fstat size %llu; seek to the end %d, position %llu\n", (unsigned long long)status.st_size,
           (int)sought, (unsigned long long)position);
    static uint8_t buffer[16u << 20];
    const u64 offsets[] = {0, 3000000000ull, 4200000000ull};
    for (size_t i = 0; i < 3; i++) {
        sought = sysLv2FsLSeek64(fd, offsets[i], SEEK_SET, &position);
        u64 got = 0;
        s32 read = sysLv2FsRead(fd, buffer, i == 0 ? sizeof buffer : 65536, &got);
        printf("lv2: seek to %llu %d (position %llu), read %d got %llu\n", (unsigned long long)offsets[i], (int)sought,
               (unsigned long long)position, (int)read, (unsigned long long)got);
    }
    sysLv2FsClose(fd);
}

/* How reads and seeks behave on a large image: each step's result, to the TTY. */
static void probe_reads(const char *path)
{
    FILE *file = file_open(path, "rb");
    if (file == NULL) {
        printf("probe: %s does not open\n", path);
        return;
    }
    static uint8_t buffer[16u << 20];
    size_t got = fread(buffer, 1, 65536, file);
    printf("probe: first 64 KiB read %u, ferror %d, feof %d, ftello %lld\n", (unsigned)got, ferror(file), feof(file),
           (long long)ftello(file));
    uint64_t size = file_size(file);
    printf("probe: file_size %llu, ftello %lld\n", (unsigned long long)size, (long long)ftello(file));
    bool seeked = file_seek(file, 0);
    printf("probe: seek to 0 %s, ftello %lld\n", seeked ? "ok" : "FAILED", (long long)ftello(file));
    got = fread(buffer, 1, 65536, file);
    printf("probe: 64 KiB after seeking read %u, ferror %d, feof %d\n", (unsigned)got, ferror(file), feof(file));
    clearerr(file);
    file_seek(file, 0);
    got = fread(buffer, 1, sizeof buffer, file);
    printf("probe: 16 MiB read %u, ferror %d, feof %d\n", (unsigned)got, ferror(file), feof(file));
    clearerr(file);
    seeked = file_seek(file, 3000000000ull);
    got = fread(buffer, 1, 65536, file);
    printf("probe: seek to 3 GB %s, read %u, ftello %lld\n", seeked ? "ok" : "FAILED", (unsigned)got,
           (long long)ftello(file));
    clearerr(file);
    seeked = file_seek(file, 4200000000ull);
    got = fread(buffer, 1, 65536, file);
    printf("probe: seek to 4.2 GB %s, read %u, ftello %lld\n", seeked ? "ok" : "FAILED", (unsigned)got,
           (long long)ftello(file));
    fclose(file);
}

int main(int argc, char **argv)
{
    /* the TTY is not a terminal, so newlib would hold the lines in a buffer */
    setvbuf(stdout, NULL, _IONBF, 0);
    char usrdir[512], payload_folder[600], data_folder[600];
    app_folder(argc > 0 ? argv[0] : NULL, usrdir, sizeof usrdir);
    snprintf(payload_folder, sizeof payload_folder, "%s/payload", usrdir);
    snprintf(data_folder, sizeof data_folder, "%s/data", usrdir);
    printf("smoke: payload %s, data %s\n", payload_folder, data_folder);
    probe_lv2(DISCS_FOLDER "/SCES_509.16.iso");
    probe_reads(DISCS_FOLDER "/SCES_509.16.iso");

    Payload payload;
    InstallError error = {0};
    if (!payload_open(&payload, payload_folder, &error)) {
        printf("smoke: FAILED: payload: %s (%s)\n", error.message, error.code);
        return 1;
    }
    HashCache *cache = hash_cache_open(data_folder);
    answer_lock = lock_new();
    Server *server = server_new(&payload, cache, receive, NULL);

    Json *editions = ask(server, "e", "{\"id\": \"e\", \"cmd\": \"editions\"}");
    json_free(editions);

    char rac1[1100] = "", rac2[1100] = "";
    DIR *folder = opendir(DISCS_FOLDER);
    struct dirent *entry;
    int n = 0;
    while (folder != NULL && (entry = readdir(folder)) != NULL) {
        size_t length = strlen(entry->d_name);
        if (length < 4 || strcasecmp(entry->d_name + length - 4, ".iso") != 0
            || strcmp(entry->d_name, "SMOKE_TEST.iso") == 0) {
            continue;
        }
        char path[1100], id[16], command[2400];
        snprintf(path, sizeof path, "%s/%s", DISCS_FOLDER, entry->d_name);
        snprintf(id, sizeof id, "i%d", n++);
        Text quoted = {0};
        text_add_json_string(&quoted, path);
        snprintf(command, sizeof command, "{\"id\": \"%s\", \"cmd\": \"identify\", \"path\": %s}", id, quoted.text);
        text_free(&quoted);
        Json *identified = ask(server, id, command);
        const char *disc = json_get_string(json_get(identified, "value"), "disc");
        if (disc != NULL && strcmp(disc, "rac1") == 0) {
            snprintf(rac1, sizeof rac1, "%s", path);
        } else if (disc != NULL && strcmp(disc, "rac2") == 0) {
            snprintf(rac2, sizeof rac2, "%s", path);
        }
        json_free(identified);
    }
    if (folder != NULL) {
        closedir(folder);
    }
    if (rac1[0] == 0) {
        printf("smoke: FAILED: no Ratchet & Clank (PAL) image in %s\n", DISCS_FOLDER);
        return 1;
    }

    Text discs = {0};
    text_add(&discs, "{\"rac1\": ");
    text_add_json_string(&discs, rac1);
    if (rac2[0] != 0) {
        text_add(&discs, ", \"rac2\": ");
        text_add_json_string(&discs, rac2);
    }
    text_add(&discs, "}");
    char command[4096];
    snprintf(command, sizeof command,
             "{\"id\": \"p\", \"cmd\": \"plan\", \"discs\": %s, \"options\": {}, "
             "\"output\": {\"mode\": \"new\", \"path\": \"" OUTPUT "\"}}", discs.text);
    json_free(ask(server, "p", command));
    snprintf(command, sizeof command,
             "{\"id\": \"b\", \"cmd\": \"build\", \"discs\": %s, \"options\": {}, "
             "\"output\": {\"mode\": \"new\", \"path\": \"" OUTPUT "\"}}", discs.text);
    Json *built = ask(server, "b", command);
    const char *sha256 = json_get_string(json_get(built, "value"), "sha256");
    printf("smoke: %s %s\n", sha256 != NULL ? "BUILT" : "FAILED", sha256 != NULL ? sha256 : "");
    json_free(built);
    text_free(&discs);

    server_free(server);
    hash_cache_close(cache);
    payload_close(&payload);
    return sha256 != NULL ? 0 : 1;
}
