/* q4: 16:9 (widescreen), a setting on the Camera page.
 *
 * A PS2 game shows 16:9 anamorphically: it draws the same 512-pixel-wide
 * frame and the television (or PCSX2 set to 16:9) stretches it a third
 * wider. So in 16:9 mode the 3D view must show a third more to the sides
 * (each frame pixel will be shown 4/3 wide), and everything 2D must be
 * drawn 3/4 as wide about the centre, or it looks stretched.
 *
 * The view (Hor+). UpdateViewContext (func_001F3140) builds every
 * projection and frustum constant from the view record's horizontal field
 * of view (+0xB0, the game's "projection scale": 0.63 in gameplay) and a
 * vertical one derived from it (+0xB4 = +0xB0 * 0.756 on PAL, 0.775 at
 * 60 Hz). Every level's copy is entered through ee_view_hook
 * (src/ws_hooks.s): for the full-screen view (not a render to a texture,
 * whose size differs from the game's screen size) the hook hands the
 * function +0xB0 * 4/3 and makes the vertical value come from the original
 * (the function's read of +0xB0 at +0x68 is patched to read +0xBC, which
 * the hook fills and the function overwrites later), then puts +0xB0 back,
 * so no other code ever sees the widened value and repeated calls never
 * compound. The culling constants are built from the same values, so the
 * frustum widens with the picture. Going Commando's own 16:9 (SCUS_972.68
 * 0x27ABC0) multiplies the vertical value by 0.75 instead, which crops the
 * top and bottom of the picture: the edition departs from it on purpose
 * (owner's request).
 *
 * 2D. The HUD, help, subtitle and menu routines append DMA-tagged GIF
 * packets to the VIF1 chain through its cursor (D_00161000), x in 12.4
 * fixed point around 2048 (the screen's centre). A draw is bracketed: the
 * cursor before and after it gives the packets it wrote, and every vertex
 * x (XYZ2/XYZF2/XYZ3/XYZF3, packed, register list or A+D) and every
 * scissor is pulled toward the centre by 3/4. A GIF tag whose vertices
 * span the whole screen (fades, tints, bars) is left as it is. HUD
 * elements anchored to the screen's left or right (HUD slot flags +0x60:
 * 4 left, 8 right; x at +0x50) are first moved out by a third of the half
 * width, so that they end up as far from the 16:9 picture's edges as they
 * were from the 4:3 one's (the native port's rule, port/game/rac1-pal/
 * host/hud.c).
 *
 * In-level cutscenes. The game letterboxes them: the bars grow to 24 lines
 * each over 24 frames (D_0015F548, while D_0015F544 asks for them). In
 * 16:9 the bars are not drawn; the view zooms in instead until the
 * original picture between the bars fills the screen's height (factor
 * H / (H - 48)), the horizontal field narrows back to the original
 * framing, and black pillars cover what lies beyond it at the sides (the
 * owner's choice: nothing outside the original framing is shown).
 *
 * The frontend (title, main menu, loading a save) is not patched: it stays
 * 4:3, as the owner chose; a save with the setting on comes up in 16:9. */
#include "modseg.h"

DECLARE_LVL(func_001F3140);        /* UpdateViewContext */
DECLARE_LVL(func_001F3D78);        /* DrawWorld: the hooked calls are in it */
DECLARE_LVL(func_001FFFB8);        /* HudDraw */
DECLARE_LVL(func_001FF1B0);        /* draw_help */
DECLARE_LVL(func_001F5148);        /* the letterbox bars */
DECLARE_LVL(func_001F4F90);        /* the subtitles */
DECLARE_LVL(func_L00_002A1540);    /* the vendor screen's render */
DECLARE_LVL_DATA(view);
DECLARE_LVL_DATA(chain_cursor);
DECLARE_LVL_DATA(letterbox_lines);
DECLARE_LVL_DATA(game_mode);
DECLARE_LVL_DATA(hud_slots);

typedef void (*VoidFn)(void);

#define WS_SETTING      (*(volatile u8 *)EE_SETTING_WIDESCREEN)
/* The GS viewport record (core): width, height, half width, half height,
 * then the screen's left, top, right and bottom edges in 12.4 around 2048. */
#define SCREEN          ((volatile s32 *)0x0013E600)
/* The game's screen size, from the resolution table InitViewContext reads
 * (D_00151880 halfwords 0xA8/0xA9). */
#define RES_W           (*(volatile s16 *)0x001519D0)
#define RES_H           (*(volatile s16 *)0x001519D2)

#define VIEW_FOV        0xB0       /* horizontal "projection scale" */
#define VIEW_FOV_SRC    0xBC       /* fov.f[3]: the patched read's source for the vertical value */
#define HUD_SLOTS       13
#define HUD_SLOT_BYTES  0x90
#define HUD_SLOT_X      0x50
#define HUD_SLOT_FLAGS  0x60
#define HUD_LEFT        4
#define HUD_RIGHT       8
#define HUD_SLOT_QUICK_SELECT 3    /* the quick-select ring (src/quick_select_pause.c) */
#define LETTERBOX_MAX   24         /* lines each bar grows to */

/* Whether the frame being drawn is in 16:9: the setting, and the screen at
 * the game's own size (a render to a texture sets its own). */
static s32 ws_on(void)
{
    return WS_SETTING != 0 && SCREEN[0] == RES_W && SCREEN[1] == RES_H;
}

/* The letterbox blend, 0 without bars to 1 with the bars fully in. */
static f32 cutscene_t(s32 L)
{
    s32 lines = *LVL_DATA(L, letterbox_lines, volatile s32 *);
    if (lines <= 0) {
        return 0.0f;
    }
    if (lines >= LETTERBOX_MAX) {
        return 1.0f;
    }
    return (f32)lines / (f32)LETTERBOX_MAX;
}

/* The vertical zoom that makes the picture between full bars fill the screen. */
static f32 cutscene_zoom(f32 t)
{
    f32 h = (f32)SCREEN[1];
    return 1.0f + t * (h / (h - 2.0f * LETTERBOX_MAX) - 1.0f);
}

/* How the frame is framed: the vertical zoom, and the share of the screen's
 * width the picture keeps (black pillars cover the rest). Square pixels on
 * the stretched screen fix the horizontal field at 4/3 of the original over
 * the zoom; what changes is how much of it is shown. In play all of it
 * (Hor+). In a cinematic (game mode 2) or with the letterbox in, the
 * original framing only: the original horizontal field, which is 3/4 of
 * the zoomed width. The letterbox blend carries the change while the bars
 * grow; a cinematic without bars (a level's arrival) cuts to it. */
#define GAME_MODE_CINEMATIC 2      /* a cinematic (the mode the subtitles are drawn in) */
#define GAME_MODE_ARRIVAL   6      /* a level's arrival or departure flight (set by the level's main loop) */
static f32 framing(s32 L, f32 *content)
{
    f32 t = cutscene_t(L);
    f32 zoom = cutscene_zoom(t);
    s32 mode = *LVL_DATA(L, game_mode, volatile s32 *);
    f32 original = (mode == GAME_MODE_CINEMATIC || mode == GAME_MODE_ARRIVAL) ? 1.0f : t;
    f32 c = zoom * (1.0f - 0.25f * original);
    *content = c < 1.0f ? c : 1.0f;
    return zoom;
}

/* --- the view ------------------------------------------------------------ */

static s32 view_level;
static s32 view_widened;
static f32 view_saved;
static s32 view_suspended;      /* the vendor's monitor screens are drawn with the 4:3 view (below) */

u32 ee_view_pre_c(s32 L)
{
    volatile f32 *fov = (volatile f32 *)(LVL_DATA(L, view, u32) + VIEW_FOV);
    volatile f32 *src = (volatile f32 *)(LVL_DATA(L, view, u32) + VIEW_FOV_SRC);
    f32 orig = *fov;

    view_level = L;
    view_widened = 0;
    *src = orig;
    if (ws_on() && !view_suspended) {
        f32 content;
        f32 zoom = framing(L, &content);
        /* Square pixels on the stretched screen: the horizontal scale is 3/4
         * of the 4:3 one at the same vertical scale, so the horizontal field
         * is 4/3 of the original divided by the zoom. */
        *src = orig / zoom;
        *fov = orig * (4.0f / 3.0f) / zoom;
        view_saved = orig;
        view_widened = 1;
    }
    return LVL(L, func_001F3140, u32) + 8;
}

void ee_view_post_c(void)
{
    if (view_widened) {
        *(volatile f32 *)(LVL_DATA(view_level, view, u32) + VIEW_FOV) = view_saved;
        view_widened = 0;
    }
}

/* --- squeezing 2D packets ------------------------------------------------ */

#define GS_CENTRE   0x8000         /* 2048 in 12.4 */

static s32 squeeze_x(s32 x)
{
    return GS_CENTRE + ((x - GS_CENTRE) * 3) / 4;
}

static void squeeze_word(volatile u32 *w)
{
    u32 v = *w;
    *w = (v & 0xFFFF0000u) | ((u32)squeeze_x((s32)(v & 0xFFFF)) & 0xFFFF);
}

/* SCISSOR_1/2 in pixels of the frame: SCAX0 bits 0-10, SCAX1 bits 16-26. */
static void squeeze_scissor(volatile u32 *w)
{
    u32 v = *w;
    s32 hw = SCREEN[2];
    s32 x0 = v & 0x7FF, x1 = (v >> 16) & 0x7FF;
    if (x0 <= 0 && x1 >= SCREEN[0] - 1) {
        return;                                   /* the whole screen */
    }
    x0 = hw + ((x0 - hw) * 3) / 4;
    x1 = hw + ((x1 - hw) * 3 + 3) / 4;
    *w = (v & 0xF800F800u) | (u32)(x0 & 0x7FF) | ((u32)(x1 & 0x7FF) << 16);
}

static s32 is_xyz(u32 reg)
{
    return reg == 0x4 || reg == 0x5 || reg == 0xC || reg == 0xD;
}

static u32 tag_reg(u32 r0, u32 r1, s32 i)
{
    return i < 8 ? (r0 >> (4 * i)) & 0xF : (r1 >> (4 * (i - 8))) & 0xF;
}

/* A GIF tag's vertices span the screen (a fade, a tint, a bar): leave it. */
static s32 full_width(s32 minx, s32 maxx)
{
    return minx <= SCREEN[4] + 16 && maxx >= SCREEN[6] - 16;
}

/* The GIF packets in N quadwords at Q. */
static void gif_squeeze(volatile u32 *q, s32 n)
{
    while (n > 0) {
        u32 t0 = q[0], t1 = q[1], r0 = q[2], r1 = q[3];
        s32 nloop = t0 & 0x7FFF;
        s32 flg = (t1 >> 26) & 3;
        s32 nreg = (t1 >> 28) & 0xF;
        s32 pass, k;
        if (nreg == 0) {
            nreg = 16;
        }
        q += 4;
        n--;
        if (flg == 0) {                                   /* PACKED: a quadword a register */
            s32 total = nloop * nreg;
            s32 minx = 0x10000, maxx = -1, keep = 0;
            if (total > n) {
                return;
            }
            for (pass = 0; pass < 2; pass++) {
                for (k = 0; k < total; k++) {
                    volatile u32 *d = q + 4 * k;
                    u32 reg = tag_reg(r0, r1, k % nreg);
                    u32 addr = d[2] & 0xFF;
                    s32 xyz = is_xyz(reg) || (reg == 0xE && is_xyz(addr));
                    if (pass == 0) {
                        if (xyz) {
                            s32 x = d[0] & 0xFFFF;
                            minx = x < minx ? x : minx;
                            maxx = x > maxx ? x : maxx;
                        }
                    } else if (xyz) {
                        if (!keep) {
                            squeeze_word(d);
                        }
                    } else if (reg == 0xE && (addr == 0x40 || addr == 0x41)) {
                        squeeze_scissor(d);              /* a text window's clip, with or without vertices */
                    }
                }
                keep = maxx >= 0 && full_width(minx, maxx);   /* the whole screen: leave its vertices */
            }
            q += 4 * total;
            n -= total;
        } else if (flg == 1) {                            /* REGLIST: a doubleword a register */
            s32 total = nloop * nreg;
            s32 qw = (total + 1) / 2;
            s32 minx = 0x10000, maxx = -1;
            if (qw > n) {
                return;
            }
            for (pass = 0; pass < 2; pass++) {
                for (k = 0; k < total; k++) {
                    volatile u32 *d = q + 2 * k;
                    if (!is_xyz(tag_reg(r0, r1, k % nreg))) {
                        continue;
                    }
                    if (pass == 0) {
                        s32 x = d[0] & 0xFFFF;
                        minx = x < minx ? x : minx;
                        maxx = x > maxx ? x : maxx;
                    } else {
                        squeeze_word(d);
                    }
                }
                if (pass == 0 && (maxx < 0 || full_width(minx, maxx))) {
                    break;
                }
            }
            q += 4 * qw;
            n -= qw;
        } else {                                          /* IMAGE: texture data */
            if (nloop > n) {
                return;
            }
            q += 4 * nloop;
            n -= nloop;
        }
    }
}

/* The VIF codes of a DMA tag (its upper half) and of its data: COUNT words
 * from W, which sits 8 bytes into the tag's quadword. */
static void vif_squeeze(volatile u32 *w, s32 count)
{
    s32 i = 0;
    while (i < count) {
        u32 code = w[i++];
        u32 cmd = (code >> 24) & 0x7F;
        u32 num = (code >> 16) & 0xFF;
        if (cmd == 0x50 || cmd == 0x51) {                 /* DIRECT, DIRECTHL */
            s32 qw = (code & 0xFFFF) ? (s32)(code & 0xFFFF) : 0x10000;
            while ((i + 2) & 3) {
                i++;                                      /* the data starts on a quadword */
            }
            if (i + 4 * qw > count) {
                return;
            }
            gif_squeeze(w + i, qw);
            i += 4 * qw;
        } else if (cmd == 0x20) {                         /* STMASK */
            i += 1;
        } else if (cmd == 0x30 || cmd == 0x31) {          /* STROW, STCOL */
            i += 4;
        } else if (cmd == 0x4A) {                         /* MPG */
            i = (i + 1) & ~1;
            i += 2 * (num ? (s32)num : 256);
        } else if (cmd >= 0x60) {                         /* UNPACK */
            s32 vn = (cmd >> 2) & 3, vl = cmd & 3;
            s32 bits = (32 >> vl) * (vn + 1) * (num ? (s32)num : 256);
            i += (bits + 31) / 32;
        }
    }
}

/* Every 2D vertex the chain received between START and END. */
static void chain_squeeze(volatile u32 *p, volatile u32 *end)
{
    while (p < end) {
        u32 tag = p[0];
        s32 qwc = tag & 0xFFFF;
        u32 id = (tag >> 28) & 7;
        if (id == 1 || id == 2) {                         /* CNT, NEXT: the data follows the tag */
            vif_squeeze(p + 2, 2 + 4 * qwc);
            if (id == 2) {
                return;                                   /* the chain goes on elsewhere */
            }
            p += 4 + 4 * qwc;
        } else if (id == 3 || id == 4) {                  /* REF, REFS: the data is elsewhere (shared, never ours) */
            p += 4;
        } else {
            return;
        }
    }
}

static volatile u32 **cursor_of(s32 L)
{
    return LVL_DATA(L, chain_cursor, volatile u32 **);
}

/* --- the HUD, help, subtitles ---------------------------------------------- */

#define HUD_SHIFT (SCREEN[2] / 3)      /* a third of the half width: 85 of 256 */

/* Move the anchored HUD slots out (by MOVED, which this fills) or back. */
static void hud_shift(s32 L, s32 *moved)
{
    u32 slots = LVL_DATA(L, hud_slots, u32);
    s32 i;
    for (i = 0; i < HUD_SLOTS; i++) {
        volatile s32 *slot = (volatile s32 *)(slots + i * HUD_SLOT_BYTES);
        s32 flags = slot[HUD_SLOT_FLAGS / 4];
        moved[i] = (flags & HUD_LEFT) ? -HUD_SHIFT : (flags & HUD_RIGHT) ? HUD_SHIFT : 0;
        if (i == HUD_SLOT_QUICK_SELECT) {
            moved[i] /= 2;            /* the ring: halfway out (owner left it to us: "maybe an in-between") */
        }
        slot[HUD_SLOT_X / 4] += moved[i];
    }
}

static void hud_unshift(s32 L, const s32 *moved)
{
    u32 slots = LVL_DATA(L, hud_slots, u32);
    s32 i;
    for (i = 0; i < HUD_SLOTS; i++) {
        ((volatile s32 *)(slots + i * HUD_SLOT_BYTES))[HUD_SLOT_X / 4] -= moved[i];
    }
}

void ee_ws_hud_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_001F3D78, EE_SITE_WS_HUD);
    s32 moved[HUD_SLOTS];
    volatile u32 *start;

    if (!ws_on()) {
        LVL(L, func_001FFFB8, VoidFn)();
        return;
    }
    hud_shift(L, moved);
    start = *cursor_of(L);
    LVL(L, func_001FFFB8, VoidFn)();
    chain_squeeze(start, *cursor_of(L));
    hud_unshift(L, moved);
}

static void bracketed(s32 L, u32 fn)
{
    volatile u32 *start = *cursor_of(L);
    ((VoidFn)fn)();
    if (ws_on()) {
        chain_squeeze(start, *cursor_of(L));
    }
}

void ee_ws_help_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_001F3D78, EE_SITE_WS_HELP);
    bracketed(L, LVL(L, func_001FF1B0, u32));
}

void ee_ws_subtitles_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_001F3D78, EE_SITE_WS_SUBTITLES);
    bracketed(L, LVL(L, func_001F4F90, u32));
}

/* --- the pause menu's panels ------------------------------------------------ */

/* The pause menu's panels are 14 mobys of class 0x472 at one place in front
 * of the menu camera; each page plays a sequence per panel that moves its
 * root joint (joint 0: x depth, y across, z up; joints 1-4 the corners,
 * relative to it) between a resting place and a parked one off to the side.
 * Resting centres are within |y| <= 11983 on the disc's data, parked ones
 * from 15056 out; the 4:3 picture's edge is at |y| = 16904 at the panels'
 * depth and the 16:9 one's at 22538 (measured in PCSX2: a parked panel's
 * inner edge at y 18195 showed at 0.807 of the 16:9 half width). So in 16:9
 * the parked panels would show at the sides, and a sliding panel would stop
 * short of the edge. The root's y is remapped: unchanged up to PANEL_Y_IN,
 * stretched from there to the 4:3 edge so that it reaches the 16:9 edge,
 * and moved out by the difference beyond: a panel parked just beyond the
 * 4:3 edge is parked just beyond the 16:9 one, and a slide covers the extra
 * width. The corners are the root's children, so a panel keeps its shape.
 *
 * The draw walker evaluates a moby's pose from its two key frames (moby
 * +0x68, +0x6C) when it draws it, so the pause renderer's panel draw
 * (func_L00_002781D8 + 0xFC, jal func_0020E180(moby, 1)) is hooked: the two
 * frames are copied with the remap applied, the moby points at the copies
 * for the call, and back. The class data itself is never changed.
 * func_0020E180 writes the data cache back (func_00118D80(0)) before the
 * walker's DMA reads the frames. */
DECLARE_LVL(func_L00_002781D8);         /* the pause menu's renderer */
DECLARE_LVL(func_0020E180);             /* draw a moby */
#define PANEL_DRAW_SITE    0xFC         /* func_L00_002781D8 + 0xFC: jal func_0020E180(panel, 1) */
#define PANEL_PROJECT_SITE 0x1EC        /* func_L00_002781D8 + 0x1EC: jal func_00238D90(corners, rectangle) */

#define PANEL_CLASS      0x472
#define PANEL_Y_IN       12500
#define PANEL_Y_43       16904
#define PANEL_Y_169      22538
#define PANEL_FRAME_QWC  8            /* the class's frames hold at most 6 quadwords of payload */
#define PANEL_SLOTS      32           /* copies by (moby >> 8) & 31: the 14 panels are mobys 0x100 apart */

typedef void (*DrawMobyFn)(u32, s32);
/* The walker does not evaluate the pose at once: it queues the two frames'
 * payload pointers (its record + 0x20 and + 0x30) and evaluates the batch
 * later, so every panel needs copies of its own that stay put until then
 * (a first build shared two and drew one panel with another's pose). */
static u32 panel_frames[PANEL_SLOTS][2][4 * (1 + PANEL_FRAME_QWC)] __attribute__((aligned(16)));
static f32 panel_scale;               /* world units per record unit: the panels' moby scale / 1024 */
static f32 panel_centre;              /* the panels' world y (they all sit at one place) */

static f32 panel_yf(f32 y)
{
    f32 a = y < 0.0f ? -y : y;
    if (a <= PANEL_Y_IN) {
        return y;
    }
    if (a <= PANEL_Y_43) {
        a = PANEL_Y_IN + (a - PANEL_Y_IN) * (f32)(PANEL_Y_169 - PANEL_Y_IN) / (f32)(PANEL_Y_43 - PANEL_Y_IN);
    } else {
        a += PANEL_Y_169 - PANEL_Y_43;
    }
    return y < 0.0f ? -a : a;
}

static s32 panel_y(s32 y)
{
    s32 a = y < 0 ? -y : y;
    if (a <= PANEL_Y_IN) {
        return y;
    }
    if (a <= PANEL_Y_43) {
        a = PANEL_Y_IN + (a - PANEL_Y_IN) * (PANEL_Y_169 - PANEL_Y_IN) / (PANEL_Y_43 - PANEL_Y_IN);
    } else {
        a += PANEL_Y_169 - PANEL_Y_43;
    }
    if (a > 32767) {
        a = 32767;
    }
    return y < 0 ? -a : a;
}

/* A copy of the key frame at SRC in OUT with the root's y remapped; SRC if it does not fit. */
static u32 panel_frame(u32 src, u32 *out)
{
    const u32 *in = (const u32 *)src;
    u16 qwc, at, count;
    s32 k;
    if (src == 0) {
        return src;
    }
    qwc = ((const u16 *)src)[3];
    at = ((const u16 *)src)[6];
    count = ((const u16 *)src)[7];
    if (qwc > PANEL_FRAME_QWC) {
        return src;
    }
    for (k = 0; k < 4 * (1 + qwc); k++) {
        out[k] = in[k];
    }
    for (k = 0; k < count; k++) {
        s16 *rec = (s16 *)((u8 *)out + 16 + at + 8 * k);
        if (((u8 *)rec)[6] == 0) {
            rec[1] = (s16)panel_y(rec[1]);
        }
    }
    return (u32)out;
}

void ee_ws_panel_draw_c(u32 moby, s32 count, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_002781D8, PANEL_DRAW_SITE);
    DrawMobyFn draw = LVL(L, func_0020E180, DrawMobyFn);
    volatile u32 *frames = (volatile u32 *)(moby + 0x68);
    u32 a, b;

    u32 (*copies)[4 * (1 + PANEL_FRAME_QWC)];

    if (!ws_on() || count != 1 || moby == 0 || *(volatile u16 *)(moby + 0xA6) != PANEL_CLASS) {
        draw(moby, count);
        return;
    }
    panel_scale = *(volatile f32 *)(moby + 0x2C) / 1024.0f;
    panel_centre = *(volatile f32 *)(moby + 0x14);
    copies = panel_frames[(moby >> 8) & (PANEL_SLOTS - 1)];
    a = frames[0];
    b = frames[1];
    frames[0] = panel_frame(a, copies[0]);
    frames[1] = panel_frame(b, copies[1]);
    draw(moby, count);
    frames[0] = a;
    frames[1] = b;
}

/* The renderer projects each panel's first and opposite corners (from the
 * panel's own update, which knows nothing of the remap) to the rectangle it
 * fills dark and draws the panel's content into (func_00238D90, at
 * func_L00_002781D8 + 0x1EC, on copies of the corners on its stack): the
 * corners move with the panel, by the remap of their centre.
 *
 * The rectangle is then given back as the 4:3 picture would have it (x and
 * width a third wider about the centre). The renderer lays a panel's
 * content out in a texture of the rectangle's size and draws it over the
 * rectangle: with the 16:9 rectangle the layout lost a quarter of its width
 * (the camera page's labels ran into their values; the vendor's monitors
 * showed three quarters of their picture). With the 4:3 one the content is
 * laid out as in retail, and the rectangle's fill, the content's quad and
 * the panels that draw on the screen directly are squeezed afterwards like
 * any 2D (the bracketed calls below). The vendor's monitors go through the
 * same projection (func_L00_002A0F60 + 0x188) and get the same treatment,
 * without the remap (they never park). */
DECLARE_LVL(func_00238D90);             /* project two corners to a screen rectangle */
DECLARE_LVL(func_L00_002A0F60);         /* the vendor's monitors */
typedef void (*ProjectFn)(f32 *, f32 *, s32 *, s32 *, s32 *, s32 *);
#define VENDOR_PROJECT_SITE 0x188

void ee_ws_panel_project_c(f32 *first, f32 *opposite, s32 *w, s32 *h, s32 *x, s32 *y, u32 ra, s32 vendor)
{
    s32 L = vendor ? ee_level_of_call(ra, lvl_func_L00_002A0F60, VENDOR_PROJECT_SITE)
                   : ee_level_of_call(ra, lvl_func_L00_002781D8, PANEL_PROJECT_SITE);
    s32 on = ws_on();
    if (on && !vendor && panel_scale > 0.0f) {
        f32 rec = ((first[1] + opposite[1]) * 0.5f - panel_centre) / panel_scale;
        f32 shift = (panel_yf(rec) - rec) * panel_scale;
        first[1] += shift;
        opposite[1] += shift;
    }
    LVL(L, func_00238D90, ProjectFn)(first, opposite, w, h, x, y);
    if (on) {
        s32 hw = SCREEN[2];
        *x = hw + ((*x - hw) * 4) / 3;
        *w = (*w * 4) / 3;
    }
}

/* The vendor's six monitor screens (VendorDrawScreens, func_0023AA38) are
 * laid out the same way with another projection, func_00238F98(a, b,
 * &width, &height, &x, &y) in floats: the first call (+0x20C) gives the
 * whole screen's size, the extent of the texture drawn back; the second
 * (+0x374) the size the screen's content is laid out at (several of the
 * content routines place text at fixed offsets made for 4:3, which ran off
 * the narrower 16:9 rectangle: "7,50" of "7,500") and where the quad goes.
 * Both get the 4:3 rectangle; the quads (+0x50C, +0x568) are bracketed. */
DECLARE_LVL(func_00238F98);             /* project two corners to a screen rectangle, in floats */
DECLARE_LVL(func_0023AA38);             /* VendorDrawScreens */
typedef void (*ProjectFFn)(void *, void *, f32 *, f32 *, f32 *, f32 *);
#define SCREENS_PROJECT_A 0x20C
#define SCREENS_PROJECT_B 0x374

void ee_ws_screens_project_c(void *a, void *b, f32 *w, f32 *h, f32 *x, f32 *y, u32 ra)
{
    s32 L;
    for (L = 0; L < 19; L++) {
        u32 f = lvl_func_0023AA38[L];
        if (ra == f + SCREENS_PROJECT_A + 8 || ra == f + SCREENS_PROJECT_B + 8) {
            break;
        }
    }
    if (L == 19) {
        L = ee_level_of_call(ra, lvl_func_0023AA38, SCREENS_PROJECT_A);   /* never expected */
    }
    LVL(L, func_00238F98, ProjectFFn)(a, b, w, h, x, y);
    if (ws_on() && !view_suspended) {
        f32 hw = (f32)SCREEN[2];
        *x = hw + (*x - hw) * (4.0f / 3.0f);
        *w = *w * (4.0f / 3.0f);
    }
}

/* The item monitor shows the item's moby drawn straight into the monitor's
 * texture (VendorDrawItemPanel, func_00239CF8: DrawMobyList) with the
 * transform func_001F2608 built for the frame from the camera and the main
 * view, placed so that it lands in the screen's top-left corner, which the
 * monitor shows. With the widened view it landed a third of the monitor's
 * width to the right. So VendorDrawScreens (called at func_L00_002A1540 +
 * 0x3C4) runs with the view and the transform rebuilt as in 4:3, and both
 * are rebuilt widened after it; its projections then come back as 4:3
 * already (no conversion), and its quads are still squeezed. */
DECLARE_LVL(func_001F2608);             /* the frame's camera-and-view transform */
typedef void (*ScreensFn)(u32);
#define VENDOR_SCREENS_SITE 0x3C4

void ee_ws_vendor_screens_c(u32 m, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_002A1540, VENDOR_SCREENS_SITE);
    if (!ws_on()) {
        LVL(L, func_0023AA38, ScreensFn)(m);
        return;
    }
    view_suspended = 1;
    LVL(L, func_001F3140, VoidFn)();
    LVL(L, func_001F2608, VoidFn)();
    LVL(L, func_0023AA38, ScreensFn)(m);
    view_suspended = 0;
    LVL(L, func_001F3140, VoidFn)();
    LVL(L, func_001F2608, VoidFn)();
}

/* --- bracketed calls (src/ws_hooks.s ee_ws_bracket) ------------------------ */

/* The arguments the bracket stub saved: $4-$11 as doublewords, $f12-$f15,
 * the return address, the callee's results. */
u32 ee_ws_saved_args[26] __attribute__((aligned(16)));

#define WS_SITE_PAUSE_FILL    1     /* func_L00_002781D8 + 0x298: a panel's dark fill (func_00201640) */
#define WS_SITE_PAUSE_DIRECT  2     /* + 0x388: jalr $a2, a panel that draws on the screen itself */
#define WS_SITE_PAUSE_QUAD    3     /* + 0x600: a panel's content drawn over its rectangle (func_001F5800) */
#define WS_SITE_PAUSE_LAST    4     /* + 0x698: the panels' last pass (func_002250B8) */
#define WS_SITE_VENDOR_QUAD   5     /* func_L00_002A0F60 + 0x258: a monitor's content over its rectangle */
#define WS_SITE_VENDOR_HUD    6     /* func_L00_002A1540 + 0x400: the vendor screen's HudDraw (bolts) */
#define WS_SITE_SCREENS_QUAD_A 7    /* func_0023AA38 + 0x50C: a monitor screen's quad (func_001F5988) */
#define WS_SITE_SCREENS_QUAD_B 8    /* func_0023AA38 + 0x568: the same at 60 Hz */
DECLARE_LVL(func_001F5988);
DECLARE_LVL(func_00201640);
DECLARE_LVL(func_001F5800);
DECLARE_LVL(func_002250B8);

static s32 bracket_level;
static s32 bracket_site;
static s32 bracket_on;
static volatile u32 *bracket_start;
static s32 bracket_moved[HUD_SLOTS];

u32 ee_ws_bracket_begin_c(s32 site, u32 ra)
{
    s32 L;
    u32 target;
    switch (site) {
    case WS_SITE_PAUSE_FILL:
        L = ee_level_of_call(ra, lvl_func_L00_002781D8, 0x298);
        target = LVL(L, func_00201640, u32);
        break;
    case WS_SITE_PAUSE_DIRECT:
        L = ee_level_of_call(ra, lvl_func_L00_002781D8, 0x388);
        target = ee_ws_saved_args[2 * 2];               /* the jalr's $a2 */
        break;
    case WS_SITE_PAUSE_QUAD:
        L = ee_level_of_call(ra, lvl_func_L00_002781D8, 0x600);
        target = LVL(L, func_001F5800, u32);
        break;
    case WS_SITE_PAUSE_LAST:
        L = ee_level_of_call(ra, lvl_func_L00_002781D8, 0x698);
        target = LVL(L, func_002250B8, u32);
        break;
    case WS_SITE_VENDOR_QUAD:
        L = ee_level_of_call(ra, lvl_func_L00_002A0F60, 0x258);
        target = LVL(L, func_001F5800, u32);
        break;
    case WS_SITE_SCREENS_QUAD_A:
        L = ee_level_of_call(ra, lvl_func_0023AA38, 0x50C);
        target = LVL(L, func_001F5988, u32);
        break;
    case WS_SITE_SCREENS_QUAD_B:
        L = ee_level_of_call(ra, lvl_func_0023AA38, 0x568);
        target = LVL(L, func_001F5988, u32);
        break;
    default:                                            /* WS_SITE_VENDOR_HUD */
        L = ee_level_of_call(ra, lvl_func_L00_002A1540, 0x400);
        target = LVL(L, func_001FFFB8, u32);
        break;
    }
    bracket_level = L;
    bracket_site = site;
    bracket_on = ws_on();
    if (bracket_on) {
        if (site == WS_SITE_VENDOR_HUD) {
            hud_shift(L, bracket_moved);
        }
        bracket_start = *cursor_of(L);
    }
    return target;
}

void ee_ws_bracket_end_c(void)
{
    if (!bracket_on) {
        return;
    }
    chain_squeeze(bracket_start, *cursor_of(bracket_level));
    if (bracket_site == WS_SITE_VENDOR_HUD) {
        hud_unshift(bracket_level, bracket_moved);
    }
    bracket_on = 0;
}

/* --- the letterbox --------------------------------------------------------- */

/* Black sprites over the frame's sides, PIXELS wide each. */
static void draw_pillars(s32 L, s32 pixels)
{
    volatile u32 **cursor = cursor_of(L);
    volatile u32 *p = *cursor;
    s32 left = SCREEN[4], right = SCREEN[6], top = SCREEN[5], bottom = SCREEN[7];
    s32 w = pixels << 4;
    s32 xs[4];
    s32 k;

    xs[0] = left;
    xs[1] = left + w;
    xs[2] = right - w;
    xs[3] = right;
    p[0] = 0x10000007;                    /* DMA CNT, 7 quadwords */
    p[1] = 0;
    p[2] = 0;                             /* VIF NOP */
    p[3] = 0x50000007;                    /* VIF DIRECT, 7 quadwords */
    /* GIF tag: NLOOP 2, EOP, PRE with PRIM sprite (6), PACKED, NREG 3:
     * RGBAQ, XYZ2, XYZ2. */
    p[4] = 0x00008002;
    p[5] = (3u << 28) | (6u << 15) | (1u << 14);
    p[6] = 0x551;
    p[7] = 0;
    p += 8;
    for (k = 0; k < 2; k++) {
        p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0x80;          /* black, opaque */
        p[4] = xs[2 * k]; p[5] = top; p[6] = 0xFFFFF3; p[7] = 0;
        p[8] = xs[2 * k + 1]; p[9] = bottom; p[10] = 0xFFFFF3; p[11] = 0;
        p += 12;
    }
    *cursor = p;
}

void ee_ws_letterbox_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_001F3D78, EE_SITE_WS_LETTERBOX);
    volatile u32 **cursor = cursor_of(L);
    volatile u32 *start = *cursor;

    LVL(L, func_001F5148, VoidFn)();      /* advances the bars' line count, and draws them */
    if (ws_on()) {
        *cursor = start;                  /* in 16:9 the zoom replaces the bars */
    }
}

/* DrawWorld's DoGifPaging after the HUD block (every frame it draws, HUD or
 * not): the pillars of the original framing go in first, over the world and
 * under the fades that follow. */
DECLARE_LVL(func_001F4748);            /* DoGifPaging */

void ee_ws_pillars_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_001F3D78, EE_SITE_WS_PILLARS);
    f32 content;
    s32 pillar;

    if (ws_on()) {
        framing(L, &content);
        pillar = (s32)((f32)SCREEN[2] * (1.0f - content) + 0.5f);
        if (pillar > 0) {
            draw_pillars(L, pillar);
        }
    }
    LVL(L, func_001F4748, VoidFn)();
}

/* --- prerendered movies ---------------------------------------------------- */

/* The movie player (Sony's ezmpeg design) decodes each frame into a texture
 * and draws it with a fixed packet in the core, D_00151EF0 (0x25 quadwords:
 * the TEST, TEX1 and TEX0 setup, then 16 sprite strips 32 pixels wide that
 * cover the screen), REF'd from the end of the tag list setImageTag
 * (func_0023C5E0) builds for the frame. In 16:9 the movie keeps its 4:3
 * picture with black bars at the sides (owner's choice): the level's call
 * of setImageTag (func_0023E298 + 0xF8) is hooked, and its last REF is sent
 * to a copy of the packet, rebuilt from the original each time, whose
 * strips are squeezed toward the centre and which ends with two black
 * sprites over the sides. The frontend's movies (its own copy of the code)
 * are not touched. */
DECLARE_LVL(func_0023C5E0);           /* setImageTag(tags, frame, width, height) */
DECLARE_LVL(func_0023E298);           /* the decoder's frame setup that calls it */

#define MOVIE_PACKET     0x00151EF0
#define MOVIE_QWC        0x25
#define MOVIE_BARS_QWC   7
#define MOVIE_STRIPS     5            /* the strips' GIF tag is quadword 4; their data starts at 5 */
#define UNCACHED(p)      ((volatile u32 *)((u32)(p) | 0x20000000))

typedef void (*TagFn)(u32, u32, s32, s32);
static u32 movie_packet[4 * (MOVIE_QWC + MOVIE_BARS_QWC)] __attribute__((aligned(16)));

void ee_ws_movie_tag_c(u32 tags, u32 frame, s32 width, s32 height, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_0023E298, EE_SITE_WS_MOVIE);
    volatile u32 *tail, *dst, *p;
    const volatile u32 *src = (const volatile u32 *)MOVIE_PACKET;
    s32 k, x0, x1, y0, y1, xs[4];

    LVL(L, func_0023C5E0, TagFn)(tags, frame, width, height);
    if (WS_SETTING == 0) {
        return;
    }
    tail = (volatile u32 *)(tags + 0x40 + (width >> 4) * (height >> 4) * 0x60);
    if (tail[4] != (0x30000000u | MOVIE_QWC) || tail[5] != MOVIE_PACKET) {
        return;                           /* not the tag list expected: leave it */
    }
    dst = UNCACHED(movie_packet);         /* the DMA reads it: write past the data cache */
    for (k = 0; k < 4 * MOVIE_QWC; k++) {
        dst[k] = src[k];
    }
    dst[4 * (MOVIE_STRIPS - 1)] &= ~0x8000u;          /* the strips' tag no longer ends the packet */
    /* XYZ2 as the raw register: X in bits 0-15, Y in 16-31 of its low word */
    x0 = src[4 * MOVIE_STRIPS + 2] & 0xFFFF;          /* the first strip's top-left vertex */
    y0 = src[4 * MOVIE_STRIPS + 2] >> 16;
    x1 = src[4 * (MOVIE_QWC - 1) + 2] & 0xFFFF;       /* the last strip's bottom-right vertex */
    y1 = src[4 * (MOVIE_QWC - 1) + 2] >> 16;
    for (k = MOVIE_STRIPS; k < MOVIE_QWC; k++) {      /* each quadword: UV, then XYZ2 (register list) */
        squeeze_word(dst + 4 * k + 2);
    }
    xs[0] = x0;
    xs[1] = squeeze_x(x0);
    xs[2] = squeeze_x(x1);
    xs[3] = x1;
    p = dst + 4 * MOVIE_QWC;
    p[0] = 0x00008002;                    /* NLOOP 2, EOP; PRE sprite (6); PACKED; RGBAQ, XYZ2, XYZ2 */
    p[1] = (3u << 28) | (6u << 15) | (1u << 14);
    p[2] = 0x551;
    p[3] = 0;
    p += 4;
    for (k = 0; k < 2; k++) {
        p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0x80;
        p[4] = xs[2 * k]; p[5] = y0; p[6] = 0; p[7] = 0;
        p[8] = xs[2 * k + 1]; p[9] = y1; p[10] = 0; p[11] = 0;
        p += 12;
    }
    tail[4] = 0x30000000u | (MOVIE_QWC + MOVIE_BARS_QWC);
    tail[5] = (u32)movie_packet;
    tail[7] = 0x50000000u | (MOVIE_QWC + MOVIE_BARS_QWC);
}
