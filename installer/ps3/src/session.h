/*
 * The builder's state: the edition, the disc images found in the discs folder and what each is, the settings, the
 * plan for the build, and the build. It sends the core its commands and takes in their events, once a frame.
 */
#ifndef RAC_PS3_SESSION_H
#define RAC_PS3_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "client.h"
#include "platform.h"

#define MAX_IMAGES 200
#define MAX_SWITCHES 8
#define MAX_ROWS 8

typedef enum {
    IMAGE_UNSEEN,       /* its boot file not read yet (the scan, on a thread of its own) */
    IMAGE_WAITING,      /* to be checked */
    IMAGE_CHECKING,
    IMAGE_DISC,         /* one of the edition's discs: `disc` says which */
    IMAGE_REFUSED,      /* the edition's game, but not a version or a copy it can use: `message` says why */
    IMAGE_OTHER,        /* another game, or no PS2 disc: not looked at further */
} ImageState;

typedef struct {
    char path[600];
    char name[256];             /* the file name */
    ImageState state;
    double progress;            /* while checking, 0 to 1 */
    char disc[8];               /* "rac1" or "rac2" */
    char message[300];
} Image;

/* One of the edition's discs, as the `editions` answer names it. */
typedef struct {
    char key[8];                /* "rac1" */
    char name[48];              /* "Ratchet & Clank" */
    char region[8];             /* "PAL" */
    bool required;
    int image;                  /* the image chosen for it, or -1 */
} EditionDisc;

typedef struct {
    char id[32];                /* "charge_boots" */
    char needs[8];              /* the disc it needs, or "" */
    bool on;
} Switch;

/* A row of the build's progress: a stage of the protocol, as it came. */
typedef struct {
    char stage[16];
    char label[48];
    uint64_t done, total;
} BuildRow;

typedef enum { BUILD_IDLE, BUILD_RUNNING, BUILD_DONE, BUILD_FAILED, BUILD_CANCELLED } BuildState;

typedef struct {
    Client *client;
    Places places;
    int next_id;
    char problem[300];          /* why the builder cannot go on at all */

    /* the edition */
    bool have_edition;
    char edition_id[32], edition_name[48], game[48], version[48];
    EditionDisc discs[2];
    int disc_count;
    char economies[4][16];
    int economy_count, economy;
    Switch switches[MAX_SWITCHES];
    int switch_count;

    /* the images in the discs folder */
    Image images[MAX_IMAGES];
    int image_count;
    bool scanned;
    struct Scan *scan;          /* the images' boot files being read, or NULL */
    int checking;               /* the image being identified, or -1 */
    char identify_id[16];

    /* the plan, for the current discs and settings */
    char output[700];
    bool output_exists;
    char plan_id[16];
    bool planned;
    uint64_t bytes_needed;
    char plan_problem[300];
    char left_out[MAX_SWITCHES][32];
    int left_out_count;

    /* the build */
    BuildState build;
    char build_id[16];
    BuildRow rows[MAX_ROWS];
    int row_count;
    char last_log[300];
    char sha256[65];
    bool cover_copied;          /* the first disc's cover was copied beside the new image */
    char failure[300];
    bool cancel_sent;
} Session;

/* Starts the session: asks for the edition and looks in the discs folder. */
void session_open(Session *session, Client *client, const Places *places);

/* Takes in the core's events and starts the next disc check; once a frame. */
void session_update(Session *session);

/* Looks in the discs folder again, keeping what is known of images already checked. */
void session_rescan(Session *session);

/* Whether every image that could be one of the edition's discs has been checked. */
bool session_discs_settled(const Session *session);

/* Asks the core for the plan, for the discs and settings as they are. */
void session_plan(Session *session);

void session_build(Session *session);
void session_cancel(Session *session);

/* The build's progress so far, 0 to 1, from its rows. */
double session_build_fraction(const Session *session);

/* The edition's name for disc KEY in its region: "Ratchet & Clank (PAL)". */
void session_disc_label(const Session *session, int disc, char *label, size_t capacity);

/* Whether switch I can be on: the disc it needs, if any, is there. */
bool session_switch_available(const Session *session, int i);

#endif
