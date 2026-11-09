#include "update.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "json.h"
#include "net.h"
#include "thread.h"

/* the repository whose releases carry the PS3 builder (installer/README.md, "Releases") */
#define LATEST_URL "https://api.github.com/repos/stephen-cli/rac-editions/releases/latest"
#define PACKAGE_NAME "EditionsBuilder.pkg"
/* the latest release's description, at most: its notes are a few kilobytes */
#define ANSWER_LIMIT (1024 * 1024)

static Lock *lock;
static Update current;              /* under the lock */
static NetRequest *active;          /* the request under way, to be stopped; under the lock */
static uint64_t package_size;       /* the newer package's size, from the release */
static char own_version[48], certificates[600], packages[600], user_agent[96], package_url[1024];
static Thread *checker, *downloader;

/* "v0.2.0" into its three numbers; false for anything else (a commit) */
static bool version_numbers(const char *text, unsigned numbers[3])
{
    char after;
    return text != NULL && sscanf(text, "v%u.%u.%u%c", &numbers[0], &numbers[1], &numbers[2], &after) == 3;
}

static bool newer(const unsigned candidate[3], const unsigned than[3])
{
    for (int i = 0; i < 3; i++) {
        if (candidate[i] != than[i]) {
            return candidate[i] > than[i];
        }
    }
    return false;
}

static void set_active(NetRequest *request)
{
    lock_take(lock);
    active = request;
    lock_release(lock);
}

typedef struct {
    char *data;
    size_t size;
} Answer;

static bool to_answer(void *context, const void *data, size_t size)
{
    Answer *answer = context;
    if (answer->size + size > ANSWER_LIMIT) {
        return false;
    }
    char *grown = realloc(answer->data, answer->size + size + 1);
    if (grown == NULL) {
        return false;
    }
    memcpy(grown + answer->size, data, size);
    answer->data = grown;
    answer->size += size;
    answer->data[answer->size] = 0;
    return true;
}

/* The latest release's tag and its package, from GitHub's answer; false when it has no package. */
static bool read_release(const Json *release, const char **tag)
{
    *tag = json_get_string(release, "tag_name");
    const Json *assets = json_get(release, "assets");
    if (assets == NULL || assets->type != JSON_ARRAY) {
        return false;
    }
    for (size_t i = 0; i < assets->count; i++) {
        const char *name = json_get_string(&assets->items[i], "name");
        const char *url = json_get_string(&assets->items[i], "browser_download_url");
        if (name != NULL && url != NULL && strcmp(name, PACKAGE_NAME) == 0) {
            snprintf(package_url, sizeof package_url, "%s", url);
            package_size = (uint64_t)json_get_integer(&assets->items[i], "size", 0);
            return true;
        }
    }
    return false;
}

static void check(void *unused)
{
    (void)unused;
    char error[300];
    if (!net_open(certificates, error, sizeof error)) {
        printf("update: not checked: %s\n", error);
        return;
    }
    Answer answer = {0};
    NetRequest request = {.url = LATEST_URL, .user_agent = user_agent, .write = to_answer, .context = &answer};
    set_active(&request);
    bool fetched = net_get(&request, error, sizeof error);
    set_active(NULL);
    if (!fetched) {
        printf("update: the check failed: %s\n", error);
        free(answer.data);
        return;
    }
    Json *release = json_parse(answer.data, answer.size, error, sizeof error);
    free(answer.data);
    if (release == NULL) {
        printf("update: GitHub's answer could not be read: %s\n", error);
        return;
    }
    const char *tag;
    bool has_package = read_release(release, &tag);
    unsigned latest[3], own[3];
    if (has_package && version_numbers(tag, latest) && version_numbers(own_version, own) && newer(latest, own)) {
        lock_take(lock);
        current.state = UPDATE_AVAILABLE;
        snprintf(current.latest, sizeof current.latest, "%s", tag);
        lock_release(lock);
        printf("update: %s is out (this is %s): %s, %llu bytes\n", tag, own_version, package_url,
               (unsigned long long)package_size);
    } else {
        printf("update: the latest release is %s%s; this is %s\n", tag != NULL ? tag : "(none)",
               has_package ? "" : ", without a package", own_version);
    }
    json_free(release);
}

typedef struct {
    FILE *file;
    uint64_t written;
} Saving;

static bool to_file(void *context, const void *data, size_t size)
{
    Saving *saving = context;
    if (fwrite(data, 1, size, saving->file) != size) {
        return false;
    }
    saving->written += size;
    return true;
}

static void download(void *unused)
{
    (void)unused;
    char path[700], part[710], error[300] = "";
    snprintf(path, sizeof path, "%s/%s", packages, PACKAGE_NAME);
    snprintf(part, sizeof part, "%s.part", path);
    file_make_directories(packages);
    Saving saving = {file_open(part, "wb"), 0};
    NetRequest request = {.url = package_url, .user_agent = user_agent, .write = to_file, .context = &saving};
    bool saved = saving.file != NULL;
    if (!saved) {
        snprintf(error, sizeof error, "%s could not be written", part);
    } else {
        set_active(&request);
        saved = net_get(&request, error, sizeof error);
        set_active(NULL);
        saved = fclose(saving.file) == 0 && saved;
        if (saved && package_size != 0 && saving.written != package_size) {
            snprintf(error, sizeof error, "the download stopped after %llu of %llu bytes",
                     (unsigned long long)saving.written, (unsigned long long)package_size);
            saved = false;
        }
        if (saved && !file_replace(part, path)) {
            snprintf(error, sizeof error, "%s could not be written", path);
            saved = false;
        }
        if (!saved) {
            file_remove(part);
        }
    }
    lock_take(lock);
    if (saved) {
        current.state = UPDATE_DOWNLOADED;
        snprintf(current.saved, sizeof current.saved, "%s", path);
    } else if (request.cancel) {
        current.state = UPDATE_AVAILABLE;
    } else {
        current.state = UPDATE_FAILED;
        snprintf(current.failure, sizeof current.failure, "%s", error);
    }
    lock_release(lock);
    printf("update: %s\n", saved ? path : request.cancel ? "download stopped" : error);
}

void update_start(const char *version, const char *certificates_path, const char *packages_folder)
{
    memset(&current, 0, sizeof current);
    snprintf(own_version, sizeof own_version, "%s", version);
    snprintf(certificates, sizeof certificates, "%s", certificates_path);
    snprintf(packages, sizeof packages, "%s", packages_folder);
    snprintf(user_agent, sizeof user_agent, "EditionsBuilder/%s (PS3)", version);
    unsigned numbers[3];
    if (!version_numbers(version, numbers)) {
        printf("update: not checked: %s is not a release\n", version);
        return;
    }
    lock = lock_new();
    checker = lock != NULL ? thread_start(check, NULL) : NULL;
}

void update_close(void)
{
    if (lock == NULL) {
        return;
    }
    update_cancel();
    lock_take(lock);
    if (active != NULL) {
        active->cancel = true;      /* the check, if it is still asking */
    }
    lock_release(lock);
    if (checker != NULL) {
        thread_join(checker);
    }
    if (downloader != NULL) {
        thread_join(downloader);
    }
    net_close();
    lock_free(lock);
    lock = NULL;
}

void update_get(Update *update)
{
    if (lock == NULL) {
        memset(update, 0, sizeof *update);
        return;
    }
    lock_take(lock);
    *update = current;
    if (current.state == UPDATE_DOWNLOADING && active != NULL) {
        uint64_t expected = package_size != 0 ? package_size : active->expected;
        update->fraction = expected != 0 ? (double)active->received / (double)expected : 0.0;
    }
    lock_release(lock);
}

void update_download(void)
{
    if (lock == NULL) {
        return;
    }
    lock_take(lock);
    bool start = current.state == UPDATE_AVAILABLE || current.state == UPDATE_FAILED;
    if (start) {
        current.state = UPDATE_DOWNLOADING;
        current.failure[0] = 0;
    }
    lock_release(lock);
    if (!start) {
        return;
    }
    /* a download before this one has ended (its state said so): its thread is only waited for */
    if (downloader != NULL) {
        thread_join(downloader);
    }
    downloader = thread_start(download, NULL);
    if (downloader == NULL) {
        lock_take(lock);
        current.state = UPDATE_FAILED;
        snprintf(current.failure, sizeof current.failure, "the download could not be started");
        lock_release(lock);
    }
}

void update_cancel(void)
{
    if (lock == NULL) {
        return;
    }
    lock_take(lock);
    if (current.state == UPDATE_DOWNLOADING && active != NULL) {
        active->cancel = true;
    }
    lock_release(lock);
}
