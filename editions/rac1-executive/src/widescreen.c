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

/* The frontends. Loading a save from the title plays the flight to the
 * planet (game mode 6) from the frontend's program, which no level patch
 * reaches: after power-on the boot executable's own code, after quitting a
 * level frontbin's (the same functions linked elsewhere). Their
 * UpdateViewContext and planet name call are patched like a level's, with
 * ids 19 and 20 after the 19 levels (tools/gen_frontend.py, which finds
 * their addresses), and the view is widened there only in game mode 6, so
 * the title and its menus stay 4:3 (owner, 2026-10-20: the flight after a
 * title-screen load was stretched). */
#define GAME_MODE_FLIGHT 6         /* DoSpaceTransition: a level's arrival and departure, the flight between */
extern const u32 ee_fe_update_view[2], ee_fe_flight[2], ee_fe_planet_name[2];
extern const u32 ee_fe_view[2], ee_fe_chain_cursor[2], ee_fe_letterbox_lines[2], ee_fe_game_mode[2];

static u32 view_record(s32 L)
{
    return L >= FE_BOOT ? ee_fe_view[FE(L)] : LVL_DATA(L, view, u32);
}

static u32 update_view_fn(s32 L)
{
    return L >= FE_BOOT ? ee_fe_update_view[FE(L)] : LVL(L, func_001F3140, u32);
}

static s32 game_mode_of(s32 L)
{
    return *(L >= FE_BOOT ? (volatile s32 *)ee_fe_game_mode[FE(L)] : LVL_DATA(L, game_mode, volatile s32 *));
}

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
    s32 lines = *(L >= FE_BOOT ? (volatile s32 *)ee_fe_letterbox_lines[FE(L)]
                               : LVL_DATA(L, letterbox_lines, volatile s32 *));
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
 * grow; a cinematic without bars cuts to it. A level's arrival and
 * departure flights (game mode 6) stay Hor+ like play (owner, 2026-10-19:
 * "those ones can stay widescreen"). */
#define GAME_MODE_CINEMATIC 2      /* a cinematic (the mode the subtitles are drawn in) */
static f32 framing(s32 L, f32 *content)
{
    f32 t = cutscene_t(L);
    f32 zoom = cutscene_zoom(t);
    s32 mode = game_mode_of(L);
    f32 original = mode == GAME_MODE_CINEMATIC ? 1.0f : t;
    f32 c = zoom * (1.0f - 0.25f * original);
    *content = c < 1.0f ? c : 1.0f;
    return zoom;
}

/* --- the view ------------------------------------------------------------ */

static s32 view_suspended;      /* the vendor's monitor screens are drawn with the 4:3 view (below) */
static s32 view_level = -1;     /* the level whose view record holds view_written */
static u32 view_written;        /* the bits of the widened value this hook left in +0xB0 */
static f32 view_orig;           /* and the game's own value it stands for */
static s32 view_applied;        /* the setting the full-screen view was last built with */

/* The widened field of view stays in the record: the renderers' culling
 * (TfragProc, TieProc, ShrubProc, MobyProc) loads +0xA0..+0xBF itself every
 * frame for its side planes, so a value put back after the projection was
 * built culled at the old 4:3 edges (objects popped in at the sides, owner
 * 2026-10-19). On the next call the hook recognises its own value by its
 * bits and starts again from the game's; any other value is the game's new
 * one. A view built in 4:3 (setting off, a render to a texture, the vendor's
 * monitors) gets the game's value back. */
typedef union { f32 f; u32 u; } FloatBits;

u32 ee_view_pre_c(s32 L)
{
    volatile f32 *fov = (volatile f32 *)(view_record(L) + VIEW_FOV);
    volatile f32 *src = (volatile f32 *)(view_record(L) + VIEW_FOV_SRC);
    FloatBits cur;
    f32 orig;
    s32 full = SCREEN[0] == RES_W && SCREEN[1] == RES_H;
    s32 frontend = L >= FE_BOOT;

    cur.f = *fov;
    orig = (L == view_level && cur.u == view_written) ? view_orig : cur.f;
    *src = orig;
    *fov = orig;
    if (full && !view_suspended && !frontend) {
        view_applied = WS_SETTING != 0;
    }
    if (ws_on() && !view_suspended && (!frontend || game_mode_of(L) == GAME_MODE_FLIGHT)) {
        f32 content;
        f32 zoom = framing(L, &content);
        FloatBits wide;
        /* Square pixels on the stretched screen: the horizontal scale is 3/4
         * of the 4:3 one at the same vertical scale, so the horizontal field
         * is 4/3 of the original divided by the zoom. */
        wide.f = orig * (4.0f / 3.0f) / zoom;
        *src = orig / zoom;
        *fov = wide.f;
        view_level = L;
        view_written = wide.u;
        view_orig = orig;
    }
    return update_view_fn(L) + 8;
}

void ee_view_post_c(void)
{
}

/* The setting changed since the full-screen view was built (toggled on the
 * Camera page): the pause menu does not rebuild the view while it is open,
 * so the panels and the world behind them kept the old projection until the
 * game was unpaused (owner, 2026-10-19). Rebuilt here, with the frame's
 * camera transform. */
DECLARE_LVL(func_001F2608);             /* the frame's camera-and-view transform */

static void refresh_view(s32 L)
{
    if ((WS_SETTING != 0) != view_applied && SCREEN[0] == RES_W && SCREEN[1] == RES_H) {
        LVL(L, func_001F3140, VoidFn)();
        LVL(L, func_001F2608, VoidFn)();
    }
}

/* --- squeezing 2D packets ------------------------------------------------ */

#define GS_CENTRE   0x8000         /* 2048 in 12.4 */
/* An anchored element's move out to the 16:9 edge, after the squeeze, in
 * 12.4: a third of the half width before the 3/4 squeeze is exactly 64
 * pixels after it (1024), plus 2 so that an element parked just past the
 * 4:3 edge (the slides park them at 512 - 0.5) clears the 16:9 edge at any
 * rendering resolution. At 85 pixels before the squeeze (the first build)
 * a parked meter's left column sat at 511.4 and showed. */
#define EDGE_OFFSET 1056

static s32 ws_offset;               /* added to every x after the squeeze, 12.4 */
static s32 ws_stretch;              /* move each primitive to its half's edge (stretch_flush) */
static volatile u32 *ws_pend[128];  /* this GIF tag's vertices, in order, while stretching */
static s32 ws_npend;
static void squeeze_word(volatile u32 *w);
static s32 ws_measuring;            /* collect the x range instead of changing anything */
static s32 ws_minx, ws_maxx;

static s32 squeeze_x(s32 x)
{
    return GS_CENTRE + ((x - GS_CENTRE) * 3) / 4;
}

static void shift_word(volatile u32 *w, s32 offset)
{
    u32 v = *w;
    s32 x = squeeze_x((s32)(v & 0xFFFF)) + offset;
    x = x < 0 ? 0 : x > 0xFFFF ? 0xFFFF : x;
    *w = (v & 0xFFFF0000u) | (u32)x;
}

static s32 half_side(s32 x)
{
    return x < GS_CENTRE ? -EDGE_OFFSET : EDGE_OFFSET;
}

/* A layout that spans the screen at fixed places (the hoverboard races'
 * bottom bar: Lap on the left, Place and Time on the right, a stretchable
 * bar under them, all from two HUD slots): each primitive (vertex pair)
 * moves to its half's edge unstretched, and a wide one (the bar's middle,
 * wider than 64 pixels) has each end moved by its own half, so it
 * stretches to reach both edges. */
static void stretch_flush(void)
{
    s32 k;
    for (k = 0; k < ws_npend; k += 2) {
        s32 x0 = *ws_pend[k] & 0xFFFF, x1;
        if (k + 1 >= ws_npend) {
            shift_word(ws_pend[k], half_side(x0));
            break;
        }
        x1 = *ws_pend[k + 1] & 0xFFFF;
        if (x1 - x0 > 64 * 16 || x0 - x1 > 64 * 16) {
            shift_word(ws_pend[k], half_side(x0));
            shift_word(ws_pend[k + 1], half_side(x1));
        } else {
            s32 side = half_side((x0 + x1) / 2);
            shift_word(ws_pend[k], side);
            shift_word(ws_pend[k + 1], side);
        }
    }
    ws_npend = 0;
}

static void put_xyz(volatile u32 *w)
{
    if (ws_stretch && ws_npend < 128) {
        ws_pend[ws_npend++] = w;
    } else {
        squeeze_word(w);
    }
}

static void squeeze_word(volatile u32 *w)
{
    u32 v = *w;
    s32 x = squeeze_x((s32)(v & 0xFFFF)) + ws_offset;
    x = x < 0 ? 0 : x > 0xFFFF ? 0xFFFF : x;
    *w = (v & 0xFFFF0000u) | (u32)x;
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
    x0 = hw + ((x0 - hw) * 3) / 4 + ws_offset / 16;
    x1 = hw + ((x1 - hw) * 3 + 3) / 4 + ws_offset / 16;
    x0 = x0 < 0 ? 0 : x0;
    x1 = x1 < 0 ? 0 : x1;
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

static void measured(s32 minx, s32 maxx)
{
    if (maxx >= 0 && !full_width(minx, maxx)) {
        ws_minx = minx < ws_minx ? minx : ws_minx;
        ws_maxx = maxx > ws_maxx ? maxx : ws_maxx;
    }
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
                            put_xyz(d);
                        }
                    } else if (reg == 0xE && (addr == 0x40 || addr == 0x41)) {
                        squeeze_scissor(d);              /* a text window's clip, with or without vertices */
                    }
                }
                keep = maxx >= 0 && full_width(minx, maxx);   /* the whole screen: leave its vertices */
                if (ws_measuring) {
                    measured(minx, maxx);
                    break;
                }
            }
            stretch_flush();
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
                        put_xyz(d);
                    }
                }
                if (pass == 0 && ws_measuring) {
                    measured(minx, maxx);
                    break;
                }
                if (pass == 0 && (maxx < 0 || full_width(minx, maxx))) {
                    break;
                }
            }
            stretch_flush();
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

/* What a DMA packet's vertices are moved by after the squeeze. */
#define CHAIN_PLAIN  0              /* nothing: squeezed about the centre */
#define CHAIN_HUD    1              /* the HUD slot whose draw wrote it (the wrapper's ranges) */
#define CHAIN_AUTO   2              /* by where it sits: left third out left, right third out right */
#define CHAIN_LEFT   3              /* all of it out to the left edge, like a left-anchored HUD slot */

static s32 hud_offset_at(volatile u32 *p);

/* A slot drawn with stretch_flush: a packet narrower than half the screen
 * (a line of text) moves whole to its half's edge; a wider one (the bar)
 * goes primitive by primitive. */
#define HUD_STRETCH 0x40000000
static s32 stretch_setup(volatile u32 *p, s32 qwc)
{
    ws_measuring = 1;
    ws_minx = 0x10000;
    ws_maxx = -1;
    vif_squeeze(p + 2, 2 + 4 * qwc);
    ws_measuring = 0;
    if (ws_maxx < 0) {
        return 0;
    }
    if (ws_maxx - ws_minx < SCREEN[0] * 16 / 2) {
        return half_side((ws_minx + ws_maxx) / 2);
    }
    ws_stretch = 1;
    return 0;
}

static s32 auto_offset(volatile u32 *p, s32 qwc)
{
    s32 third = SCREEN[0] * 16 / 3, cx;
    ws_measuring = 1;
    ws_minx = 0x10000;
    ws_maxx = -1;
    vif_squeeze(p + 2, 2 + 4 * qwc);
    ws_measuring = 0;
    if (ws_maxx < 0) {
        return 0;
    }
    cx = (ws_minx + ws_maxx) / 2 - SCREEN[4];
    return cx < third ? -EDGE_OFFSET : cx > 2 * third ? EDGE_OFFSET : 0;
}

/* Every 2D vertex the chain received between START and END. */
static void chain_walk(volatile u32 *p, volatile u32 *end, s32 mode)
{
    while (p < end) {
        u32 tag = p[0];
        s32 qwc = tag & 0xFFFF;
        u32 id = (tag >> 28) & 7;
        if (id == 1 || id == 2 || id == 5) {              /* CNT, NEXT, CALL: the data follows the tag */
            /* A NEXT does not end the walk: SetupGifPaging leaves a hole that
             * DoGifPaging fills with a NEXT out to the texture uploads, which
             * NEXT back to the packets after the hole, so a draw with its own
             * paging pair (the "Quit?" prompt) is still laid out in order in
             * the buffer. A first build stopped there and left the prompt
             * stretched. */
            ws_offset = mode == CHAIN_HUD ? hud_offset_at(p) : mode == CHAIN_AUTO ? auto_offset(p, qwc)
                      : mode == CHAIN_LEFT ? -EDGE_OFFSET : 0;
            if (ws_offset == HUD_STRETCH) {
                ws_offset = stretch_setup(p, qwc);
            }
            vif_squeeze(p + 2, 2 + 4 * qwc);
            ws_offset = 0;
            ws_stretch = 0;
            p += 4 + 4 * qwc;
        } else if (id == 3 || id == 4) {                  /* REF, REFS: the data is elsewhere (shared, never ours) */
            p += 4;
        } else {
            return;
        }
    }
}

static void chain_squeeze(volatile u32 *p, volatile u32 *end)
{
    chain_walk(p, end, CHAIN_PLAIN);
}

static volatile u32 **cursor_of(s32 L)
{
    return L >= FE_BOOT ? (volatile u32 **)ee_fe_chain_cursor[FE(L)] : LVL_DATA(L, chain_cursor, volatile u32 **);
}

/* --- the HUD, help, subtitles ---------------------------------------------- */

/* The HUD's 13 slots have fixed places and anchor flags (+0x60). An element
 * anchored left or right is moved out to the 16:9 edge after it is drawn:
 * HudDraw calls each slot's draw callback (+0x18) through a wrapper that
 * notes the stretch of the chain the slot wrote, and those vertices get
 * EDGE_OFFSET with the squeeze. Moving the slot's x before the draw instead
 * (the first build) broke the ammo display: its draw (func_L00_0023B140)
 * passes x + width as its backing's width, a slip in the original that is
 * harmless at x = 20, so the backing lost what x lost.
 *
 * The quick-select ring's draw is the edition's (src/quick_select_pages.c),
 * which names its level from its return address inside HudDraw, so it is
 * not wrapped: its slot's x moves half the way out instead (owner: "maybe
 * an in-between"), and its L1 glyph's clamp moves with it
 * (ee_ws_ring_shift). */
extern void ee_qs_draw(void *slot);
typedef s32 (*SlotDrawFn)(void *);
#define HUD_SLOT_DRAW   0x18
#define HUD_RANGES      48

static s32 hud_level;
static u32 hud_base;
static u32 hud_draw[HUD_SLOTS];
static s32 hud_slot_offset[HUD_SLOTS];
static s32 hud_moved[HUD_SLOTS];
static s32 ring_moved;
static volatile u32 *hud_from[HUD_RANGES];
static volatile u32 *hud_to[HUD_RANGES];
static s32 hud_off[HUD_RANGES];
static s32 hud_count;

static s32 hud_slot_wrapper(void *slot)
{
    s32 i = ((u32)slot - hud_base) / HUD_SLOT_BYTES;
    volatile u32 *from = *cursor_of(hud_level);
    s32 r = ((SlotDrawFn)hud_draw[i])(slot);
    if (hud_count < HUD_RANGES) {
        hud_from[hud_count] = from;
        hud_to[hud_count] = *cursor_of(hud_level);
        hud_off[hud_count] = hud_slot_offset[i];
        hud_count++;
    }
    return r;
}

static s32 hud_offset_at(volatile u32 *p)
{
    s32 k;
    for (k = 0; k < hud_count; k++) {
        if (p >= hud_from[k] && p < hud_to[k]) {
            return hud_off[k];
        }
    }
    return 0;
}

/* The hoverboard races' bottom bar (Rilgar, Kalebo III): func_L05_00266D10
 * (slot 5: the bar, Lap, Place, at fixed places across the screen) is
 * stretched to the edges. The time (func_L05_002670A0, slot 7) stays
 * centred: it is squeezed in place, although its slot is anchored right,
 * since its pieces would otherwise go to the halves they sit in. */
#define RACE_BAR  1
#define RACE_TIME 2
static s32 race_hud(s32 L, u32 draw)
{
    if (L == 5) {
        return draw == 0x00266D10 ? RACE_BAR : draw == 0x002670A0 ? RACE_TIME : 0;
    }
    if (L == 16) {
        return draw == 0x00239390 ? RACE_BAR : draw == 0x00239720 ? RACE_TIME : 0;
    }
    return 0;
}

static void hud_begin(s32 L)
{
    s32 i;
    hud_level = L;
    hud_base = LVL_DATA(L, hud_slots, u32);
    hud_count = 0;
    ring_moved = 0;
    for (i = 0; i < HUD_SLOTS; i++) {
        volatile s32 *slot = (volatile s32 *)(hud_base + i * HUD_SLOT_BYTES);
        s32 flags = slot[HUD_SLOT_FLAGS / 4];
        u32 draw = (u32)slot[HUD_SLOT_DRAW / 4];
        s32 side = (flags & HUD_LEFT) ? -1 : (flags & HUD_RIGHT) ? 1 : 0;
        s32 race;
        hud_moved[i] = 0;
        hud_draw[i] = 0;
        race = draw != 0 ? race_hud(L, draw) : 0;
        if (race == RACE_TIME) {
            continue;
        }
        if (race == RACE_BAR) {
            hud_draw[i] = draw;
            hud_slot_offset[i] = HUD_STRETCH;
            slot[HUD_SLOT_DRAW / 4] = (s32)(u32)hud_slot_wrapper;
            continue;
        }
        if (draw == 0 || side == 0) {
            continue;
        }
        if (draw == (u32)ee_qs_draw) {
            hud_moved[i] = side * (SCREEN[2] / 3) / 2;
            ring_moved = hud_moved[i];
            slot[HUD_SLOT_X / 4] += hud_moved[i];
            continue;
        }
        hud_draw[i] = draw;
        hud_slot_offset[i] = side * EDGE_OFFSET;
        slot[HUD_SLOT_DRAW / 4] = (s32)(u32)hud_slot_wrapper;
    }
}

static void hud_end(volatile u32 *start)
{
    s32 i;
    for (i = 0; i < HUD_SLOTS; i++) {
        volatile s32 *slot = (volatile s32 *)(hud_base + i * HUD_SLOT_BYTES);
        if (hud_draw[i] != 0 && (u32)slot[HUD_SLOT_DRAW / 4] == (u32)hud_slot_wrapper) {
            slot[HUD_SLOT_DRAW / 4] = (s32)hud_draw[i];  /* unless the draw changed it itself */
        }
        slot[HUD_SLOT_X / 4] -= hud_moved[i];
    }
    chain_walk(start, *cursor_of(hud_level), CHAIN_HUD);
    ring_moved = 0;
}

s32 ee_ws_ring_shift(void)
{
    return ring_moved;
}

void ee_ws_hud_c(u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_001F3D78, EE_SITE_WS_HUD);
    volatile u32 *start;

    refresh_view(L);
    if (!ws_on()) {
        LVL(L, func_001FFFB8, VoidFn)();
        return;
    }
    start = *cursor_of(L);
    hud_begin(L);
    LVL(L, func_001FFFB8, VoidFn)();
    hud_end(start);
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
DECLARE_LVL(func_001F7A50);             /* set up a 2^n render target */
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

    refresh_view(L);
    if (moby != 0 && *(volatile u16 *)(moby + 0xA6) == PANEL_CLASS) {
        ee_controls_panel(moby);   /* in 4:3 too: the Controls page's list panel */
    }
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

/* The pause menu draws each panel's content into a texture: its renderer
 * (func_L00_002781D8) sets up a 2^n render target (func_001F7A50, + 0x458,
 * which builds a view of the target's size, field 1.0), calls the panel
 * (+ 0x49C), puts the frame's view back (func_001F7B40) and draws the
 * middle of the target over the panel's rectangle with the quad bracketed
 * at + 0x600, which is squeezed. The Weapons, Gadgets and Quick Select
 * pages' item previews (FUN_0021e110 in the NTSC decomp) draw their mobys
 * there, and mobys are projected through the camera block
 * (func_001F2608: the camera times the view's projection, D_00187040, and
 * its copy at view + 0x280), which the frame built from the main view, not
 * the target's. With the widened main view the previews were narrowed
 * twice, by the view and by the squeeze, and showed three quarters as wide
 * as in 4:3 (owner, 2026-10-19). So before the target is set up the camera
 * block is built from the main view as 4:3 (the view record and the
 * screen are put back as they were; the block lives elsewhere), and after
 * the panel has drawn it is built from the widened main view again. */
typedef void (*TargetFn)(s32, s32, s32, f32);
typedef s32 (*PanelFn)(u32);
#define PANEL_TARGET_SITE  0x458
#define PANEL_TEXTURE_SITE 0x49C
#define VIEW_SAVED_WORDS   (0x280 / 4)     /* the record up to the camera block's copy */

static u32 main_view[VIEW_SAVED_WORDS];
static s32 main_screen[8];
static s32 main_saved;

static void swap_view(s32 L, u32 *view, s32 *screen)
{
    volatile u32 *rec = (volatile u32 *)LVL_DATA(L, view, u32);
    s32 k;
    for (k = 0; k < VIEW_SAVED_WORDS; k++) {
        u32 t = rec[k];
        rec[k] = view[k];
        view[k] = t;
    }
    for (k = 0; k < 8; k++) {
        s32 t = SCREEN[k];
        SCREEN[k] = screen[k];
        screen[k] = t;
    }
}

void ee_ws_panel_target_c(s32 a, s32 b, s32 flag, u32 ra, f32 f)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_002781D8, PANEL_TARGET_SITE);
    volatile u32 *rec = (volatile u32 *)LVL_DATA(L, view, u32);
    s32 k;
    main_saved = ws_on();
    if (main_saved) {
        for (k = 0; k < VIEW_SAVED_WORDS; k++) {
            main_view[k] = rec[k];
        }
        for (k = 0; k < 8; k++) {
            main_screen[k] = SCREEN[k];
        }
        view_suspended = 1;
        LVL(L, func_001F3140, VoidFn)();
        LVL(L, func_001F2608, VoidFn)();
        view_suspended = 0;
        for (k = 0; k < VIEW_SAVED_WORDS; k++) {
            rec[k] = main_view[k];
        }
        for (k = 0; k < 8; k++) {
            SCREEN[k] = main_screen[k];
        }
    }
    LVL(L, func_001F7A50, TargetFn)(a, b, flag, f);
}

s32 ee_ws_panel_texture_c(u32 item, u32 fn, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_002781D8, PANEL_TEXTURE_SITE);
    s32 r = ((PanelFn)fn)(item);
    if (main_saved) {
        swap_view(L, main_view, main_screen);      /* the main view, widened */
        LVL(L, func_001F2608, VoidFn)();
        swap_view(L, main_view, main_screen);      /* the target's again */
        main_saved = 0;
    }
    return r;
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
#define WS_SITE_QUIT_PROMPT   9     /* func_L00_001F9248 + 0x60: game mode 4's prompt ("Quit Race?", func_001FBE80) */
#define WS_SITE_CREDITS_NAME  10    /* func_L00_001F92E8 + 0xC8: a credits name tag (func_L00_001FCC50) */
#define WS_SITE_PLANET_NAME   11    /* func_002305A0 + 0x140: the flight screen's planet name (func_0022FD20) */
#define WS_SITE_SHIP          32    /* + level: a ship level's HUD, a draw callback (ship_hud) */
#define WS_SITE_TRESPASSER    64    /* + level: the Trespasser puzzle's rings, a draw callback (trespasser_draw) */
#define WS_SITE_TIMER         96    /* + level: a countdown's frame and digits, a draw callback (timer_draw) */
DECLARE_LVL(func_001F5988);
DECLARE_LVL(func_L00_001F9248);         /* the frame of game mode 4 (the freeze prompt) */
DECLARE_LVL(func_001FBE80);             /* its prompt */
DECLARE_LVL(func_L00_001F92E8);         /* game mode 7's frame (the credits) */
DECLARE_LVL(func_L00_001FCC50);         /* a credits name tag */
/* The flight to a planet (the level transition, space.c in the decomp) is
 * drawn by the leaving level's program (func_002305A0); its 3D planet and
 * stars get the 16:9 view like any frame. The planet's name is a 256 x 32
 * picture drawn 32 pixels from the left (func_0022FD20): squeezed and moved
 * out to the left edge as the HUD's left elements are (owner, 2026-10-19). */
DECLARE_LVL(func_002305A0);
DECLARE_LVL(func_0022FD20);

/* The ship levels draw their HUD (radar, health, missiles, messages) from a
 * draw callback the ship registers every frame (through the registrar 0x1B8
 * before AddDrawCallback), not through HudDraw. The registration's address
 * is pointed at ee_ws_br_ship_NN, and the callback's drawing is squeezed
 * with each draw call moved by where it sits (CHAIN_AUTO): the corners'
 * elements go out to the 16:9 corners as the HUD slots do, messages in the
 * middle stay centred. Pokitaru, Gemlik, Drek's fleet. */
static u32 ship_hud(s32 L)
{
    switch (L) {
    case 11: return 0x00313218;         /* func_L11_00313218 */
    case 13: return 0x002BAA68;         /* func_L13_002BAA68 */
    case 17: return 0x002ED498;         /* func_L17_002ED498 */
    }
    return 0;
}

/* The Trespasser's puzzle (TrespasserPuzzleUpdate, func_L02_002D9F08) draws
 * its three rings, beams and markers as 2D quads about the screen's centre
 * from a draw callback (RingMarkersDraw, func_L02_002DA820) it registers each
 * frame of the puzzle view (+ 0x3B0, AddDrawCallback), so in 16:9 the circles
 * came out a third wider than tall. The registration's address is pointed at
 * ee_ws_br_tres_NN and the drawing squeezed about the centre (CHAIN_PLAIN).
 * The levels with the puzzle (OpenRAC's catalogue). */
static u32 trespasser_draw(s32 L)
{
    switch (L) {
    case 2:  return 0x002DA820;         /* func_L02_002DA820 */
    case 4:  return 0x002D67D8;
    case 6:  return 0x002E3688;
    case 8:  return 0x002F3E08;
    case 11: return 0x002FEA90;
    case 13: return 0x002F73C8;
    case 18: return 0x002DACD8;
    }
    return 0;
}

/* The countdowns: the Nebula G34 warship's self-destruct (Blarg Station,
 * TimerMobyL06_Draw func_L06_00303630) and the timer moby of Quartu and
 * Drek's Fleet (TimerMoby_Draw, func_L15_002D8BB0) draw a UI frame and
 * large centred digits from a draw callback their moby registers each frame
 * (AddDrawCallback, a lui/addiu pair of $a0 around the jal), not from a HUD
 * slot, so in 16:9 they came out a third too wide (owner, 2026-10-20).
 * Squeezed in place (CHAIN_PLAIN), frame and digits together. */
static u32 timer_draw(s32 L)
{
    switch (L) {
    case 6:  return 0x00303630;         /* func_L06_00303630 */
    case 15: return 0x002D8BB0;         /* func_L15_002D8BB0 */
    case 17: return 0x002D7310;
    }
    return 0;
}
DECLARE_LVL(func_00201640);
DECLARE_LVL(func_001F5800);
DECLARE_LVL(func_002250B8);

static s32 bracket_level;
static s32 bracket_site;
static s32 bracket_on;
static volatile u32 *bracket_start;

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
    case WS_SITE_QUIT_PROMPT:
        L = ee_level_of_call(ra, lvl_func_L00_001F9248, 0x60);
        target = LVL(L, func_001FBE80, u32);
        break;
    case WS_SITE_CREDITS_NAME:
        L = ee_level_of_call(ra, lvl_func_L00_001F92E8, 0xC8);
        target = LVL(L, func_L00_001FCC50, u32);
        break;
    case WS_SITE_PLANET_NAME:
        if (ra == ee_fe_flight[0] + 0x140 + 8 || ra == ee_fe_flight[1] + 0x140 + 8) {
            L = ra == ee_fe_flight[0] + 0x140 + 8 ? FE_BOOT : FE_BIN;   /* a frontend's flight */
            target = ee_fe_planet_name[FE(L)];
            break;
        }
        L = ee_level_of_call(ra, lvl_func_002305A0, 0x140);
        target = LVL(L, func_0022FD20, u32);
        break;
    case WS_SITE_VENDOR_HUD:
        L = ee_level_of_call(ra, lvl_func_L00_002A1540, 0x400);
        target = LVL(L, func_001FFFB8, u32);
        break;
    default:
        if (site >= WS_SITE_TIMER) {                    /* WS_SITE_TIMER + level */
            L = site - WS_SITE_TIMER;
            target = timer_draw(L);
        } else if (site >= WS_SITE_TRESPASSER) {        /* WS_SITE_TRESPASSER + level */
            L = site - WS_SITE_TRESPASSER;
            target = trespasser_draw(L);
        } else {                                        /* WS_SITE_SHIP + level */
            L = site - WS_SITE_SHIP;
            target = ship_hud(L);
        }
        break;
    }
    bracket_level = L;
    bracket_site = site;
    bracket_on = ws_on();
    if (bracket_on) {
        bracket_start = *cursor_of(L);
        if (site == WS_SITE_VENDOR_HUD) {
            hud_begin(L);
        }
    }
    return target;
}

void ee_ws_bracket_end_c(void)
{
    if (!bracket_on) {
        return;
    }
    if (bracket_site == WS_SITE_VENDOR_HUD) {
        hud_end(bracket_start);
    } else {
        chain_walk(bracket_start, *cursor_of(bracket_level),
                   bracket_site >= WS_SITE_SHIP && bracket_site < WS_SITE_TRESPASSER ? CHAIN_AUTO
                   : bracket_site == WS_SITE_PLANET_NAME ? CHAIN_LEFT : CHAIN_PLAIN);
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

/* --- the credits ------------------------------------------------------------- */

/* Game mode 7 (Goodies > Credits, and the ending) draws no 3D world: its
 * frame (func_L00_001F92E8) copies a full-screen picture straight into the
 * draw buffer with an IMAGE transfer (func_001F7680(source), at + 0x24;
 * the buffer's byte address is D_0015EF84), fades it, and draws the name
 * tags (func_L00_001FCC50, at + 0xC8, inside a paging pair). A copy into
 * the frame cannot be drawn narrower, so in 16:9 the picture goes into the
 * Z buffer's memory instead (nothing in this mode draws in 3D), is drawn
 * from there as a texture into the middle 3/4 of the frame with Z writes
 * masked and black bars at the sides, and the Z memory is zeroed again
 * with an IMAGE transfer of zeros, so the name tags' depth test finds what
 * a cleared buffer holds. The name tags are squeezed like any 2D. The
 * frame's ResetGsRegisters (+ 0x94) puts TEST back before the name tags. */
DECLARE_LVL(func_001F7680);             /* the credits' picture: IMAGE copy into the draw buffer */
typedef void (*PictureFn)(u32);
#define CREDITS_PICTURE_SITE 0x24
#define DRAW_BUFFER         (*(volatile u32 *)0x0015EF84)
#define FRAME_ZPSM          (*(volatile u16 *)(0x00151880 + 0x16C))   /* the frame setup (framebuf.c FrameCfg) */
#define FRAME_ZBP           (*(volatile u16 *)(0x00151880 + 0x16E))
#define ZERO_QWC            1024                                       /* a 16 KB block of zeros */
#define ZERO_PER_TAG        28                                         /* REFs per IMAGE GIF tag: 28672 quadwords */

static u32 zero_block[4 * ZERO_QWC] __attribute__((aligned(64)));

/* One A+D quadword: register ADDR = HI:LO. */
static volatile u32 *ad(volatile u32 *p, u32 addr, u32 lo, u32 hi)
{
    p[0] = lo;
    p[1] = hi;
    p[2] = addr;
    p[3] = 0;
    return p + 4;
}

void ee_ws_credits_picture_c(u32 source, u32 ra)
{
    s32 L = ee_level_of_call(ra, lvl_func_L00_001F92E8, CREDITS_PICTURE_SITE);
    volatile u32 **cursor;
    volatile u32 *p, *tag;
    u32 saved, zbp, zbuf, zblock;
    s32 left, right, top, bottom, h, qw, done, k, n;

    if (!ws_on()) {
        LVL(L, func_001F7680, PictureFn)(source);
        return;
    }
    zbp = FRAME_ZBP;
    zbuf = zbp | ((u32)(FRAME_ZPSM & 0xF) << 24);
    zblock = zbp * 32;                          /* 8 KB pages, 256-byte blocks */
    saved = DRAW_BUFFER;
    DRAW_BUFFER = zbp * 8192;
    LVL(L, func_001F7680, PictureFn)(source);   /* the picture into the Z memory */
    DRAW_BUFFER = saved;

    cursor = cursor_of(L);
    left = SCREEN[4];
    right = SCREEN[6];
    top = SCREEN[5];
    bottom = SCREEN[7];
    h = SCREEN[1];

    /* The picture, squeezed, and the bars. */
    tag = *cursor;
    p = tag + 4;
    p[0] = 0x8000 | 18;                         /* GIF tag: NLOOP 18, EOP, PACKED, NREG 1: A+D */
    p[1] = 1u << 28;
    p[2] = 0xE;
    p[3] = 0;
    p += 4;
    p = ad(p, 0x4E, zbuf, 1);                   /* ZBUF_1, Z writes masked */
    p = ad(p, 0x47, 0x30000, 0);                /* TEST_1: Z test always, no alpha test */
    p = ad(p, 0x06, zblock | (8u << 14) | (9u << 26) | (1u << 30), 2u | (1u << 3));   /* TEX0_1: 512 x 512 CT32, DECAL */
    p = ad(p, 0x14, (1u << 5) | (1u << 6), 0);  /* TEX1_1: bilinear */
    p = ad(p, 0x08, 1u | (1u << 2), 0);         /* CLAMP_1: clamp */
    p = ad(p, 0x3F, 0, 0);                      /* TEXFLUSH */
    p = ad(p, 0x00, 0x116, 0);                  /* PRIM: sprite, textured, UV */
    p = ad(p, 0x01, 0x80808080u, 0x3F800000u);  /* RGBAQ */
    p = ad(p, 0x03, 0, 0);                      /* UV 0, 0 */
    p = ad(p, 0x05, (u32)squeeze_x(left) | ((u32)top << 16), 0);
    p = ad(p, 0x03, (512u << 4) | (((u32)h << 4) << 16), 0);
    p = ad(p, 0x05, (u32)squeeze_x(right) | ((u32)bottom << 16), 0);
    p = ad(p, 0x00, 0x6, 0);                    /* PRIM: sprite, flat */
    p = ad(p, 0x01, 0x80000000u, 0x3F800000u);  /* black */
    p = ad(p, 0x05, (u32)left | ((u32)top << 16), 0);
    p = ad(p, 0x05, (u32)squeeze_x(left) | ((u32)bottom << 16), 0);
    p = ad(p, 0x05, (u32)squeeze_x(right) | ((u32)top << 16), 0);
    p = ad(p, 0x05, (u32)right | ((u32)bottom << 16), 0);
    qw = (p - tag) / 4 - 1;
    tag[0] = 0x10000000u | qw;                  /* DMA CNT */
    tag[1] = 0;
    tag[2] = 0;
    tag[3] = 0x50000000u | qw;                  /* VIF DIRECT */

    /* The Z memory zeroed, and ZBUF_1 back. */
    tag = p;
    p = tag + 4;
    p[0] = 0x8000 | 6;
    p[1] = 1u << 28;
    p[2] = 0xE;
    p[3] = 0;
    p += 4;
    p = ad(p, 0x4E, zbuf, 0);                                      /* ZBUF_1 as the game set it */
    p = ad(p, 0x50, 0, zblock | (8u << 16));                       /* BITBLTBUF: DBP, DBW 8, CT32 */
    p = ad(p, 0x51, 0, 0);                                         /* TRXPOS 0, 0 */
    p = ad(p, 0x52, 512, (u32)h);                                  /* TRXREG 512 x h */
    p = ad(p, 0x53, 0, 0);                                         /* TRXDIR: host to local */
    p = ad(p, 0x3F, 0, 0);                                         /* TEXFLUSH */
    qw = (p - tag) / 4 - 1;
    tag[0] = 0x10000000u | qw;
    tag[1] = 0;
    tag[2] = 0;
    tag[3] = 0x50000000u | qw;
    n = 512 * h / 4;                                               /* quadwords to send */
    for (done = 0; done < n; ) {
        s32 chunk = n - done;
        if (chunk > ZERO_PER_TAG * ZERO_QWC) {
            chunk = ZERO_PER_TAG * ZERO_QWC;
        }
        p[0] = 0x10000001;                                         /* CNT 1: the IMAGE GIF tag */
        p[1] = 0;
        p[2] = 0;
        p[3] = 0x50000001;
        p[4] = (u32)chunk | (done + chunk >= n ? 0x8000u : 0);
        p[5] = 2u << 26;                                           /* FLG IMAGE */
        p[6] = 0;
        p[7] = 0;
        p += 8;
        for (k = 0; k < chunk; k += ZERO_QWC) {
            s32 q = chunk - k < ZERO_QWC ? chunk - k : ZERO_QWC;
            p[0] = 0x30000000u | (u32)q;                           /* REF to the zeros */
            p[1] = (u32)zero_block & 0x0FFFFFFF;
            p[2] = 0;
            p[3] = 0x50000000u | (u32)q;
            p += 4;
        }
        done += chunk;
    }
    *cursor = p;
}

/* The 16:9 row on the title screen's Video page (tools/gen_frontend.py adds
 * it to the page's toggle list). A save's own setting applies when it loads,
 * as its cheat flags do. Starting a new game resets the game state from the
 * disc's initial state (func_00209DC0), which zeroes the flag; retail keeps
 * the current volumes and stereo setting across that reset, so the title
 * screen's programs keep the 16:9 setting across it too: every call of the
 * reset in them goes to their stub (src/ws_hooks.s), which passes FE(id). */
extern const u32 ee_fe_reset[2];

void ee_fe_reset_c(s32 fe)
{
    u8 widescreen = *(volatile u8 *)EE_SETTING_WIDESCREEN;
    ((VoidFn)ee_fe_reset[fe])();
    *(volatile u8 *)EE_SETTING_WIDESCREEN = widescreen;
}
