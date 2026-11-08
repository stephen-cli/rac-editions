/*
 * What the builder needs from the machine it runs on: the controller, the folders it reads and writes, and whether
 * the player has asked to leave. platform_ps3.c is the PS3 (PSL1GHT); platform_pc.c is a preview on a computer,
 * driven by the keyboard, for working on the screens. Either starts a test script (script.h) when given one.
 */
#ifndef RAC_PS3_PLATFORM_H
#define RAC_PS3_PLATFORM_H

#include <stdbool.h>

#include <SDL.h>

/* The controller's buttons as the builder uses them. ACCEPT and BACK are Cross and Circle, or the other way round
   where the console is set so (Japan's default). */
enum {
    PAD_UP = 1 << 0,
    PAD_DOWN = 1 << 1,
    PAD_LEFT = 1 << 2,
    PAD_RIGHT = 1 << 3,
    PAD_ACCEPT = 1 << 4,
    PAD_BACK = 1 << 5,
    PAD_TRIANGLE = 1 << 6,
    PAD_START = 1 << 7,
};

typedef struct {
    char discs[512];            /* the folder of disc images: /dev_hdd0/PS2ISO */
    char payload[512];          /* the edition's payload, beside the app */
    char data[512];             /* the app's own data: the discs' hashes */
    char fonts[512];
} Places;

/* Starts the controller and the system's callbacks and fills PLACES; false when the app cannot run. */
bool platform_open(int argc, char **argv, Places *places);
void platform_close(void);

/* Called once a frame: the buttons held now (PAD_*), after the system's callbacks have run. */
unsigned platform_pad(void);

/* Whether the player has asked to leave (the PS button's Quit Game, or the window closed). */
bool platform_quitting(void);

/* Whether ACCEPT is Cross (else Circle), for the button prompts. */
bool platform_accept_is_cross(void);

/* Calls FOUND(CONTEXT, NAME) for each entry's name in FOLDER; false when the folder cannot be read. */
bool platform_list_folder(const char *folder, void (*found)(void *context, const char *name), void *context);

#endif
