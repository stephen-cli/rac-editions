/*
 * Draws the PS3 builder's XMB icon, ICON0.PNG (320x176), in the builder's look: the desktop app's icon (a bolt-gold
 * hexagonal nut on a chamfered plate) beside the builder's name, over deep space.
 *
 *   ps3-icon FONTS_FOLDER OUT.png
 */
#include <math.h>
#include <stdio.h>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#define W 320
#define H 176

static void colour(SDL_Renderer *renderer, Uint32 rgb, Uint8 alpha)
{
    SDL_SetRenderDrawColor(renderer, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, alpha);
}

/* A line THICK pixels wide, as a row of discs along it. */
static void thick_line(SDL_Renderer *renderer, double x1, double y1, double x2, double y2, double thick)
{
    double length = hypot(x2 - x1, y2 - y1);
    for (double t = 0; t <= length; t += 0.5) {
        double x = x1 + (x2 - x1) * t / length, y = y1 + (y2 - y1) * t / length;
        for (double dy = -thick / 2; dy <= thick / 2; dy += 0.5) {
            double half = sqrt(thick * thick / 4 - dy * dy);
            SDL_RenderDrawLine(renderer, (int)(x - half), (int)(y + dy), (int)(x + half), (int)(y + dy));
        }
    }
}

static void text(SDL_Renderer *renderer, TTF_Font *font, int x, int y, SDL_Color ink, const char *string)
{
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, string, ink);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect where = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &where);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: ps3-icon FONTS_FOLDER OUT.png\n");
        return 2;
    }
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    SDL_Surface *canvas = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(canvas);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    /* deep space with a few stars */
    colour(renderer, 0x070B13, 255);
    SDL_RenderClear(renderer);
    Uint32 seed = 7;
    for (int i = 0; i < 60; i++) {
        seed = seed * 1664525u + 1013904223u;
        int x = (int)(seed >> 8) % W;
        seed = seed * 1664525u + 1013904223u;
        int y = (int)(seed >> 8) % H;
        Uint8 light = (Uint8)(110 + (seed >> 26) * 2);
        SDL_SetRenderDrawColor(renderer, light, light, (Uint8)(light + 15), 255);
        SDL_RenderDrawPoint(renderer, x, y);
    }

    /* the plate: two corners cut, as the desktop icon's */
    int px = 14, py = 18, size = 140, cut = 18;
    colour(renderer, 0x0E141F, 255);
    for (int row = 0; row < size; row++) {
        int left = cut - row > 0 ? cut - row : 0;
        int right = row - (size - 1 - cut) > 0 ? row - (size - 1 - cut) : 0;
        SDL_RenderDrawLine(renderer, px + left, py + row, px + size - 1 - right, py + row);
    }
    colour(renderer, 0x34425A, 255);
    SDL_Point outline[] = {{px + cut, py}, {px + size - 1, py}, {px + size - 1, py + size - 1 - cut},
                           {px + size - 1 - cut, py + size - 1}, {px, py + size - 1}, {px, py + cut}, {px + cut, py}};
    SDL_RenderDrawLines(renderer, outline, 7);

    /* the nut: a hexagon and its hole, in bolt gold */
    double cx = px + size / 2.0, cy = py + size / 2.0, radius = size * 0.30;
    colour(renderer, 0xF2B53A, 255);
    for (int i = 0; i < 6; i++) {
        double a1 = -M_PI / 2 + i * M_PI / 3, a2 = -M_PI / 2 + (i + 1) * M_PI / 3;
        thick_line(renderer, cx + radius * cos(a1), cy + radius * sin(a1), cx + radius * cos(a2),
                   cy + radius * sin(a2), 8);
    }
    for (int i = 0; i < 72; i++) {
        double a1 = i * M_PI / 36, a2 = (i + 1) * M_PI / 36, r = radius * 0.36;
        thick_line(renderer, cx + r * cos(a1), cy + r * sin(a1), cx + r * cos(a2), cy + r * sin(a2), 8);
    }

    /* the name */
    char path[600];
    snprintf(path, sizeof path, "%s/ChakraPetch-Bold.ttf", argv[1]);
    TTF_Font *display = TTF_OpenFont(path, 27);
    snprintf(path, sizeof path, "%s/ChakraPetch-SemiBold.ttf", argv[1]);
    TTF_Font *label = TTF_OpenFont(path, 14);
    if (display == NULL || label == NULL) {
        fprintf(stderr, "ps3-icon: %s\n", TTF_GetError());
        return 1;
    }
    text(renderer, label, 168, 50, (SDL_Color){0x3F, 0xD7, 0xE6, 255}, "RATCHET & CLANK");
    text(renderer, display, 168, 66, (SDL_Color){0xE6, 0xED, 0xF5, 255}, "Editions");
    text(renderer, display, 168, 96, (SDL_Color){0xE6, 0xED, 0xF5, 255}, "Builder");

    SDL_RenderPresent(renderer);
    int saved = IMG_SavePNG(canvas, argv[2]);
    if (saved != 0) {
        fprintf(stderr, "ps3-icon: %s\n", IMG_GetError());
    }
    TTF_CloseFont(display);
    TTF_CloseFont(label);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(canvas);
    TTF_Quit();
    SDL_Quit();
    return saved == 0 ? 0 : 1;
}
