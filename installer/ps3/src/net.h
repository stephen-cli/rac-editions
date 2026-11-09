/*
 * HTTPS requests, for the update check (update.h). net_ps3.c is the PS3's, on the toolchain's libcurl with mbedTLS
 * and the certificates beside the app; net_pc.c is the preview's, which has no network.
 */
#ifndef RAC_PS3_NET_H
#define RAC_PS3_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *url;
    const char *user_agent;
    /* each piece of the answer's body, as it comes; false stops the request */
    bool (*write)(void *context, const void *data, size_t size);
    void *context;
    /* the body's bytes so far and in all (0 until known), for a progress bar */
    volatile uint64_t received, expected;
    /* set from another thread to stop the request */
    volatile bool cancel;
} NetRequest;

/* Starts the network, with the root certificates in CERTIFICATES (a PEM file); false with ERROR filled when it
   cannot. */
bool net_open(const char *certificates, char *error, size_t capacity);
void net_close(void);

/* REQUEST's URL fetched with GET, redirects followed; false with ERROR filled when it failed, was refused (an HTTP
   status of 400 or more) or was stopped. */
bool net_get(NetRequest *request, char *error, size_t capacity);

#endif
