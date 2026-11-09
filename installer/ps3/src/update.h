/*
 * The update check: at start, on a thread of its own, the latest rac-editions release on GitHub is compared with
 * the builder's own version (its payload's, a release's tag). A newer one can be downloaded, its EditionsBuilder.pkg
 * saved in the PS3's packages folder for Package Manager to install; a running app cannot replace itself. A build
 * between releases (its version a commit) does not check, and a check that fails (no network, GitHub unreachable)
 * says nothing: the builder works offline as before.
 */
#ifndef RAC_PS3_UPDATE_H
#define RAC_PS3_UPDATE_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    UPDATE_NONE,            /* not checked, checking, failed to check, or this is the latest */
    UPDATE_AVAILABLE,       /* a newer release: `latest` names it */
    UPDATE_DOWNLOADING,     /* `fraction` of it so far */
    UPDATE_DOWNLOADED,      /* saved as `saved` */
    UPDATE_FAILED,          /* the download failed: `failure` says why; it can be tried again */
} UpdateState;

typedef struct {
    UpdateState state;
    char latest[32];        /* "v0.3.0" */
    double fraction;
    char saved[600];
    char failure[300];
} Update;

/* Starts the check for the builder at VERSION ("v0.2.0"), with the root certificates in CERTIFICATES; a download
   is saved in PACKAGES. */
void update_start(const char *version, const char *certificates, const char *packages);

/* Ends the check and any download (a part-saved package is deleted), waiting for them. */
void update_close(void);

/* What is known now. */
void update_get(Update *update);

/* Downloads the newer release's package, when there is one (UPDATE_AVAILABLE or UPDATE_FAILED). */
void update_download(void);

/* Stops a download; the state goes back to UPDATE_AVAILABLE. */
void update_cancel(void);

#endif
