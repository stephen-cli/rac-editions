#include "script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#ifdef RAC_PREVIEW
#include <SDL_image.h>
#endif

/* The builder's state for "await": the screen and what it is waiting on (main.c). */
const char *preview_state(void);

static char *script;
static const char *at;
static char shots[512];
static bool as_png;
static Uint32 step_started;
static unsigned scripted;           /* a scripted press, held for a few frames */
static int scripted_frames;
static char shot_due[64];
static bool quitting;

void script_start(const char *text, const char *shots_folder, bool png)
{
    size_t length = strlen(text);
    script = malloc(length + 1);
    if (script == NULL) {
        return;
    }
    /* new lines separate steps as ";" does */
    for (size_t i = 0; i <= length; i++) {
        script[i] = text[i] == '\n' || text[i] == '\r' ? ';' : text[i];
    }
    at = script;
    snprintf(shots, sizeof shots, "%s", shots_folder);
    as_png = png;
    step_started = SDL_GetTicks();
}

bool script_playing(void)
{
    return at != NULL && *at != 0;
}

bool script_quitting(void)
{
    return quitting;
}

static unsigned button_named(const char *name)
{
    static const struct {
        const char *name;
        unsigned button;
    } names[] = {{"up", PAD_UP},         {"down", PAD_DOWN},     {"left", PAD_LEFT},
                 {"right", PAD_RIGHT},   {"accept", PAD_ACCEPT}, {"back", PAD_BACK},
                 {"triangle", PAD_TRIANGLE}, {"start", PAD_START}};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(names[i].name, name) == 0) {
            return names[i].button;
        }
    }
    return 0;
}

/* Runs the current step; moves on to the next when it is done. */
static void play(void)
{
    if (!script_playing() || scripted_frames > 0 || shot_due[0]) {
        return;
    }
    while (*at == ';' || *at == ' ') {
        at++;
    }
    const char *end = strchr(at, ';');
    size_t length = end != NULL ? (size_t)(end - at) : strlen(at);
    char step[256];
    snprintf(step, sizeof step, "%.*s", (int)(length < sizeof step ? length : sizeof step - 1), at);
    bool done = true;
    if (step[0] == 0) {
        done = true;
    } else if (strncmp(step, "wait=", 5) == 0) {
        done = SDL_GetTicks() - step_started >= (Uint32)atoi(step + 5);
    } else if (strncmp(step, "await=", 6) == 0) {
        done = strcmp(preview_state(), step + 6) == 0;
        if (!done && SDL_GetTicks() - step_started > 30u * 60u * 1000u) {
            fprintf(stderr, "script: gave up waiting for %s (state %s)\n", step + 6, preview_state());
            quitting = true;
        }
    } else if (strncmp(step, "shot=", 5) == 0) {
        snprintf(shot_due, sizeof shot_due, "%s", step + 5);
    } else if (strcmp(step, "quit") == 0) {
        quitting = true;
    } else {
        scripted = button_named(step);
        scripted_frames = 3;
        if (scripted == 0) {
            fprintf(stderr, "script: unknown step %s\n", step);
        }
    }
    if (done) {
        printf("script: %s (state %s)\n", step, preview_state());
        at = end != NULL ? end + 1 : at + length;
        step_started = SDL_GetTicks();
    }
}

unsigned script_pad(void)
{
    play();
    unsigned held = 0;
    if (scripted_frames > 0) {
        /* held for two frames, then let go for one, so the builder sees a press */
        held = scripted_frames > 1 ? scripted : 0;
        scripted_frames--;
    }
    return held;
}

bool script_shot_due(void)
{
    return shot_due[0] != 0;
}

void script_save_shot(SDL_Surface *surface)
{
    if (!shot_due[0]) {
        return;
    }
    if (surface != NULL) {
        char path[700];
        snprintf(path, sizeof path, "%s/%s.%s", shots, shot_due, as_png ? "png" : "bmp");
#ifdef RAC_PREVIEW
        int saved = as_png ? IMG_SavePNG(surface, path) : SDL_SaveBMP(surface, path);
#else
        int saved = SDL_SaveBMP(surface, path);
#endif
        if (saved != 0) {
            fprintf(stderr, "script: %s: %s\n", path, SDL_GetError());
        }
        SDL_FreeSurface(surface);
    }
    shot_due[0] = 0;
    step_started = SDL_GetTicks();
}
