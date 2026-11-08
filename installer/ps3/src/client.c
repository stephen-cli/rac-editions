#include "client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hash_cache.h"
#include "installer.h"
#include "serve.h"
#include "thread.h"

typedef struct Event {
    struct Event *next;
    Json *json;
} Event;

struct Client {
    Payload payload;
    HashCache *cache;
    Server *server;
    Lock *lock;
    Event *first, *last;
};

static void receive(void *context, const char *line, size_t length)
{
    Client *client = context;
    char error[128];
    Json *json = json_parse(line, length, error, sizeof error);
    /* into the log: each event but the progress of a long command */
    const char *kind = json_get_string(json, "event");
    if (kind != NULL && strcmp(kind, "progress") != 0) {
        printf("event: %.*s\n", (int)(length < 400 ? length : 400), line);
    }
    Event *event = json != NULL ? calloc(1, sizeof *event) : NULL;
    if (event == NULL) {
        json_free(json);
        return;
    }
    event->json = json;
    lock_take(client->lock);
    if (client->last != NULL) {
        client->last->next = event;
    } else {
        client->first = event;
    }
    client->last = event;
    lock_release(client->lock);
}

Client *client_open(const char *payload_folder, const char *data_folder, char *error, size_t capacity)
{
    Client *client = calloc(1, sizeof *client);
    if (client == NULL) {
        snprintf(error, capacity, "out of memory");
        return NULL;
    }
    InstallError problem = {0};
    if (!payload_open(&client->payload, payload_folder, &problem)) {
        snprintf(error, capacity, "%s", problem.message);
        payload_close(&client->payload);
        free(client);
        return NULL;
    }
    client->cache = hash_cache_open(data_folder);
    client->lock = lock_new();
    client->server = client->lock != NULL ? server_new(&client->payload, client->cache, receive, client) : NULL;
    if (client->server == NULL) {
        snprintf(error, capacity, "out of memory");
        client_close(client);
        return NULL;
    }
    return client;
}

void client_close(Client *client)
{
    if (client == NULL) {
        return;
    }
    server_free(client->server);
    Json *event;
    while (client->lock != NULL && (event = client_next_event(client)) != NULL) {
        json_free(event);
    }
    lock_free(client->lock);
    hash_cache_close(client->cache);
    payload_close(&client->payload);
    free(client);
}

void client_send(Client *client, const char *command)
{
    printf("send: %s\n", command);
    server_command(client->server, command, strlen(command));
    printf("sent\n");
}

Json *client_next_event(Client *client)
{
    lock_take(client->lock);
    Event *event = client->first;
    if (event != NULL) {
        client->first = event->next;
        if (client->first == NULL) {
            client->last = NULL;
        }
    }
    lock_release(client->lock);
    if (event == NULL) {
        return NULL;
    }
    Json *json = event->json;
    free(event);
    return json;
}

bool client_knows_boot_file(Client *client, const char *boot_file)
{
    return json_get(json_get(client->payload.edition, "boot_files"), boot_file) != NULL;
}
