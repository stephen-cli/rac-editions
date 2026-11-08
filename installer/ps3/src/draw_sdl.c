/*
 * Drawing in the preview: SDL's renderer in a window the size of a TV's mode, 1280x720 unless RAC_PS3_DISPLAY says
 * otherwise ("1920x1080"), so the PS3's scaling can be seen on a computer. Triangles go through
 * SDL_RenderGeometry; the glyph atlas is a texture.
 */
#include "draw.h"

#include <stdio.h>
#include <stdlib.h>

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *atlas;

bool draw_open(int *width, int *height, char *error, size_t capacity)
{
    *width = 1280;
    *height = 720;
    const char *display = getenv("RAC_PS3_DISPLAY");
    if (display != NULL && sscanf(display, "%dx%d", width, height) != 2) {
        *width = 1280;
        *height = 720;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        snprintf(error, capacity, "video: %s", SDL_GetError());
        return false;
    }
    window = SDL_CreateWindow("Editions Builder (PS3 preview)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              *width, *height, 0);
    renderer = window != NULL ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (renderer == NULL) {
        snprintf(error, capacity, "video: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    return true;
}

void draw_close(void)
{
    if (atlas != NULL) {
        SDL_DestroyTexture(atlas);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

bool draw_atlas_open(int width, int height)
{
    atlas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
    if (atlas == NULL) {
        return false;
    }
    SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(atlas, SDL_ScaleModeNearest);
    void *clear = calloc((size_t)width * (size_t)height, 4);
    if (clear != NULL) {
        SDL_UpdateTexture(atlas, NULL, clear, width * 4);
        free(clear);
    }
    return true;
}

void draw_atlas_put(int x, int y, const SDL_Surface *glyph)
{
    SDL_Rect where = {x, y, glyph->w, glyph->h};
    SDL_UpdateTexture(atlas, &where, glyph->pixels, glyph->pitch);
}

void draw_begin(Colour clear)
{
    SDL_SetRenderDrawColor(renderer, clear.r, clear.g, clear.b, 255);
    SDL_RenderClear(renderer);
}

static void geometry(SDL_Texture *texture, const DrawVertex *vertices, int count)
{
    SDL_Vertex converted[1024];
    for (int start = 0; start < count; start += 1023) {
        int part = count - start < 1023 ? count - start : 1023;
        for (int i = 0; i < part; i++) {
            const DrawVertex *from = &vertices[start + i];
            converted[i].position = (SDL_FPoint){from->x, from->y};
            converted[i].color = (SDL_Color){from->colour.r, from->colour.g, from->colour.b, from->colour.a};
            converted[i].tex_coord = (SDL_FPoint){from->u, from->v};
        }
        SDL_RenderGeometry(renderer, texture, converted, part, NULL, 0);
    }
}

void draw_shapes(const DrawVertex *vertices, int count)
{
    geometry(NULL, vertices, count);
}

void draw_text(const DrawVertex *vertices, int count)
{
    geometry(atlas, vertices, count);
}

/* The frame, read before it is shown: after that the renderer's buffer is undefined. */
static SDL_Surface *screenshot(void)
{
    int width, height;
    SDL_GetRendererOutputSize(renderer, &width, &height);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface != NULL &&
        SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) != 0) {
        SDL_FreeSurface(surface);
        return NULL;
    }
    return surface;
}

SDL_Surface *draw_end(bool want_screenshot)
{
    SDL_Surface *shot = want_screenshot ? screenshot() : NULL;
    SDL_RenderPresent(renderer);
    return shot;
}
