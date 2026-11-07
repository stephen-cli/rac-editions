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

#endif
