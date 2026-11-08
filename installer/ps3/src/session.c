#include "session.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disc.h"
#include "files.h"
#include "thread.h"
#include "words.h"

/* A and B compared without regard to ASCII case. */
static int compare_letters(const char *a, const char *b)
{
    while (*a != 0 && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void copy(char *into, size_t capacity, const char *from)
{
    snprintf(into, capacity, "%s", from != NULL ? from : "");
}

/* A new command id: "<KIND><n>". */
static void new_id(Session *session, const char *kind, char *id, size_t capacity)
{
    snprintf(id, capacity, "%s%d", kind, ++session->next_id);
}

static bool is_id(const Json *event, const char *id)
{
    const Json *value = json_get(event, "id");
    return id[0] != 0 && value != NULL && value->type == JSON_STRING && strcmp(value->string, id) == 0;
}

static void take_edition(Session *session, const Json *editions)
{
    if (editions == NULL || editions->type != JSON_ARRAY || editions->count == 0) {
        copy(session->problem, sizeof session->problem, "The installer has no edition to build.");
        return;
    }
    const Json *edition = &editions->items[0];
    copy(session->edition_id, sizeof session->edition_id, json_get_string(edition, "id"));
    copy(session->edition_name, sizeof session->edition_name, json_get_string(edition, "name"));
    copy(session->game, sizeof session->game, json_get_string(edition, "game"));
    copy(session->version, sizeof session->version, json_get_string(edition, "version"));
    const Json *discs = json_get(edition, "discs");
    session->disc_count = 0;
    for (size_t i = 0; discs != NULL && i < discs->count && session->disc_count < 2; i++) {
        EditionDisc *disc = &session->discs[session->disc_count++];
        copy(disc->key, sizeof disc->key, discs->keys[i]);
        copy(disc->name, sizeof disc->name, json_get_string(&discs->items[i], "name"));
        copy(disc->region, sizeof disc->region, json_get_string(&discs->items[i], "region"));
        const Json *required = json_get(&discs->items[i], "required");
        disc->required = required != NULL && required->type == JSON_TRUE;
        disc->image = -1;
    }
    const Json *options = json_get(edition, "options");
    const Json *economies = json_get(options, "economy");
    session->economy_count = 0;
    for (size_t i = 0; economies != NULL && i < economies->count && session->economy_count < 4; i++) {
        copy(session->economies[session->economy_count++], sizeof session->economies[0], economies->items[i].string);
    }
    session->economy = 0;
    const Json *switches = json_get(options, "switches");
    session->switch_count = 0;
    for (size_t i = 0; switches != NULL && i < switches->count && session->switch_count < MAX_SWITCHES; i++) {
        Switch *entry = &session->switches[session->switch_count++];
        copy(entry->id, sizeof entry->id, json_get_string(&switches->items[i], "id"));
        copy(entry->needs, sizeof entry->needs, json_get_string(&switches->items[i], "needs"));
        entry->on = true;
    }
    session->have_edition = true;
    snprintf(session->output, sizeof session->output, "%s/%s %s.iso", session->places.discs, session->game,
             session->edition_name);
    FileInfo info;
    session->output_exists = file_info(session->output, &info);
    session_rescan(session);
}

static int disc_index(const Session *session, const char *key)
{
    for (int i = 0; i < session->disc_count; i++) {
        if (strcmp(session->discs[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

void session_disc_label(const Session *session, int disc, char *label, size_t capacity)
{
    snprintf(label, capacity, "%s (%s)", session->discs[disc].name, session->discs[disc].region);
}

/* Each edition disc without an image takes the first image found to be it. */
static void choose_discs(Session *session)
{
    for (int d = 0; d < session->disc_count; d++) {
        EditionDisc *disc = &session->discs[d];
        if (disc->image >= 0 && disc->image < session->image_count && session->images[disc->image].state == IMAGE_DISC) {
            continue;
        }
        disc->image = -1;
        for (int i = 0; i < session->image_count; i++) {
            if (session->images[i].state == IMAGE_DISC && strcmp(session->images[i].disc, disc->key) == 0) {
                disc->image = i;
                break;
            }
        }
    }
}

/* Why an identified image is not one the edition takes, in the desktop builder's words. */
static void refusal(const Session *session, const Json *found, char *message, size_t capacity)
{
    const char *game = json_get_string(found, "game");
    const char *region = json_get_string(found, "region");
    int wanted = game != NULL ? disc_index(session, game) : -1;
    if (wanted >= 0 && region != NULL) {
        const EditionDisc *disc = &session->discs[wanted];
        snprintf(message, capacity, "This is the %s version. The %s %s the %s version of %s.", region_name(region),
                 session->edition_name, disc->required ? "is built from" : "uses", region_name(disc->region),
                 disc->name);
        return;
    }
    snprintf(message, capacity, "This isn't a disc the %s uses.", session->edition_name);
}

static void take_identify(Session *session, const Json *event, bool failed)
{
    if (session->checking < 0) {
        return;
    }
    Image *image = &session->images[session->checking];
    if (failed) {
        image->state = IMAGE_REFUSED;
        copy(image->message, sizeof image->message, error_words(json_get_string(event, "code")));
    } else {
        const Json *found = json_get(event, "value");
        const char *disc = json_get_string(found, "disc");
        const Json *known = json_get(found, "known");
        if (disc != NULL && known != NULL && known->type == JSON_TRUE && disc_index(session, disc) >= 0) {
            image->state = IMAGE_DISC;
            copy(image->disc, sizeof image->disc, disc);
        } else if (json_get_string(found, "game") != NULL) {
            image->state = IMAGE_REFUSED;
            refusal(session, found, image->message, sizeof image->message);
        } else {
            image->state = IMAGE_OTHER;
        }
    }
    session->checking = -1;
    session->identify_id[0] = 0;
    choose_discs(session);
    session->planned = false;
}

static void take_plan(Session *session, const Json *event, bool failed)
{
    session->plan_id[0] = 0;
    session->planned = true;
    session->plan_problem[0] = 0;
    session->left_out_count = 0;
    if (failed) {
        copy(session->plan_problem, sizeof session->plan_problem, error_words(json_get_string(event, "code")));
        return;
    }
    const Json *value = json_get(event, "value");
    session->bytes_needed = (uint64_t)json_get_integer(value, "bytes_needed", 0);
    const Json *left_out = json_get(value, "left_out");
    for (size_t i = 0; left_out != NULL && i < left_out->count && session->left_out_count < MAX_SWITCHES; i++) {
        copy(session->left_out[session->left_out_count++], sizeof session->left_out[0],
             json_get_string(&left_out->items[i], "id"));
    }
}

/* The label of a build row: the protocol's stages, in the order a build of a new image sends them. The core writes
   it in one pass: the levels and HelpDesk lines are made ready first, then patched as the writing reaches them,
   and the image is hashed as it is written, so there is no check of it after. */
static const char *row_label(const char *stage)
{
    if (strcmp(stage, "verify") == 0) {
        return "Check the discs";
    }
    if (strcmp(stage, "convert") == 0) {
        return "Fill in the edition's code";
    }
    if (strcmp(stage, "patch") == 0) {
        return "Prepare the levels and HelpDesk";
    }
    if (strcmp(stage, "write") == 0) {
        return "Write the new image";
    }
    return stage;
}

static void take_build_progress(Session *session, const Json *event)
{
    const char *stage = json_get_string(event, "stage");
    if (stage == NULL) {
        return;
    }
    BuildRow *row = session->row_count > 0 ? &session->rows[session->row_count - 1] : NULL;
    if (row == NULL || strcmp(row->stage, stage) != 0) {
        if (session->row_count == MAX_ROWS) {
            return;
        }
        row = &session->rows[session->row_count];
        memset(row, 0, sizeof *row);
        copy(row->stage, sizeof row->stage, stage);
        copy(row->label, sizeof row->label, row_label(stage));
        session->row_count++;
    }
    row->done = (uint64_t)json_get_integer(event, "done", 0);
    row->total = (uint64_t)json_get_integer(event, "total", 0);
}

/* The extensions of a cover beside a disc image, in the order webMAN MOD looks for them
   (include/scan/games_covers.h): the image's name with ".iso" swapped for one of these. */
static const char *const COVER_EXTENSIONS[] = {".JPG", ".PNG", ".jpg", ".png"};
#define COVER_EXTENSION_COUNT (sizeof COVER_EXTENSIONS / sizeof COVER_EXTENSIONS[0])

/* The cover beside IMAGE, as webMAN MOD would find it, into COVER; false when there is none. */
static bool find_cover(const char *image, char *cover, size_t capacity)
{
    size_t stem = strlen(image) - 4;
    for (size_t i = 0; i < COVER_EXTENSION_COUNT; i++) {
        snprintf(cover, capacity, "%.*s%s", (int)stem, image, COVER_EXTENSIONS[i]);
        FileInfo info;
        if (file_info(cover, &info)) {
            return true;
        }
    }
    return false;
}

static bool copy_file(const char *from, const char *to)
{
    FILE *input = file_open(from, "rb");
    if (input == NULL) {
        return false;
    }
    FILE *output = file_open(to, "wbx");
    if (output == NULL) {
        fclose(input);
        return false;
    }
    static unsigned char buffer[64 * 1024];
    bool good = true;
    size_t got;
    while (good && (got = fread(buffer, 1, sizeof buffer, input)) > 0) {
        good = fwrite(buffer, 1, got, output) == got;
    }
    good = good && !ferror(input);
    fclose(input);
    good = fclose(output) == 0 && good;
    if (!good) {
        file_remove(to);
    }
    return good;
}

/* Gives the new image the cover of the edition's first disc, so webMAN MOD shows it with the same picture: the
   cover's extension is kept. A cover the new image already has is left as it is. */
static void copy_cover(Session *session)
{
    session->cover_copied = false;
    int image = session->discs[0].image;
    char cover[sizeof session->output + 8], existing[sizeof session->output + 8];
    if (image < 0 || !find_cover(session->images[image].path, cover, sizeof cover) ||
        find_cover(session->output, existing, sizeof existing)) {
        return;
    }
    snprintf(existing, sizeof existing, "%.*s%s", (int)(strlen(session->output) - 4), session->output,
             cover + strlen(cover) - 4);
    session->cover_copied = copy_file(cover, existing);
    printf("session: cover %s -> %s: %s\n", cover, existing, session->cover_copied ? "copied" : "not copied");
}

static void take_build_answer(Session *session, const Json *event, bool failed)
{
    session->build_id[0] = 0;
    if (!failed) {
        session->build = BUILD_DONE;
        copy(session->sha256, sizeof session->sha256, json_get_string(json_get(event, "value"), "sha256"));
        session->output_exists = true;
        copy_cover(session);
        return;
    }
    const char *code = json_get_string(event, "code");
    session->build = code != NULL && strcmp(code, "cancelled") == 0 ? BUILD_CANCELLED : BUILD_FAILED;
    copy(session->failure, sizeof session->failure, error_words(code));
    copy(session->last_log, sizeof session->last_log, json_get_string(event, "message"));
}

static void take_event(Session *session, const Json *event)
{
    const char *kind = json_get_string(event, "event");
    if (kind == NULL) {
        return;
    }
    bool result = strcmp(kind, "result") == 0, failed = strcmp(kind, "error") == 0;
    if (is_id(event, "editions")) {
        if (result) {
            take_edition(session, json_get(event, "value"));
        } else if (failed) {
            copy(session->problem, sizeof session->problem, json_get_string(event, "message"));
        }
    } else if (is_id(event, session->identify_id)) {
        if (strcmp(kind, "progress") == 0 && session->checking >= 0) {
            int64_t total = json_get_integer(event, "total", 0);
            session->images[session->checking].progress =
                total > 0 ? (double)json_get_integer(event, "done", 0) / (double)total : 0;
        } else if (result || failed) {
            take_identify(session, event, failed);
        }
    } else if (is_id(event, session->plan_id)) {
        if (result || failed) {
            take_plan(session, event, failed);
        }
    } else if (is_id(event, session->build_id)) {
        if (strcmp(kind, "progress") == 0) {
            take_build_progress(session, event);
        } else if (strcmp(kind, "log") == 0) {
            copy(session->last_log, sizeof session->last_log, json_get_string(event, "text"));
        } else if (result || failed) {
            take_build_answer(session, event, failed);
        }
    }
}

void session_open(Session *session, Client *client, const Places *places)
{
    memset(session, 0, sizeof *session);
    session->client = client;
    session->places = *places;
    session->checking = -1;
    client_send(client, "{\"id\": \"editions\", \"cmd\": \"editions\"}");
}

static void scan_take(Session *session);

/* Sends the next waiting image to be identified, one at a time: the discs share one drive. */
static void check_next(Session *session)
{
    if (session->checking >= 0 || !session->have_edition || session->build == BUILD_RUNNING) {
        return;
    }
    for (int i = 0; i < session->image_count; i++) {
        Image *image = &session->images[i];
        if (image->state != IMAGE_WAITING) {
            continue;
        }
        image->state = IMAGE_CHECKING;
        image->progress = 0;
        session->checking = i;
        new_id(session, "identify", session->identify_id, sizeof session->identify_id);
        Text command = {0};
        text_format(&command, "{\"id\": \"%s\", \"cmd\": \"identify\", \"path\": ", session->identify_id);
        text_add_json_string(&command, image->path);
        text_add(&command, "}");
        if (command.text != NULL) {
            client_send(session->client, command.text);
        }
        text_free(&command);
        return;
    }
}

void session_update(Session *session)
{
    Json *event;
    while ((event = client_next_event(session->client)) != NULL) {
        take_event(session, event);
        json_free(event);
    }
    scan_take(session);
    check_next(session);
}

static int by_name(const void *a, const void *b)
{
    return compare_letters(((const Image *)a)->name, ((const Image *)b)->name);
}

/* What was learned of each image before a rescan, kept by path. */
static Image previous[MAX_IMAGES];
static int previous_count;

/* One entry of the discs folder: an image is listed, and looked at unless it was before. */
static void found_entry(void *context, const char *name)
{
    Session *session = context;
    size_t length = strlen(name);
    if (session->image_count == MAX_IMAGES || length < 5 || compare_letters(name + length - 4, ".iso") != 0) {
        return;
    }
    Image *image = &session->images[session->image_count];
    memset(image, 0, sizeof *image);
    snprintf(image->path, sizeof image->path, "%s/%s", session->places.discs, name);
    copy(image->name, sizeof image->name, name);
    if (strcmp(image->path, session->output) == 0) {
        return;                 /* the edition, built before */
    }
    for (int i = 0; i < previous_count; i++) {
        if (strcmp(previous[i].path, image->path) == 0 && previous[i].state != IMAGE_CHECKING) {
            *image = previous[i];
            session->image_count++;
            return;
        }
    }
    image->state = IMAGE_UNSEEN;
    session->image_count++;
}

/*
 * The scan: each new image's boot file read (disc_kind), on a thread of its own. Only an image whose boot file the
 * edition knows can be one of its discs; the rest are not hashed. On a PS3 with a library of games, reading them
 * all took the screen's thread six seconds; now the screen goes on, and the images join as they are read.
 */
typedef struct Scan {
    Client *client;
    Lock *lock;
    int count;
    char (*paths)[600];
    bool *candidate;
    int done;                   /* how many are read, under LOCK */
    int taken;                  /* how many of those the images have */
    Thread *thread;
} Scan;

static void scan_images(void *argument)
{
    Scan *scan = argument;
    for (int i = 0; i < scan->count; i++) {
        DiscKind kind;
        bool candidate = disc_kind(scan->paths[i], &kind) && kind.is_ps2_disc &&
                         client_knows_boot_file(scan->client, kind.boot_file);
        lock_take(scan->lock);
        scan->candidate[i] = candidate;
        scan->done = i + 1;
        lock_release(scan->lock);
    }
}

static void scan_free(Scan *scan)
{
    if (scan != NULL) {
        lock_free(scan->lock);
        free(scan->paths);
        free(scan->candidate);
        free(scan);
    }
}

/* The scan of the images not seen yet, started; when it cannot be, they are read here and now. */
static void scan_start(Session *session)
{
    Scan *scan = calloc(1, sizeof *scan);
    int count = 0;
    for (int i = 0; i < session->image_count; i++) {
        count += session->images[i].state == IMAGE_UNSEEN;
    }
    if (scan != NULL && count > 0) {
        scan->client = session->client;
        scan->lock = lock_new();
        scan->paths = malloc((size_t)count * sizeof *scan->paths);
        scan->candidate = calloc((size_t)count, sizeof *scan->candidate);
        for (int i = 0; scan->paths != NULL && i < session->image_count; i++) {
            if (session->images[i].state == IMAGE_UNSEEN) {
                copy(scan->paths[scan->count++], sizeof scan->paths[0], session->images[i].path);
            }
        }
        scan->thread = scan->lock != NULL && scan->paths != NULL && scan->candidate != NULL
                           ? thread_start(scan_images, scan) : NULL;
    }
    if (scan != NULL && scan->thread != NULL) {
        session->scan = scan;
        return;
    }
    scan_free(scan);
    for (int i = 0; i < session->image_count; i++) {
        Image *image = &session->images[i];
        DiscKind kind;
        if (image->state == IMAGE_UNSEEN) {
            image->state = disc_kind(image->path, &kind) && kind.is_ps2_disc &&
                                   client_knows_boot_file(session->client, kind.boot_file)
                               ? IMAGE_WAITING : IMAGE_OTHER;
        }
    }
}

/* What the scan has read so far, taken into the images; the scan ended once it has read them all. */
static void scan_take(Session *session)
{
    Scan *scan = session->scan;
    if (scan == NULL) {
        return;
    }
    lock_take(scan->lock);
    int done = scan->done;
    lock_release(scan->lock);
    for (; scan->taken < done; scan->taken++) {
        for (int i = 0; i < session->image_count; i++) {
            Image *image = &session->images[i];
            if (image->state == IMAGE_UNSEEN && strcmp(image->path, scan->paths[scan->taken]) == 0) {
                image->state = scan->candidate[scan->taken] ? IMAGE_WAITING : IMAGE_OTHER;
                break;
            }
        }
    }
    if (done == scan->count) {
        thread_join(scan->thread);
        scan_free(scan);
        session->scan = NULL;
    }
}

void session_rescan(Session *session)
{
    if (session->checking >= 0 || session->scan != NULL || session->build == BUILD_RUNNING) {
        return;
    }
    previous_count = session->image_count;
    memcpy(previous, session->images, sizeof(Image) * (size_t)previous_count);
    session->image_count = 0;
    platform_list_folder(session->places.discs, found_entry, session);
    qsort(session->images, (size_t)session->image_count, sizeof(Image), by_name);
    for (int d = 0; d < session->disc_count; d++) {
        session->discs[d].image = -1;
    }
    choose_discs(session);
    session->scanned = true;
    session->planned = false;
    scan_start(session);
}

bool session_discs_settled(const Session *session)
{
    if (!session->scanned || session->checking >= 0 || session->scan != NULL) {
        return false;
    }
    for (int i = 0; i < session->image_count; i++) {
        if (session->images[i].state == IMAGE_WAITING || session->images[i].state == IMAGE_UNSEEN) {
            return false;
        }
    }
    return true;
}

bool session_switch_available(const Session *session, int i)
{
    const Switch *entry = &session->switches[i];
    if (entry->needs[0] == 0) {
        return true;
    }
    int disc = disc_index(session, entry->needs);
    return disc >= 0 && session->discs[disc].image >= 0;
}

/* The command fields a plan and a build share: the discs, the settings and the new image. */
static void add_build_fields(const Session *session, Text *command)
{
    text_add(command, ", \"discs\": {");
    bool first = true;
    for (int d = 0; d < session->disc_count; d++) {
        if (session->discs[d].image < 0) {
            continue;
        }
        text_add(command, first ? "" : ", ");
        text_add_json_string(command, session->discs[d].key);
        text_add(command, ": ");
        text_add_json_string(command, session->images[session->discs[d].image].path);
        first = false;
    }
    text_add(command, "}, \"options\": {\"economy\": ");
    text_add_json_string(command, session->economy_count > 0 ? session->economies[session->economy] : NULL);
    text_add(command, ", \"without\": [");
    first = true;
    for (int i = 0; i < session->switch_count; i++) {
        if (!session->switches[i].on && session_switch_available(session, i)) {
            text_add(command, first ? "" : ", ");
            text_add_json_string(command, session->switches[i].id);
            first = false;
        }
    }
    text_add(command, "]}, \"output\": {\"mode\": \"new\", \"path\": ");
    text_add_json_string(command, session->output);
    text_add(command, "}}");
}

void session_plan(Session *session)
{
    if (session->plan_id[0] != 0) {
        return;
    }
    session->planned = false;
    new_id(session, "plan", session->plan_id, sizeof session->plan_id);
    Text command = {0};
    text_format(&command, "{\"id\": \"%s\", \"cmd\": \"plan\"", session->plan_id);
    add_build_fields(session, &command);
    if (command.text != NULL) {
        client_send(session->client, command.text);
    }
    text_free(&command);
}

void session_build(Session *session)
{
    if (session->build == BUILD_RUNNING) {
        return;
    }
    session->build = BUILD_RUNNING;
    session->row_count = 0;
    session->last_log[0] = session->sha256[0] = session->failure[0] = 0;
    session->cancel_sent = false;
    new_id(session, "build", session->build_id, sizeof session->build_id);
    Text command = {0};
    text_format(&command, "{\"id\": \"%s\", \"cmd\": \"build\"", session->build_id);
    add_build_fields(session, &command);
    if (command.text != NULL) {
        client_send(session->client, command.text);
    }
    text_free(&command);
}

void session_cancel(Session *session)
{
    if (session->build != BUILD_RUNNING || session->cancel_sent) {
        return;
    }
    char command[96];
    snprintf(command, sizeof command, "{\"id\": \"cancel\", \"cmd\": \"cancel\", \"of\": \"%s\"}", session->build_id);
    client_send(session->client, command);
    session->cancel_sent = true;
}

double session_build_fraction(const Session *session)
{
    /* how much of a build each stage takes, roughly (the desktop builder's weights) */
    double done = 0, all = 0;
    static const struct {
        const char *label;
        double weight;
    } WEIGHTS[] = {{"Check the discs", 5}, {"Fill in the edition's code", 3},
                   {"Prepare the levels and HelpDesk", 8}, {"Write the new image", 84}};
    for (size_t w = 0; w < sizeof WEIGHTS / sizeof WEIGHTS[0]; w++) {
        all += WEIGHTS[w].weight;
        for (int i = 0; i < session->row_count; i++) {
            const BuildRow *row = &session->rows[i];
            if (strcmp(row->label, WEIGHTS[w].label) == 0) {
                bool finished = i < session->row_count - 1 || session->build == BUILD_DONE;
                double part = finished ? 1 : (row->total > 0 ? (double)row->done / (double)row->total : 0);
                done += WEIGHTS[w].weight * part;
            }
        }
    }
    return all > 0 ? done / all : 0;
}
