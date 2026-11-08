#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_ttf.h>

#include "draw.h"

/* rac-editions-web packages/ui/src/tokens/tokens.ts, the dark theme */
const Colour COLOUR_SPACE = {0x07, 0x0B, 0x13, 255};
const Colour COLOUR_PLATE = {20, 28, 42, 219};          /* plate-glass, 0.86 */
const Colour COLOUR_RAIL = {14, 20, 31, 184};           /* rail-glass, 0.72 */
const Colour COLOUR_EDGE = {0x34, 0x42, 0x5A, 255};
const Colour COLOUR_EDGE_SOFT = {0x27, 0x33, 0x49, 255};
const Colour COLOUR_INK = {0xE6, 0xED, 0xF5, 255};
const Colour COLOUR_INK_DIM = {0x9A, 0xA9, 0xBD, 255};
const Colour COLOUR_INK_FAINT = {0x86, 0x96, 0xAD, 255};
const Colour COLOUR_HOLO = {0x3F, 0xD7, 0xE6, 255};
const Colour COLOUR_HOLO_GLASS = {63, 215, 230, 20};    /* 0.08 */
const Colour COLOUR_HOLO_LINE = {63, 215, 230, 89};     /* 0.35 */
const Colour COLOUR_BOLT = {0xF2, 0xB5, 0x3A, 255};
const Colour COLOUR_BOLT_INK = {0x1A, 0x12, 0x06, 255};
const Colour COLOUR_BOLT_GLASS = {242, 181, 58, 31};    /* 0.12 */
const Colour COLOUR_HAZARD = {0xFF, 0x7A, 0x1A, 255};
const Colour COLOUR_HAZARD_GLASS = {255, 122, 26, 26};  /* 0.10 */
const Colour COLOUR_CLANK = {0x8B, 0xE3, 0x6B, 255};
const Colour COLOUR_CLANK_GLASS = {139, 227, 107, 26};  /* 0.10 */

/* The display: its pixels for each of the screen's (1.5 at 1080p, 1 at 720p). */
static float scale = 1.0f;
static float origin_x, origin_y;    /* where the screen starts on the display: centred when their shapes differ */
static Uint32 frame;

static TTF_Font *fonts[FONT_COUNT];
static void *font_files[FONT_COUNT];   /* each font's file, read whole */
static const struct {
    const char *file;
    int size;                   /* in screen pixels; opened at the display's */
} FONT_FILES[FONT_COUNT] = {
    [FONT_TITLE] = {"ChakraPetch-Bold.ttf", 44},    [FONT_HEADING] = {"ChakraPetch-SemiBold.ttf", 27},
    [FONT_LABEL] = {"ChakraPetch-SemiBold.ttf", 17}, [FONT_BODY] = {"Barlow-Regular.ttf", 23},
    [FONT_BODY_BOLD] = {"Barlow-SemiBold.ttf", 23}, [FONT_SMALL] = {"Barlow-Regular.ttf", 19},
};

/* ---- Triangles, gathered and sent in order ---- */

#define BATCH 6000              /* a multiple of 3 */
static DrawVertex batch[BATCH];
static int batch_count;
static bool batch_is_text;

static void flush(void)
{
    if (batch_count > 0) {
        for (int i = 0; i < batch_count; i++) {
            batch[i].x += origin_x;
            batch[i].y += origin_y;
        }
        if (batch_is_text) {
            draw_text(batch, batch_count);
        } else {
            draw_shapes(batch, batch_count);
        }
    }
    batch_count = 0;
}

static void triangle(bool text, DrawVertex a, DrawVertex b, DrawVertex c)
{
    if (batch_is_text != text || batch_count + 3 > BATCH) {
        flush();
        batch_is_text = text;
    }
    batch[batch_count++] = a;
    batch[batch_count++] = b;
    batch[batch_count++] = c;
}

static DrawVertex point(float x, float y, Colour colour)
{
    return (DrawVertex){x, y, 0, 0, colour};
}

/* A quadrilateral in display pixels, its corners in order around it. */
static void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Colour c0, Colour c1,
                 Colour c2, Colour c3)
{
    triangle(false, point(x0, y0, c0), point(x1, y1, c1), point(x2, y2, c2));
    triangle(false, point(x0, y0, c0), point(x2, y2, c2), point(x3, y3, c3));
}

/* A screen position in display pixels, on a whole pixel, so edges come out even. */
static float at(double screen)
{
    return (float)round(screen * scale);
}

/* A rectangle of COLOUR, in screen pixels. */
static void fill(float x, float y, float w, float h, Colour colour)
{
    if (w <= 0 || h <= 0 || colour.a == 0) {
        return;
    }
    float x0 = at(x), y0 = at(y), x1 = at(x + w), y1 = at(y + h);
    if (x1 == x0) {
        x1 = x0 + 1;
    }
    if (y1 == y0) {
        y1 = y0 + 1;
    }
    quad(x0, y0, x1, y0, x1, y1, x0, y1, colour, colour, colour, colour);
}

/* A convex polygon of COUNT corners in display pixels, filled: a fan from its first corner. */
static void polygon(const float (*corners)[2], int count, Colour colour)
{
    for (int i = 1; i + 1 < count; i++) {
        triangle(false, point(corners[0][0], corners[0][1], colour), point(corners[i][0], corners[i][1], colour),
                 point(corners[i + 1][0], corners[i + 1][1], colour));
    }
}

/* A convex polygon's outline, THICK display pixels wide, inside its edge: the band between the polygon and the
   polygon with each side moved in by THICK (the sides' new meeting points, as a mitre). */
static void outline(const float (*corners)[2], int count, float thick, Colour colour)
{
    float inner[8][2];
    if (count > 8) {
        return;
    }
    /* which way round the corners go decides which side is in */
    float area = 0;
    for (int i = 0; i < count; i++) {
        const float *p = corners[i], *q = corners[(i + 1) % count];
        area += p[0] * q[1] - q[0] * p[1];
    }
    float inward = area > 0 ? 1.0f : -1.0f;
    for (int i = 0; i < count; i++) {
        const float *before = corners[(i + count - 1) % count], *p = corners[i], *after = corners[(i + 1) % count];
        float ax = p[0] - before[0], ay = p[1] - before[1], bx = after[0] - p[0], by = after[1] - p[1];
        float al = sqrtf(ax * ax + ay * ay), bl = sqrtf(bx * bx + by * by);
        if (al == 0 || bl == 0) {
            inner[i][0] = p[0];
            inner[i][1] = p[1];
            continue;
        }
        /* each side's normal, pointing in */
        float nax = -ay / al * inward, nay = ax / al * inward, nbx = -by / bl * inward, nby = bx / bl * inward;
        float join = 1 + nax * nbx + nay * nby;
        inner[i][0] = p[0] + thick * (nax + nbx) / join;
        inner[i][1] = p[1] + thick * (nay + nby) / join;
    }
    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;
        quad(corners[i][0], corners[i][1], corners[j][0], corners[j][1], inner[j][0], inner[j][1], inner[i][0],
             inner[i][1], colour, colour, colour, colour);
    }
}

/* A line between two points in display pixels, WIDE pixels across, from colour FROM to colour TO. */
static void segment(float x1, float y1, float x2, float y2, float wide, Colour from, Colour to)
{
    float dx = x2 - x1, dy = y2 - y1;
    float length = sqrtf(dx * dx + dy * dy);
    if (length == 0) {
        return;
    }
    float nx = -dy / length * wide / 2, ny = dx / length * wide / 2;
    quad(x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny, from, to, to, from);
}

/* The thickness of a one-pixel edge on the display: whole pixels, at least one. */
static float hairline(void)
{
    float pixels = roundf(scale);
    return pixels < 1 ? 1 : pixels;
}

/* ---- Text: glyphs drawn once, at the display's size, into the atlas ---- */

#define ATLAS_W 2048
#define ATLAS_H 2048
#define GLYPHS 4096             /* a power of two */

typedef struct {
    bool used;
    int font;
    Uint32 code;
    bool drawn;                 /* in the atlas (a space has nothing to draw) */
    int x, y, w, h;             /* where in the atlas */
    int offset;                 /* where the glyph's picture starts, from the pen */
    int advance;
} Glyph;
static Glyph glyphs[GLYPHS];
static bool atlas_ready;
static int shelf_x, shelf_y, shelf_h;

static Glyph *glyph_of(int font, Uint32 code)
{
    unsigned slot = (code * 2654435761u + (unsigned)font * 40503u) & (GLYPHS - 1);
    for (int probe = 0; probe < GLYPHS; probe++, slot = (slot + 1) & (GLYPHS - 1)) {
        Glyph *glyph = &glyphs[slot];
        if (glyph->used && glyph->font == font && glyph->code == code) {
            return glyph;
        }
        if (glyph->used) {
            continue;
        }
        memset(glyph, 0, sizeof *glyph);
        glyph->used = true;
        glyph->font = font;
        glyph->code = code;
        int minx, maxx, miny, maxy;
        if (TTF_GlyphMetrics32(fonts[font], code, &minx, &maxx, &miny, &maxy, &glyph->advance) != 0) {
            return glyph;
        }
        SDL_Surface *picture = TTF_RenderGlyph32_Blended(fonts[font], code, (SDL_Color){255, 255, 255, 255});
        if (picture == NULL) {
            return glyph;
        }
        /* SDL_ttf starts a glyph that reaches left of the pen as far right as it reaches */
        glyph->offset = minx < 0 ? minx : 0;
        if (shelf_x + picture->w + 1 > ATLAS_W) {
            shelf_x = 0;
            shelf_y += shelf_h + 1;
            shelf_h = 0;
        }
        if (atlas_ready && picture->w > 0 && shelf_y + picture->h <= ATLAS_H) {
            SDL_Surface *argb = picture->format->format == SDL_PIXELFORMAT_ARGB8888
                                    ? picture
                                    : SDL_ConvertSurfaceFormat(picture, SDL_PIXELFORMAT_ARGB8888, 0);
            if (argb != NULL) {
                draw_atlas_put(shelf_x, shelf_y, argb);
                glyph->drawn = true;
                glyph->x = shelf_x;
                glyph->y = shelf_y;
                glyph->w = picture->w;
                glyph->h = picture->h;
                shelf_x += picture->w + 1;
                shelf_h = picture->h > shelf_h ? picture->h : shelf_h;
                if (argb != picture) {
                    SDL_FreeSurface(argb);
                }
            }
        }
        SDL_FreeSurface(picture);
        return glyph;
    }
    return NULL;
}

/* The next character of UTF-8 TEXT at *AT, moving *AT past it. */
static Uint32 next_code(const char *text, size_t *at)
{
    const unsigned char *s = (const unsigned char *)text + *at;
    Uint32 code = s[0];
    int extra = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : code >= 0xC0 ? 1 : 0;
    code = extra == 3 ? code & 0x07 : extra == 2 ? code & 0x0F : extra == 1 ? code & 0x1F : code;
    (*at)++;
    for (int i = 0; i < extra && (s[1 + i] & 0xC0) == 0x80; i++) {
        code = code << 6 | (s[1 + i] & 0x3F);
        (*at)++;
    }
    return code;
}

/* A string laid out once: its glyphs and where each starts from the pen's start. Wrapping measures the same words
   many times a frame, and each glyph pair asks FreeType for its kerning, which on the PPU took a long paragraph's
   frame to 137 ms; so a string is laid out the first time it is measured or drawn, then reused. */
#define LAYOUTS 1024                /* a power of two; emptied when three quarters full */
typedef struct {
    char *text;                     /* NULL: an empty entry */
    int font;
    int width;                      /* display pixels */
    int count;
    Glyph **glyphs;
    int *pens;
} Layout;
static Layout layouts[LAYOUTS];
static int layout_count;

static void forget_layouts(void)
{
    for (int i = 0; i < LAYOUTS; i++) {
        free(layouts[i].text);
        free(layouts[i].glyphs);
        free(layouts[i].pens);
    }
    memset(layouts, 0, sizeof layouts);
    layout_count = 0;
}

/* TEXT in FONT laid out: NULL without memory for it. */
static const Layout *layout_of(int font, const char *text)
{
    unsigned hash = 2166136261u ^ (unsigned)font;
    for (const char *c = text; *c != 0; c++) {
        hash = (hash ^ (unsigned char)*c) * 16777619u;
    }
    unsigned slot = hash & (LAYOUTS - 1);
    while (layouts[slot].text != NULL) {
        if (layouts[slot].font == font && strcmp(layouts[slot].text, text) == 0) {
            return &layouts[slot];
        }
        slot = (slot + 1) & (LAYOUTS - 1);
    }
    if (layout_count >= LAYOUTS * 3 / 4) {
        forget_layouts();
        return layout_of(font, text);
    }
    size_t length = strlen(text);
    Layout *layout = &layouts[slot];
    layout->text = malloc(length + 1);
    layout->glyphs = malloc((length + 1) * sizeof *layout->glyphs);
    layout->pens = malloc((length + 1) * sizeof *layout->pens);
    if (layout->text == NULL || layout->glyphs == NULL || layout->pens == NULL) {
        free(layout->text);
        free(layout->glyphs);
        free(layout->pens);
        memset(layout, 0, sizeof *layout);
        return NULL;
    }
    memcpy(layout->text, text, length + 1);
    layout->font = font;
    layout_count++;
    int pen = 0;
    Uint32 before = 0;
    for (size_t i = 0; text[i] != 0;) {
        Uint32 code = next_code(text, &i);
        Glyph *glyph = glyph_of(font, code);
        if (glyph == NULL) {
            continue;
        }
        if (before != 0) {
            pen += TTF_GetFontKerningSizeGlyphs32(fonts[font], before, code);
        }
        layout->glyphs[layout->count] = glyph;
        layout->pens[layout->count] = pen;
        layout->count++;
        pen += glyph->advance;
        before = code;
    }
    layout->width = pen;
    return layout;
}

/* TEXT laid out from display pixel X, Y (its line's top): drawn when DRAW, its width in display pixels returned. */
static int lay_out(int font, float x, float y, Colour colour, const char *text, bool draw)
{
    const Layout *layout = layout_of(font, text);
    if (layout == NULL) {
        return 0;
    }
    for (int i = 0; draw && i < layout->count; i++) {
        const Glyph *glyph = layout->glyphs[i];
        if (!glyph->drawn) {
            continue;
        }
        float x0 = x + (float)(layout->pens[i] + glyph->offset), y0 = y;
        float x1 = x0 + (float)glyph->w, y1 = y0 + (float)glyph->h;
        float u0 = (float)glyph->x / ATLAS_W, v0 = (float)glyph->y / ATLAS_H;
        float u1 = (float)(glyph->x + glyph->w) / ATLAS_W, v1 = (float)(glyph->y + glyph->h) / ATLAS_H;
        triangle(true, (DrawVertex){x0, y0, u0, v0, colour}, (DrawVertex){x1, y0, u1, v0, colour},
                 (DrawVertex){x1, y1, u1, v1, colour});
        triangle(true, (DrawVertex){x0, y0, u0, v0, colour}, (DrawVertex){x1, y1, u1, v1, colour},
                 (DrawVertex){x0, y1, u0, v1, colour});
    }
    return layout->width;
}

/* ---- The builder's drawing ---- */

static void make_glow(void);

bool ui_open(int width, int height, const char *fonts_folder, char *error, size_t capacity)
{
    /* the screen as large as the display holds, centred */
    float across = (float)width / SCREEN_W, down = (float)height / SCREEN_H;
    scale = across < down ? across : down;
    origin_x = floorf((width - SCREEN_W * scale) / 2);
    origin_y = floorf((height - SCREEN_H * scale) / 2);
    if (TTF_Init() != 0) {
        snprintf(error, capacity, "fonts: %s", TTF_GetError());
        return false;
    }
    for (int i = 0; i < FONT_COUNT; i++) {
        char path[640];
        snprintf(path, sizeof path, "%s/%s", fonts_folder, FONT_FILES[i].file);
        /* the whole file in memory: FreeType otherwise reads it as glyphs are drawn, and on the PS3 a read on the
           drawing thread waits behind a build's writes */
        size_t size;
        font_files[i] = SDL_LoadFile(path, &size);
        SDL_RWops *stream = font_files[i] != NULL ? SDL_RWFromConstMem(font_files[i], (int)size) : NULL;
        fonts[i] = stream != NULL ? TTF_OpenFontRW(stream, 1, (int)roundf(FONT_FILES[i].size * scale)) : NULL;
        if (fonts[i] == NULL) {
            snprintf(error, capacity, "%s: %s", path, TTF_GetError());
            return false;
        }
    }
    atlas_ready = draw_atlas_open(ATLAS_W, ATLAS_H);
    if (!atlas_ready) {
        snprintf(error, capacity, "the text's atlas could not be made");
        return false;
    }
    make_glow();
    return true;
}

/* The printable ASCII of every font, rendered a few glyphs a frame (ui_finish) from the start, so a screen's first
   frame seldom stops to render text: on the PS3 a glyph takes about 10 ms, and all of them nearly six seconds. */
static int warm_font;
static Uint32 warm_code = 32;

static void warm_glyphs(void)
{
    for (int i = 0; i < 4 && warm_font < FONT_COUNT; i++) {
        glyph_of(warm_font, warm_code);
        if (++warm_code == 127) {
            warm_code = 32;
            warm_font++;
        }
    }
}

void ui_close(void)
{
    for (int i = 0; i < FONT_COUNT; i++) {
        if (fonts[i] != NULL) {
            TTF_CloseFont(fonts[i]);
        }
        fonts[i] = NULL;
        SDL_free(font_files[i]);
        font_files[i] = NULL;
    }
    forget_layouts();
    TTF_Quit();
}

/* The starfield, as rac-editions-web's (packages/ui/src/effects/starfield.ts): stars in 3D drifting slowly towards
   the viewer and twinkling, each a soft glow (without the web's cross-shaped flares). ui_background's warp runs
   them past at speed as streaks: a star appears faint near the centre, brightens, and its streak lengthens as it
   nears the edge. They move by the clock, not by the frame, so a slow frame does not slow them. */
#define STARS 520
#define IDLE_SPEED 0.004f
#define WARP_SPEED 1.15f
#define GLOW_SCALE 9.0f             /* a glow is drawn at this many times the star's core radius */
#define GLOW_SIZE 64                /* the glow sprite's pixels, in the atlas */

typedef struct {
    float x, y, z;
    int colour;
    float size, twinkle, phase;
    /* this frame's, for the glow pass */
    float head_x, head_y, radius, alpha;
    bool shown;
} Star;
static Star stars[STARS];
static bool stars_placed;

/* mostly white and cool white, some holo cyan, a few bolt gold and warm white; how often each is picked */
static const Colour STAR_COLOURS[] = {
    {232, 238, 247, 255}, {196, 220, 255, 255}, {63, 215, 230, 255}, {242, 181, 58, 255}, {255, 214, 190, 255}};
static const float STAR_WEIGHTS[] = {0.5f, 0.22f, 0.13f, 0.07f, 0.08f};
#define STAR_COLOUR_COUNT 5

static Uint32 star_seed = 0x5EED1E55u;
static float speed = IDLE_SPEED;
static float clock_seconds;
static Uint32 last_ticks;
static float glow_u0, glow_v0, glow_u1, glow_v1;
static bool glow_ready;

static float random_unit(void)
{
    star_seed = star_seed * 1664525u + 1013904223u;
    return (float)(star_seed >> 8) / 16777216.0f;
}

/* The glow: a small bright core, then a halo falling off quickly, as light does; white, tinted as it is drawn. */
static void make_glow(void)
{
    SDL_Surface *glow = SDL_CreateRGBSurfaceWithFormat(0, GLOW_SIZE, GLOW_SIZE, 32, SDL_PIXELFORMAT_ARGB8888);
    if (glow == NULL) {
        return;
    }
    static const float STOPS[][2] = {{0, 1}, {0.1f, 0.85f}, {0.2f, 0.3f}, {0.45f, 0.07f}, {1, 0}};
    float half = GLOW_SIZE / 2.0f;
    for (int y = 0; y < GLOW_SIZE; y++) {
        Uint32 *row = (Uint32 *)((Uint8 *)glow->pixels + (size_t)y * (size_t)glow->pitch);
        for (int x = 0; x < GLOW_SIZE; x++) {
            float dx = x + 0.5f - half, dy = y + 0.5f - half;
            float r = sqrtf(dx * dx + dy * dy) / half, a = 0;
            for (int s = 0; s + 1 < 5; s++) {
                if (r >= STOPS[s][0] && r <= STOPS[s + 1][0]) {
                    float t = (r - STOPS[s][0]) / (STOPS[s + 1][0] - STOPS[s][0]);
                    a = STOPS[s][1] + (STOPS[s + 1][1] - STOPS[s][1]) * t;
                }
            }
            row[x] = (Uint32)(a * 255 + 0.5f) << 24 | 0x00FFFFFFu;
        }
    }
    /* the atlas's first shelf, before any glyph */
    draw_atlas_put(0, 0, glow);
    SDL_FreeSurface(glow);
    glow_u0 = 0.5f / ATLAS_W;
    glow_v0 = 0.5f / ATLAS_H;
    glow_u1 = (GLOW_SIZE - 0.5f) / ATLAS_W;
    glow_v1 = (GLOW_SIZE - 0.5f) / ATLAS_H;
    shelf_x = GLOW_SIZE + 1;
    shelf_h = GLOW_SIZE;
    glow_ready = true;
}

/* The screen's size on the display, and where a star at X, Y, Z lands on it. */
static void project(const Star *star, float z, float *x, float *y)
{
    float w = SCREEN_W * scale, h = SCREEN_H * scale;
    float reach = (w > h ? w : h) * 0.5f;
    *x = w / 2 + star->x / z * reach;
    *y = h / 2 + star->y / z * reach;
}

static bool on_screen(float x, float y)
{
    float margin = 40 * scale;
    return x > -margin && x < SCREEN_W * scale + margin && y > -margin && y < SCREEN_H * scale + margin;
}

/* A star placed at random; FAR puts it at the back, where new stars appear as others pass. */
static void spawn(Star *star, bool far)
{
    for (int tries = 0; tries < 8; tries++) {
        star->x = (random_unit() * 2 - 1) * 1.2f;
        star->y = (random_unit() * 2 - 1) * 1.2f;
        star->z = far ? 0.9f + random_unit() * 0.1f : 0.12f + random_unit() * 0.88f;
        float x, y;
        project(star, star->z, &x, &y);
        if (on_screen(x, y)) {
            break;
        }
    }
    float pick = random_unit();
    star->colour = 0;
    for (int i = 0; i < STAR_COLOUR_COUNT; i++) {
        pick -= STAR_WEIGHTS[i];
        if (pick <= 0) {
            star->colour = i;
            break;
        }
    }
    /* most stars are faint points; a long tail of brighter ones */
    float r = random_unit();
    star->size = 0.45f + r * r * 1.9f;
    star->twinkle = 0.4f + random_unit() * 1.2f;
    star->phase = random_unit() * 6.2831853f;
}

static Colour faded(Colour colour, float alpha)
{
    colour.a = (Uint8)(alpha <= 0 ? 0 : alpha >= 1 ? 255 : alpha * 255 + 0.5f);
    return colour;
}

/* The sky's light: a wide radial glow from the top right, as the web's. */
static void sky(void)
{
    float w = SCREEN_W * scale, h = SCREEN_H * scale;
    float cx = w * 0.72f, radius = (w > h ? w : h) * 0.8f;
    Colour centre = {26, 46, 82, 140}, rim = {26, 46, 82, 0};
    for (int i = 0; i < 32; i++) {
        float a0 = 3.14159265f * i / 32, a1 = 3.14159265f * (i + 1) / 32;
        triangle(false, point(cx, 0, centre), point(cx + radius * cosf(a0), radius * sinf(a0), rim),
                 point(cx + radius * cosf(a1), radius * sinf(a1), rim));
    }
}

void ui_background(double warp)
{
    frame++;
    draw_begin(COLOUR_SPACE);
    Uint32 now = SDL_GetTicks();
    if (!stars_placed) {
        for (int i = 0; i < STARS; i++) {
            spawn(&stars[i], false);
        }
        stars_placed = true;
        last_ticks = now;
    }
    /* a long gap counts as one short step, so stars don't jump */
    float dt = (float)(now - last_ticks) / 1000.0f;
    dt = dt > 0.05f ? 0.05f : dt;
    last_ticks = now;
    float target = IDLE_SPEED + (float)(warp < 0 ? 0 : warp > 1 ? 1 : warp) * WARP_SPEED;
    speed += (target - speed) * (dt * 1.6f < 1 ? dt * 1.6f : 1);
    clock_seconds += dt;
    sky();

    /* above the idle drift, stars stretch into streaks as the speed rises */
    float streak = (speed - 0.04f) / 0.5f;
    streak = streak < 0 ? 0 : streak > 1 ? 1 : streak;
    for (int i = 0; i < STARS; i++) {
        Star *star = &stars[i];
        star->shown = false;
        star->z -= speed * dt;
        float x, y;
        project(star, star->z, &x, &y);
        if (star->z <= 0.03f || !on_screen(x, y)) {
            spawn(star, true);
            continue;
        }
        Colour colour = STAR_COLOURS[star->colour];
        float depth = 1 - star->z;
        /* a gentle shimmer: each star's brightness swings between 64% and 100%, slowly */
        float twinkle = 0.82f + 0.18f * sinf(clock_seconds * star->twinkle + star->phase);
        float alpha = (0.4f + depth * 0.8f) * (twinkle + (1 - twinkle) * streak);
        /* a new star fades in from nothing as it leaves the back */
        float arrival = depth / 0.25f;
        alpha *= arrival < 1 ? arrival : 1;
        alpha = alpha > 1 ? 1 : alpha;
        float radius = star->size * (0.45f + depth * 1.3f) * scale;
        if (streak > 0.02f) {
            /* the streak: from where the star was a moment ago, faint, to where it is */
            float tail_z = star->z + speed * 0.09f * streak, tail_x, tail_y;
            project(star, tail_z < 1.2f ? tail_z : 1.2f, &tail_x, &tail_y);
            float wide = radius * 1.1f < scale ? scale : radius * 1.1f;
            segment(tail_x, tail_y, x, y, wide, faded(colour, 0), faded(colour, alpha));
        }
        /* the glow, giving way to the streak as it lengthens: the two cross-fade as the warp starts and ends,
           where a short streak alone shows almost nothing */
        float glow = 1 - streak * 2;
        star->shown = glow > 0;
        star->head_x = x;
        star->head_y = y;
        star->radius = radius;
        star->alpha = alpha * (glow > 0 ? glow : 0);
    }
    /* the glows, from the atlas, in one pass */
    for (int i = 0; glow_ready && i < STARS; i++) {
        const Star *star = &stars[i];
        if (!star->shown) {
            continue;
        }
        float half = star->radius * GLOW_SCALE / 2;
        float x0 = star->head_x - half, y0 = star->head_y - half, x1 = star->head_x + half, y1 = star->head_y + half;
        Colour colour = faded(STAR_COLOURS[star->colour], star->alpha);
        triangle(true, (DrawVertex){x0, y0, glow_u0, glow_v0, colour}, (DrawVertex){x1, y0, glow_u1, glow_v0, colour},
                 (DrawVertex){x1, y1, glow_u1, glow_v1, colour});
        triangle(true, (DrawVertex){x0, y0, glow_u0, glow_v0, colour}, (DrawVertex){x1, y1, glow_u1, glow_v1, colour},
                 (DrawVertex){x0, y1, glow_u0, glow_v1, colour});
    }
}

void ui_finish(void)
{
    flush();
    warm_glyphs();
}

void ui_panel(int x, int y, int w, int h, int cut, Colour colour, Colour edge)
{
    if (cut * 2 > h) {
        cut = h / 2;
    }
    if (cut <= 0) {
        fill((float)x, (float)y, (float)w, (float)h, colour);
        if (edge.a > 0) {
            float corners[4][2] = {{at(x), at(y)}, {at(x + w), at(y)}, {at(x + w), at(y + h)}, {at(x), at(y + h)}};
            outline((const float(*)[2])corners, 4, hairline(), edge);
        }
        return;
    }
    /* two corners cut: the top left and the bottom right */
    float corners[6][2] = {{at(x + cut), at(y)},     {at(x + w), at(y)},       {at(x + w), at(y + h - cut)},
                           {at(x + w - cut), at(y + h)}, {at(x), at(y + h)}, {at(x), at(y + cut)}};
    if (colour.a > 0) {
        polygon((const float(*)[2])corners, 6, colour);
    }
    if (edge.a > 0) {
        outline((const float(*)[2])corners, 6, hairline(), edge);
    }
}

int ui_text(int font, int x, int y, Colour colour, const char *text)
{
    if (text == NULL || text[0] == 0) {
        return 0;
    }
    int width = lay_out(font, at(x), at(y), colour, text, true);
    return (int)ceilf((float)width / scale);
}

int ui_text_width(int font, const char *text)
{
    if (text == NULL || text[0] == 0) {
        return 0;
    }
    return (int)ceilf((float)lay_out(font, 0, 0, COLOUR_INK, text, false) / scale);
}

int ui_line_height(int font)
{
    return (int)roundf((float)TTF_FontLineSkip(fonts[font]) / scale);
}

int ui_wrap(int font, int x, int y, int width, Colour colour, const char *text)
{
    int line_height = ui_line_height(font);
    int top = y;
    const char *start = text;
    char line[512];
    while (start != NULL && *start != 0) {
        /* the longest run of whole words from START that fits WIDTH */
        size_t fitting = 0;
        size_t next_at = 0;
        for (;;) {
            size_t next = next_at;
            while (start[next] == ' ') {
                next++;
            }
            while (start[next] != 0 && start[next] != ' ') {
                next++;
            }
            if (next == next_at || next >= sizeof line) {
                break;
            }
            snprintf(line, sizeof line, "%.*s", (int)next, start);
            if (fitting > 0 && ui_text_width(font, line) > width) {
                break;
            }
            fitting = next;
            next_at = next;
            if (start[next_at] == 0) {
                break;
            }
        }
        if (fitting == 0) {
            break;
        }
        snprintf(line, sizeof line, "%.*s", (int)fitting, start);
        ui_text(font, x, y, colour, line);
        y += line_height;
        start += fitting;
        while (*start == ' ') {
            start++;
        }
    }
    return y - top;
}

void ui_bar(int x, int y, int w, int h, double fraction, Colour fill_colour)
{
    if (fraction < 0) {
        fraction = 0;
    } else if (fraction > 1) {
        fraction = 1;
    }
    ui_panel(x, y, w, h, h / 2, COLOUR_EDGE_SOFT, (Colour){0, 0, 0, 0});
    int filled = (int)(w * fraction + 0.5);
    if (filled > h / 2) {
        ui_panel(x, y, filled, h, h / 2, fill_colour, (Colour){0, 0, 0, 0});
    }
}

/* ---- The controller's symbols ---- */

#define ROUND 48                /* a circle's sides */

/* A filled circle, centre and radius in display pixels. */
static void disc(float cx, float cy, float radius, Colour colour)
{
    for (int i = 0; i < ROUND; i++) {
        float a0 = 6.2831853f * i / ROUND, a1 = 6.2831853f * (i + 1) / ROUND;
        triangle(false, point(cx, cy, colour), point(cx + radius * cosf(a0), cy + radius * sinf(a0), colour),
                 point(cx + radius * cosf(a1), cy + radius * sinf(a1), colour));
    }
}

/* A circle's outline, THICK display pixels wide inside RADIUS. */
static void ring(float cx, float cy, float radius, float thick, Colour colour)
{
    float inner = radius - thick;
    for (int i = 0; i < ROUND; i++) {
        float a0 = 6.2831853f * i / ROUND, a1 = 6.2831853f * (i + 1) / ROUND;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        quad(cx + radius * c0, cy + radius * s0, cx + radius * c1, cy + radius * s1, cx + inner * c1,
             cy + inner * s1, cx + inner * c0, cy + inner * s0, colour, colour, colour, colour);
    }
}

/* A stroke of a symbol, in screen pixels, three wide, its ends squared off past the points. */
static void stroke(float x1, float y1, float x2, float y2, Colour colour)
{
    float dx = x2 - x1, dy = y2 - y1, length = sqrtf(dx * dx + dy * dy);
    float ex = length > 0 ? dx / length * 1.5f : 0, ey = length > 0 ? dy / length * 1.5f : 0;
    segment((x1 - ex) * scale, (y1 - ey) * scale, (x2 + ex) * scale, (y2 + ey) * scale, 3 * scale, colour, colour);
}

void ui_glyph(int x, int y, int size, int glyph)
{
    float cx = x * scale, cy = y * scale, radius = size / 2.0f * scale;
    disc(cx, cy, radius - scale, COLOUR_EDGE_SOFT);
    ring(cx, cy, radius, 2 * scale, COLOUR_EDGE);
    float s = size * 3 / 10.0f;
    switch (glyph) {
    case GLYPH_CROSS: {
        const Colour blue = {0x7C, 0xB2, 0xE8, 255};
        stroke(x - s, y - s, x + s, y + s, blue);
        stroke(x - s, y + s, x + s, y - s, blue);
        break;
    }
    case GLYPH_CIRCLE:
        ring(cx, cy, s * scale + 1.5f * scale, 3 * scale, (Colour){0xFF, 0x6B, 0x6B, 255});
        break;
    case GLYPH_TRIANGLE: {
        const Colour green = {0x40, 0xE0, 0xA0, 255};
        float top = y - s, bottom = y + s * 3 / 4;
        stroke((float)x, top, x - s, bottom, green);
        stroke(x - s, bottom, x + s, bottom, green);
        stroke(x + s, bottom, (float)x, top, green);
        break;
    }
    default: {
        const Colour pink = {0xF0, 0x8C, 0xD8, 255};
        stroke(x - s, y - s, x + s, y - s, pink);
        stroke(x + s, y - s, x + s, y + s, pink);
        stroke(x + s, y + s, x - s, y + s, pink);
        stroke(x - s, y + s, x - s, y - s, pink);
        break;
    }
    }
}

int ui_prompt(int x, int y, int glyph, const char *label)
{
    int height = ui_line_height(FONT_BODY);
    int size = 30;
    ui_glyph(x + size / 2, y + height / 2, size, glyph);
    return size + 10 + ui_text(FONT_BODY, x + size + 10, y, COLOUR_INK_DIM, label);
}
