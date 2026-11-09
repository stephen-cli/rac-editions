/*
 * The PS3's network: PSL1GHT's net library under the toolchain's libcurl, which speaks TLS through mbedTLS. mbedTLS
 * has no store of its own, so the roots it checks a server against are a PEM file beside the app (Mozilla's, as
 * curl publishes them: data/cacert.pem).
 */
#include "net.h"

#include <stdio.h>
#include <string.h>

#include <curl/curl.h>
#include <net/net.h>

static char certificates_path[512];
static bool opened;

bool net_open(const char *certificates, char *error, size_t capacity)
{
    if (opened) {
        return true;
    }
    s32 status = netInitialize();
    if (status < 0) {
        snprintf(error, capacity, "the network library could not start (%08x)", (unsigned)status);
        return false;
    }
    CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
    if (code != CURLE_OK) {
        netDeinitialize();
        snprintf(error, capacity, "libcurl could not start: %s", curl_easy_strerror(code));
        return false;
    }
    snprintf(certificates_path, sizeof certificates_path, "%s", certificates);
    opened = true;
    return true;
}

void net_close(void)
{
    if (opened) {
        curl_global_cleanup();
        netDeinitialize();
        opened = false;
    }
}

static size_t body(char *data, size_t size, size_t count, void *argument)
{
    NetRequest *request = argument;
    size_t length = size * count;
    return request->write(request->context, data, length) ? length : 0;
}

static int progress(void *argument, curl_off_t expected, curl_off_t received, curl_off_t sent_expected,
                    curl_off_t sent)
{
    (void)sent_expected;
    (void)sent;
    NetRequest *request = argument;
    request->expected = expected > 0 ? (uint64_t)expected : 0;
    request->received = received > 0 ? (uint64_t)received : 0;
    return request->cancel ? 1 : 0;
}

bool net_get(NetRequest *request, char *error, size_t capacity)
{
    if (!opened) {
        snprintf(error, capacity, "the network is not started");
        return false;
    }
    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        snprintf(error, capacity, "libcurl could not make a request");
        return false;
    }
    char curl_error[CURL_ERROR_SIZE] = "";
    struct curl_slist *headers = curl_slist_append(NULL, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_URL, request->url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, request->user_agent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    /* an HTTP status of 400 or more fails the request before its body is written */
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, certificates_path);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    /* a transfer that stalls (under 100 bytes a second for 30 seconds) is given up */
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, request);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, request);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
    CURLcode code = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK) {
        snprintf(error, capacity, "%s", curl_error[0] != 0 ? curl_error : curl_easy_strerror(code));
        return false;
    }
    return true;
}
