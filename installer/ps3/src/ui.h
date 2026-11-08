/*
 * Drawing in the builder's look, from rac-editions-web's design tokens (its dark theme): the colours, Chakra Petch
 * for headings and Barlow for text, panels with two corners cut, a starfield behind everything. The screens are
 * laid out on a 1280x720 screen; the drawing (draw.h) is at the display's own resolution, so shapes stay sharp
 * and text is rendered at the size it is seen.
 */
#ifndef RAC_PS3_UI_H
#define RAC_PS3_UI_H

#include <stdbool.h>
#include <stddef.h>

#include "draw.h"

/* The screen the builder is laid out on, scaled to the display. */
#define SCREEN_W 1280
#define SCREEN_H 720

extern const Colour COLOUR_SPACE, COLOUR_PLATE, COLOUR_RAIL, COLOUR_EDGE, COLOUR_EDGE_SOFT, COLOUR_INK,
    COLOUR_INK_DIM, COLOUR_INK_FAINT, COLOUR_HOLO, COLOUR_HOLO_GLASS, COLOUR_HOLO_LINE, COLOUR_BOLT,
    COLOUR_BOLT_INK, COLOUR_BOLT_GLASS, COLOUR_HAZARD, COLOUR_HAZARD_GLASS, COLOUR_CLANK, COLOUR_CLANK_GLASS;

enum {
    FONT_TITLE,         /* Chakra Petch Bold 44: a screen's heading */
    FONT_HEADING,       /* Chakra Petch SemiBold 27 */
    FONT_LABEL,         /* Chakra Petch SemiBold 17: small capitals over sections */
    FONT_BODY,          /* Barlow 23 */
    FONT_BODY_BOLD,     /* Barlow SemiBold 23 */
    FONT_SMALL,         /* Barlow 19 */
    FONT_COUNT
};

/* The controller's symbols for button prompts. */
enum { GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_TRIANGLE, GLYPH_SQUARE };

/* Drawing on a display WIDTH by HEIGHT pixels, with the fonts from FONTS_FOLDER; false with ERROR filled when
   one cannot be loaded. */
bool ui_open(int width, int height, const char *fonts_folder, char *error, size_t capacity);
void ui_close(void);

/* Begins a frame: space, and the starfield; WARP from 0 (still) to 1 (a build's full speed). */
void ui_background(double warp);

/* Sends what is left of the frame to be drawn; then it can be shown (draw_end). */
void ui_finish(void);

/* A panel: two corners (top left, bottom right) cut by CUT pixels, filled, with an edge. */
void ui_panel(int x, int y, int w, int h, int cut, Colour fill, Colour edge);

/* TEXT in FONT with its top left at X, Y; returns its width. */
int ui_text(int font, int x, int y, Colour colour, const char *text);
int ui_text_width(int font, const char *text);
int ui_line_height(int font);

/* TEXT wrapped to WIDTH; returns the height it took. */
int ui_wrap(int font, int x, int y, int width, Colour colour, const char *text);

/* A progress bar, FRACTION (0 to 1) full. */
void ui_bar(int x, int y, int w, int h, double fraction, Colour fill);

/* A controller symbol, SIZE pixels across, its centre at X, Y. */
void ui_glyph(int x, int y, int size, int glyph);

/* A prompt, the symbol and its LABEL, from X, Y (the label's top); returns its width. */
int ui_prompt(int x, int y, int glyph, const char *label);

#endif
