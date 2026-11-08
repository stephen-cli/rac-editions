/*
 * The Editions Builder for the PS3: builds an edition from the player's disc images in /dev_hdd0/PS2ISO with the
 * installer core, in-process, behind screens for the controller. The same code runs as a preview on a computer
 * (platform_pc.c).
 */
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "client.h"
#include "platform.h"
#include "screens.h"
#include "script.h"
#include "session.h"
#include "thread.h"
#include "ui.h"

static Screens screens;
static Session session;

const char *preview_state(void)
{
    return screens_state(&screens, &session);
}

/* Shown when the builder cannot even start: on the PS3, the TTY is the only place left to say why. */
static int fail(const char *what, const char *why)
{
    fprintf(stderr, "editions builder: %s: %s\n", what, why);
    return 1;
}

int main(int argc, char **argv)
{
    Places places;
    if (!platform_open(argc, argv, &places)) {
        return fail("start", "the controller or the folders");
    }
    /* SDL for its clock, and for SDL_ttf; the drawing is the graphics chip's (draw.h) */
    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        return fail("start", SDL_GetError());
    }
    char error[300];
    int width, height;
    if (!draw_open(&width, &height, error, sizeof error)) {
        return fail("video", error);
    }
    printf("display: %dx%d\n", width, height);
    if (!ui_open(width, height, places.fonts, error, sizeof error)) {
        return fail("fonts", error);
    }
    Client *client = client_open(places.payload, places.data, error, sizeof error);
    screens_open(&screens);
    if (client != NULL) {
        session_open(&session, client, &places);
    } else {
        memset(&session, 0, sizeof session);
        snprintf(session.problem, sizeof session.problem,
                 "The edition's files beside the builder could not be read (%s). Install the builder again.", error);
    }

    unsigned held = 0;
    Uint32 timing_since = SDL_GetTicks(), frame_started = timing_since, slowest = 0;
    unsigned timed_frames = 0;
    while (!platform_quitting() && !screens.quit && !script_quitting()) {
        /* into the log, every 5 seconds: how long frames take */
        Uint32 now_ms = SDL_GetTicks();
        slowest = now_ms - frame_started > slowest ? now_ms - frame_started : slowest;
        frame_started = now_ms;
        timed_frames++;
        if (now_ms - timing_since >= 5000) {
            printf("frames: %u ms each, slowest %u ms, on %s\n", (unsigned)((now_ms - timing_since) / timed_frames),
                   (unsigned)slowest, screens_state(&screens, &session));
            timing_since = now_ms;
            timed_frames = 0;
            slowest = 0;
        }
        unsigned now = platform_pad() | script_pad();
        unsigned pressed = now & ~held;
        held = now;
        if (client != NULL) {
            session_update(&session);
        }
        screens_frame(&screens, &session, pressed);
        ui_finish();
        SDL_Surface *shot = draw_end(script_shot_due());
        script_save_shot(shot);
    }

    /* leaving mid-build: the core cancels it, which deletes the unfinished image, before the app ends */
    if (client != NULL && session.build == BUILD_RUNNING) {
        session_cancel(&session);
        while (session.build == BUILD_RUNNING) {
            session_update(&session);
            thread_sleep(50);
        }
    }
    session_close(&session);
    client_close(client);
    ui_close();
    draw_close();
    SDL_Quit();
    platform_close();
    return 0;
}
