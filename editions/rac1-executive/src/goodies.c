/* rz2/rz6: the title screen's Goodies row, unlocked by beating the game,
 * and the Randomizer's pages and saves behind it.
 *
 * The row appears under Options on the title screen's start menu once Drek
 * has been beaten in the edition, or once the memory card holds a Challenge
 * Mode save (a save whose completion count is above 0, the Load Game list's
 * "R" badge) or a Randomizer save (one with a Randomizer record, the green
 * R). Nothing announces it. It is not the pause menu's Goodies.
 *
 * Where the flag lives. The save directory BESCES-50916RATCHET holds
 * save0.bin-save4.bin (one per slot) and a 15360-byte file named like the
 * directory. Retail writes that file once, when it creates the directory
 * (MemCard_UpdateState, func_00209E68, state 10: a RAM dump from
 * 0x15EE84), and never reads it back: the directory check only wants it
 * listed. So it is the card's one global record the 2002 game ignores. The
 * edition keeps its own 16 bytes in its tail (EDITION_OFFSET): a magic
 * word, a version, flags and the game's own save checksum over the first
 * 12 bytes; anything else there reads as "no flags". A directory the 2002
 * game re-creates loses the record, and with it the flag; the Challenge
 * Mode save condition still holds then. Beating Drek writes the record at
 * once (ee_drek_beaten); without a card, it is written at the next start
 * menu that finds one.
 *
 * Reading the card. The title screen reads no slot until a Load or Save
 * page opens, so the edition reads the card itself: the record, then each
 * slot's first bytes and its Randomizer record. It reads them in the
 * background while PRESS START shows, one command per frame in the card
 * machine's idle turns (ee_title_card_c), so the start menu opens at once
 * with what was read; it reads them again when the card changes, and
 * finishes a scan PRESS START left unfinished when the menu opens. A save
 * file starts {game blob size, level blob size}, then the game blob
 * {length, checksum, records {id, size, data}} from the save block table at
 * 0x1A05C0, whose third record (id 2) is the completion count 0x15EF20, at
 * file offset 0x30; the Randomizer's record sits in the twentieth level
 * blob (src/randomizer_save.c, ee_rz_record_offset). The calls are retail's
 * own libmc wrappers (core). The game's card state machine polls the card
 * all the time on the title screen, one command at a time: its busy flag
 * (MC_BUSY) makes it ask the command's result with a non-blocking sync each
 * frame, and clear the flag once the command is done. The background scan
 * issues its commands only between the machine's, and holds the machine
 * back while one runs. Where the edition reads at once (the start menu, a
 * pause page, Drek), it waits for the machine's command with a blocking
 * sync into the machine's own result words and clears the flag, which is
 * what the machine would have seen, then runs its commands to the end one
 * by one, as MemCard_CheckFreeSpace (func_00209BB8) does. It leaves the
 * card alone while the machine runs a longer sequence of its own (a
 * directory scan, a save). What it read stays until the next start menu: a
 * card swapped while the menu is open shows its badges and seeds at the
 * next PRESS START.
 *
 * The row. The start menu (page D_001D4948) is three one-row text lists,
 * New Game, Load Game and Options, in panels 2-4 (screens 2-4), linked up
 * and down in a loop. A page has 14 panels, mobys of class 0x472, each
 * playing the page's sequence for it (moby_anims); panels without a screen
 * park off screen. The Goodies row is a fourth list in panel 0, whose
 * screen holds an empty placeholder, and the four rows are centred where
 * the three were, with sequences the edition builds from the rows' own
 * (install_sequences). Pause_Open (func_00219E90) creates the panels and
 * starts them on the page's sequences, and the page switch that follows in
 * the same Pause_Update (func_0021A1A0) plays them in: its call of
 * Pause_Open in the title screen's programs goes to
 * ee_title_menu_open_19/20, which decides, lets Pause_Open run with the
 * retail page, then installs the sequences and adds the row before the
 * switch reads the page. The row opens the Goodies page, Randomizer, then
 * New Game or Reuse Seed (build_pages).
 *
 * Randomizer saves (rz2). The Randomizer's New Game is retail's New Game
 * page (title box, the five save slots, the picture of the selected save's
 * planet, the card's status text, "Select / Exit") with a copy of the slot
 * list whose update arms a Randomizer game (src/randomizer_save.c) while
 * it runs, so that the save the list creates gets a seed and a layout; the
 * overwrite question (CONFIRM_PAGE) comes back to this page rather than
 * retail's while the Randomizer's is the one asking. A save so made shows a
 * green R where Challenge Mode's is (EE_ICON_RANDOMIZER; the badge draw
 * wraps the slot lists' own, Pause_DrawSaveSlots) and, under the two lines
 * naming its planet ("Robot Plant / Quartu"), a third: "Seed" and the nine
 * buttons of its code; that box is stretched down by a line whenever the
 * card holds such a save. Reuse Seed takes nine presses, then Cross (the
 * prompt box offers Confirm and Exit) decodes them (src/randomizer.c) and
 * opens the same New Game with that seed; a code that fails its check, or
 * carries another logic version, is cleared and "Invalid seed code" shows
 * in red until the next press. */
#include "randomizer.h"

typedef void (*VoidFn)(void);

/* libmc's calls, through retail's wrappers (core). */
extern s32 func_001238B0(s32 port, s32 slot, const char *name, s32 mode);   /* sceMcOpen */
extern s32 func_00123A10(s32 fd);                                            /* sceMcClose */
extern s32 func_00123AC8(s32 fd, s32 offset, s32 origin);                    /* sceMcSeek */
extern s32 func_00123C30(s32 fd, void *buffer, s32 size);                    /* sceMcRead */
extern s32 func_00123D48(s32 fd, const void *buffer, s32 size);              /* sceMcWrite */
extern s32 func_00123F30(s32 mode, s32 *command, s32 *result);               /* sceMcSync */
extern void func_00122598(s32);                                              /* waits a moment, as the game does between syncs */

#define MC_OPEN_READ    1
#define MC_OPEN_WRITE   2

/* The card state machine's record (D_0013D390, core). */
#define MC_PORT         (*(volatile s32 *)0x0013D390)
#define MC_SLOT         (*(volatile s32 *)0x0013D394)
#define MC_TYPE         (*(volatile s32 *)0x0013D398)    /* +8: 2 is a PS2 card */
#define MC_COMMAND      ((s32 *)0x0013D450)              /* +0xC0, +0xC4: the pending command's result words */
#define MC_RESULT       ((s32 *)0x0013D454)
#define MC_BUSY         (*(volatile s32 *)0x0013D464)    /* +0xD4: a command is running */
#define MC_STATE        (*(volatile s32 *)0x0013D46C)    /* +0xDC: 0-2 polling the card, 3 and up a sequence */
#define MC_REQUEST      (*(volatile s32 *)0x0013D474)    /* +0xE4: a requested sequence, -1 none */
#define MC_MAIN_FILE    ((const char *)0x0013D340)       /* "/BESCES-50916RATCHET/BESCES-50916RATCHET" */
#define MC_DIR_LENGTH   21                               /* "/BESCES-50916RATCHET/" */
#define MC_SLOTS        5

#define EDITION_OFFSET  0x3BF0      /* the record: the main file's last 16 bytes */
#define EDITION_MAGIC   0x47444545  /* "EEDG" */
#define EDITION_VERSION 1
#define EDITION_DREK    1           /* flags: Drek has been beaten */
#define EDITION_START_SHIFT 1       /* flags bits 1-2: the Randomizer Options page's Starting Planet (RzStart) */
#define EDITION_START_MASK  (3 << EDITION_START_SHIFT)

#define SAVE_HEAD       0x40        /* a save file's first bytes: sizes, blob header, records 0-2 */
#define SAVE_COMPLETES  (0x30 / 4)  /* the completion count's word in them */

/* The start menu, in both title screen programs' data (at the executable's
 * addresses; tools/gen_frontend.py checks it). */
#define TITLE_PAGE      0x001D4948
#define TITLE_NEW_GAME  0x001D4A18  /* then Load Game, then Options */
#define TITLE_OPTIONS   0x001D4AB8
#define TITLE_LIST_STRIDE 0x50
#define TITLE_PLACEHOLDER 0x001CE678 /* the start menu's screens 0 and 1: an empty list, no update or draw */
#define PANELS_ADDRESS  0x001D6120                       /* the 14 panel mobys */
#define PANELS          ((volatile u32 *)PANELS_ADDRESS)
#define PANEL_ROW       0           /* the Goodies row's screen and panel */
#define PANEL_ROW_PARKED 0          /* the start menu's sequence for panel 0: parked */
#define ROW_FIRST_PANEL 2           /* New Game, Load Game, Options: panels 2-4 */
#define SEQ_ROW         218         /* the sequence id the row's panel plays, which no page plays */
#define SEQ_COPIES      36          /* the row's, the rows' 3, the opened pages' 8, the Goodies pages' 13, the confirm page's picture */
#define SEQ_FRAMES      8
#define FRAME_QWC       8
#define FRAME_RECORDS_MAX 8
#define TEXT_GOODIES    0x4FC2      /* "Goodies", the pause menu's own word */
#define TEXT_NEW_GAME   0x4F2A      /* "New Game" */
#define ACTION_OPEN     3           /* Pause_TextListUpdate: open the page in param */
#define ACTION_FREEZE   4           /* the same through the game-mode freeze, as New Game does (mode_freeze_init(3, page)) */

/* The pages behind the row, built from the title screen's own (both
 * programs keep them at these addresses; tools/gen_frontend.py checks). */
#define MENU_CURRENT    (*(volatile u32 *)0x001D5F74)    /* D_001D5F70 + 4: the page shown */
#define MENU_NEXT       (*(volatile u32 *)0x001D5F78)    /* + 8: the page to switch to */
#define MENU_TICKS      (*(volatile s32 *)0x001D60C4)    /* + 0x154: updates since the card went idle; the pages wait for 11 */
#define MENU_LOAD_KIND  (*(volatile s32 *)0x001D60C0)    /* + 0x150: what the freeze opens, 0 a new game, 1 a load (the lists' actions 4 and 5) */
#define GAME_FLAGS      (*(volatile s32 *)0x0015EFB4)    /* D_0015EFB4: bit 2 a new game being started, bit 4 a load (the same actions) */
#define PAD_PRESSED     (*(volatile u32 *)0x0013CBF4)    /* D_0013CA40 + 0x1B4: buttons newly down, no stick directions */
#define PAGE_FOCUS      0x40
#define PAGE_WORDS      (0x88 / 4)
#define PAGE_BACK_WORD  (0x38 / 4)
#define PAGE_FOCUS_WORD (0x40 / 4)
#define PAGE_SCREEN_WORD(n) ((0x44 + 4 * (n)) / 4)
#define WIDGET_WORDS    (0x50 / 4)
#define WIDGET_DRAW     1           /* a widget's words: update, draw, enter, leave */
#define OPTIONS_PAGE    0x001D4B08  /* title box, three-row list, "Select / Exit" box */
#define OPTIONS_TITLE   0x001D0C58  /* a title box: its text id at + 0x34 */
#define OPTIONS_LIST    0x001D4BE8
#define SELECT_PROMPT   0x001CFE90  /* "\x10 Select", "\x12 Exit" */
#define VIDEO_PAGE      0x001D4E38  /* title box, a wider list box, "Select / Exit" box */
#define VIDEO_TITLE     0x001D4EC0
#define EXIT_PROMPT     0x001CE7B8  /* "\x12 Exit" alone */
#define NEW_GAME_PAGE   0x001D5008
#define NEW_GAME_SLOTS  0x001D50E8  /* its slot list (SaveSlotMenu_Update, Pause_DrawSaveSlots): screen 1, the focus */
#define LOAD_GAME_SLOTS 0x001D1BB0  /* Load Game's (Pause_LoadSlotInput, Pause_DrawSaveSlots) */
#define NAME_BOX        0x001D1B08  /* both pages' screen 3: the selected save's planet named in two lines */
#define NAME_PANEL      3
#define CONFIRM_PAGE    0x001D51B8  /* New Game's "overwrite?" question; its back (+0x38) returns to the slot list */
#define SEQ_CONFIRM_NAME 0x84       /* the confirm page's sequence for the name panel */
#define LIST_ENTER      2           /* a list's enter callback (word 2): Options' sets its rows, so the copies have none */
#define SEED_FLAGS      0x42        /* list flags: one colour (2), left-aligned text (0x40) */
#define TEXT_EXIT       0x4EE0      /* "\x12 Exit", the prompt boxes' row */
#define SEED_BOX_PX     240         /* the Reuse Seed box's width: nine symbols from the list draw's x 4 */
#define NAME_BOX_PX     322         /* the name box's width in pixels (measured), which gives record units per pixel */
#define TEXT_RED        0x800F0FFF  /* the HUD's red (0xFF0F0FFF) at the menus' alpha */
#define FONT_HEIGHT     12          /* the list draw's normal font: a row's text sits row_height - 6 - 1 from its top */
#define GOODIES_LIST_HALF    1250   /* the list box's half height for one row (Options' is 3770 for three) */
#define TEXT_OPTIONS    0x4EE7      /* "Options", the start menu's own word */
/* Sequence ids no page plays (checked over the executable's page records):
 * the Goodies, Randomizer and Reuse Seed pages' boxes (title, list, prompt),
 * the Randomizer New Game page's boxes, and its title box as retail has it
 * (retail's 196 now starts at the Goodies row). */
static const u8 goodies_ids[3] = { 216, 217, 211 };
static const u8 randomizer_ids[3] = { 3, 83, 138 };
static const u8 reuse_ids[3] = { 139, 140, 141 };
static const u8 new_game_ids[3] = { 183, 184, 174 };
#define SEQ_NEW_GAME_TITLE 173

/* The save pages' drawing (exe-kind functions: each title program has its
 * own copy). A widget's draw gets the widget, whose width and height
 * (+0x20, +0x24) are the box's in pixels. */
DECLARE_LVL(func_00200198);             /* GetIconFrame(icon, frame) */
DECLARE_LVL(func_00200468);             /* HudSprite(frame, x, y, w, h, alpha) */
DECLARE_LVL(func_001F4630);             /* SetupGifPaging(0): before a widget's own drawing */
DECLARE_LVL(func_001F4748);             /* DoGifPaging: after it */
DECLARE_LVL_DATA(menu_state);           /* the menu system's record (D_001D5F70 in the title programs): + 0x154 the card's idle ticks */
DECLARE_LVL(func_001F6EA8);             /* FontPrintCenter(x, y, colour, text, count): the normal font, centred on x */
DECLARE_LVL(func_001F69F0);             /* Font_DrawString(colour, text, count, texture, font, x, y, scale): the normal font, scaled */
DECLARE_LVL(func_001F4868);             /* GetEffectTex(n): the texture of effect n (1: the normal font's sheet) */
DECLARE_LVL(func_001FE540);             /* msg_string(id) */
DECLARE_LVL(func_001FBC80);             /* Freeze_Enter(mode, page, arg): the game-mode freeze a list's action 4 opens a page through */
typedef void (*FreezeFn)(s32, void *, s32);
typedef s32 (*WidgetFn)(void *);
typedef s32 (*IconFn)(s32, s32);
typedef void (*SpriteFn)(s32, s32, s32, s32, s32, s32);
typedef void (*PagingFn)(s32);
typedef void (*PrintFn)(s32, s32, u64, const char *, s32);
typedef void (*ScaledPrintFn)(u64, const char *, s32, u64, const void *, f32, f32, f32);
typedef u64 (*EffectTexFn)(s32);
typedef const char *(*MsgFn)(s32);
#define MC_SLOT_LEVEL(slot) (*(volatile s32 *)(0x0013D390 + 0x20 + 0x1C * (slot)))  /* the card's summary of a slot: its level, -1 empty */
#define MC_SLOT_TIME(slot)  (*(volatile s32 *)(0x0013D390 + 0x2C + 0x1C * (slot)))  /* its play time */
#define MC_SUMMARY_STATE    (*(volatile s32 *)(0x0013D390 + 8))                      /* 2: the summaries are read */
#define MENU_TICKS_OFFSET   0x154       /* MENU_TICKS in the menu record (the levels' is elsewhere: LVL_DATA menu_state) */
#define PAL_MODE        (*(volatile s32 *)0x0015EE80)    /* 50 Hz: the slot list's rows are 0x24 apart after their 0x30, not 0x1B */
#define WIDGET_WIDTH    (0x20 / 4)
#define WIDGET_HEIGHT   (0x24 / 4)
#define WIDGET_SLOT     (0x40 / 4)  /* a slot list's selected slot; the name box names the focused list's */
#define BADGE_X         0x4E        /* where Pause_DrawSaveSlots puts the Challenge Mode R */
#define BADGE_SIZE      16
#define SLOT_ROW        0x30
#define TEXT_NORMAL     0x80FFA888  /* the save pages' text colour */
#define NAME_LINE       16          /* the name box's lines: y 4 and 20 (DrawSaveLevelName, func_00223E40) */
#define NAME_FIRST_Y    4
#define SEED_GAP        8           /* between "Seed" and the symbols */
#define TEXT_MARGIN     6
#define GLYPH_ADVANCE   25          /* the seed code's symbols (install_glyphs) */
#define FONT_NORMAL_TEX 1           /* GetEffectTex's normal font */
#define STRETCH_NUM     3           /* the name box's bottom corners move down by 3/4 of its half height: one more line */
#define STRETCH_DEN     4

struct MenuPage {
    s32 moby_anims[14];
    u32 back;
    s32 state;
    u32 focus;
    u32 screens[14];
};

struct MenuItem {
    s16 text;
    s16 action;
    s32 param;
    s16 subtext;
    s16 fade_timer;
};

struct MenuList {
    u32 update;
    u32 draw;
    u32 enter;
    u32 leave;
    s32 unk10;
    u32 moby;
    s32 x;
    s32 y;
    s32 width;
    s32 height;
    s32 pad28[2];
    s32 flags;
    struct MenuItem *items;
    u32 up;
    u32 down;
    s32 selected;
    s32 scroll;
};

/* A slot as last read from the card. */
struct SlotScan {
    u8 valid;                           /* a well-formed save */
    u8 challenge;                       /* its completion count is above 0 */
    struct RzSlot rz;                   /* its Randomizer record, if any */
};

static u8 drek_pending;             /* beaten this session, not yet on the card */
static u8 card_flags;               /* the record's flags as last read */
static u8 start_setting;            /* the Starting Planet (RzStart): the record's, or as changed this session */
static u8 start_unsaved;            /* changed on the Randomizer Options page and not yet on the card */

/* The Starting Planet the record's FLAGS hold; Novalis for none. */
static s32 start_of_flags(s32 flags)
{
    s32 start = (flags & EDITION_START_MASK) >> EDITION_START_SHIFT;
    return start < RZ_START_MODES ? start : RZ_START_NOVALIS;
}
static u8 card_buffer[EE_RZ_RECORD_SIZE] __attribute__((aligned(64)));
static struct SlotScan slots[MC_SLOTS];
static s32 rz_saves;                /* Randomizer saves on the card as last scanned */
static s32 scan_summary[2 * MC_SLOTS];  /* the card's summary of the slots (level, time) at that scan */
static s32 title_program;           /* the title screen program running the pages (19, 20) */

static struct MenuItem goodies_items[2] = { { TEXT_GOODIES, ACTION_OPEN, 0, 0, 0 }, { 0, 0, 0, 0, 0 } };
static struct MenuList goodies_list;
static u32 seq_store[SEQ_COPIES][16] __attribute__((aligned(16)));     /* 0x1C header + 8 frame pointers, 16-aligned */
static u32 frame_store[SEQ_COPIES][SEQ_FRAMES][4 * (1 + FRAME_QWC)] __attribute__((aligned(16)));

/* The card command just issued, run to its end: its result, or -1 if it could not be issued. */
static s32 card_finish(s32 issued)
{
    s32 command, result;
    if (issued != 0) {
        return -1;
    }
    while (func_00123F30(1, &command, &result) == 0) {
        func_00122598(0);
    }
    return result;
}

static void scan_settle(void);

/* The card is free for the edition's calls: the scan's command and the
 * machine's, if any, finished; 0 while the machine runs a sequence. */
static s32 card_take(void)
{
    scan_settle();
    if (MC_STATE >= 3 || MC_REQUEST >= 0) {
        return 0;
    }
    if (MC_BUSY) {
        func_00123F30(0, MC_COMMAND, MC_RESULT);
        MC_BUSY = 0;
    }
    return MC_MAIN_FILE[0] == '/';
}

/* The card is idle and a PS2 card, as the save pages require before they show a slot. */
static s32 card_ready(void)
{
    return MC_STATE < 3 && MC_REQUEST < 0 && MENU_TICKS >= 11 && MC_TYPE == 2;
}

/* SIZE bytes at OFFSET of the file NAME into card_buffer; 0 if any step fails. */
static s32 card_read(const char *name, s32 offset, s32 size)
{
    s32 fd = card_finish(func_001238B0(MC_PORT, MC_SLOT, name, MC_OPEN_READ));
    s32 ok;
    if (fd < 0) {
        return 0;
    }
    ok = card_finish(func_00123AC8(fd, offset, 0)) == offset
         && card_finish(func_00123C30(fd, card_buffer, size)) == size;
    card_finish(func_00123A10(fd));
    return ok;
}

static s32 card_write(const char *name, s32 offset, s32 size)
{
    s32 fd = card_finish(func_001238B0(MC_PORT, MC_SLOT, name, MC_OPEN_WRITE));
    s32 ok;
    if (fd < 0) {
        return 0;
    }
    ok = card_finish(func_00123AC8(fd, offset, 0)) == offset
         && card_finish(func_00123D48(fd, card_buffer, size)) == size;
    card_finish(func_00123A10(fd));
    return ok;
}

/* The flags of the edition's record in card_buffer, 0 for none or a damaged record. */
static s32 record_flags(void)
{
    u32 *words = (u32 *)card_buffer;
    if (words[0] != EDITION_MAGIC || (words[1] & 0xFFFF) != EDITION_VERSION
        || words[3] != ee_save_checksum(card_buffer, 12)) {
        return 0;
    }
    return words[2];
}

/* The edition's record from the main file: its flags. */
static s32 read_record(void)
{
    if (!card_read(MC_MAIN_FILE, EDITION_OFFSET, 16)) {
        return 0;
    }
    return record_flags();
}

static s32 write_record(s32 flags)
{
    u32 *words = (u32 *)card_buffer;
    words[0] = EDITION_MAGIC;
    words[1] = EDITION_VERSION;
    words[2] = flags;
    words[3] = ee_save_checksum(card_buffer, 12);
    return card_write(MC_MAIN_FILE, EDITION_OFFSET, 16);
}

/* The card scan: the edition's record from the main file, then each slot's
 * file: whether it holds a well-formed save, whether that is a Challenge
 * Mode save, and its Randomizer record. It runs one libmc command at a time,
 * so the title screen can run it in the background, a command per turn
 * (ee_title_card_c), and the start menu or a pause page can run the rest to
 * its end at once (scan_run). */
#define SCAN_MAIN       (-1)        /* scan.file: the main file, the edition's record */
#define SCAN_DONE       MC_SLOTS    /* scan.file: no scan running */
#define MC_CARD_RESULT  (*(volatile s32 *)0x0013D3AC)    /* +0x1C: its last sceMcGetInfo result, 0 the same card */
#define MC_FREE         (*(volatile s32 *)0x0013D39C)    /* +0xC: the card's free space at that check */

enum ScanStep {
    STEP_OPEN,
    STEP_SEEK_EDITION,
    STEP_READ_EDITION,
    STEP_READ_HEAD,
    STEP_SEEK_RZ,
    STEP_READ_RZ,
    STEP_CLOSE
};

static struct {
    s8 file;                /* SCAN_MAIN, a slot, or SCAN_DONE */
    u8 step;
    u8 in_flight;           /* the step's command is running */
    u8 redo;                /* the step's result went to another caller: read the file again once it is closed */
    s32 fd;
    s32 record_offset;      /* the Randomizer record's offset in a save file, 0 for none */
    s32 command;            /* the running command's words for sceMcSync */
    s32 result;
    s32 card_free;          /* the card the scan read: MC_FREE, card_ok() and scan_summary at its start */
    s32 card_ok;
    char name[MC_DIR_LENGTH + 10];
} scan = { SCAN_DONE };
volatile u32 ee_debug_card[4];  /* PINE: title frames, background commands, scans finished, commands run at once */
static u8 scan_valid;       /* a scan has finished, so its results stand for the card it read */
/* A scan reads into these and publishes them into slots, rz_saves and
 * card_flags when it finishes, so the badges and the seed lines keep
 * showing the last scan while a new one runs (a scan can take many frames,
 * and stall while a save page keeps the card busy: the Reuse Seed page lost
 * every badge until PRESS START). */
static struct SlotScan staging[MC_SLOTS];
static s32 staging_rz_saves;
static u8 staging_flags;

/* The machine's last check found the same PS2 card as before. */
static s32 card_ok(void)
{
    return MC_CARD_RESULT == 0 && MC_TYPE == 2;
}

/* "/BESCES-50916RATCHET/saveN.bin" for slot SLOT, in scan.name. */
static const char *save_name(s32 slot)
{
    const char *file = "save0.bin";
    s32 i;
    for (i = 0; i < MC_DIR_LENGTH; i++) {
        scan.name[i] = MC_MAIN_FILE[i];
    }
    for (i = 0; file[i] != 0; i++) {
        scan.name[MC_DIR_LENGTH + i] = file[i];
    }
    scan.name[MC_DIR_LENGTH + 4] = '0' + slot;
    scan.name[MC_DIR_LENGTH + i] = 0;
    return scan.name;
}

/* The current step's command: 0 once issued. */
static s32 scan_issue(void)
{
    switch (scan.step) {
    case STEP_OPEN:
        if (scan.file == SCAN_MAIN) {
            return func_001238B0(MC_PORT, MC_SLOT, MC_MAIN_FILE, MC_OPEN_READ);
        }
        return func_001238B0(MC_PORT, MC_SLOT, save_name(scan.file), MC_OPEN_READ);
    case STEP_SEEK_EDITION:
        return func_00123AC8(scan.fd, EDITION_OFFSET, 0);
    case STEP_READ_EDITION:
        return func_00123C30(scan.fd, card_buffer, 16);
    case STEP_READ_HEAD:
        return func_00123C30(scan.fd, card_buffer, SAVE_HEAD);
    case STEP_SEEK_RZ:
        return func_00123AC8(scan.fd, scan.record_offset, 0);
    case STEP_READ_RZ:
        return func_00123C30(scan.fd, card_buffer, EE_RZ_RECORD_SIZE);
    }
    return func_00123A10(scan.fd);
}

static void scan_next_file(void)
{
    if (scan.redo) {
        scan.redo = 0;
    } else {
        scan.file++;
    }
    scan.step = STEP_OPEN;
    if (scan.file == SCAN_DONE) {
        s32 slot;
        for (slot = 0; slot < MC_SLOTS; slot++) {
            slots[slot] = staging[slot];
        }
        rz_saves = staging_rz_saves;
        card_flags = staging_flags;
        if (!start_unsaved) {
            start_setting = start_of_flags(card_flags);
        }
        scan_valid = 1;
        ee_debug_card[2]++;
    }
}

/* The current step's RESULT, and the step after it. */
static void scan_result(s32 result)
{
    u32 *words = (u32 *)card_buffer;
    switch (scan.step) {
    case STEP_OPEN:
        if (result < 0) {
            scan_next_file();
            return;
        }
        scan.fd = result;
        scan.step = scan.file == SCAN_MAIN ? STEP_SEEK_EDITION : STEP_READ_HEAD;
        return;
    case STEP_SEEK_EDITION:
        if (result == EDITION_OFFSET) {
            scan.step = STEP_READ_EDITION;
            return;
        }
        break;
    case STEP_READ_EDITION:
        if (result == 16) {
            staging_flags = record_flags();
        }
        break;
    case STEP_READ_HEAD:
        /* {sizes}, {length = size - 8, checksum}, {0, 4, level}, {1, 4, bolts}, {2, 4, completions} */
        if (result != SAVE_HEAD || words[2] != words[0] - 8 || words[4] != 0 || words[7] != 1
            || words[10] != 2 || words[11] != 4) {
            break;
        }
        staging[scan.file].valid = 1;
        staging[scan.file].challenge = (s32)words[SAVE_COMPLETES] > 0;
        if (scan.record_offset > 0) {
            scan.step = STEP_SEEK_RZ;
            return;
        }
        break;
    case STEP_SEEK_RZ:
        if (result == scan.record_offset) {
            scan.step = STEP_READ_RZ;
            return;
        }
        break;
    case STEP_READ_RZ:
        if (result == EE_RZ_RECORD_SIZE) {
            ee_rz_record_parse(card_buffer, &staging[scan.file].rz);
            staging_rz_saves |= staging[scan.file].rz.present;
        }
        break;
    case STEP_CLOSE:
        scan_next_file();
        return;
    }
    scan.step = STEP_CLOSE;
}

/* The current step's command finished, but its result went to another
 * sceMcSync caller (the game's own loops that run the machine directly,
 * while the scan's command ran, as when the title screen is left). */
static void scan_lost(void)
{
    if (scan.step == STEP_OPEN) {
        return;                 /* open the file again; the lost one's number is unknown */
    }
    if (scan.step == STEP_CLOSE) {
        scan_next_file();
        return;
    }
    scan.redo = 1;
    scan.step = STEP_CLOSE;
}

/* The scan's running command: 1 once it has finished and the scan has
 * moved on, 0 while it runs. WAIT waits for it. */
static s32 scan_poll(s32 wait)
{
    s32 status;
    for (;;) {
        status = func_00123F30(1, &scan.command, &scan.result);
        if (status != 0 || !wait) {
            break;
        }
        func_00122598(0);
    }
    if (status == 0) {
        return 0;
    }
    scan.in_flight = 0;
    if (status < 0) {
        scan_lost();
    } else {
        scan_result(scan.result);
    }
    return 1;
}

/* The scan's command, if one runs, finished: the card is free for another caller. */
/* The edition's record written in the background, a libmc command per
 * frame in the card machine's idle turns (ee_title_card_c), as the scan
 * reads: write_record's four blocking commands stall the pages for about a
 * second. It runs while the start menu is open too: the Randomizer pages
 * ask the machine for nothing, and the machine waits while a command of the
 * job's runs. */
enum RecordStep {
    RECORD_IDLE,
    RECORD_OPEN,
    RECORD_SEEK,
    RECORD_WRITE,
    RECORD_CLOSE
};

static struct {
    u8 step;
    u8 in_flight;
    s32 fd;
    s32 flags;              /* the flags it writes */
    s32 command;
    s32 result;
} record_job;
static u8 record_job_buffer[16] __attribute__((aligned(64)));

static void record_job_start(s32 flags)
{
    u32 *words = (u32 *)record_job_buffer;
    words[0] = EDITION_MAGIC;
    words[1] = EDITION_VERSION;
    words[2] = flags;
    words[3] = ee_save_checksum(record_job_buffer, 12);
    record_job.flags = flags;
    record_job.step = RECORD_OPEN;
}

/* The current step's command: 0 once issued. */
static s32 record_job_issue(void)
{
    switch (record_job.step) {
    case RECORD_OPEN:
        return func_001238B0(MC_PORT, MC_SLOT, MC_MAIN_FILE, MC_OPEN_WRITE);
    case RECORD_SEEK:
        return func_00123AC8(record_job.fd, EDITION_OFFSET, 0);
    case RECORD_WRITE:
        return func_00123D48(record_job.fd, record_job_buffer, 16);
    }
    return func_00123A10(record_job.fd);
}

/* The current step's RESULT (-1 when it went to another sceMcSync caller), and the step after it. A record
 * written becomes the card's flags; a Starting Planet changed again meanwhile stays unsaved for the next job. */
static void record_job_result(s32 result)
{
    switch (record_job.step) {
    case RECORD_OPEN:
        if (result < 0) {
            record_job.step = RECORD_IDLE;      /* no save directory, or no card: the setting stays for the session */
            return;
        }
        record_job.fd = result;
        record_job.step = RECORD_SEEK;
        return;
    case RECORD_SEEK:
        record_job.step = result == EDITION_OFFSET ? RECORD_WRITE : RECORD_CLOSE;
        return;
    case RECORD_WRITE:
        if (result == 16) {
            card_flags = (u8)record_job.flags;
            start_unsaved = start_of_flags(record_job.flags) != start_setting;
        }
        record_job.step = RECORD_CLOSE;
        return;
    }
    record_job.step = RECORD_IDLE;
}

/* The job's running command: 1 once it has finished and the job has moved on, 0 while it runs. WAIT waits. */
static s32 record_job_poll(s32 wait)
{
    s32 status;
    for (;;) {
        status = func_00123F30(1, &record_job.command, &record_job.result);
        if (status != 0 || !wait) {
            break;
        }
        func_00122598(0);
    }
    if (status == 0) {
        return 0;
    }
    record_job.in_flight = 0;
    record_job_result(status < 0 ? -1 : record_job.result);
    return 1;
}

static void scan_settle(void)
{
    if (scan.in_flight) {
        scan_poll(1);
    }
    if (record_job.in_flight) {
        record_job_poll(1);
    }
}

/* A new scan for the title program or level PROGRAM, with its results cleared. */
static void scan_start(s32 program)
{
    s32 slot;
    if (scan.file != SCAN_DONE && scan.step != STEP_OPEN) {
        card_finish(func_00123A10(scan.fd));    /* the file an unfinished scan left open */
    }
    scan.file = SCAN_MAIN;
    scan.step = STEP_OPEN;
    scan.redo = 0;
    scan.record_offset = ee_rz_record_offset(program);
    scan.card_free = MC_FREE;
    scan.card_ok = card_ok();
    scan_valid = 0;
    staging_flags = 0;
    staging_rz_saves = 0;
    for (slot = 0; slot < MC_SLOTS; slot++) {
        staging[slot].valid = 0;
        staging[slot].challenge = 0;
        staging[slot].rz.present = 0;
        scan_summary[2 * slot] = MC_SLOT_LEVEL(slot);
        scan_summary[2 * slot + 1] = MC_SLOT_TIME(slot);
    }
}

/* The scan's remaining steps, each run to its end. The card must be taken (card_take). */
static void scan_run(void)
{
    while (scan.file != SCAN_DONE) {
        if (!scan.in_flight) {
            if (scan_issue() != 0) {
                if (scan.step != STEP_OPEN) {
                    card_finish(func_00123A10(scan.fd));
                }
                scan.file = SCAN_DONE;      /* libmc refused it; the title screen tries again later */
                return;
            }
            scan.in_flight = 1;
            ee_debug_card[3]++;
        }
        scan_poll(1);
    }
}

/* The card's summary of the slots differs from the last scan's: a save made
 * since (its play time moved on), another card, or no scan yet. */
static s32 summary_changed(void)
{
    s32 slot;
    for (slot = 0; slot < MC_SLOTS; slot++) {
        if (scan_summary[2 * slot] != MC_SLOT_LEVEL(slot) || scan_summary[2 * slot + 1] != MC_SLOT_TIME(slot)) {
            return 1;
        }
    }
    return 0;
}

/* The card may differ from the one the last scan read: its slots' summary,
 * its free space or the machine's check moved on (a save made in a level,
 * another card, no card). */
static s32 card_changed(void)
{
    return summary_changed() || scan.card_free != MC_FREE || scan.card_ok != card_ok();
}

/* Drek is beaten. Once the ending's movie has played, Drek's update
 * (FinalBossUpdate, func_L18_002F4050, state 0x1B's last phase) opens the
 * choice between going back in time and Challenge Mode by calling the pause
 * entry (func_00219C70) with 0x21: once per win, whichever is chosen, and
 * before either restores (the time warp loads the snapshot taken before the
 * fight) or resets (Challenge Mode) the game state, so no saved byte would
 * keep a flag set here. The call goes here instead (tools/overlay_patch.py
 * goodies, level 18 only): the flag goes on the card's record now if it
 * can, else at the next start menu, then the menu opens as in retail. */
DECLARE_LVL(func_00219C70);             /* the pause entry */
#define VELDIN_2        18
typedef void (*PauseFn)(s32);

void ee_drek_beaten(s32 menu)
{
    drek_pending = 1;
    if (card_take() && write_record(read_record() | EDITION_DREK)) {
        card_flags |= EDITION_DREK;
        drek_pending = 0;
    }
    LVL(VELDIN_2, func_00219C70, PauseFn)(menu);
}

/* The unlock: Drek beaten (on the card or this session), or a Challenge
 * Mode or Randomizer save on the card. The title screen's background scan
 * has usually read the card by now; whatever it has left runs here, as does
 * a scan of a card that has changed since. */
static s32 goodies_unlocked(void)
{
    s32 slot;
    if (card_take()) {
        if (scan.file == SCAN_DONE && (!scan_valid || card_changed())) {
            scan_start(title_program);
        }
        scan_run();
        if (drek_pending && !(card_flags & EDITION_DREK) && write_record(card_flags | EDITION_DREK)) {
            card_flags |= EDITION_DREK;
            drek_pending = 0;
        }
    } else if (!scan_valid) {
        return drek_pending;
    }
    if (drek_pending || (card_flags & EDITION_DREK) || rz_saves) {
        return 1;
    }
    for (slot = 0; slot < MC_SLOTS; slot++) {
        if (slots[slot].challenge) {
            return 1;
        }
    }
    return 0;
}

/* A panel sequence: a 0x1C-byte header (+0: bounding sphere {x, y, z, r} in
 * record units; +0x10: frame count) and its frames' pointers. A frame: a
 * 16-byte header (+6: quadwords after it; +12: offset of its records after
 * the header; +14: record count), then per joint a record {s16 x, s16 y,
 * s16 z, u8 joint, u8 flag}: joint 0 the panel's centre (x depth, y across,
 * z up), joints 1-4 its corners relative to it. */
#define FRAME_QWC_OF(frame)     (((u16 *)(frame))[3])
#define FRAME_RECORDS(frame)    ((s16 *)((u8 *)(frame) + 16 + ((u16 *)(frame))[6]))
#define FRAME_COUNT(frame)      (((u16 *)(frame))[7])
#define SEQ_FRAME_COUNT(seq)    (*(u8 *)((seq) + 0x10))
#define SEQ_FRAME(seq, f)       (*(u32 *)((seq) + 0x1C + 4 * (f)))
#define RECORD_JOINT(rec, k)    (((u8 *)&(rec)[4 * (k)])[6])

static u32 *class_sequences;        /* the panels' class's sequence table */
static s32 installed;               /* copies in it */
static u32 seq_id[SEQ_COPIES];
static u32 seq_original[SEQ_COPIES];
static u32 seq_installed[SEQ_COPIES];   /* what the table points at for each */
static s32 opened_panel0[3];        /* the opened pages' retail sequence for panel 0 */
volatile u32 ee_debug_rz[4];        /* the name box's width and height as last drawn, the seed line's width, its scale * 1000 (PINE) */

/* The resting z of sequence SEQ's centre: its first frame's. */
static s32 rest_z(u32 seq)
{
    return FRAME_RECORDS(SEQ_FRAME(seq, 0))[2];
}

static s32 is_installed(s32 id)
{
    s32 i;
    for (i = 0; i < installed; i++) {
        if (seq_id[i] == (u32)id) {
            return 1;
        }
    }
    return 0;
}

/* The class's sequences as retail has them. A title screen program loaded
 * again brings fresh class data, which no longer points at the copies. */
static void restore_sequences(void)
{
    s32 i;
    for (i = 0; i < installed; i++) {
        if (class_sequences[seq_id[i]] == seq_installed[i]) {
            class_sequences[seq_id[i]] = seq_original[i];
        }
    }
    installed = 0;
}

/* Sequence ID becomes SEQ in the class's table, retail's kept for restore_sequences. */
static s32 point_sequence(s32 id, u32 seq)
{
    seq_id[installed] = id;
    seq_original[installed] = class_sequences[id];
    seq_installed[installed] = seq;
    class_sequences[id] = seq;
    installed++;
    return 1;
}

/* Sequence ID as retail has it, whether or not a copy has taken its place. */
static u32 retail_sequence(s32 id)
{
    s32 i;
    for (i = 0; i < installed; i++) {
        if (seq_id[i] == (u32)id) {
            return seq_original[i];
        }
    }
    return class_sequences[id];
}

static s32 abs32(s32 v)
{
    return v < 0 ? -v : v;
}

/* Sequence ID becomes a copy of BASE whose records are moved by DELTA
 * ({x, y, z} per record): every frame by all of it, or with TAPER by all of
 * it at the first frame down to none at the last, so the sequence still
 * ends where retail's does; and, if FIXED is given, by FIXED where the
 * sequence rests (the stretched name box): at every frame of a sequence
 * whose box stands still, else by none of it at the first frame growing
 * to all of it at the last, since a page's sequence starts where the start
 * menu's row rests and a switch back plays it to that frame again (the
 * Load Game row came back stretched when every frame was). 0 if BASE does
 * not fit the copy.
 *
 * A moby keeps its sequence's bounding sphere from the header when the
 * sequence starts (moby + 0xF0, read as a quadword, so the header must be
 * 16-byte aligned), and the panel draws only while that sphere is where the
 * panel is: copies whose sphere stayed behind, or was read from a misaligned
 * header, showed the panel's dark face without its frame. So the sphere
 * moves with the centre (half the move for a taper) and grows by the rest:
 * the taper's other half and any change of the corners. */
static s32 copy_sequence(s32 id, u32 base, const s16 *delta, const s16 *fixed, s32 records, s32 taper, s32 reverse)
{
    u32 *seq = seq_store[installed];
    f32 *sphere = (f32 *)seq;
    s16 *first = FRAME_RECORDS(SEQ_FRAME(base, 0));
    s32 frames = SEQ_FRAME_COUNT(base);
    s32 span = taper && frames > 1 ? frames - 1 : 1;
    s32 f, k, axis, centre = 0, half_move = 0, corners = 0, still = 1;
    if (installed == SEQ_COPIES || frames == 0 || frames > SEQ_FRAMES) {
        return 0;
    }
    if (frames > 1 && FRAME_COUNT(SEQ_FRAME(base, frames - 1)) == records) {
        s16 *last = FRAME_RECORDS(SEQ_FRAME(base, frames - 1));
        for (k = 0; k < 3 * records; k++) {
            if (first[4 * (k / 3) + k % 3] != last[4 * (k / 3) + k % 3]) {
                still = 0;
            }
        }
    }
    for (k = 0; k < records; k++) {
        if (RECORD_JOINT(first, k) == 0) {
            centre = k;
        }
    }
    for (k = 0; k < 0x1C / 4; k++) {
        seq[k] = ((u32 *)base)[k];
    }
    for (axis = 0; axis < 3; axis++) {
        s32 move = delta[3 * centre + axis];
        sphere[axis] += taper ? (f32)move / 2 : (f32)move;
        if (taper) {
            half_move += (move < 0 ? -move : move) / 2;
        }
    }
    for (k = 0; k < records; k++) {
        s32 change = 0;
        for (axis = 0; axis < 3; axis++) {
            s32 d = delta[3 * k + axis] - delta[3 * centre + axis];
            change += abs32(d) + (fixed ? abs32(fixed[3 * k + axis]) : 0);
        }
        corners = change > corners ? change : corners;
    }
    sphere[3] += (f32)(half_move + corners);
    for (f = 0; f < frames; f++) {
        u32 *frame = (u32 *)SEQ_FRAME(base, f);
        u32 *copy = frame_store[installed][f];
        s32 weight = taper ? span - f : 1;
        s32 grow = still || frames < 2 ? 1 : 0;
        s16 *rec;
        if (FRAME_QWC_OF(frame) > FRAME_QWC || FRAME_COUNT(frame) != records) {
            return 0;
        }
        for (k = 0; k < 4 * (1 + FRAME_QWC_OF(frame)); k++) {
            copy[k] = frame[k];
        }
        rec = FRAME_RECORDS(copy);
        for (k = 0; k < records; k++) {
            for (axis = 0; axis < 3; axis++) {
                rec[4 * k + axis] += delta[3 * k + axis] * weight / span;
                if (fixed) {
                    s32 at = reverse ? frames - 1 - f : f;      /* the stretch grows in, or fades out */
                    rec[4 * k + axis] += grow ? fixed[3 * k + axis] : fixed[3 * k + axis] * at / (frames - 1);
                }
            }
        }
        seq[0x1C / 4 + f] = (u32)copy;
    }
    return point_sequence(id, (u32)seq);
}

/* The move that stretches a box down by a line: its bottom corners (the
 * records below the centre) by STRETCH_NUM / STRETCH_DEN of its half height
 * at the last frame of BASE, where it rests, or at its first (AT_FIRST), for
 * a sequence that starts from the box. */
static void stretch_delta(u32 base, s32 records, s16 *out, s32 at_first)
{
    s16 *rec = FRAME_RECORDS(SEQ_FRAME(base, 0));
    s16 *last = FRAME_RECORDS(SEQ_FRAME(base, at_first ? 0 : SEQ_FRAME_COUNT(base) - 1));
    s32 k, half = 0;
    for (k = 0; k < records; k++) {
        if (RECORD_JOINT(rec, k) != 0 && abs32(last[4 * k + 2]) > half) {
            half = abs32(last[4 * k + 2]);
        }
    }
    for (k = 0; k < records; k++) {
        out[3 * k] = 0;
        out[3 * k + 1] = 0;
        out[3 * k + 2] = (s16)(RECORD_JOINT(rec, k) != 0 && rec[4 * k + 2] < 0 ? -(half * STRETCH_NUM / STRETCH_DEN) : 0);
    }
}

/* Sequence ID's copy with the panel's centre moved DZ up, and the box
 * stretched down if STRETCH: 1 growing in to its last frame, where the box
 * rests; -1 from its first frame, fading out to the last (a sequence that
 * leaves the stretched box, as the confirm page's does). */
static s32 raise_sequence(s32 id, s32 dz, s32 taper, s32 stretch)
{
    s16 delta[3 * FRAME_RECORDS_MAX], fixed[3 * FRAME_RECORDS_MAX];
    u32 base = class_sequences[id];
    s16 *rec = FRAME_RECORDS(SEQ_FRAME(base, 0));
    s32 records = FRAME_COUNT(SEQ_FRAME(base, 0));
    s32 k;
    if (records > FRAME_RECORDS_MAX) {
        return 0;
    }
    for (k = 0; k < records; k++) {
        delta[3 * k] = 0;
        delta[3 * k + 1] = 0;
        delta[3 * k + 2] = RECORD_JOINT(rec, k) == 0 ? dz : 0;
    }
    stretch_delta(base, records, fixed, stretch < 0);
    return copy_sequence(id, base, delta, stretch ? fixed : 0, records, taper, stretch < 0);
}

/* Sequence ID's copy of BASE starting from the records REST instead of its own first frame. */
static s32 sequence_from(s32 id, u32 base, const s16 *rest, s32 records, s32 taper, s32 stretch)
{
    s16 delta[3 * FRAME_RECORDS_MAX], fixed[3 * FRAME_RECORDS_MAX];
    s16 *first = FRAME_RECORDS(SEQ_FRAME(base, 0));
    s32 k;
    if (FRAME_COUNT(SEQ_FRAME(base, 0)) != records) {
        return 0;
    }
    for (k = 0; k < 3 * records; k++) {
        delta[k] = rest[k] - first[4 * (k / 3) + k % 3];
    }
    stretch_delta(base, records, fixed, 0);
    return copy_sequence(id, base, delta, stretch ? fixed : 0, records, taper, 0);
}

/* The sequences of a start menu with the Goodies row, centred. The rows
 * (panels 2-4) move up by half their spacing, and the Goodies row rests a
 * row below Options: Load Game's sequence, moved, as SEQ_ROW. The pages the
 * rows open play their panels 2-4 from where the rows rest, so theirs get
 * the same move, tapered to nothing at their last frame, their own layout.
 * Their panel 0 is a title box sliding in from the side (Load Game, New
 * Game), whose sequence then starts from the Goodies row at rest, all of its
 * records tapered; or it is parked (Options) and plays SEQ_ROW, which slides
 * the row out and back in (title_rows). With a Randomizer save on the card,
 * New Game's and Load Game's name box (their panel 3, one sequence for
 * both) and the confirm page's are stretched down for the seed line. 0 if a
 * sequence is not as expected. */
static s32 install_sequences(void)
{
    struct MenuPage *page = (struct MenuPage *)TITLE_PAGE;
    u32 panel = PANELS[PANEL_ROW];
    u32 cls, middle;
    s16 rest[3 * FRAME_RECORDS_MAX], *rec;
    s32 top, bottom, spacing, raise, records, k, j;
    if (panel == 0 || (cls = *(u32 *)(panel + 0x24)) == 0 || *(u8 *)(cls + 0xC) <= SEQ_ROW) {
        return 0;
    }
    class_sequences = (u32 *)(cls + 0x48);
    top = rest_z(class_sequences[page->moby_anims[ROW_FIRST_PANEL]]);
    bottom = rest_z(class_sequences[page->moby_anims[ROW_FIRST_PANEL + 2]]);
    spacing = (top - bottom) / 2;
    raise = spacing / 2;
    middle = class_sequences[page->moby_anims[ROW_FIRST_PANEL + 1]];
    records = FRAME_COUNT(SEQ_FRAME(middle, 0));
    if (records > FRAME_RECORDS_MAX) {
        return 0;
    }
    rec = FRAME_RECORDS(SEQ_FRAME(middle, 0));
    for (k = 0; k < records; k++) {             /* the Goodies row at rest: Load Game's first frame, moved */
        rest[3 * k] = rec[4 * k];
        rest[3 * k + 1] = rec[4 * k + 1];
        rest[3 * k + 2] = RECORD_JOINT(rec, k) == 0 ? bottom - spacing + raise : rec[4 * k + 2];
    }
    if (!sequence_from(SEQ_ROW, middle, rest, records, 0, 0)) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        struct MenuList *row = (struct MenuList *)(TITLE_NEW_GAME + k * TITLE_LIST_STRIDE);
        struct MenuPage *opens = (struct MenuPage *)row->items[0].param;
        s32 title = opened_panel0[k];
        if (!raise_sequence(page->moby_anims[ROW_FIRST_PANEL + k], raise, 0, 0)) {
            return 0;
        }
        for (j = ROW_FIRST_PANEL; j < ROW_FIRST_PANEL + 3; j++) {
            s32 id = opens->moby_anims[j];
            s32 name = rz_saves && k < 2 && j == NAME_PANEL;            /* New Game's and Load Game's name box */
            s32 frames = SEQ_FRAME_COUNT(class_sequences[id]);
            if (!is_installed(id) && (frames > 1 || name)
                && !raise_sequence(id, frames > 1 ? raise : 0, 1, name)) {
                return 0;
            }
        }
        if (SEQ_FRAME_COUNT(class_sequences[title]) > 1 && !is_installed(title)
            && !sequence_from(title, class_sequences[title], rest, records, 1, 0)) {
            return 0;
        }
    }
    if (rz_saves && !is_installed(SEQ_CONFIRM_NAME)) {
        raise_sequence(SEQ_CONFIRM_NAME, 0, 0, -1);        /* the question's box: from the stretched name box, retail's size at rest */
    }
    return 1;
}

/* A box's place and size: per joint (0 the centre, 1-4 the corners) {x, y, z}. */
#define JOINTS 5
typedef s16 Layout[JOINTS][3];

/* FRAME's records as a layout. 0 if a joint is out of range. */
static s32 frame_layout(u32 frame, Layout out)
{
    s16 *rec = FRAME_RECORDS(frame);
    s32 k, axis;
    if (FRAME_COUNT(frame) != JOINTS) {
        return 0;
    }
    for (k = 0; k < JOINTS; k++) {
        s32 joint = RECORD_JOINT(rec, k);
        if (joint >= JOINTS) {
            return 0;
        }
        for (axis = 0; axis < 3; axis++) {
            out[joint][axis] = rec[4 * k + axis];
        }
    }
    return 1;
}

/* The layout as records in FRAME's order, {x, y, z} each (sequence_from's REST). */
static void layout_records(u32 frame, Layout layout, s16 *out)
{
    s16 *rec = FRAME_RECORDS(frame);
    s32 k, axis;
    for (k = 0; k < JOINTS; k++) {
        for (axis = 0; axis < 3; axis++) {
            out[3 * k + axis] = layout[RECORD_JOINT(rec, k)][axis];
        }
    }
}

/* Sequence ID becomes a box going from FROM to TO the way the Options
 * page's sub-pages change their boxes (Language, Video: Video Options'
 * prompt box, retail sequence 195, shrinks and grows back in place): its
 * corners shrink to about a fifth of FROM's size, then grow to TO's, while
 * the centre moves from one to the other. The template's frames give the
 * timing: the size at each frame against its first (shrinking) or last
 * (growing). 0 if the template is not as expected. */
static s32 morph_sequence(s32 id, Layout from, Layout to)
{
    u32 template = retail_sequence(((struct MenuPage *)VIDEO_PAGE)->moby_anims[ROW_FIRST_PANEL + 2]);
    u32 *seq = seq_store[installed];
    f32 *sphere = (f32 *)seq;
    Layout t0, tf;
    s32 frames = SEQ_FRAME_COUNT(template);
    s32 f, k, axis, smallest = 0, size0, size_last, grow;
    if (installed == SEQ_COPIES || frames < 2 || frames > SEQ_FRAMES
        || !frame_layout(SEQ_FRAME(template, 0), t0) || !frame_layout(SEQ_FRAME(template, frames - 1), tf)) {
        return 0;
    }
    size0 = abs32(t0[1][1]);
    size_last = abs32(tf[1][1]);
    for (f = 1; f < frames; f++) {
        Layout tl;
        Layout ts;
        frame_layout(SEQ_FRAME(template, f), tl);
        frame_layout(SEQ_FRAME(template, smallest), ts);
        if (abs32(tl[1][1]) < abs32(ts[1][1])) {
            smallest = f;
        }
    }
    if (size0 == 0 || size_last == 0) {
        return 0;
    }
    for (k = 0; k < 0x1C / 4; k++) {
        seq[k] = ((u32 *)template)[k];
    }
    grow = 0;
    for (axis = 0; axis < 3; axis++) {
        sphere[axis] += (f32)((from[0][axis] + to[0][axis]) / 2 - t0[0][axis]);
        grow += abs32(to[0][axis] - from[0][axis]) / 2;
    }
    for (k = 1; k < JOINTS; k++) {
        s32 big = abs32(from[k][1]) + abs32(from[k][2]);
        s32 other = abs32(to[k][1]) + abs32(to[k][2]);
        s32 own = abs32(t0[k][1]) + abs32(t0[k][2]);
        big = other > big ? other : big;
        grow = big - own > grow ? big - own : grow;
    }
    sphere[3] += (f32)grow;
    for (f = 0; f < frames; f++) {
        u32 *frame = (u32 *)SEQ_FRAME(template, f);
        u32 *copy = frame_store[installed][f];
        Layout now;
        s16 *rec;
        if (FRAME_QWC_OF(frame) > FRAME_QWC || !frame_layout((u32)frame, now)) {
            return 0;
        }
        for (k = 0; k < 4 * (1 + FRAME_QWC_OF(frame)); k++) {
            copy[k] = frame[k];
        }
        rec = FRAME_RECORDS(copy);
        for (k = 0; k < JOINTS; k++) {
            s32 joint = RECORD_JOINT(rec, k);
            for (axis = 0; axis < 3; axis++) {
                if (joint == 0) {
                    rec[4 * k + axis] = from[0][axis] + (to[0][axis] - from[0][axis]) * f / (frames - 1);
                } else if (f < smallest) {
                    rec[4 * k + axis] = from[joint][axis] * abs32(now[1][1]) / size0;
                } else {
                    rec[4 * k + axis] = to[joint][axis] * abs32(now[1][1]) / size_last;
                }
            }
        }
        seq[0x1C / 4 + f] = (u32)copy;
    }
    return point_sequence(id, (u32)seq);
}

/* A layout with its corners' half height set to HALF. */
static void set_half_height(Layout layout, s32 half)
{
    s32 k;
    for (k = 1; k < JOINTS; k++) {
        layout[k][2] = layout[k][2] < 0 ? -half : half;
    }
}

/* A layout with its corners' half width set to HALF. */
static void set_half_width(Layout layout, s32 half)
{
    s32 k;
    for (k = 1; k < JOINTS; k++) {
        layout[k][1] = layout[k][1] < 0 ? -half : half;
    }
}

/* The Goodies pages' sequences, each box morphing as the Options page's
 * sub-pages do (morph_sequence): Goodies from the start menu's rows to the
 * Options page's layout with a list box one row high, Randomizer from there
 * to the Options page's own layout (three rows: New Game, Reuse Seed,
 * Options; Randomizer Options plays the Video Options page's retail
 * sequences from it), Reuse Seed from there to the Video Options
 * page's layout with its list box narrowed to the nine symbols (SEED_BOX_PX,
 * in record units through the name box's measured width) and its prompt
 * box the Options page's, two rows for Confirm and Exit. The Randomizer's
 * New Game is retail's New Game page with
 * copies of its sequences that start from the Randomizer page's boxes (all
 * records tapered, as for the start menu's pages), its name box
 * stretched like the start menu's pages'; its title box slides in as
 * retail's does. A page switch back plays these backwards. 0 if a sequence
 * is not as expected. */
static s32 install_randomizer_sequences(void)
{
    struct MenuPage *title = (struct MenuPage *)TITLE_PAGE;
    struct MenuPage *options = (struct MenuPage *)OPTIONS_PAGE;
    struct MenuPage *video = (struct MenuPage *)VIDEO_PAGE;
    struct MenuPage *new_game = (struct MenuPage *)NEW_GAME_PAGE;
    Layout rows[3], goodies[3], randomizer[3], reuse[3];
    s16 rest[3 * JOINTS];
    s32 j, k, axis;
    for (j = 0; j < 3; j++) {
        u32 opt = retail_sequence(options->moby_anims[ROW_FIRST_PANEL + j]);
        u32 vid = retail_sequence(video->moby_anims[ROW_FIRST_PANEL + j]);
        if (!frame_layout(SEQ_FRAME(class_sequences[title->moby_anims[ROW_FIRST_PANEL + j]], 0), rows[j])
            || !frame_layout(SEQ_FRAME(opt, SEQ_FRAME_COUNT(opt) - 1), goodies[j])
            || !frame_layout(SEQ_FRAME(vid, SEQ_FRAME_COUNT(vid) - 1), reuse[j])) {
            return 0;
        }
        for (k = 0; k < JOINTS; k++) {
            for (axis = 0; axis < 3; axis++) {
                randomizer[j][k][axis] = goodies[j][k][axis];
            }
        }
    }
    set_half_height(goodies[1], GOODIES_LIST_HALF);
    {
        Layout name;
        u32 seq = retail_sequence(new_game->moby_anims[NAME_PANEL]);
        if (frame_layout(SEQ_FRAME(seq, SEQ_FRAME_COUNT(seq) - 1), name) && name[1][1] != 0) {
            set_half_width(reuse[1], abs32(name[1][1]) * SEED_BOX_PX / NAME_BOX_PX);
        }
        for (k = 0; k < JOINTS; k++) {
            for (axis = 0; axis < 3; axis++) {
                reuse[2][k][axis] = randomizer[2][k][axis];
            }
        }
    }
    for (j = 0; j < 3; j++) {
        u32 base = retail_sequence(new_game->moby_anims[ROW_FIRST_PANEL + j]);
        if (!morph_sequence(goodies_ids[j], rows[j], goodies[j])
            || !morph_sequence(randomizer_ids[j], goodies[j], randomizer[j])
            || !morph_sequence(reuse_ids[j], randomizer[j], reuse[j])) {
            return 0;
        }
        layout_records(SEQ_FRAME(base, 0), randomizer[j], rest);
        if (FRAME_COUNT(SEQ_FRAME(base, 0)) != JOINTS
            || !sequence_from(new_game_ids[j], base, rest, JOINTS, 1, rz_saves && j == NAME_PANEL - ROW_FIRST_PANEL)) {
            return 0;
        }
    }
    return point_sequence(SEQ_NEW_GAME_TITLE, retail_sequence(new_game->moby_anims[PANEL_ROW]));
}

/* The pages: copies of the title screen's own page records and boxes, made
 * when the start menu opens (the boxes' functions differ between the boot
 * executable and frontbin). */
static u32 goodies_page[PAGE_WORDS];
static u32 randomizer_page[PAGE_WORDS];
static u32 reuse_page[PAGE_WORDS];
static u32 new_game_page[PAGE_WORDS];
static u32 rz_options_page[PAGE_WORDS];
static u32 goodies_title[WIDGET_WORDS];
static u32 randomizer_title[WIDGET_WORDS];
static u32 reuse_title[WIDGET_WORDS];
static u32 rz_options_title[WIDGET_WORDS];
static u32 rz_slots[WIDGET_WORDS];      /* the Randomizer New Game's slot list: retail's, its update and draw wrapped */
static u32 rz_options_toggle[WIDGET_WORDS];    /* the Video Options page's toggle list, over start_entries */
static struct MenuList goodies_menu;
static struct MenuList randomizer_menu;
static struct MenuList seed_box;
static struct MenuList reuse_prompt;    /* "Exit" while entering, "Confirm" and "Exit" once nine are in */
static struct MenuItem goodies_menu_items[2];
static struct MenuItem randomizer_items[4];

/* A toggle list's row (ToggleList_PadInput): its label, the byte it flips,
 * the text shown while the byte is nonzero and while it is 0, and flags (1:
 * the Video page's mode switch). A row with label 0 ends the list. */
struct ToggleEntry {
    s32 label;
    u8 *byte;
    s32 on_text;
    s32 off_text;
    s32 flags;
};
#define TOGGLE_ENTRIES_WORD (0x34 / 4)  /* a toggle list's rows */
#define TOGGLE_CURSOR_WORD  (0x38 / 4)  /* its selected row */
static u8 start_shown = 1;
static const s32 start_texts[RZ_START_MODES] = { EE_TEXT_START_NOVALIS, EE_TEXT_START_VELDIN, EE_TEXT_START_RANDOM };
static struct ToggleEntry start_entries[2] = {
    { EE_TEXT_START_PLANET, &start_shown, EE_TEXT_START_NOVALIS, EE_TEXT_START_NOVALIS, 0 },
    { 0, 0, 0, 0, 0 },
};
static u32 retail_toggle_update;        /* ToggleList_PadInput, this program's */
static struct MenuItem seed_items[2] = { { EE_TEXT_SEED_INPUT, ACTION_OPEN, 0, 0, 0 }, { 0, 0, 0, 0, 0 } };
static struct MenuItem reuse_prompt_items[3];
static u32 retail_seed_box_draw;        /* DrawMenuItemList, this program's */
static u32 retail_slots_update;         /* SaveSlotMenu_Update, this program's */
static u32 retail_slots_draw;           /* Pause_DrawSaveSlots */
static u32 retail_name_draw;            /* the name box's draw, DrawSaveLevelName */
static u32 retail_list_update;          /* Pause_TextListUpdate */

static void copy_words(u32 *dst, u32 src, s32 count)
{
    s32 k;
    for (k = 0; k < count; k++) {
        dst[k] = ((u32 *)src)[k];
    }
}

static void set_item(struct MenuItem *item, s32 text, s32 action, u32 param)
{
    item->text = text;
    item->action = action;
    item->param = param;
    item->subtext = 0;
    item->fade_timer = 0;
}

/* A one-column list like the Options page's, with ITEMS. */
static void make_list(struct MenuList *list, struct MenuItem *items)
{
    copy_words((u32 *)list, OPTIONS_LIST, sizeof(*list) / 4);
    ((u32 *)list)[LIST_ENTER] = 0;
    list->moby = 0;
    list->items = items;
    list->selected = 0;
    list->scroll = 0;
}

/* A page like SOURCE with these boxes in screens 2-4, BACK its back page. */
static void make_page(u32 *page, u32 source, u32 back, u32 title, u32 box, u32 prompt)
{
    struct MenuPage *p = (struct MenuPage *)page;
    copy_words(page, source, PAGE_WORDS);
    p->back = back;
    p->focus = box;
    p->screens[ROW_FIRST_PANEL] = title;
    p->screens[ROW_FIRST_PANEL + 1] = box;
    p->screens[ROW_FIRST_PANEL + 2] = prompt;
    page[0x80 / 4] = 0;             /* no focus change pending */
}

s32 ee_seed_box_update(struct MenuList *box);
s32 ee_seed_box_draw(struct MenuList *box);
static void seed_prompt(s32 complete);
s32 ee_rz_slots_update(void *widget);
s32 ee_rz_slots_draw(void *widget);
s32 ee_rz_name_draw(void *widget);
s32 ee_rz_menu_update(void *list);
s32 ee_rz_options_update(void *widget);

static void build_pages(void)
{
    struct MenuPage *page;
    struct MenuPage *video = (struct MenuPage *)VIDEO_PAGE;
    s32 j;
    copy_words(goodies_title, OPTIONS_TITLE, WIDGET_WORDS);
    goodies_title[0x34 / 4] = TEXT_GOODIES;
    copy_words(randomizer_title, OPTIONS_TITLE, WIDGET_WORDS);
    randomizer_title[0x34 / 4] = EE_TEXT_RANDOMIZER;
    copy_words(reuse_title, VIDEO_TITLE, WIDGET_WORDS);
    reuse_title[0x34 / 4] = EE_TEXT_REUSE_SEED;
    copy_words(rz_options_title, VIDEO_TITLE, WIDGET_WORDS);
    rz_options_title[0x34 / 4] = EE_TEXT_RZ_OPTIONS;

    set_item(&goodies_menu_items[0], EE_TEXT_RANDOMIZER, ACTION_OPEN, (u32)randomizer_page);
    set_item(&goodies_menu_items[1], 0, 0, 0);
    set_item(&randomizer_items[0], TEXT_NEW_GAME, ACTION_FREEZE, (u32)new_game_page);
    set_item(&randomizer_items[1], EE_TEXT_REUSE_SEED, ACTION_OPEN, (u32)reuse_page);
    set_item(&randomizer_items[2], TEXT_OPTIONS, ACTION_OPEN, (u32)rz_options_page);
    set_item(&randomizer_items[3], 0, 0, 0);
    make_list(&goodies_menu, goodies_menu_items);
    make_list(&randomizer_menu, randomizer_items);
    retail_list_update = randomizer_menu.update;
    randomizer_menu.update = (u32)ee_rz_menu_update;    /* forgets a Reuse Seed code: New Game from here is fresh */
    make_list(&seed_box, seed_items);
    seed_box.update = (u32)ee_seed_box_update;
    retail_seed_box_draw = seed_box.draw;
    seed_box.draw = (u32)ee_seed_box_draw;
    seed_box.flags = SEED_FLAGS;
    copy_words((u32 *)&reuse_prompt, SELECT_PROMPT, sizeof(reuse_prompt) / 4);   /* the struct holds the words a list uses, not all 0x50 */
    reuse_prompt.items = reuse_prompt_items;
    seed_prompt(0);

    make_page(goodies_page, OPTIONS_PAGE, TITLE_PAGE, (u32)goodies_title, (u32)&goodies_menu, SELECT_PROMPT);
    page = (struct MenuPage *)goodies_page;
    page->moby_anims[PANEL_ROW] = SEQ_ROW;              /* the Goodies row slides out as the page opens */
    for (j = 0; j < 3; j++) {
        page->moby_anims[ROW_FIRST_PANEL + j] = goodies_ids[j];
    }

    make_page(randomizer_page, OPTIONS_PAGE, (u32)goodies_page, (u32)randomizer_title, (u32)&randomizer_menu,
              SELECT_PROMPT);
    page = (struct MenuPage *)randomizer_page;
    page->moby_anims[PANEL_ROW] = PANEL_ROW_PARKED;
    for (j = 0; j < 3; j++) {
        page->moby_anims[ROW_FIRST_PANEL + j] = randomizer_ids[j];
    }

    make_page(reuse_page, VIDEO_PAGE, (u32)randomizer_page, (u32)reuse_title, (u32)&seed_box, (u32)&reuse_prompt);
    page = (struct MenuPage *)reuse_page;
    for (j = 0; j < 3; j++) {
        page->moby_anims[ROW_FIRST_PANEL + j] = reuse_ids[j];
    }

    /* Randomizer Options: the Video Options page with its own title and the
     * Starting Planet's row in a copy of its toggle list; the Randomizer
     * page's boxes are the Options page's, so the Video page's own
     * sequences take them there and back */
    copy_words(rz_options_toggle, video->focus, WIDGET_WORDS);
    retail_toggle_update = rz_options_toggle[0];
    rz_options_toggle[0] = (u32)ee_rz_options_update;
    rz_options_toggle[LIST_ENTER] = 0;
    rz_options_toggle[TOGGLE_ENTRIES_WORD] = (u32)start_entries;
    rz_options_toggle[TOGGLE_CURSOR_WORD] = 0;
    start_entries[0].on_text = start_texts[start_setting];
    make_page(rz_options_page, VIDEO_PAGE, (u32)randomizer_page, (u32)rz_options_title, (u32)rz_options_toggle,
              video->screens[ROW_FIRST_PANEL + 2]);

    /* retail's New Game page with the slot list copied: its update arms a
     * Randomizer game (ee_rz_slots_update), its draw adds the badges */
    copy_words(rz_slots, NEW_GAME_SLOTS, WIDGET_WORDS);
    rz_slots[0] = (u32)ee_rz_slots_update;
    rz_slots[WIDGET_DRAW] = (u32)ee_rz_slots_draw;
    copy_words(new_game_page, NEW_GAME_PAGE, PAGE_WORDS);
    page = (struct MenuPage *)new_game_page;
    page->back = (u32)randomizer_page;
    page->focus = (u32)rz_slots;
    page->screens[1] = (u32)rz_slots;
    page->moby_anims[PANEL_ROW] = SEQ_NEW_GAME_TITLE;
    for (j = 0; j < 3; j++) {
        page->moby_anims[ROW_FIRST_PANEL + j] = new_game_ids[j];
    }
    new_game_page[0x80 / 4] = 0;

    goodies_items[0].param = (u32)goodies_page;
}

/* --- Reuse Seed's input box ------------------------------------------------ */

/* The buttons a seed is entered with, in the game's pad layout, and the
 * font's glyph for each. Every symbol takes the shoulder buttons' width (L1,
 * R1, L2, R2: the game's own codes 0x14-0x17, advance 25), so nine always
 * fill the same space: Cross, Circle and Square are the edition's copies
 * centred in that width (codes 0x02-0x04, unused by any string; codes below
 * 0x20 draw as untinted button sprites), and the D-pad's arms are the
 * edition's (0x1A-0x1D); install_glyphs. The code's digits (src/randomizer.h:
 * Up, Right, Down, Left, Cross, Circle, Square, L1, L2, R1, R2) in the same
 * order, and the glyph of each digit. */
static const u16 seed_buttons[RZ_CODE_BUTTONS] = { 0x0040, 0x0020, 0x0080, 0x0004, 0x0008, 0x0001, 0x0002,
                                                   0x1000, 0x2000, 0x4000, 0x8000 };
static const u8 seed_glyphs[RZ_CODE_BUTTONS] = { 0x02, 0x03, 0x04, 0x14, 0x15, 0x16, 0x17, 0x1A, 0x1B, 0x1C, 0x1D };
static const u8 seed_digit_of_button[RZ_CODE_BUTTONS] = { 4, 5, 6, 7, 9, 8, 10, 0, 1, 2, 3 };
static const u8 seed_glyph_of_digit[RZ_CODE_BUTTONS] = { 0x1A, 0x1B, 0x1C, 0x1D, 0x02, 0x03, 0x04, 0x14, 0x16, 0x15, 0x17 };
#define PAD_TRIANGLE 0x0010
static char seed_text[RZ_CODE_LENGTH + 1];
static u8 seed_digits[RZ_CODE_LENGTH];
static s32 seed_length;
static s32 seed_error;              /* the last code was refused: the box shows "Invalid seed code" until a press */

/* The seed box's glyphs. The menu fonts (effect textures 1-3: normal, small,
 * large) are 256 x 128 8-bit sheets in the title screen's data, which every
 * draw pass uploads afresh from EE memory (GetEffectTex func_001F4868 queues
 * them after SetupGifPaging resets the texture cursor). The effect bank
 * pointer and the table of {TEX0, image offset >> 4, palette offset >> 4,
 * log2 w, log2 h} entries sit at the executable's addresses in both title
 * programs. Their metrics, {u, v, y offset, advance} per character code,
 * leave codes 0x02-0x07 and 0x1A-0x1F empty (advance 0: not drawn), and the
 * sheets' corner u 0xD0-0xFF, v 0x00-0x4F is unused: tools/gen_glyphs.py
 * draws the glyphs there (EE_GLYPHS: centred copies of Cross, Circle and
 * Square, the D-pad's four arms), and they go in here the first
 * time the seed box or a save page's picture box runs after the title
 * screen's data loads (a fresh load has advance 0 again). The data cache
 * is written back before the next upload reads them. */
#define EE_GLYPHS 7
extern const u8 ee_glyph_cells[3][EE_GLYPHS][16][24];
extern const u8 ee_glyph_places[EE_GLYPHS][4];
extern void func_00118D80(s32);         /* FlushCache (core) */
#define EFFECT_BANK     (*(volatile u32 *)0x0015F560)
#define EFFECT_TABLE    0x0018D540
static const u32 font_metrics[3] = { 0x001DF3D0, 0x001DF770, 0x001DFB10 };   /* normal, small, large */

/* The executable's data (the menus' pages and widgets, the fonts' metrics, the panel table) is relinked into
 * each level program as one block, so a title-screen address has the same offset from the menu record
 * (LEVEL_DATA menu_state) there; the title's two programs keep it in place. */
#define TITLE_MENU_STATE 0x001D5F70
#define LEVEL_PROGRAMS   19

static u32 exe_data(s32 L, u32 title_address)
{
    if (L < LEVEL_PROGRAMS) {
        return (u32)LVL_DATA(L, menu_state, u8 *) + (title_address - TITLE_MENU_STATE);
    }
    return title_address;
}

/* The glyphs' metrics (cell u, v, and advance) into program L's three font metrics tables, once. */
static void install_glyph_metrics(s32 L)
{
    s32 font, g;
    for (font = 0; font < 3; font++) {
        volatile u8 *metrics = (volatile u8 *)exe_data(L, font_metrics[font]);
        if (metrics[4 * ee_glyph_places[0][0] + 3] != 0) {
            continue;
        }
        for (g = 0; g < EE_GLYPHS; g++) {
            metrics[4 * ee_glyph_places[g][0]] = ee_glyph_places[g][1];
            metrics[4 * ee_glyph_places[g][0] + 1] = ee_glyph_places[g][2];
            metrics[4 * ee_glyph_places[g][0] + 2] = 0;
            metrics[4 * ee_glyph_places[g][0] + 3] = ee_glyph_places[g][3];
        }
    }
}

/* The title screen's glyphs: their pixels into the three sheets in the effect bank, and their metrics. A
 * level's fonts have the pixels from the disc (tools/splice_boots.py paints them) and get the metrics only. */
static void install_glyphs(void)
{
    s32 font, g, x, y;
    if (EFFECT_BANK == 0 || ((volatile u8 *)font_metrics[0])[4 * ee_glyph_places[0][0] + 3] != 0) {
        return;
    }
    for (font = 0; font < 3; font++) {
        u32 offset = *(volatile u16 *)(EFFECT_TABLE + 16 * (font + 1) + 8) << 4;
        volatile u8 *sheet = (volatile u8 *)(EFFECT_BANK + offset);
        for (g = 0; g < EE_GLYPHS; g++) {
            u32 u = ee_glyph_places[g][1];
            u32 v = ee_glyph_places[g][2];
            for (y = 0; y < 16; y++) {
                for (x = 0; x < 24; x++) {
                    sheet[(v + y) * 256 + u + x] = ee_glyph_cells[font][g][y][x];
                }
            }
        }
    }
    install_glyph_metrics(title_program);
    func_00118D80(0);
}

DECLARE_LVL(func_0022ED80);             /* the menus' sound (1: the cursor's tick) */
typedef void (*MenuSoundFn)(s32, s32, u32);

/* The text the box shows: the buttons entered, left to right (src/text.c). */
const char *ee_seed_text(void)
{
    return seed_text;
}

/* The Reuse Seed page's prompt box: "Exit" alone, or "Confirm" and "Exit" once a code is complete. */
static void seed_prompt(s32 complete)
{
    s32 n = 0;
    if (complete) {
        set_item(&reuse_prompt_items[n++], EE_TEXT_CONFIRM, 0, 0);
    }
    set_item(&reuse_prompt_items[n++], TEXT_EXIT, 0, 0);
    set_item(&reuse_prompt_items[n], 0, 0, 0);
}

static void clear_seed_box(void)
{
    seed_length = 0;
    seed_text[0] = 0;
    seed_error = 0;
    seed_prompt(0);
}

static void menu_sound(s32 kind, u32 moby)
{
    if (lvl_func_0022ED80[title_program] != 0) {
        LVL(title_program, func_0022ED80, MenuSoundFn)(kind, 0x11, moby);
    }
}

/* The box's update, in place of the list's own: each button of the set
 * pressed adds its glyph; Triangle empties the box and goes back. With nine
 * in, the prompt box offers Confirm and Exit, and Cross decodes the code:
 * one that passes opens the Randomizer's New Game with its seed; one that
 * fails empties the box and shows "Invalid seed code" in red
 * (ee_seed_box_draw) until the next press, which starts a new code. */
s32 ee_seed_box_update(struct MenuList *box)
{
    u32 pressed = PAD_PRESSED;
    s32 k;
    install_glyphs();
    if (MENU_CURRENT == 0 || *(u32 *)(MENU_CURRENT + PAGE_FOCUS) != (u32)box) {
        return 0;
    }
    if (pressed & PAD_TRIANGLE) {
        clear_seed_box();
        MENU_NEXT = ((struct MenuPage *)MENU_CURRENT)->back;
        return 0;
    }
    if (seed_length == RZ_CODE_LENGTH) {
        u32 seed;
        s32 version, mode, random_start;
        if (!(pressed & seed_buttons[0])) {                 /* Cross confirms; nothing else counts */
            return 0;
        }
        if (rz_code_decode(seed_digits, &seed, &version, &mode, &random_start)) {
            clear_seed_box();
            /* the code says Random or not; Novalis or Veldin is the player's own setting (one layout) */
            ee_rz_set_seed(seed, random_start ? RZ_START_RANDOM
                                 : start_setting == RZ_START_VELDIN ? RZ_START_VELDIN : RZ_START_NOVALIS, 1);
            /* as the list's action 4 (Pause_TextListUpdate) opens New Game */
            menu_sound(0, box->moby);
            MENU_LOAD_KIND = 0;
            GAME_FLAGS = (GAME_FLAGS | 2) & ~4;
            LVL(title_program, func_001FBC80, FreezeFn)(3, new_game_page, 0);
        } else {
            clear_seed_box();
            seed_error = 1;
            menu_sound(1, box->moby);
        }
        return 0;
    }
    for (k = 0; k < RZ_CODE_BUTTONS; k++) {
        if ((pressed & seed_buttons[k]) && seed_length < RZ_CODE_LENGTH) {
            seed_error = 0;
            seed_text[seed_length] = seed_glyphs[k];     /* the glyphs' advance spaces them */
            seed_text[seed_length + 1] = 0;
            seed_digits[seed_length] = seed_digit_of_button[k];
            seed_length++;
            menu_sound(1, box->moby);
        }
    }
    if (seed_length == RZ_CODE_LENGTH) {
        seed_prompt(1);
    }
    return 0;
}

/* The box's draw: the list's own (the symbols entered, left-aligned at x 4
 * in the normal font, in the row's middle), then, after a refused code,
 * "Invalid seed code" centred in red where the symbols would be. */
s32 ee_seed_box_draw(struct MenuList *box)
{
    s32 r = ((WidgetFn)retail_seed_box_draw)(box);
    if (seed_error) {
        s32 y = box->height / 2 - FONT_HEIGHT / 2 - 1;
        LVL(title_program, func_001F4630, PagingFn)(0);
        LVL(title_program, func_001F6EA8, PrintFn)(box->width / 2, y, TEXT_RED,
                                                    LVL(title_program, func_001FE540, MsgFn)(EE_TEXT_BAD_SEED), -1);
        LVL(title_program, func_001F4748, VoidFn)();
    }
    return r;
}

/* --- The Randomizer's save pages ------------------------------------------- */

/* The Randomizer page's list: a New Game chosen here starts from a fresh
 * seed, where the Starting Planet says. */
s32 ee_rz_menu_update(void *list)
{
    ee_rz_set_seed(0, start_setting, 0);
    ee_rz_arm(0);
    return ((WidgetFn)retail_list_update)(list);
}

/* The Starting Planet's row: retail's toggle (ToggleList_PadInput,
 * func_00221E60), which flips its byte on Cross and shows the entry's first
 * text while the byte is nonzero. The byte is kept at 1 and the first text
 * is the setting's name, so a flip is a step to the next setting. Leaving
 * the page puts a changed setting in the edition's record on the card, in
 * the background (record_job); with no card, or no save directory yet, it
 * stays for the session. */
s32 ee_rz_options_update(void *widget)
{
    static u8 save_tried;               /* once per change: a card without the save directory refuses every time */
    s32 r;
    start_shown = 1;
    r = ((WidgetFn)retail_toggle_update)(widget);
    if (!start_shown) {
        start_setting = (u8)((start_setting + 1) % RZ_START_MODES);
        start_unsaved = 1;              /* kept over the next scan's value until it is on the card */
        save_tried = 0;
        start_shown = 1;
    }
    start_entries[0].on_text = start_texts[start_setting];
    if (start_unsaved && !save_tried && record_job.step == RECORD_IDLE
        && MENU_NEXT == ((struct MenuPage *)rz_options_page)->back) {
        save_tried = 1;
        record_job_start((card_flags & ~EDITION_START_MASK) | (start_setting << EDITION_START_SHIFT));
    }
    return r;
}

/* The Randomizer New Game's slot list: retail's update, with a Randomizer
 * game armed while it runs, and the overwrite question sent back here. */
s32 ee_rz_slots_update(void *widget)
{
    s32 r;
    ee_rz_arm(1);
    r = ((WidgetFn)retail_slots_update)(widget);
    if (MENU_NEXT == CONFIRM_PAGE) {
        ((u32 *)CONFIRM_PAGE)[PAGE_BACK_WORD] = (u32)new_game_page;
    }
    return r;
}

/* A green R on each slot holding a Randomizer save, where
 * Pause_DrawSaveSlots puts the Challenge Mode R, at the rows it uses (4,
 * then 0x30 and a gap of 0x24 in 50 Hz, 0x1B otherwise), inside the
 * caller's paging; PROGRAM runs the drawing. */
static void draw_badges(s32 program)
{
    s32 row = SLOT_ROW + (PAL_MODE ? 0x24 : 0x1B);
    s32 i, y;
    for (i = 0, y = 4; i < MC_SLOTS; i++, y += row) {
        if (slots[i].rz.present) {
            s32 frame = LVL(program, func_00200198, IconFn)(EE_ICON_RANDOMIZER, 0);
            LVL(program, func_00200468, SpriteFn)(frame, BADGE_X, y, BADGE_SIZE, BADGE_SIZE, 0x80);
        }
    }
}

/* The slot lists' draw (New Game's, the Randomizer's copy of it, Load
 * Game's): retail's, then the badges. Retail's New Game page drawing the
 * list means retail's flow: nothing armed, and the overwrite question back
 * to retail's page. */
s32 ee_rz_slots_draw(void *widget)
{
    s32 r;
    if (MENU_CURRENT == NEW_GAME_PAGE) {
        ee_rz_arm(0);
        ((u32 *)CONFIRM_PAGE)[PAGE_BACK_WORD] = NEW_GAME_PAGE;
    }
    r = ((WidgetFn)retail_slots_draw)(widget);
    if (!rz_saves || !card_ready()) {
        return r;
    }
    LVL(title_program, func_001F4630, PagingFn)(0);
    draw_badges(title_program);
    LVL(title_program, func_001F4748, VoidFn)();
    return r;
}

/* The end of Pause_DrawSaveSlots in level L (its DoGifPaging call,
 * src/rz_hooks.s): the pause menu's Save and Load pages show the badges
 * too. The level's copy of the menu record has the idle ticks the title's
 * card_ready reads; the slots are scanned again, inside the draw, when the
 * card is idle with its summaries read and they differ from the last scan's
 * (a save just made on the Save page, another card, the title's scan made
 * before the summaries were read). */
static void pause_name_stretch(s32 L);
volatile u32 ee_debug_rz_pause[4];      /* PINE: draws, draws with the card idle, scans, rz_saves */

void ee_rz_slots_end_c(s32 L)
{
    s32 ticks = *(volatile s32 *)(LVL_DATA(L, menu_state, u8 *) + MENU_TICKS_OFFSET);
    ee_debug_rz_pause[0]++;
    if (MC_STATE < 3 && MC_REQUEST < 0 && ticks >= 11 && MC_TYPE == 2 && MC_SUMMARY_STATE == 2) {
        ee_debug_rz_pause[1]++;
        if (summary_changed() && card_take()) {
            ee_debug_rz_pause[2]++;
            scan_start(L);
            scan_run();
        }
        ee_debug_rz_pause[3] = (u32)rz_saves;
        if (rz_saves) {
            pause_name_stretch(L);
            draw_badges(L);
        }
    }
    LVL(L, func_001F4748, VoidFn)();
}

/* The glyphs of SLOT's seed code into OUT (RZ_CODE_LENGTH + 1 bytes). */
static void seed_line(const struct RzSlot *slot, char *out)
{
    u8 digits[RZ_CODE_LENGTH];
    s32 i;
    rz_code_encode(slot->seed, slot->version, slot->weapon_mode, slot->start_mode == RZ_START_RANDOM, digits);
    for (i = 0; i < RZ_CODE_LENGTH; i++) {
        out[i] = (char)seed_glyph_of_digit[digits[i]];
    }
    out[RZ_CODE_LENGTH] = 0;
}

/* The width of TEXT in the normal font: its metrics' advances (the seed
 * symbols' 25 each; FontPrint draws codes below 0x20 as 24-wide cells). */
static s32 text_width(const char *text, const volatile u8 *metrics)
{
    s32 w = 0;
    while (*text) {
        w += metrics[4 * (u8)*text + 3];
        text++;
    }
    return w;
}

/* The name box's draw. Retail (DrawSaveLevelName, func_00223E40) names the
 * planet of the focused slot list's slot, from the card's summary of the
 * slot (D_0013D390 + 0x20 + 0x1C * slot: its level, -1 for an empty slot,
 * which gets "empty" centred instead), in two centred lines of the normal
 * font at y 4 and 20 ("Robot Plant", "Quartu"), while the card is idle. For
 * a Randomizer save a third line follows at y 36, where the stretched box
 * has room: "Seed", then the code's nine symbols, centred together. The
 * box is 322 pixels wide and the line about 280; a translation that would
 * not fit is drawn smaller through the scaled font print
 * (Font_DrawString). */
static void draw_seed_line(const u32 *words, const struct RzSlot *rz, s32 L)
{
    const volatile u8 *metrics = (const volatile u8 *)exe_data(L, font_metrics[0]);
    const char *label = LVL(L, func_001FE540, MsgFn)(EE_TEXT_SEED);
    char line[RZ_CODE_LENGTH + 1];
    s32 width = (s32)words[WIDGET_WIDTH];
    s32 label_w = text_width(label, metrics);
    s32 total = label_w + SEED_GAP + RZ_CODE_LENGTH * GLYPH_ADVANCE;
    f32 scale = total + 2 * TEXT_MARGIN <= width ? 1.0f : (f32)(width - 2 * TEXT_MARGIN) / (f32)total;
    f32 x = ((f32)width - (f32)total * scale) * 0.5f;
    f32 y = (f32)(NAME_FIRST_Y + 2 * NAME_LINE);
    u64 tex;
    ee_debug_rz[0] = (u32)width;
    ee_debug_rz[1] = words[WIDGET_HEIGHT];
    ee_debug_rz[2] = (u32)total;
    ee_debug_rz[3] = (u32)(s32)(scale * 1000.0f);      /* through s32: the compiler has no float-to-unsigned helper here */
    seed_line(rz, line);
    LVL(L, func_001F4630, PagingFn)(0);
    tex = LVL(L, func_001F4868, EffectTexFn)(FONT_NORMAL_TEX);
    LVL(L, func_001F69F0, ScaledPrintFn)(TEXT_NORMAL, label, -1, tex, (const void *)metrics, x, y, scale);
    LVL(L, func_001F69F0, ScaledPrintFn)(TEXT_NORMAL, line, -1, tex, (const void *)metrics,
                                         x + (f32)(label_w + SEED_GAP) * scale, y, scale);
    LVL(L, func_001F4748, VoidFn)();
}

/* The slot the focused slot list is on, clamped to the five. */
static s32 focused_slot(u32 menu_record)
{
    u32 page = *(volatile u32 *)(menu_record + 4);
    u32 *list = page != 0 ? (u32 *)*(volatile u32 *)(page + PAGE_FOCUS) : 0;
    s32 slot = list != 0 ? (s32)list[WIDGET_SLOT] : 0;
    return slot < 0 ? 0 : slot >= MC_SLOTS ? MC_SLOTS - 1 : slot;
}

s32 ee_rz_name_draw(void *widget)
{
    s32 slot = focused_slot(TITLE_MENU_STATE);
    const struct RzSlot *rz = &slots[slot].rz;
    s32 r;
    install_glyphs();
    r = ((WidgetFn)retail_name_draw)(widget);
    if (rz_saves && rz->present && card_ready() && MC_SLOT_LEVEL(slot) != -1) {
        draw_seed_line((const u32 *)widget, rz, title_program);
    }
    return r;
}

/* The name box's draw on the pause menu's Save and Load pages in level L (src/rz_hooks.s stubs, set into the
 * widget by ee_rz_pause_open): retail's DrawSaveLevelName, then the Seed line, under the pages' own conditions
 * for showing a slot (the card idle for 11 of the menu record's ticks, a PS2 card). The slots were scanned by
 * the slot list's draw (ee_rz_slots_end_c), which comes before this one in the page. */
DECLARE_LVL(func_00223E40);             /* DrawSaveLevelName(widget) */

s32 ee_rz_pause_name_c(void *widget, s32 L)
{
    u32 menu_record = (u32)LVL_DATA(L, menu_state, u8 *);
    s32 ticks = *(volatile s32 *)(menu_record + MENU_TICKS_OFFSET);
    s32 slot = focused_slot(menu_record);
    const struct RzSlot *rz = &slots[slot].rz;
    s32 r = LVL(L, func_00223E40, WidgetFn)(widget);
    if (rz_saves && rz->present && MC_STATE < 3 && MC_REQUEST < 0 && ticks >= 11 && MC_TYPE == 2
        && MC_SLOT_LEVEL(slot) != -1) {
        draw_seed_line((const u32 *)widget, rz, L);
    }
    return r;
}

/* The pause menu opening in level L (src/charge_boots.c's pause entry hook). The Save and Load pages (the
 * executable's pages D_001D1948 and D_001D19D0, relinked into the level) share the title screen's name box
 * widget, whose draw now goes through ee_rz_pause_name_c; the seed symbols get their metrics (their pixels are
 * in the level's fonts from the disc); and with a Randomizer save on the card the name panel, which both pages
 * play as sequence PAUSE_NAME_SEQ, is stretched down a line as the title screen's is. The title's sequence
 * copies belong to its program and are forgotten here; the title installs its own again when it opens. */
#define PAUSE_NAME_SEQ  0x7F            /* both pause pages' moby_anims[NAME_PANEL] */
#define PAUSE_FIRST_ROW 0x001CE938      /* the pause menu's front page's (D_001CE808) first row: Weapons */
extern const u32 ee_rz_name_draw_table[LEVEL_PROGRAMS];       /* the stubs, by level (src/rz_hooks.s) */
extern const u32 ee_rz_pause_row_draw_table[LEVEL_PROGRAMS];
static u32 retail_row_draw[LEVEL_PROGRAMS];                    /* DrawMenuItemList, each level program's */
static s32 pause_program = -1;

/* The pause pages' name panel stretched a line, once a Randomizer save is known and the panel mobys exist. The
 * pause menu builds them after its entry, and a panel keeps the sequence it started with, so this runs from the
 * front page's first row's draw (ee_rz_pause_row_c), every frame the front page shows, before any page with the
 * name panel can open; and from the slot list's draw, for a save the card scan found only then. */
static void pause_name_stretch(s32 L)
{
    u32 panel, cls;
    if (!rz_saves || is_installed(PAUSE_NAME_SEQ)) {
        return;
    }
    panel = *(volatile u32 *)exe_data(L, PANELS_ADDRESS + 4 * PANEL_ROW);
    if (panel == 0 || (cls = *(u32 *)(panel + 0x24)) == 0 || *(u8 *)(cls + 0xC) <= PAUSE_NAME_SEQ) {
        return;
    }
    class_sequences = (u32 *)(cls + 0x48);
    raise_sequence(PAUSE_NAME_SEQ, 0, 0, 1);
}

/* The front page's first row's draw in level L (src/rz_hooks.s stubs): the stretch, then retail's. */
s32 ee_rz_pause_row_c(void *widget, s32 L)
{
    pause_name_stretch(L);
    return ((WidgetFn)retail_row_draw[L])(widget);
}

void ee_rz_pause_open(s32 L)
{
    u32 *name_box;
    u32 *row;
    if (L >= LEVEL_PROGRAMS) {
        return;
    }
    row = (u32 *)exe_data(L, PAUSE_FIRST_ROW);
    if (row[WIDGET_DRAW] != ee_rz_pause_row_draw_table[L]) {
        retail_row_draw[L] = row[WIDGET_DRAW];
        row[WIDGET_DRAW] = ee_rz_pause_row_draw_table[L];
    }
    if (L != pause_program) {
        pause_program = L;
        installed = 0;
    }
    name_box = (u32 *)exe_data(L, NAME_BOX);
    name_box[WIDGET_DRAW] = ee_rz_name_draw_table[L];
    install_glyph_metrics(L);
    pause_name_stretch(L);
}

/* The save pages' widgets, in this program's data: the two slot lists' draw
 * and the name box's go through the edition's (retail's kept for them),
 * the overwrite question comes back to retail's New Game page, nothing is
 * armed. A title program loaded afresh has retail's words again. */
static void patch_widgets(void)
{
    u32 *new_game = (u32 *)NEW_GAME_SLOTS;
    u32 *load_game = (u32 *)LOAD_GAME_SLOTS;
    u32 *name = (u32 *)NAME_BOX;
    if (new_game[WIDGET_DRAW] != (u32)ee_rz_slots_draw) {
        retail_slots_draw = new_game[WIDGET_DRAW];
    }
    if (name[WIDGET_DRAW] != (u32)ee_rz_name_draw) {
        retail_name_draw = name[WIDGET_DRAW];
    }
    retail_slots_update = new_game[0];
    new_game[WIDGET_DRAW] = (u32)ee_rz_slots_draw;
    load_game[WIDGET_DRAW] = (u32)ee_rz_slots_draw;
    name[WIDGET_DRAW] = (u32)ee_rz_name_draw;
    ((u32 *)CONFIRM_PAGE)[PAGE_BACK_WORD] = NEW_GAME_PAGE;
    ee_rz_arm(0);
    ee_rz_set_seed(0, start_setting, 0);
    clear_seed_box();
}

/* With the Goodies row locked and a Randomizer save on the card, the start
 * menu stays retail's and only the name boxes are stretched: New Game's and
 * Load Game's (one sequence), and the confirm page's from its first frame
 * only. That sequence takes the name box to the overwrite question's box,
 * and back on No: stretched throughout, the question ran down over the
 * prompt box; not stretched, the name box came back a line short. */
static void install_name_stretch(void)
{
    u32 panel = PANELS[PANEL_ROW];
    u32 cls;
    if (panel == 0 || (cls = *(u32 *)(panel + 0x24)) == 0 || *(u8 *)(cls + 0xC) <= SEQ_ROW) {
        return;
    }
    class_sequences = (u32 *)(cls + 0x48);
    raise_sequence(((struct MenuPage *)NEW_GAME_PAGE)->moby_anims[NAME_PANEL], 0, 0, 1);
    raise_sequence(SEQ_CONFIRM_NAME, 0, 0, -1);
}

/* The start menu as retail has it, or with the Goodies row in panel 0, its
 * sequences installed. Screens 0 and 1 hold retail's empty list
 * TITLE_PLACEHOLDER (its moby is panel 1's), so screen 0 is free; the row
 * takes it rather than a screen after the rows, whose panel lost its frame
 * in 16:9 on the title screen. */
static void title_rows(s32 with_goodies)
{
    struct MenuPage *page = (struct MenuPage *)TITLE_PAGE;
    struct MenuList *new_game = (struct MenuList *)TITLE_NEW_GAME;
    struct MenuList *options = (struct MenuList *)TITLE_OPTIONS;
    s32 k;
    for (k = 0; k < 3; k++) {
        struct MenuList *row = (struct MenuList *)(TITLE_NEW_GAME + k * TITLE_LIST_STRIDE);
        struct MenuPage *opens = (struct MenuPage *)row->items[0].param;
        if (opens->moby_anims[PANEL_ROW] != SEQ_ROW) {
            opened_panel0[k] = opens->moby_anims[PANEL_ROW];
        }
        opens->moby_anims[PANEL_ROW] = opened_panel0[k];
        if (with_goodies && SEQ_FRAME_COUNT(class_sequences[opened_panel0[k]]) < 2) {
            opens->moby_anims[PANEL_ROW] = SEQ_ROW;
        }
    }
    page->moby_anims[PANEL_ROW] = PANEL_ROW_PARKED;
    page->screens[PANEL_ROW] = TITLE_PLACEHOLDER;
    options->down = TITLE_NEW_GAME;
    new_game->up = TITLE_OPTIONS;
    if (!with_goodies) {
        return;
    }
    for (k = 0; k < (s32)(sizeof(goodies_list) / 4); k++) {     /* this program's list functions and flags */
        ((u32 *)&goodies_list)[k] = ((u32 *)options)[k];
    }
    goodies_list.moby = 0;
    goodies_list.items = goodies_items;
    goodies_list.up = TITLE_OPTIONS;
    goodies_list.down = TITLE_NEW_GAME;
    goodies_list.selected = 0;
    goodies_list.scroll = 0;
    goodies_items[0].fade_timer = 0;
    page->moby_anims[PANEL_ROW] = SEQ_ROW;
    page->screens[PANEL_ROW] = (u32)&goodies_list;
    options->down = (u32)&goodies_list;
    new_game->up = (u32)&goodies_list;
}

/* Pause_Update's call of Pause_Open in the title screen's program L (19, 20;
 * src/goodies_hooks.s stubs). The decision comes first and Pause_Open runs
 * on the retail page; the row and its sequences go in before the page
 * switch that follows reads them. The save pages' widgets are patched
 * whether or not the row shows: a Randomizer save shows its badge and seed
 * on any card. */
DECLARE_LVL(func_00219E90);             /* Pause_Open */

void ee_title_menu_open_c(s32 L)
{
    s32 unlocked;
    title_program = L;
    unlocked = goodies_unlocked();
    patch_widgets();
    restore_sequences();
    title_rows(0);
    LVL(L, func_00219E90, VoidFn)();
    if (unlocked && install_sequences() && install_randomizer_sequences()) {
        title_rows(1);
        build_pages();
        return;
    }
    restore_sequences();
    if (rz_saves) {
        install_name_stretch();
    }
}

/* The title screen's main loop (func_001EBB48) runs the card machine,
 * MemCard_UpdateState (func_00209E68), once a frame; in program L (19, 20)
 * the call comes here (src/goodies_hooks.s stubs). The scan steps in the
 * machine's idle turns: between its card checks (sceMcGetInfo, state 0-1)
 * it waits ten frames in state 2 with no command running, and libmc runs
 * one command at a time, so while a command of the scan's runs, the machine
 * waits for it. Only while the start menu is closed (Pause_Update, which
 * counts its updates, has not run since the last frame): the start menu's
 * pages ask the machine for their own sequences, and New Game or Load Game
 * leave the title screen through loops that run the machine directly. The
 * scan starts again when the card changes; a scan the PRESS START screen
 * did not finish is finished when the start menu opens (goodies_unlocked).
 * A record write (record_job) steps in the same turns, menu open or not. */
DECLARE_LVL(func_00209E68);             /* MemCard_UpdateState */
#define MENU_UPDATES    (*(volatile s32 *)0x001D6080)    /* D_001D5F70 + 0x110: Pause_Update's count, held at 0x7D00 */
static s32 menu_updates_seen;

/* The machine is between its card checks, with no command or request. */
static s32 machine_idle(void)
{
    return !MC_BUSY && MC_STATE == 2 && MC_REQUEST < 0 && MC_MAIN_FILE[0] == '/';
}

void ee_title_card_c(s32 L)
{
    s32 menu_open = MENU_UPDATES != menu_updates_seen;
    menu_updates_seen = MENU_UPDATES;
    title_program = L;
    ee_debug_card[0]++;
    if (scan.in_flight && !scan_poll(0)) {
        return;
    }
    if (record_job.in_flight && !record_job_poll(0)) {
        return;
    }
    if (record_job.step != RECORD_IDLE && machine_idle()) {
        if (record_job_issue() == 0) {
            record_job.in_flight = 1;
            return;
        }
        record_job.step = RECORD_IDLE;      /* libmc refused it */
    }
    if (!menu_open && machine_idle()) {
        if (scan.file == SCAN_DONE && (!scan_valid || card_changed())) {
            scan_start(L);
        }
        if (scan.file != SCAN_DONE && scan.step == STEP_OPEN && !card_ok()) {
            scan.file = SCAN_DONE;          /* no card, or one the machine has yet to check twice: none read */
            scan_valid = 1;
        }
        if (scan.file != SCAN_DONE && scan_issue() == 0) {
            scan.in_flight = 1;
            ee_debug_card[1]++;
            return;
        }
    }
    LVL(L, func_00209E68, VoidFn)();
}
