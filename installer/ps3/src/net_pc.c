/*
 * The preview's network: none. The update check (update.h) finds no network, and the builder carries on as it does
 * on a PS3 offline.
 */
#include "net.h"

#include <stdio.h>

bool net_open(const char *certificates, char *error, size_t capacity)
{
    (void)certificates;
    snprintf(error, capacity, "the preview has no network");
    return false;
}

void net_close(void)
{
}

bool net_get(NetRequest *request, char *error, size_t capacity)
{
    (void)request;
    snprintf(error, capacity, "the preview has no network");
    return false;
}
