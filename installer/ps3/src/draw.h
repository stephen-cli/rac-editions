/*
 * The screen's drawing, done by the graphics chip at the display's own resolution: on the PS3 the RSX, through
 * tiny3d (draw_rsx.c); on a computer, SDL's renderer (draw_sdl.c), for the preview. Everything is triangles in
 * display pixels, blended: shapes in plain colours, and text from one atlas of glyphs, white on clear, which a
 * vertex's colour tints.
 */
#ifndef RAC_PS3_DRAW_H
#define RAC_PS3_DRAW_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL.h>

typedef struct {
    Uint8 r, g, b, a;
} Colour;

/* A triangle's corner: where it is, in display pixels; where in the atlas (0 to 1), for text; its colour. */
typedef struct {
    float x, y;
    float u, v;
    Colour colour;
} DrawVertex;

/* The display, and its size in pixels into WIDTH and HEIGHT; false with ERROR filled when it cannot be had. */
bool draw_open(int *width, int *height, char *error, size_t capacity);
void draw_close(void);

/* The atlas of glyphs, WIDTH by HEIGHT pixels, clear; made once. */
bool draw_atlas_open(int width, int height);

/* GLYPH (ARGB8888) into the atlas at X, Y. */
void draw_atlas_put(int x, int y, const SDL_Surface *glyph);

/* A frame: begun cleared to CLEAR, then triangles, in the order they are to be seen, then shown. When SCREENSHOT,
   draw_end also returns the frame, as an ARGB8888 surface the caller frees (NULL when it cannot be read). */
void draw_begin(Colour clear);
void draw_shapes(const DrawVertex *vertices, int count);
void draw_text(const DrawVertex *vertices, int count);
SDL_Surface *draw_end(bool screenshot);

#endif
