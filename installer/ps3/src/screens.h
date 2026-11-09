/*
 * The builder's screens, as the desktop builder's steps: the edition, the discs, review and build (with the edition
 * settings in it), the build, and the image ready; and a newer builder's download (update.h). Each draws itself and
 * takes the controller's presses.
 */
#ifndef RAC_PS3_SCREENS_H
#define RAC_PS3_SCREENS_H

#include <stdbool.h>

#include "session.h"

typedef enum { SCREEN_EDITION, SCREEN_DISCS, SCREEN_REVIEW, SCREEN_BUILD, SCREEN_DONE, SCREEN_UPDATE } ScreenId;

typedef struct {
    ScreenId screen;
    int selected;               /* the row the controller is on, in the review */
    unsigned cancel_asked;      /* when Circle was first pressed to cancel the build (SDL_GetTicks), or 0 */
    unsigned frame;
    bool quit;                  /* the player chose to leave */
} Screens;

void screens_open(Screens *screens);

/* One frame: PRESSED holds the buttons pressed since the last frame (PAD_*). */
void screens_frame(Screens *screens, Session *session, unsigned pressed);

/* The state for the preview's scripts: "edition", "discs:checking", "discs:ready", "review:planned",
   "build:running", "build:done", "build:failed", "update:downloading", "update:downloaded", "update:failed",
   "update:stopped", "problem". */
const char *screens_state(const Screens *screens, const Session *session);

#endif
