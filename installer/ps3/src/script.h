/*
 * Test scripts: the builder driven without a player, for the preview and for a test run on the PS3 (RPCS3). A
 * script is steps separated by ";" (or new lines): a button (up, down, left, right, accept, back, triangle, start),
 * "wait=MS", "await=STATE" (until the builder's state, screens_state(), is STATE; at most 30 minutes),
 * "shot=NAME" (a screenshot) and "quit".
 */
#ifndef RAC_PS3_SCRIPT_H
#define RAC_PS3_SCRIPT_H

#include <stdbool.h>

#include <SDL.h>

/* Plays SCRIPT (copied); screenshots go to SHOTS_FOLDER, as PNG when PNG is true (the preview) or BMP. */
void script_start(const char *script, const char *shots_folder, bool png);

/* Whether a script is playing. */
bool script_playing(void);

/* Once a frame, before the builder looks at the controller: the buttons the script holds now. */
unsigned script_pad(void);

/* Whether the script has asked to quit. */
bool script_quitting(void);

/* Whether the script wants this frame as a screenshot; script_save_shot then saves it (SURFACE, freed) and goes on. */
bool script_shot_due(void);
void script_save_shot(SDL_Surface *surface);

#endif
