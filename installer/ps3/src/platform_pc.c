/*
 * The preview on a computer: the same builder in a 1280x720 window, for working on the screens. The keyboard stands
 * in for the controller (arrows, Enter for Cross, Escape or Backspace for Circle, T for Triangle, S for Start), and
 * the folders come from the command line:
 *
 *   ps3-preview --discs DIR --payload DIR --data DIR --fonts DIR
 *
 * RAC_PS3_SCRIPT plays a test script (script.h) instead of waiting for keys, with screenshots as PNGs in
 * RAC_PS3_SHOTS.
 */
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "script.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#endif

static bool quitting;

bool platform_open(int argc, char **argv, Places *places)
{
    memset(places, 0, sizeof *places);
    for (int i = 1; i + 1 < argc; i += 2) {
        char *into = strcmp(argv[i], "--discs") == 0     ? places->discs
                     : strcmp(argv[i], "--payload") == 0 ? places->payload
                     : strcmp(argv[i], "--data") == 0    ? places->data
                     : strcmp(argv[i], "--fonts") == 0   ? places->fonts
                                                         : NULL;
        if (into == NULL) {
            fprintf(stderr, "usage: ps3-preview --discs DIR --payload DIR --data DIR --fonts DIR\n");
            return false;
        }
        snprintf(into, 512, "%s", argv[i + 1]);
    }
    const char *script = getenv("RAC_PS3_SCRIPT");
    if (script != NULL) {
        const char *shots = getenv("RAC_PS3_SHOTS");
        script_start(script, shots != NULL ? shots : ".", true);
    }
    return places->discs[0] && places->payload[0] && places->data[0] && places->fonts[0];
}

void platform_close(void)
{
}

unsigned platform_pad(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            quitting = true;
        }
    }
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    unsigned held = 0;
    held |= keys[SDL_SCANCODE_UP] ? PAD_UP : 0;
    held |= keys[SDL_SCANCODE_DOWN] ? PAD_DOWN : 0;
    held |= keys[SDL_SCANCODE_LEFT] ? PAD_LEFT : 0;
    held |= keys[SDL_SCANCODE_RIGHT] ? PAD_RIGHT : 0;
    held |= keys[SDL_SCANCODE_RETURN] ? PAD_ACCEPT : 0;
    held |= keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_BACKSPACE] ? PAD_BACK : 0;
    held |= keys[SDL_SCANCODE_T] ? PAD_TRIANGLE : 0;
    held |= keys[SDL_SCANCODE_S] ? PAD_START : 0;
    return held;
}

bool platform_quitting(void)
{
    return quitting;
}

bool platform_accept_is_cross(void)
{
    return true;
}

bool platform_list_folder(const char *folder, void (*found)(void *context, const char *name), void *context)
{
#ifdef _WIN32
    char pattern[600];
    snprintf(pattern, sizeof pattern, "%s\\*", folder);
    WIN32_FIND_DATAA entry;
    HANDLE search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        found(context, entry.cFileName);
    } while (FindNextFileA(search, &entry));
    FindClose(search);
#else
    DIR *directory = opendir(folder);
    if (directory == NULL) {
        return false;
    }
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        found(context, entry->d_name);
    }
    closedir(directory);
#endif
    return true;
}
