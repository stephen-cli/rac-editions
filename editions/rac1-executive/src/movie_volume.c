/* Movie volume: a fourth row on the Sound page, between Music volume and
 * the stereo/mono toggle, in the pause menu and in the title screen's
 * Options, for the sound of movies (the prerendered cutscenes, the Goodies
 * movies) and of the in-level cutscenes, which retail ties to the Sound
 * effects volume.
 *
 * Movies. 989snd plays a movie's sound stream in volume group 5
 * (audioDecCreate: snd_InitMovieSound(0x400, 0x1000, 0x400, 0, 5, 3)), and
 * nothing else uses that group: bank sounds play in groups 0 and 4, music in
 * 1, dialogue streams in 2 (OpenRAC docs/engine/systems/AUDIO.md). Retail
 * sets group 5 to the sound effects volume. Only three calls ever set a
 * group's level (snd_SetMasterVolume, func_0012E348): Sound_InitState, the
 * per-frame sound_update and the end of the movie loop, each once for group
 * 5; those go to ee_movie_group_volume, which sends the movie volume.
 *
 * In-level cutscenes. Their sound is one stream on the dialogue player
 * (music_start_track_by_id, D_001517D0 + 0x50, group 2) carrying the
 * scene's speech, music and effects: sound_update has a branch for game
 * mode 2, a cutscene playing, which mutes the music group and halves bank
 * groups 0 and 3 under it (measured on Goodies > In-Level Movies: mode 2,
 * the dialogue and music players both streaming). Group 2 also carries
 * speech outside cutscenes (the HelpDesk voice, the vendor, NPC dialogue),
 * so it follows the movie volume only during a cutscene. The rule: while a
 * cutscene plays, the groups retail derives from the sound effects volume
 * (0 and 2 at 8/10, 3 and 4 at 7/10, 0 and 3 halved there) take the movie
 * volume instead, so the whole scene follows the Movie volume. The branch's
 * own calls for groups 0 and 3 go to ee_volume_cutscene, the other branch's
 * group 0 call to ee_volume_play; each records which branch ran, and the
 * calls for groups 2 and 4, made after the branches, read it. No level
 * lookup is needed: the sites alone say which branch ran.
 *
 * Saved like retail's volumes. Those are save blocks 0x22-0x24 (stereo,
 * music, sound effects): loading a save applies them, and the new-game
 * reset (func_00209DC0, which rebuilds the game state from the disc's
 * initial state, also run by the challenge-mode carry-over) keeps the
 * current ones around it. The movie volume is a u16 in the saved statistics
 * block's spare bytes (EE_SETTING_MOVIE_VOLUME, after the quick-select
 * pages'; retail never reads or writes them, so a retail game keeps and
 * re-saves it), as the volume + 1, 0 meaning none. The segment keeps the
 * session's value: a save's value replaces it, and where the save has none
 * (a retail save, or the reset just zeroed the block) the session's goes
 * back in, as the reset keeps retail's volumes. Before any is known (power
 * on) it is the sound effects volume, as movies played in retail.
 * movie_volume() does this whenever the volume is read, which the sound
 * update does every frame, on the title screen too.
 *
 * The page. Its widget (level data in every level, the boot executable's
 * and frontbin's own data on the title screen) calls input func_0021DB30
 * (Pause_SoundOptionsInput) and draw func_0021DE08 (DrawSoundMenu);
 * tools/gen_menu.py and tools/gen_frontend.py find it and point both at the
 * program's stubs in src/movie_hooks.s, which pass the program's number
 * (0-18 a level, FE_BOOT/FE_BIN the title screen). These are retail's two
 * functions (OpenRAC src/game/pause.c) with four rows instead of three.
 * Retail's input also copies a per-row description id when the widget has
 * flag 0x20, which the Sound widget never has; that path is left out. The
 * orange bar is icon EE_ICON_MOVIE_BAR (src/hud_icons.c). */
#include "modseg.h"

extern void func_0012E348(s32 group, s32 volume);   /* snd_SetMasterVolume (core) */
extern void func_0012E380(s32 mono);                /* snd_SetPlaybackMode (core) */
const char *ee_msg_string_edition(s32 id);          /* src/text.c */
extern const int ee_text_first;

DECLARE_LVL(func_0022ED80);       /* menu sound(kind, id, moby) */
DECLARE_LVL(func_001F4630);       /* setup_gif_paging(0) */
DECLARE_LVL(func_001F4748);       /* do_gif_paging() */
DECLARE_LVL(func_001FE540);       /* msg_string(id) */
DECLARE_LVL(func_001F6CF8);       /* font_print_right(x, y, colour, text, limit) */
DECLARE_LVL(func_001F68E8);       /* font_print_large(x, y, colour, text, limit) */
DECLARE_LVL(func_00200198);       /* GetIconFrame(icon, sub) */
DECLARE_LVL(func_00201190);       /* a textured HUD quad, coordinates in 1/16 pixel */
DECLARE_LVL(func_00201640);       /* a filled rectangle */
DECLARE_LVL_DATA(menu_state);     /* D_001D5F70: +4 the current page, +8 the next, +0x124 the close lock */


typedef void (*SoundFn)(s32, s32, u32);
typedef void (*GifFn)(s32);
typedef void (*VoidFn)(void);
typedef const char *(*MsgFn)(s32);
typedef void (*FontFn)(s32, s32, u64, const char *, s32);
typedef s32 (*FrameFn)(s32, s32);
typedef void (*QuadFn)(s32, s32, s32, s32, s32, s32, s32, s32, s32, s32);
typedef void (*RectFn)(s32, s32, s32, s32, u64, s32);

#define STEREO          (*(volatile s32 *)0x0015EEE8)
#define MUSIC_VOLUME    (*(volatile s32 *)0x0015EEEC)
#define SFX_VOLUME      (*(volatile s32 *)0x0015EEF0)
#define MIXER           ((volatile s32 *)0x0013E698)   /* D_0013E650 + 0x48: the six groups' levels */
#define MOVIE_SAVED     (*(volatile u16 *)EE_SETTING_MOVIE_VOLUME)
#define PAD             0x0013CA40
#define PAD_HELD        (*(volatile u32 *)(PAD + 0x1C0))
#define PAD_PRESSED     (*(volatile u32 *)(PAD + 0x1C4))
#define BTN_TRIANGLE    0x10u
#define BTN_CROSS       0x40u
#define BTN_CLOSE       0xD00u          /* Start, Select and the third close button, as retail tests them */
#define BTN_UP          0x1000u
#define BTN_RIGHT       0x2000u
#define BTN_DOWN        0x4000u
#define BTN_LEFT        0x8000u

#define VOLUME_MAX      0x400
#define VOLUME_STEP     3

/* The widget: +0x14 the moby its sounds play at, +0x20/+0x24 its size, +0x40 the row. */
#define W_OWNER(w)      (*(u32 *)((w) + 0x14))
#define W_WIDTH(w)      (*(s32 *)((w) + 0x20))
#define W_HEIGHT(w)     (*(s32 *)((w) + 0x24))
#define W_ROW(w)        (*(s32 *)((w) + 0x40))

enum { ROW_SFX, ROW_MUSIC, ROW_MOVIE, ROW_STEREO, ROWS };

#define TEXT_SFX        0x5217          /* "Sound effects volume" */
#define TEXT_MUSIC      0x5218          /* "Music volume" */
#define TEXT_STEREO     0x5219          /* "Toggle stereo/mono" */
#define TEXT_MONO       0x521A
#define TEXT_STEREO_ON  0x521B
#define ICON_SLIDER     0xE99E          /* frame 8 the green bar, 9 the blue */
#define COLOUR_ROW      0x80FFA888u
#define COLOUR_SELECTED 0x8020FFFFu

static s32 ee_movie_volume = -1;        /* the session's; -1 until one is known */
static s32 cutscene;                    /* this frame's sound update took its cutscene branch */

static s32 movie_volume(void)
{
    u32 saved = MOVIE_SAVED;
    if (saved != 0 && saved <= VOLUME_MAX + 1) {
        ee_movie_volume = saved - 1;
    } else {
        if (ee_movie_volume < 0) {
            ee_movie_volume = SFX_VOLUME;
        }
        MOVIE_SAVED = (u16)(ee_movie_volume + 1);
    }
    return ee_movie_volume;
}

/* Groups 2 and 4 every frame, and group 5 at all three of its calls. */
void ee_movie_group_volume(s32 group, s32 volume)
{
    if (group == 5) {
        volume = movie_volume();
    } else if (cutscene && group == 2) {
        volume = movie_volume() * 8 / 10;
    } else if (cutscene && group == 4) {
        volume = movie_volume() * 7 / 10;
    }
    func_0012E348(group, volume);
}

/* The cutscene branch's groups 0 and 3, which retail halves there. */
void ee_volume_cutscene(s32 group, s32 volume)
{
    cutscene = 1;
    func_0012E348(group, (movie_volume() * (group == 0 ? 8 : 7) / 10) >> 1);
}

/* The other branch's group 0, as retail sends it. */
void ee_volume_play(s32 group, s32 volume)
{
    cutscene = 0;
    func_0012E348(group, volume);
}

static s32 step(s32 v, s32 d)
{
    v += d;
    return v < 0 ? 0 : v > VOLUME_MAX ? VOLUME_MAX : v;
}

static const char *text(s32 L, s32 id)
{
    return id >= ee_text_first ? ee_msg_string_edition(id) : LVL(L, func_001FE540, MsgFn)(id);
}

/* Pause_SoundOptionsInput with the Movie volume row. 1 closes the menu,
 * -1 closes the page, 0 stays. */
s32 ee_sound_input_c(u8 *w, s32 L)
{
    u8 *ms = LVL_DATA(L, menu_state, u8 *);
    u8 *page = *(u8 **)(ms + 4);
    u32 pressed = PAD_PRESSED;
    u32 held = PAD_HELD;
    s32 *row = &W_ROW(w);
    s32 previous, sfx, music, d;

    if ((pressed & BTN_CLOSE) && *(s32 *)(ms + 0x124) == 0) {
        return 1;
    }
    if (pressed & BTN_TRIANGLE) {
        u8 *back = *(u8 **)(page + 0x38);
        if (back != 0) {
            *(u8 **)(ms + 8) = back;
            return 0;
        }
        if (*(s32 *)(ms + 0x124) == 0) {
            return -1;
        }
    }
    previous = *row;
    if (pressed & BTN_UP) {
        *row = (previous + ROWS - 1) % ROWS;
    }
    if (pressed & BTN_DOWN) {
        *row = (*row + 1) % ROWS;
    }
    if (*row != previous || *(s32 *)(page + 0x80) != 0) {
        LVL(L, func_0022ED80, SoundFn)(1, 0x11, W_OWNER(w));
    }

    sfx = SFX_VOLUME;
    music = MUSIC_VOLUME;
    d = 0;
    if (held & BTN_RIGHT) {
        d += VOLUME_STEP;
    }
    if (held & BTN_LEFT) {
        d -= VOLUME_STEP;
    }
    if (d != 0) {
        if (*row == ROW_SFX) {
            SFX_VOLUME = step(sfx, d);
        } else if (*row == ROW_MUSIC) {
            MUSIC_VOLUME = step(music, d);
        } else if (*row == ROW_MOVIE) {
            ee_movie_volume = step(movie_volume(), d);    /* the sound update sends it */
            MOVIE_SAVED = (u16)(ee_movie_volume + 1);
        }
    }
    if (sfx != SFX_VOLUME || music != MUSIC_VOLUME) {   /* retail's mixer levels */
        s32 v = SFX_VOLUME;
        MIXER[0] = v * 8 / 10;
        MIXER[1] = MIXER[2] = MUSIC_VOLUME;
        MIXER[3] = v * 7 / 10;
        MIXER[4] = v * 7 / 10;
        MIXER[5] = v;
    }
    if (pressed & BTN_CROSS) {
        if (*row == ROW_STEREO) {
            STEREO = !STEREO;
        }
        func_0012E380(!STEREO);
        LVL(L, func_0022ED80, SoundFn)(0, 0x11, W_OWNER(w));
    }
    return 0;
}

/* DrawSoundMenu with the Movie volume row: rows at fifths of the panel's
 * height instead of quarters, each a right-aligned label and, for the
 * volumes, a slider bar right of the centre; the selected label is yellow. */
s32 ee_sound_draw_c(u8 *w, s32 L)
{
    s32 width = W_WIDTH(w);
    s32 height = W_HEIGHT(w);
    s32 cx = width >> 1;
    s32 r;

    LVL(L, func_001F4630, GifFn)(0);
    for (r = 0; r < ROWS; r++) {
        static const u16 labels[ROWS] = { TEXT_SFX, TEXT_MUSIC, EE_TEXT_MOVIE_VOLUME, TEXT_STEREO };
        s32 y = height * (r + 1) / (ROWS + 1);
        u64 colour = W_ROW(w) == r ? COLOUR_SELECTED : COLOUR_ROW;
        LVL(L, func_001F6CF8, FontFn)(cx - 8, y - 8, colour, text(L, labels[r]), -1);
        if (r == ROW_STEREO) {
            LVL(L, func_001F68E8, FontFn)(cx + 8, y - 8, COLOUR_ROW, text(L, STEREO != 0 ? TEXT_STEREO_ON : TEXT_MONO), -1);
        } else {
            s32 volume = r == ROW_SFX ? SFX_VOLUME : r == ROW_MUSIC ? MUSIC_VOLUME : movie_volume();
            s32 frame = r == ROW_MOVIE ? LVL(L, func_00200198, FrameFn)(EE_ICON_MOVIE_BAR, 0) : 0;
            s32 length = (width - (cx + 0x4A)) * volume / 1024;
            if (frame == 0) {          /* the movie bar's icon missing: green, as retail drew this volume */
                frame = LVL(L, func_00200198, FrameFn)(ICON_SLIDER, r == ROW_MUSIC ? 9 : 8);
            }
            LVL(L, func_00201640, RectFn)(cx + 7, y - 8, width - 0x3F, y + 8, 0x80696969u, 0);
            LVL(L, func_00201640, RectFn)(cx + 9, y - 6, width - 0x41, y + 6, 0x80383838u, 0);
            LVL(L, func_00201190, QuadFn)(frame, (cx + 9) << 4, (y - 6) << 4, (cx + 8 + length) << 4, (y + 5) << 4,
                                  0, 0xA0, 0x1F0, 0x150, 0x80);
        }
    }
    LVL(L, func_001F4748, VoidFn)();
    return 2;
}
