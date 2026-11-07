/*
 * The mod segment's holes: the tables the edition makes from the bytes of a player's disc, so a release carries
 * none of the games' bytes. holes.json names each hole's symbol, place, size and fill, as tools/payload.py's
 * HOLES and Filler describe them; each fill here makes the same bytes as Filler's.
 */
#ifndef RAC_FILL_H
#define RAC_FILL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "json.h"

typedef struct Filler Filler;

/* A filler for DISCS (an object of disc ids to image paths); NULL when out of memory. */
Filler *filler_new(const Json *discs);
void filler_free(Filler *filler);

/*
 * The bytes of the hole SYMBOL, made as FILL says, into OUT, which holds SIZE bytes; false with ERROR set when the
 * disc cannot be read or the bytes made are not SIZE. A source read for one hole is kept for the next.
 */
bool filler_fill(Filler *filler, const char *symbol, const Json *fill, uint8_t *out, size_t size, char *error,
                 size_t error_capacity);

/*
 * The Reuse Seed box's glyphs in a menu font (tools/gen_glyphs.py). A font is a 256 x 128 sheet of 8-bit pixels
 * with a 256-entry palette as stored (RGBA in the GS's CSM1 order, alpha 0..0x80). The mod segment's
 * ee_glyph_cells holds the title screen's fonts' cells; each level's own copies of the fonts get them painted in.
 */
#define GLYPH_FONT_W 256
#define GLYPH_FONT_H 128
#define GLYPH_CELLS_SIZE (7 * 16 * 24)

/* The seven 24 x 16 cells for the font PIXELS with PALETTE into CELLS (GLYPH_CELLS_SIZE bytes), in ee_glyph_cells'
   order; false with ERROR set when a cell's place in the sheet is not blank. */
bool glyph_cells(const uint8_t *pixels, const uint8_t *palette, uint8_t *cells, char *error, size_t capacity);

/* CELLS (made by glyph_cells) drawn into the font PIXELS in their places. */
void glyph_paint(uint8_t *pixels, const uint8_t *cells);

#endif
