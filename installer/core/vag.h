/*
 * The HelpDesk lines' audio: the games' VAG files (a 0x30-byte header, then SPU-ADPCM frames of 16 bytes), the cut
 * lists that join pieces of the disc's lines into a new line, and the encoder that writes the new line as the
 * game's own are written. The same maths as tools/gen_helpdesk.py's description, bit for bit: every value is an
 * integer or an IEEE double computed in the same order, rounded half to even where Python rounds.
 */
#ifndef RAC_VAG_H
#define RAC_VAG_H

#include <stddef.h>
#include <stdint.h>

#define VAG_RATE 44100
#define VAG_HEADER 0x30

/* The audio frames of the VAG file VAG (SIZE bytes), not its silent first frame or its end frame, as 16-bit
 * samples: a new array (*COUNT samples), or NULL for a file too short for its header's size or without memory. */
int16_t *vag_decode(const uint8_t *vag, size_t size, size_t *count);

/* A piece of a cut list: SAMPLES (COUNT of them) played from FROM for LENGTH, placed at AT, faded in over FADE_IN
 * before AT and out over FADE_OUT after its end; times in frames of 1/24 s. */
struct vag_clip {
    const int16_t *samples;
    size_t count;
    double at, length, from, fade_in, fade_out;
};

/* The line the N clips make, faded in over 2 ms and out over 12 ms: a new array (*COUNT samples), or NULL
 * without memory. */
int16_t *vag_render(const struct vag_clip *clips, size_t n, size_t *count);

/* SAMPLES (COUNT) as a VAG file named NAME in the format of the game's lines, padded to whole 2048-byte sectors:
 * a new array (*SIZE bytes), or NULL without memory. */
uint8_t *vag_encode(const int16_t *samples, size_t count, const char *name, size_t *size);

#endif
