/*
 * Drawing on the PS3: the RSX, through tiny3d, at the TV's own mode (720p or 1080p). tiny3d's 2D mode is set to
 * display pixels (a viewport scale of 1), and its blending to "source over": each triangle's alpha mixes it with
 * what is under it. Text is the glyph atlas, a texture in the RSX's memory that the PPU writes glyphs into.
 */
#include "draw.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <tiny3d.h>

/* Waits until the flip last sent has happened, and with it the frame's drawing: gcmGetFlipStatus is 0 then.
   Not tiny3d's waitFlip, which also resets the status for the flip after, so tiny3d_Flip's own call would then
   wait for a flip not yet sent until it gives up (2 s); nor tiny3d_WaitRSX, newer than the toolchain's library.
   At most 100 ms, should the flip never come. */
static void flip_shown(void)
{
    for (int tries = 0; tries < 500 && gcmGetFlipStatus() != 0; tries++) {
        usleep(200);
    }
}

static u32 *atlas;
static u32 atlas_offset;
static int atlas_width, atlas_height;

bool draw_open(int *width, int *height, char *error, size_t capacity)
{
    /* the vertices of a frame: a few thousand at most, 36 bytes each with a texture */
    if (tiny3d_Init(4 * 1024 * 1024) < 0) {
        snprintf(error, capacity, "the RSX could not be started");
        return false;
    }
    *width = Video_Resolution.width;
    *height = Video_Resolution.height;
    tiny3d_UserViewport(1, 0.0f, 0.0f, 1.0f, 1.0f, (float)*width / 1920.0f, (float)*height / 1080.0f);
    return true;
}

void draw_close(void)
{
    flip_shown();
}

bool draw_atlas_open(int width, int height)
{
    atlas = tiny3d_AllocTexture((u32)(width * height * 4));
    if (atlas == NULL) {
        return false;
    }
    memset(atlas, 0, (size_t)width * (size_t)height * 4);
    atlas_offset = tiny3d_TextureOffset(atlas);
    atlas_width = width;
    atlas_height = height;
    return true;
}

void draw_atlas_put(int x, int y, const SDL_Surface *glyph)
{
    /* ARGB8888 in a big-endian word is the RSX's A8R8G8B8: rows copy as they are */
    for (int row = 0; row < glyph->h && y + row < atlas_height; row++) {
        int width = x + glyph->w <= atlas_width ? glyph->w : atlas_width - x;
        memcpy(atlas + (size_t)(y + row) * (size_t)atlas_width + (size_t)x,
               (const Uint8 *)glyph->pixels + (size_t)row * (size_t)glyph->pitch, (size_t)width * 4);
    }
}

void draw_begin(Colour clear)
{
    tiny3d_Clear(0xFF000000u | (u32)clear.r << 16 | (u32)clear.g << 8 | clear.b, TINY3D_CLEAR_ALL);
    tiny3d_AlphaTest(0, 0, TINY3D_ALPHA_FUNC_ALWAYS);
    tiny3d_BlendFunc(1, (blend_src_func)(TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA),
                     (blend_dst_func)(TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA |
                                      TINY3D_BLEND_FUNC_DST_ALPHA_ONE_MINUS_SRC_ALPHA),
                     (blend_func)(TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD));
    tiny3d_Project2D();
}

static u32 rgba(Colour colour)
{
    return (u32)colour.r << 24 | (u32)colour.g << 16 | (u32)colour.b << 8 | colour.a;
}

/* Every triangle at the same depth: tiny3d tests depth "less or equal", so each is drawn over what came before. */
void draw_shapes(const DrawVertex *vertices, int count)
{
    if (count < 3) {
        return;
    }
    tiny3d_SetPolygon(TINY3D_TRIANGLES);
    for (int i = 0; i < count; i++) {
        tiny3d_VertexPos(vertices[i].x, vertices[i].y, 0.0f);
        tiny3d_VertexColor(rgba(vertices[i].colour));
    }
    tiny3d_End();
}

void draw_text(const DrawVertex *vertices, int count)
{
    if (count < 3 || atlas == NULL) {
        return;
    }
    /* glyphs sit on whole pixels at their own size, so the nearest texel is the exact one */
    tiny3d_SetTexture(0, atlas_offset, (u32)atlas_width, (u32)atlas_height, (u32)atlas_width * 4,
                      TINY3D_TEX_FORMAT_A8R8G8B8, 0);
    tiny3d_SetPolygon(TINY3D_TRIANGLES);
    for (int i = 0; i < count; i++) {
        tiny3d_VertexPos(vertices[i].x, vertices[i].y, 0.0f);
        tiny3d_VertexColor(rgba(vertices[i].colour));
        tiny3d_VertexTexture(vertices[i].u, vertices[i].v);
    }
    tiny3d_End();
}

static SDL_Surface *screenshot(void)
{
    /* tiny3d_Flip showed the buffer drawn into and moved on to the other one; draw_end waited for the flip, so
       the frame is all there */
    int width = Video_Resolution.width, height = Video_Resolution.height;
    const u32 *shown = Video_buffer[!Video_currentBuffer];
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == NULL) {
        return NULL;
    }
    for (int row = 0; row < height; row++) {
        u32 *into = (u32 *)((Uint8 *)surface->pixels + (size_t)row * (size_t)surface->pitch);
        const u32 *from = (const u32 *)((const Uint8 *)shown + (size_t)row * (size_t)Video_pitch);
        for (int x = 0; x < width; x++) {
            into[x] = from[x] | 0xFF000000u;
        }
    }
    return surface;
}

SDL_Surface *draw_end(bool want_screenshot)
{
    tiny3d_Flip();
    /* tiny3d_Flip sends the flip and starts its vertex buffer over at once; the RSX may still be drawing this
       frame from it, and the next frame's vertices would overwrite the ones it has yet to read (on a console,
       full-screen flashes of garbage). So the next frame waits until this one is on the screen. */
    flip_shown();
    return want_screenshot ? screenshot() : NULL;
}
