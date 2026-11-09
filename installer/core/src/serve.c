#include "serve.h"

#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "json.h"
#include "thread.h"

/* At most this many identifies and builds at once; the builder runs one build and a disc or two. */
#define MAX_RUNNING 16

/* A command's job: its events carry the command's id, written back exactly as the builder sent it. */
typedef struct {
    Job job;
    Server *server;
    char *id;                   /* the command's id as JSON text: "\"7\"" or "7" */
    Json *command;
} ServeJob;

struct Server {
    const Payload *payload;
    HashCache *cache;
    ServerSend *send;
    void *send_context;
    Lock *output_lock;          /* one event per line, whole, whichever thread sends it */
    Lock *running_lock;
    ServeJob *running[MAX_RUNNING];
};

static void send_line(Server *server, const Text *line)
{
    if (line->failed || line->text == NULL) {
        return;
    }
    lock_take(server->output_lock);
    server->send(server->send_context, line->text, line->length);
    lock_release(server->output_lock);
}

static void send_error(Server *server, const char *id, const char *code, const char *message)
{
    Text line = {0};
    text_add(&line, "{\"event\": \"error\", \"id\": ");
    text_add(&line, id != NULL ? id : "null");
    text_add(&line, ", \"code\": ");
    text_add_json_string(&line, code);
    text_add(&line, ", \"message\": ");
    text_add_json_string(&line, message);
    text_add(&line, "}");
    send_line(server, &line);
    text_free(&line);
}

static void send_result(Server *server, const char *id, const Text *value)
{
    Text line = {0};
    text_add(&line, "{\"event\": \"result\", \"id\": ");
    text_add(&line, id);
    text_add(&line, ", \"value\": ");
    text_append(&line, value->text != NULL ? value->text : "null", value->text != NULL ? value->length : 4);
    text_add(&line, "}");
    send_line(server, &line);
    text_free(&line);
}

static void job_sends_progress(Job *job, const char *stage, uint64_t done, uint64_t total)
{
    ServeJob *serve_job = job->context;
    Text line = {0};
    text_add(&line, "{\"event\": \"progress\", \"id\": ");
    text_add(&line, serve_job->id);
    text_add(&line, ", \"stage\": ");
    text_add_json_string(&line, stage);
    text_format(&line, ", \"done\": %llu, \"total\": %llu}", (unsigned long long)done, (unsigned long long)total);
    send_line(serve_job->server, &line);
    text_free(&line);
}

static void job_sends_log(Job *job, const char *text)
{
    ServeJob *serve_job = job->context;
    Text line = {0};
    text_add(&line, "{\"event\": \"log\", \"id\": ");
    text_add(&line, serve_job->id);
    text_add(&line, ", \"text\": ");
    text_add_json_string(&line, text);
    text_add(&line, "}");
    send_line(serve_job->server, &line);
    text_free(&line);
}

static ServeJob *new_job(Server *server, const Json *id, Json *command)
{
    ServeJob *serve_job = calloc(1, sizeof *serve_job);
    if (serve_job == NULL) {
        return NULL;
    }
    Text id_text = {0};
    text_add_json(&id_text, id);
    serve_job->id = id_text.text;
    serve_job->server = server;
    serve_job->command = command;
    serve_job->job.progress = job_sends_progress;
    serve_job->job.log = job_sends_log;
    serve_job->job.context = serve_job;
    return serve_job;
}

static void free_job(ServeJob *serve_job)
{
    free(serve_job->id);
    json_free(serve_job->command);
    free(serve_job);
}

static bool add_running(Server *server, ServeJob *serve_job)
{
    bool added = false;
    lock_take(server->running_lock);
    for (int i = 0; i < MAX_RUNNING && !added; i++) {
        if (server->running[i] == NULL) {
            server->running[i] = serve_job;
            added = true;
        }
    }
    lock_release(server->running_lock);
    return added;
}

static void remove_running(Server *server, ServeJob *serve_job)
{
    lock_take(server->running_lock);
    for (int i = 0; i < MAX_RUNNING; i++) {
        if (server->running[i] == serve_job) {
            server->running[i] = NULL;
        }
    }
    lock_release(server->running_lock);
}

static int running_count(Server *server)
{
    int count = 0;
    lock_take(server->running_lock);
    for (int i = 0; i < MAX_RUNNING; i++) {
        count += server->running[i] != NULL;
    }
    lock_release(server->running_lock);
    return count;
}

/* Cancels the running command whose id is OF (as JSON text); whether there was one. */
static bool cancel_running(Server *server, const char *of)
{
    bool found = false;
    lock_take(server->running_lock);
    for (int i = 0; i < MAX_RUNNING; i++) {
        if (server->running[i] != NULL && strcmp(server->running[i]->id, of) == 0) {
            server->running[i]->job.cancelled = 1;
            found = true;
        }
    }
    lock_release(server->running_lock);
    return found;
}

/* Runs SERVE_JOB's command and sends its result or its error. */
static void answer(ServeJob *serve_job)
{
    Server *server = serve_job->server;
    const Json *command = serve_job->command;
    const char *name = json_get_string(command, "cmd");
    Text value = {0};
    InstallError error = {0};
    bool answered = false;
    if (strcmp(name, "editions") == 0) {
        answered = command_editions(server->payload, &value, &error);
    } else if (strcmp(name, "identify") == 0) {
        const char *path = json_get_string(command, "path");
        if (path == NULL) {
            error.code = "internal";
            snprintf(error.message, sizeof error.message, "bad command: identify needs a path");
        } else {
            answered = command_identify(server->payload, server->cache, path, &serve_job->job, &value, &error);
        }
    } else if (strcmp(name, "plan") == 0) {
        answered = command_plan(server->payload, json_get(command, "discs"), json_get(command, "options"),
                                json_get(command, "output"), &value, &error);
    } else if (strcmp(name, "build") == 0) {
        answered = command_build(server->payload, server->cache, json_get(command, "discs"),
                                 json_get(command, "options"), json_get(command, "output"), build_patch, NULL,
                                 &serve_job->job, &value, &error);
    } else {
        error.code = "internal";
        snprintf(error.message, sizeof error.message, "unknown command %s", name);
    }
    if (answered && value.failed) {
        answered = false;
        error.code = "internal";
        snprintf(error.message, sizeof error.message, "out of memory");
    }
    if (answered) {
        send_result(server, serve_job->id, &value);
    } else {
        send_error(server, serve_job->id, error.code, error.message);
    }
    text_free(&value);
}

static void run_in_background(void *argument)
{
    ServeJob *serve_job = argument;
    answer(serve_job);
    remove_running(serve_job->server, serve_job);
    free_job(serve_job);
}

/* One line from INPUT without its line break, in a buffer the caller frees; NULL at the end of the input. */
static char *read_line(FILE *input, size_t *length)
{
    Text line = {0};
    char chunk[4096];
    while (fgets(chunk, sizeof chunk, input) != NULL) {
        size_t got = strlen(chunk);
        text_append(&line, chunk, got);
        if (got > 0 && chunk[got - 1] == '\n') {
            break;
        }
    }
    if (line.text == NULL) {
        return NULL;
    }
    while (line.length > 0 && (line.text[line.length - 1] == '\n' || line.text[line.length - 1] == '\r')) {
        line.text[--line.length] = 0;
    }
    *length = line.length;
    return line.text;
}

static void handle_command(Server *server, const char *line, size_t length)
{
    char parse_error[128];
    Json *command = json_parse(line, length, parse_error, sizeof parse_error);
    const Json *id = json_get(command, "id");
    const char *name = json_get_string(command, "cmd");
    if (command == NULL || id == NULL || name == NULL) {
        char message[256];
        snprintf(message, sizeof message, "bad command: %s", command == NULL ? parse_error : "it needs an id and a cmd");
        send_error(server, NULL, "internal", message);
        json_free(command);
        return;
    }
    ServeJob *serve_job = new_job(server, id, command);
    if (serve_job == NULL || serve_job->id == NULL) {
        send_error(server, NULL, "internal", "out of memory");
        if (serve_job != NULL) {
            free_job(serve_job);
        } else {
            json_free(command);
        }
        return;
    }
    if (strcmp(name, "cancel") == 0) {
        const Json *of = json_get(command, "of");
        Text of_text = {0};
        if (of != NULL) {
            text_add_json(&of_text, of);
        }
        bool cancelling = of_text.text != NULL && cancel_running(server, of_text.text);
        text_free(&of_text);
        Text value = {0};
        text_add(&value, cancelling ? "{\"cancelling\": true}" : "{\"cancelling\": false}");
        send_result(server, serve_job->id, &value);
        text_free(&value);
        free_job(serve_job);
        return;
    }
    if (strcmp(name, "identify") == 0 || strcmp(name, "build") == 0) {
        if (!add_running(server, serve_job)) {
            send_error(server, serve_job->id, "internal", "too many commands running at once");
            free_job(serve_job);
            return;
        }
        if (!thread_start_detached(run_in_background, serve_job)) {
            remove_running(server, serve_job);
            send_error(server, serve_job->id, "internal", "a thread could not be started");
            free_job(serve_job);
        }
        return;
    }
    answer(serve_job);
    free_job(serve_job);
}

Server *server_new(const Payload *payload, HashCache *cache, ServerSend *send, void *context)
{
    Server *server = calloc(1, sizeof *server);
    if (server == NULL) {
        return NULL;
    }
    *server = (Server){.payload = payload, .cache = cache, .send = send, .send_context = context};
    server->output_lock = lock_new();
    server->running_lock = lock_new();
    if (server->output_lock == NULL || server->running_lock == NULL) {
        lock_free(server->output_lock);
        lock_free(server->running_lock);
        free(server);
        return NULL;
    }
    Text ready = {0};
    text_add(&ready, "{\"event\": \"ready\", \"protocol\": ");
    text_format(&ready, "%d, \"installer\": ", PROTOCOL_VERSION);
    text_add_json_string(&ready, payload_version(payload));
    text_add(&ready, "}");
    send_line(server, &ready);
    text_free(&ready);
    return server;
}

void server_command(Server *server, const char *line, size_t length)
{
    size_t start = 0;
    while (start < length && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }
    if (start < length) {
        handle_command(server, line, length);
    }
}

void server_free(Server *server)
{
    if (server == NULL) {
        return;
    }
    while (running_count(server) > 0) {
        thread_sleep(50);
    }
    lock_free(server->output_lock);
    lock_free(server->running_lock);
    free(server);
}

/* An event line to the FILE in CONTEXT, flushed so the builder sees it at once. */
static void send_to_file(void *context, const char *line, size_t length)
{
    FILE *output = context;
    fwrite(line, 1, length, output);
    fputc('\n', output);
    fflush(output);
}

int serve(const Payload *payload, HashCache *cache, FILE *input, FILE *output)
{
    Server *server = server_new(payload, cache, send_to_file, output);
    if (server == NULL) {
        return 1;
    }
    size_t length;
    char *line;
    while ((line = read_line(input, &length)) != NULL) {
        server_command(server, line, length);
        free(line);
    }
    /* the input has ended: the identifies and builds still running answer before the installer exits */
    server_free(server);
    return 0;
}
