/*
 * The HelpDesk lines' audio (vag.h). Each frame of 28 samples is encoded with the filter and shift whose decoded
 * samples come closest to the line (squared error), decoded as the game will, so errors do not build up; the
 * search tries, for each filter, the shift the frame's range needs and its two neighbours, and digital silence is
 * written as retail writes it (filter 0, shift 12).
 */
#include "vag.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 2048
#define FRAME (VAG_RATE / 24.0)                 /* a frame of the cut lists' times, in samples */
#define FADE_IN (VAG_RATE * 2 / 1000)
#define FADE_OUT (VAG_RATE * 12 / 1000)

static const int FILTERS[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

/* V >> S rounding down, for negative V too, as the game's arithmetic shift does (C leaves it to the compiler). */
static int64_t shift_down(int64_t v, unsigned s)
{
    return v >= 0 ? v >> s : -((-v - 1) >> s) - 1;
}

static int32_t clamp16(int64_t v)
{
    return v < -32768 ? -32768 : v > 32767 ? 32767 : (int32_t)v;
}

static uint32_t read_u32_be(const uint8_t *at)
{
    return (uint32_t)at[0] << 24 | (uint32_t)at[1] << 16 | (uint32_t)at[2] << 8 | at[3];
}

static void write_u32_be(uint8_t *at, uint32_t v)
{
    at[0] = (uint8_t)(v >> 24);
    at[1] = (uint8_t)(v >> 16);
    at[2] = (uint8_t)(v >> 8);
    at[3] = (uint8_t)v;
}

int16_t *vag_decode(const uint8_t *vag, size_t size, size_t *count)
{
    size_t frames, kept;
    int16_t *out;
    int64_t s1 = 0, s2 = 0;

    *count = 0;
    if (size < VAG_HEADER)
        return NULL;
    frames = read_u32_be(vag + 12) / 16;
    if (VAG_HEADER + 16 * frames > size)
        return NULL;
    kept = frames >= 2 ? (frames - 2) * 28 : 0;
    out = malloc(kept * sizeof *out + 1);
    if (!out)
        return NULL;
    for (size_t n = 0; n < frames; n++) {
        const uint8_t *frame = vag + VAG_HEADER + 16 * n;
        unsigned shift = frame[0] & 15, filter = frame[0] >> 4;
        int f0 = filter < 5 ? FILTERS[filter][0] : 0, f1 = filter < 5 ? FILTERS[filter][1] : 0;
        for (unsigned k = 0; k < 28; k++) {
            int nibble = (frame[2 + k / 2] >> (4 * (k & 1))) & 15;
            int64_t value = shift_down(((nibble ^ 8) - 8) * 4096, shift);
            value = clamp16(value + shift_down(f0 * s1 + f1 * s2 + 32, 6));
            s2 = s1;
            s1 = value;
            if (n >= 1 && n + 1 < frames)       /* not the first frame, nor the last */
                out[(n - 1) * 28 + k] = (int16_t)value;
        }
    }
    *count = kept;
    return out;
}

int16_t *vag_render(const struct vag_clip *clips, size_t n, size_t *count)
{
    double end = 0;
    size_t length;
    double *mix;
    int16_t *out;

    for (size_t c = 0; c < n; c++)
        if (c == 0 || clips[c].at + clips[c].length > end)
            end = clips[c].at + clips[c].length;
    length = (size_t)nearbyint(end * FRAME);
    mix = calloc(length + 1, sizeof *mix);
    out = malloc(length * sizeof *out + 1);
    if (!mix || !out) {
        free(mix);
        free(out);
        return NULL;
    }
    for (size_t c = 0; c < n; c++) {
        const struct vag_clip *clip = &clips[c];
        double first = clip->at - clip->fade_in, last = clip->at + clip->length + clip->fade_out;
        long from = (long)(first * FRAME), to = (long)(last * FRAME) + 1;
        if (from < 0)
            from = 0;
        if (to > (long)length)
            to = (long)length;
        for (long i = from; i < to; i++) {
            double t = i / FRAME, gain = 1.0;
            long s;
            if (t < first || t >= last)
                continue;
            s = (long)nearbyint((t - clip->at + clip->from) * FRAME);
            if (s < 0 || (size_t)s >= clip->count)
                continue;
            if (clip->fade_in != 0 && t < clip->at + clip->fade_in)
                gain = (t - first) / (2 * clip->fade_in);
            if (clip->fade_out != 0 && t > clip->at + clip->length - clip->fade_out)
                gain *= (last - t) / (2 * clip->fade_out);
            mix[i] += gain * clip->samples[s];
        }
    }
    for (size_t i = 0; i < FADE_IN && i < length; i++)
        mix[i] *= (double)i / FADE_IN;
    for (size_t i = 0; i < FADE_OUT && i < length; i++)
        mix[length - 1 - i] *= (double)i / FADE_OUT;
    for (size_t i = 0; i < length; i++) {
        double v = nearbyint(mix[i]);
        out[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
    free(mix);
    *count = length;
    return out;
}

struct frame {
    int filter, shift;
    uint8_t nibbles[28];
    int64_t s1, s2;                             /* the decoder's state after the frame */
};

/* The frame of 28 samples X that decodes closest to X after the decoder's state S1, S2. */
static struct frame encode_frame(const int32_t *x, int64_t s1, int64_t s2)
{
    struct frame best;
    int64_t best_error = -1;
    int silent = 1;

    for (int k = 0; k < 28; k++)
        silent &= x[k] == 0;
    if (silent) {
        memset(&best, 0, sizeof best);
        best.shift = 12;
        return best;
    }
    for (int filter = 0; filter < 5; filter++) {
        int f0 = FILTERS[filter][0], f1 = FILTERS[filter][1], fit = 12;
        int64_t a = s1, b = s2, peak = 0;
        /* the shift the frame needs: residuals of the line's own samples against the filter */
        for (int k = 0; k < 28; k++) {
            int64_t d = x[k] - shift_down(f0 * a + f1 * b + 32, 6);
            if ((d >= 0 ? d : -d) > peak)
                peak = d >= 0 ? d : -d;
            b = a;
            a = x[k];
        }
        while (fit > 0 && peak > (int64_t)7 << (12 - fit))     /* the finest shift whose steps still reach it */
            fit--;
        for (int shift = fit - 1; shift <= fit + 1; shift++) {
            int64_t step, half, error = 0;
            uint8_t nibbles[28];
            int k;
            if (shift < 0 || shift > 12)
                continue;
            step = (int64_t)1 << (12 - shift);
            half = step >> 1;
            a = s1;
            b = s2;
            for (k = 0; k < 28; k++) {
                int64_t p = shift_down(f0 * a + f1 * b + 32, 6), d = x[k] - p, q, r;
                q = d >= 0 ? (d + half) / step : -((half - d) / step);
                q = q < -8 ? -8 : q > 7 ? 7 : q;
                r = clamp16(q * step + p);
                error += (x[k] - r) * (x[k] - r);
                if (best_error >= 0 && error >= best_error)
                    break;
                nibbles[k] = (uint8_t)((uint64_t)q & 15);
                b = a;
                a = r;
            }
            if (k == 28 && (best_error < 0 || error < best_error)) {
                best_error = error;
                best.filter = filter;
                best.shift = shift;
                memcpy(best.nibbles, nibbles, sizeof nibbles);
                best.s1 = a;
                best.s2 = b;
            }
        }
    }
    return best;
}

uint8_t *vag_encode(const int16_t *samples, size_t count, const char *name, size_t *size)
{
    size_t blocks = (count + 27) / 28, body = 16 + 16 * blocks + 16, total = VAG_HEADER + body;
    uint8_t *out, head = 0;
    int64_t s1 = 0, s2 = 0;

    total += (SECTOR - total % SECTOR) % SECTOR;
    out = calloc(total, 1);
    if (!out)
        return NULL;
    for (size_t n = 0; n < blocks; n++) {
        int32_t x[28];
        uint8_t *frame = out + VAG_HEADER + 16 + 16 * n;
        struct frame f;
        for (size_t k = 0; k < 28; k++)
            x[k] = 28 * n + k < count ? samples[28 * n + k] : 0;
        f = encode_frame(x, s1, s2);
        s1 = f.s1;
        s2 = f.s2;
        head = (uint8_t)(f.filter << 4 | f.shift);
        frame[0] = head;
        frame[1] = n == blocks - 1 ? 1 : 0;
        for (size_t k = 0; k < 28; k += 2)
            frame[2 + k / 2] = (uint8_t)(f.nibbles[k] | f.nibbles[k + 1] << 4);
    }
    out[VAG_HEADER + 16 + 16 * blocks] = head;  /* the end frame repeats the last frame's first byte */
    out[VAG_HEADER + 16 + 16 * blocks + 1] = 7;
    memcpy(out, "VAGp", 4);
    write_u32_be(out + 4, 0x20);
    write_u32_be(out + 12, (uint32_t)body);
    write_u32_be(out + 16, VAG_RATE);
    for (size_t k = 0; k < 16 && name[k]; k++)
        out[0x20 + k] = (uint8_t)name[k];
    *size = total;
    return out;
}
