/* rz2/rz6: the title screen's Goodies row, unlocked by beating the game.
 *
 * The row appears under Options on the title screen's start menu once Drek
 * has been beaten in the edition, or once the memory card holds a Challenge
 * Mode save (a save whose completion count is above 0, the Load Game list's
 * "R" badge). Nothing announces it. It is not the pause menu's Goodies.
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
 * page opens, so the edition reads the card itself when the start menu
 * opens: the record, then each slot's first bytes. A save file starts
 * {game blob size, level blob size}, then the game blob {length, checksum,
 * records {id, size, data}} from the save block table at 0x1A05C0, whose
 * third record (id 2) is the completion count 0x15EF20, at file offset
 * 0x30. The calls are retail's own libmc wrappers (core), run to the end
 * one by one as MemCard_CheckFreeSpace (func_00209BB8) does. The game's
 * card state machine polls the card all the time on the title screen,
 * one command at a time: its busy flag (MC_BUSY) makes it ask the command's
 * result with a non-blocking sync each frame, and clear the flag once the
 * command is done. The edition waits for that command with a blocking sync
 * into the machine's own result words and clears the flag, which is what
 * the machine would have seen; it leaves the card alone while the machine
 * runs a longer sequence of its own (a directory scan, a save).
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
 * New Game (retail's New Game page) or Reuse Seed, a box that shows the
 * buttons pressed (build_pages). */
#include "modseg.h"

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

#define SAVE_HEAD       0x40        /* a save file's first bytes: sizes, blob header, records 0-2 */
#define SAVE_COMPLETES  (0x30 / 4)  /* the completion count's word in them */

/* The start menu, in both title screen programs' data (at the executable's
 * addresses; tools/gen_frontend.py checks it). */
#define TITLE_PAGE      0x001D4948
#define TITLE_NEW_GAME  0x001D4A18  /* then Load Game, then Options */
#define TITLE_OPTIONS   0x001D4AB8
#define TITLE_LIST_STRIDE 0x50
#define TITLE_PLACEHOLDER 0x001CE678 /* the start menu's screens 0 and 1: an empty list, no update or draw */
#define PANELS          ((volatile u32 *)0x001D6120)     /* the 14 panel mobys */
#define PANEL_ROW       0           /* the Goodies row's screen and panel */
#define PANEL_ROW_PARKED 0          /* the start menu's sequence for panel 0: parked */
#define ROW_FIRST_PANEL 2           /* New Game, Load Game, Options: panels 2-4 */
#define SEQ_ROW         218         /* the sequence id the row's panel plays, which no page plays */
#define SEQ_COPIES      32          /* the row's, the rows' 3, the opened pages' 8, the Goodies pages' 13 */
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
#define PAD_PRESSED     (*(volatile u32 *)0x0013CBF4)    /* D_0013CA40 + 0x1B4: buttons newly down, no stick directions */
#define PAGE_FOCUS      0x40
#define PAGE_WORDS      (0x88 / 4)
#define WIDGET_WORDS    (0x50 / 4)
#define OPTIONS_PAGE    0x001D4B08  /* title box, three-row list, "Select / Exit" box */
#define OPTIONS_TITLE   0x001D0C58  /* a title box: its text id at + 0x34 */
#define OPTIONS_LIST    0x001D4BE8
#define SELECT_PROMPT   0x001CFE90  /* "\x10 Select", "\x12 Exit" */
#define VIDEO_PAGE      0x001D4E38  /* title box, a wider list box, "Select / Exit" box */
#define VIDEO_TITLE     0x001D4EC0
#define EXIT_PROMPT     0x001CE7B8  /* "\x12 Exit" alone */
#define NEW_GAME_PAGE   0x001D5008
#define LIST_ENTER      2           /* a list's enter callback (word 2): Options' sets its rows, so the copies have none */
#define SEED_FLAGS      0x42        /* list flags: one colour (2), left-aligned text (0x40) */
#define SEED_MAX        15
#define GOODIES_LIST_HALF    1250   /* the list box's half height for one row (Options' is 3770 for three) */
#define RANDOMIZER_LIST_HALF 2100   /* for two rows */
/* Sequence ids no page plays (checked over the executable's page records):
 * the Goodies, Randomizer and Reuse Seed pages' boxes (title, list, prompt),
 * the Randomizer New Game page's boxes, and its title box as retail has it
 * (retail's 196 now starts at the Goodies row). */
static const u8 goodies_ids[3] = { 216, 217, 211 };
static const u8 randomizer_ids[3] = { 3, 83, 138 };
static const u8 reuse_ids[3] = { 139, 140, 141 };
static const u8 new_game_ids[3] = { 183, 184, 174 };
#define SEQ_NEW_GAME_TITLE 173

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

static u8 drek_pending;             /* beaten this session, not yet on the card */
static u8 card_flags;               /* the record's flags as last read */
static u8 card_buffer[64] __attribute__((aligned(64)));

static struct MenuItem goodies_items[2] = { { TEXT_GOODIES, ACTION_OPEN, 0, 0, 0 }, { 0, 0, 0, 0, 0 } };
static struct MenuList goodies_list;
static u32 seq_store[SEQ_COPIES][16] __attribute__((aligned(16)));     /* 0x1C header + 8 frame pointers, 16-aligned */
static u32 frame_store[SEQ_COPIES][SEQ_FRAMES][4 * (1 + FRAME_QWC)] __attribute__((aligned(16)));

/* memcard_Checksum (func_0020BB10): a 16-bit shift register, seeded with
 * the CRC-32 polynomial, over the bytes. */
static u32 save_checksum(const u8 *data, s32 length)
{
    u32 crc = 0xEDB88320;
    s32 i, bit;
    for (i = 0; i < length; i++) {
        crc ^= data[i] << 8;
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1F45;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc & 0xFFFF;
}

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

/* The card is free for the edition's calls: the machine's running command,
 * if any, finished into its own words; 0 while the machine runs a sequence. */
static s32 card_take(void)
{
    if (MC_STATE >= 3 || MC_REQUEST >= 0) {
        return 0;
    }
    if (MC_BUSY) {
        func_00123F30(0, MC_COMMAND, MC_RESULT);
        MC_BUSY = 0;
    }
    return MC_MAIN_FILE[0] == '/';
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

/* The edition's record from the main file: its flags, 0 for none or a damaged record. */
static s32 read_record(void)
{
    u32 *words = (u32 *)card_buffer;
    if (!card_read(MC_MAIN_FILE, EDITION_OFFSET, 16)) {
        return 0;
    }
    if (words[0] != EDITION_MAGIC || (words[1] & 0xFFFF) != EDITION_VERSION
        || words[3] != save_checksum(card_buffer, 12)) {
        return 0;
    }
    return words[2];
}

static s32 write_record(s32 flags)
{
    u32 *words = (u32 *)card_buffer;
    words[0] = EDITION_MAGIC;
    words[1] = EDITION_VERSION;
    words[2] = flags;
    words[3] = save_checksum(card_buffer, 12);
    return card_write(MC_MAIN_FILE, EDITION_OFFSET, 16);
}

/* Slot SLOT's file holds a Challenge Mode save: a well-formed game blob whose completion count is above 0. */
static s32 challenge_save(s32 slot)
{
    char name[MC_DIR_LENGTH + 10];
    const char *file = "save0.bin";
    u32 *words = (u32 *)card_buffer;
    s32 i;
    for (i = 0; i < MC_DIR_LENGTH; i++) {
        name[i] = MC_MAIN_FILE[i];
    }
    for (i = 0; file[i] != 0; i++) {
        name[MC_DIR_LENGTH + i] = file[i];
    }
    name[MC_DIR_LENGTH + 4] = '0' + slot;
    name[MC_DIR_LENGTH + i] = 0;
    if (!card_read(name, 0, SAVE_HEAD)) {
        return 0;
    }
    /* {sizes}, {length = size - 8, checksum}, {0, 4, level}, {1, 4, bolts}, {2, 4, completions} */
    if (words[2] != words[0] - 8 || words[4] != 0 || words[7] != 1 || words[10] != 2 || words[11] != 4) {
        return 0;
    }
    return (s32)words[SAVE_COMPLETES] > 0;
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

/* The unlock: Drek beaten (on the card or this session), or a Challenge Mode save on the card. */
static s32 goodies_unlocked(void)
{
    s32 slot;
    if (!card_take()) {
        return drek_pending;
    }
    card_flags = read_record();
    if (drek_pending && !(card_flags & EDITION_DREK) && write_record(card_flags | EDITION_DREK)) {
        card_flags |= EDITION_DREK;
        drek_pending = 0;
    }
    if (drek_pending || (card_flags & EDITION_DREK)) {
        return 1;
    }
    for (slot = 0; slot < MC_SLOTS; slot++) {
        if (challenge_save(slot)) {
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

/* Sequence ID becomes a copy of BASE whose records are moved by DELTA
 * ({x, y, z} per record): every frame by all of it, or with TAPER by all of
 * it at the first frame down to none at the last, so the sequence still
 * ends where retail's does. 0 if BASE does not fit the copy.
 *
 * A moby keeps its sequence's bounding sphere from the header when the
 * sequence starts (moby + 0xF0, read as a quadword, so the header must be
 * 16-byte aligned), and the panel draws only while that sphere is where the
 * panel is: copies whose sphere stayed behind, or was read from a misaligned
 * header, showed the panel's dark face without its frame. So the sphere
 * moves with the centre (half the move for a taper) and grows by the rest:
 * the taper's other half and any change of the corners. */
static s32 copy_sequence(s32 id, u32 base, const s16 *delta, s32 records, s32 taper)
{
    u32 *seq = seq_store[installed];
    f32 *sphere = (f32 *)seq;
    s16 *first = FRAME_RECORDS(SEQ_FRAME(base, 0));
    s32 frames = SEQ_FRAME_COUNT(base);
    s32 span = taper && frames > 1 ? frames - 1 : 1;
    s32 f, k, axis, centre = 0, half_move = 0, corners = 0;
    if (installed == SEQ_COPIES || frames == 0 || frames > SEQ_FRAMES) {
        return 0;
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
            change += d < 0 ? -d : d;
        }
        corners = change > corners ? change : corners;
    }
    sphere[3] += (f32)(half_move + corners);
    for (f = 0; f < frames; f++) {
        u32 *frame = (u32 *)SEQ_FRAME(base, f);
        u32 *copy = frame_store[installed][f];
        s32 weight = taper ? span - f : 1;
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
            }
        }
        seq[0x1C / 4 + f] = (u32)copy;
    }
    return point_sequence(id, (u32)seq);
}

/* Sequence ID's copy with the panel's centre moved DZ up. */
static s32 raise_sequence(s32 id, s32 dz, s32 taper)
{
    s16 delta[3 * FRAME_RECORDS_MAX];
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
    return copy_sequence(id, base, delta, records, taper);
}

/* Sequence ID's copy of BASE starting from the records REST instead of its own first frame. */
static s32 sequence_from(s32 id, u32 base, const s16 *rest, s32 records, s32 taper)
{
    s16 delta[3 * FRAME_RECORDS_MAX];
    s16 *first = FRAME_RECORDS(SEQ_FRAME(base, 0));
    s32 k;
    if (FRAME_COUNT(SEQ_FRAME(base, 0)) != records) {
        return 0;
    }
    for (k = 0; k < 3 * records; k++) {
        delta[k] = rest[k] - first[4 * (k / 3) + k % 3];
    }
    return copy_sequence(id, base, delta, records, taper);
}

/* The sequences of a start menu with the Goodies row, centred. The rows
 * (panels 2-4) move up by half their spacing, and the Goodies row rests a
 * row below Options: Load Game's sequence, moved, as SEQ_ROW. The pages the
 * rows open play their panels 2-4 from where the rows rest, so theirs get
 * the same move, tapered to nothing at their last frame, their own layout.
 * Their panel 0 is a title box sliding in from the side (Load Game, New
 * Game), whose sequence then starts from the Goodies row at rest, all of its
 * records tapered; or it is parked (Options) and plays SEQ_ROW, which slides
 * the row out and back in (title_rows). 0 if a sequence is not as expected. */
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
    if (!sequence_from(SEQ_ROW, middle, rest, records, 0)) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        struct MenuList *row = (struct MenuList *)(TITLE_NEW_GAME + k * TITLE_LIST_STRIDE);
        struct MenuPage *opens = (struct MenuPage *)row->items[0].param;
        s32 title = opened_panel0[k];
        if (!raise_sequence(page->moby_anims[ROW_FIRST_PANEL + k], raise, 0)) {
            return 0;
        }
        for (j = ROW_FIRST_PANEL; j < ROW_FIRST_PANEL + 3; j++) {
            s32 id = opens->moby_anims[j];
            if (!is_installed(id) && SEQ_FRAME_COUNT(class_sequences[id]) > 1 && !raise_sequence(id, raise, 1)) {
                return 0;
            }
        }
        if (SEQ_FRAME_COUNT(class_sequences[title]) > 1 && !is_installed(title)
            && !sequence_from(title, class_sequences[title], rest, records, 1)) {
            return 0;
        }
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

static s32 abs32(s32 v)
{
    return v < 0 ? -v : v;
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

/* The Goodies pages' sequences, each box morphing as the Options page's
 * sub-pages do (morph_sequence): Goodies from the start menu's rows to the
 * Options page's layout with a list box one row high, Randomizer from there
 * to a list two rows high, Reuse Seed from there to the Video Options
 * page's layout. The Randomizer's New Game is retail's New Game page with
 * copies of its sequences that start from the Randomizer page's boxes (all
 * records tapered, as for the start menu's pages); its title box slides in
 * as retail's does. A page switch back plays these backwards. 0 if a
 * sequence is not as expected. */
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
    set_half_height(randomizer[1], RANDOMIZER_LIST_HALF);
    for (j = 0; j < 3; j++) {
        u32 base = retail_sequence(new_game->moby_anims[ROW_FIRST_PANEL + j]);
        if (!morph_sequence(goodies_ids[j], rows[j], goodies[j])
            || !morph_sequence(randomizer_ids[j], goodies[j], randomizer[j])
            || !morph_sequence(reuse_ids[j], randomizer[j], reuse[j])) {
            return 0;
        }
        layout_records(SEQ_FRAME(base, 0), randomizer[j], rest);
        if (FRAME_COUNT(SEQ_FRAME(base, 0)) != JOINTS || !sequence_from(new_game_ids[j], base, rest, JOINTS, 1)) {
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
static u32 goodies_title[WIDGET_WORDS];
static u32 randomizer_title[WIDGET_WORDS];
static u32 reuse_title[WIDGET_WORDS];
static struct MenuList goodies_menu;
static struct MenuList randomizer_menu;
static struct MenuList seed_box;
static struct MenuItem goodies_menu_items[2];
static struct MenuItem randomizer_items[3];
static struct MenuItem seed_items[2] = { { EE_TEXT_SEED_INPUT, ACTION_OPEN, 0, 0, 0 }, { 0, 0, 0, 0, 0 } };
static s32 title_program;           /* the title screen program running the pages (19, 20) */

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

static void build_pages(void)
{
    struct MenuPage *page;
    s32 j;
    copy_words(goodies_title, OPTIONS_TITLE, WIDGET_WORDS);
    goodies_title[0x34 / 4] = TEXT_GOODIES;
    copy_words(randomizer_title, OPTIONS_TITLE, WIDGET_WORDS);
    randomizer_title[0x34 / 4] = EE_TEXT_RANDOMIZER;
    copy_words(reuse_title, VIDEO_TITLE, WIDGET_WORDS);
    reuse_title[0x34 / 4] = EE_TEXT_REUSE_SEED;

    set_item(&goodies_menu_items[0], EE_TEXT_RANDOMIZER, ACTION_OPEN, (u32)randomizer_page);
    set_item(&goodies_menu_items[1], 0, 0, 0);
    set_item(&randomizer_items[0], TEXT_NEW_GAME, ACTION_FREEZE, (u32)new_game_page);
    set_item(&randomizer_items[1], EE_TEXT_REUSE_SEED, ACTION_OPEN, (u32)reuse_page);
    set_item(&randomizer_items[2], 0, 0, 0);
    make_list(&goodies_menu, goodies_menu_items);
    make_list(&randomizer_menu, randomizer_items);
    make_list(&seed_box, seed_items);
    seed_box.update = (u32)ee_seed_box_update;
    seed_box.flags = SEED_FLAGS;

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

    make_page(reuse_page, VIDEO_PAGE, (u32)randomizer_page, (u32)reuse_title, (u32)&seed_box, EXIT_PROMPT);
    page = (struct MenuPage *)reuse_page;
    for (j = 0; j < 3; j++) {
        page->moby_anims[ROW_FIRST_PANEL + j] = reuse_ids[j];
    }

    copy_words(new_game_page, NEW_GAME_PAGE, PAGE_WORDS);
    page = (struct MenuPage *)new_game_page;
    page->back = (u32)randomizer_page;
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
 * R1, L2, R2: the game's own codes 0x14-0x17, advance 25), so fifteen always
 * fill the same space: Cross, Circle and Square are the edition's copies
 * centred in that width (codes 0x02-0x04, unused by any string; codes below
 * 0x20 draw as untinted button sprites), and the D-pad's arms are the
 * edition's (0x1A-0x1D); install_glyphs. */
static const u16 seed_buttons[11] = { 0x0040, 0x0020, 0x0080, 0x0004, 0x0008, 0x0001, 0x0002,
                                      0x1000, 0x2000, 0x4000, 0x8000 };
static const u8 seed_glyphs[11] = { 0x02, 0x03, 0x04, 0x14, 0x15, 0x16, 0x17, 0x1A, 0x1B, 0x1C, 0x1D };
#define PAD_TRIANGLE 0x0010
static char seed_text[SEED_MAX + 1];
static s32 seed_length;

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
 * time the seed box runs after the title screen's data loads (a fresh load
 * has advance 0 again). The data cache is written back before the next
 * upload reads them. */
#define EE_GLYPHS 7
extern const u8 ee_glyph_cells[3][EE_GLYPHS][16][24];
extern const u8 ee_glyph_places[EE_GLYPHS][4];
extern void func_00118D80(s32);         /* FlushCache (core) */
#define EFFECT_BANK     (*(volatile u32 *)0x0015F560)
#define EFFECT_TABLE    0x0018D540
static const u32 font_metrics[3] = { 0x001DF3D0, 0x001DF770, 0x001DFB10 };   /* normal, small, large */

static void install_glyphs(void)
{
    s32 font, g, x, y;
    if (EFFECT_BANK == 0 || ((volatile u8 *)font_metrics[0])[4 * ee_glyph_places[0][0] + 3] != 0) {
        return;
    }
    for (font = 0; font < 3; font++) {
        u32 offset = *(volatile u16 *)(EFFECT_TABLE + 16 * (font + 1) + 8) << 4;
        volatile u8 *sheet = (volatile u8 *)(EFFECT_BANK + offset);
        volatile u8 *metrics = (volatile u8 *)font_metrics[font];
        for (g = 0; g < EE_GLYPHS; g++) {
            u32 u = ee_glyph_places[g][1];
            u32 v = ee_glyph_places[g][2];
            for (y = 0; y < 16; y++) {
                for (x = 0; x < 24; x++) {
                    sheet[(v + y) * 256 + u + x] = ee_glyph_cells[font][g][y][x];
                }
            }
            metrics[4 * ee_glyph_places[g][0]] = u;
            metrics[4 * ee_glyph_places[g][0] + 1] = v;
            metrics[4 * ee_glyph_places[g][0] + 2] = 0;
            metrics[4 * ee_glyph_places[g][0] + 3] = ee_glyph_places[g][3];
        }
    }
    func_00118D80(0);
}

DECLARE_LVL(func_0022ED80);             /* the menus' sound (1: the cursor's tick) */
typedef void (*MenuSoundFn)(s32, s32, u32);

/* The text the box shows: the buttons entered, left to right (src/text.c). */
const char *ee_seed_text(void)
{
    return seed_text;
}

/* The box's update, in place of the list's own: each button of the set
 * pressed adds its glyph, Triangle empties the box and goes back. */
s32 ee_seed_box_update(struct MenuList *box)
{
    u32 pressed = PAD_PRESSED;
    s32 k;
    install_glyphs();
    if (MENU_CURRENT == 0 || *(u32 *)(MENU_CURRENT + PAGE_FOCUS) != (u32)box) {
        return 0;
    }
    if (pressed & PAD_TRIANGLE) {
        seed_length = 0;
        seed_text[0] = 0;
        MENU_NEXT = ((struct MenuPage *)MENU_CURRENT)->back;
        return 0;
    }
    for (k = 0; k < 11; k++) {
        if ((pressed & seed_buttons[k]) && seed_length < SEED_MAX) {
            seed_text[seed_length] = seed_glyphs[k];     /* the glyphs' advance spaces them */
            seed_text[seed_length + 1] = 0;
            seed_length++;
            if (lvl_func_0022ED80[title_program] != 0) {
                LVL(title_program, func_0022ED80, MenuSoundFn)(1, 0x11, box->moby);
            }
        }
    }
    return 0;
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
 * switch that follows reads them. */
DECLARE_LVL(func_00219E90);             /* Pause_Open */

void ee_title_menu_open_c(s32 L)
{
    s32 unlocked = goodies_unlocked();
    title_program = L;
    restore_sequences();
    title_rows(0);
    LVL(L, func_00219E90, VoidFn)();
    if (unlocked && install_sequences() && install_randomizer_sequences()) {
        title_rows(1);
        build_pages();
    } else {
        restore_sequences();
    }
}
