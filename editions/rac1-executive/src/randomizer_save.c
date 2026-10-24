/* rz2: the Randomizer save: where a save keeps its seed and layout, how a
 * Randomizer new game starts, and how the title screen reads a slot's
 * record (src/randomizer.h for the logic, src/goodies.c for the menus).
 *
 * A save file is {game blob size, level blob size}, the game blob, then
 * twenty level blobs (memcard_MakeWholeSave, func_0020BA00), one per entry
 * of the level save table D_001A08C0 and per level page: the per-level
 * records are arrays of twenty, saved and restored as record i of blob i.
 * The game has nineteen levels (0-18), so the twentieth page is never a
 * level's: block 0xBBA, the level's 0x800 bytes of moby state, is read at
 * D_00141FC0 + (level << 11) for the current level only (the save's
 * Menu_ReadSector, the load's func_002083E0), and the twentieth page,
 * 0x14B7C0-0x14BFBF, is 2 KB the 2002 game writes to the card and reads
 * back without ever looking at. The Randomizer's record lives at its
 * start: the 2002 game loads such a save as a plain save (the vanilla
 * data is a plain new game's), keeps the page across its own saves, and
 * sees no corruption; the game's own save-size check passes because the
 * file is the size it always is. The record has a magic word, a size, a
 * version and the game's own save checksum (memcard_Checksum,
 * func_0020BB10) over itself, so anything else there reads as no record.
 *
 * Starting one. The Randomizer's New Game page (src/goodies.c) is retail's
 * with the slot list's update wrapped: while it runs, a Randomizer new
 * game is armed here. Creating the save (Menu_BeginSaveSlot, func_00227D20,
 * or the no-card start) begins with the game-state reset from the disc's
 * initial state (func_00209DC0), which the title screen's programs call
 * through ee_fe_reset_19/20 (src/widescreen.c); that calls
 * ee_rz_after_reset once the reset is done, and an armed game gets its
 * layout written into the page before memcard_MakeWholeSave copies it to
 * the card. The seed is the one Reuse Seed decoded, or a fresh one from
 * the clock and the menu's tick. The layout itself (the item of each
 * check) is stored, not recomputed, so a later logic version still plays
 * an old save as it was made. The record also keeps what the player has
 * collected and received (src/randomizer_play.c): a bit per check, the
 * Gold Bolts received, the special items received; those change in place,
 * with the checksum redone, and the save carries them. */
#include "randomizer.h"

#define RZ_MAGIC        0x5A524545u     /* "EERZ" */
#define RZ_RECORD_VERSION 2
#define RZ_FLAG_BOOTS   1
#define RZ_FLAG_BONUS_SEEN 2            /* the bonus planet was reached: every vendor sells the Charge Boots */

struct RzRecord {
    u32 magic;
    u16 size;                           /* EE_RZ_RECORD_SIZE */
    u8 record_version;
    u8 flags;
    u32 seed;
    u8 logic_version;
    u8 weapon_mode;
    u8 bonus_planet;
    u8 bonus_price;
    u8 checks;
    u8 pad[3];
    u8 item[RZ_CHECKS_MAX + 1];
    u8 collected[(RZ_CHECKS_MAX + 7) / 8];  /* a bit per check */
    u8 gold;                            /* Gold Bolts received */
    u8 specials;                        /* bit n: the special item of flag byte n received */
    u8 pad2[2];
    u16 checksum;                       /* memcard_Checksum over everything before it */
    u8 tail[EE_RZ_RECORD_SIZE - (4 + 2 + 1 + 1 + 4 + 4 + 4 + RZ_CHECKS_MAX + 1 + 12 + 4 + 2)];
};

#define RECORD          (*(volatile struct RzRecord *)EE_RZ_PAGE)
#define CHECKSUM_BYTES  ((s32)((u8 *)&((struct RzRecord *)0)->checksum - (u8 *)0))

/* The game and level save tables, at the executable's addresses (the title
 * screen's programs keep their data there; a level relocates it). */
DECLARE_LVL_DATA(save_game_table);      /* the game's save records {address, size, id, 0}, to a zero address */
DECLARE_LVL_DATA(save_level_table);     /* a level page's; both move with each program's data (tools/modseg.py) */
DECLARE_LVL(func_0020BBC8);             /* memcard_PrepData(buffer, page, table): the table's records for the page */
typedef s32 (*PrepFn)(void *, s32, const void *);

/* The card machine (D_0013D390) as memcard_Save (func_0020BFC8) drives it
 * for an autosave: the game's records and one level page, packed into two
 * buffers, go to the card when the request is set; the page's index is the
 * machine's, and the write sequence seeks it. The machine's own save
 * writes the current level's page; the record's page (19) gets the same
 * sequence, started here, once the record changed and the machine is idle. */
#define MC                  ((volatile u8 *)0x0013D390)
#define MC_CARD             (*(volatile s32 *)(MC + 0xC8))          /* the card in use, -1 none */
#define MC_SLOT_FILE(card)  (*(volatile s32 *)(MC + 0xC0 * (card) + 0x14))  /* its save's file, < 0 none */
#define MC_PAGE             (*(volatile s32 *)(MC + 0xD0))          /* the level page the save writes */
#define MC_STATE            (*(volatile s32 *)(MC + 0xDC))          /* >= 3: a sequence is running */
#define MC_REQUEST          (*(volatile s32 *)(MC + 0xE4))          /* >= 0: a request waits (0xF: save) */
#define MC_REQUEST_CARD     (*(volatile s32 *)(MC + 0xE8))
#define MC_PENDING          (*(volatile s32 *)(MC + 0xFC))          /* the save mask: 0, no autosaves yet */
#define MC_SAVE_GAME_BUF    ((void *)0x0014EFD0)
#define MC_SAVE_LEVEL_BUF   ((void *)0x001507D0)
#define MC_REQUEST_SAVE     0xF
#define GAME_FLAGS          (*(volatile u32 *)0x0015EFB4)
#define GAME_FLAG_SAVING    0x200                                   /* memcard_Save sets it for slot 0 */
#define HIDDEN_PAGE_INDEX 19            /* the level page no level owns: EE_RZ_PAGE */
#define HIDDEN_PAGE_BLOCK_ID 0xBBA      /* the level save table's record of a level's moby state */

static s32 record_dirty;                /* the page changed since the card last had it */

/* For the PINE test scripts (docs/DEBUGGING.md): record autosaves started; frames one waited for the machine. */
volatile u32 ee_debug_rz_card[2];

extern void func_00121A80(void *clock);              /* sceCdReadClock into 8 bytes (core) */
#define MENU_TICKS      (*(volatile s32 *)0x001D60C4)   /* D_001D5F70 + 0x154: the menus' ticks since the card idled */

static u8 armed;                        /* a Randomizer new game is being created */
static u8 seed_given;                   /* with this seed (Reuse Seed) rather than a fresh one */
static u32 given_seed;

/* memcard_Checksum (func_0020BB10): a 16-bit shift register, seeded with
 * the CRC-32 polynomial, over the bytes. */
u32 ee_save_checksum(const u8 *data, s32 length)
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

/* memcard_GetDataSize (func_0020BAD8) over a save table: {address, size,
 * id, 0} entries to a zero address; a blob is 8 bytes, then 8 + size
 * rounded up to 4 per record, then 8. */
static s32 blob_size(const s32 *table)
{
    s32 size = 8;
    while (table[0] != 0) {
        size = (size + 8 + table[1] + 3) & ~3;
        table += 4;
    }
    return size + 8;
}

s32 ee_rz_record_offset(s32 program)
{
    const s32 *level_table = LVL_DATA(program, save_level_table, const s32 *);
    const s32 *entry = level_table;
    s32 offset = 8 + blob_size(LVL_DATA(program, save_game_table, const s32 *))
                 + HIDDEN_PAGE_INDEX * blob_size(level_table) + 8;
    while (entry[0] != 0 && entry[2] != HIDDEN_PAGE_BLOCK_ID) {
        offset = (offset + 8 + entry[1] + 3) & ~3;
        entry += 4;
    }
    if (entry[0] == 0) {
        return -1;
    }
    return offset + 8;
}

static s32 record_valid(const struct RzRecord *record)
{
    return record->magic == RZ_MAGIC && record->size == EE_RZ_RECORD_SIZE
           && record->record_version == RZ_RECORD_VERSION
           && record->checksum == ee_save_checksum((const u8 *)record, CHECKSUM_BYTES);
}

s32 ee_rz_record_parse(const u8 *bytes, struct RzSlot *out)
{
    const struct RzRecord *record = (const struct RzRecord *)bytes;
    out->present = 0;
    out->version = 0;
    out->weapon_mode = 0;
    out->with_boots = 0;
    out->seed = 0;
    if (!record_valid(record)) {
        return 0;
    }
    out->present = 1;
    out->version = record->logic_version;
    out->weapon_mode = record->weapon_mode;
    out->with_boots = (record->flags & RZ_FLAG_BOOTS) != 0;
    out->seed = record->seed;
    return 1;
}

s32 ee_rz_active(void)
{
    struct RzRecord copy;
    s32 i;
    for (i = 0; i < EE_RZ_RECORD_SIZE; i++) {
        ((u8 *)&copy)[i] = ((volatile u8 *)EE_RZ_PAGE)[i];
    }
    return record_valid(&copy);
}

/* The record in the page, read and changed in place (the checksum follows). */
static void rechecksum(void)
{
    u8 bytes[EE_RZ_RECORD_SIZE];
    s32 i;
    for (i = 0; i < CHECKSUM_BYTES; i++) {
        bytes[i] = ((volatile u8 *)EE_RZ_PAGE)[i];
    }
    RECORD.checksum = (u16)ee_save_checksum(bytes, CHECKSUM_BYTES);
    record_dirty = 1;
}

/* The record's page to the card, the way memcard_Save autosaves the
 * current level's page: the game's autosaves (the givers', the ship's)
 * never write page 19, so without this a save that only ever autosaved
 * came back from the card with the record as it was at the last full save,
 * its checks uncollected. Under memcard_Save's own conditions: a card with
 * the save's file, an autosave done before (the mask), the machine idle. */
void ee_rz_record_autosave(s32 L)
{
    s32 card;
    if (!record_dirty) {
        return;
    }
    card = MC_CARD;
    if (card == -1 || MC_SLOT_FILE(card) < 0 || MC_PENDING == 0) {
        return;
    }
    if (MC_STATE >= 3 || MC_REQUEST >= 0) {
        ee_debug_rz_card[1]++;
        return;
    }
    MC_PAGE = HIDDEN_PAGE_INDEX;
    LVL(L, func_0020BBC8, PrepFn)(MC_SAVE_GAME_BUF, 0, LVL_DATA(L, save_game_table, const void *));
    LVL(L, func_0020BBC8, PrepFn)(MC_SAVE_LEVEL_BUF, HIDDEN_PAGE_INDEX, LVL_DATA(L, save_level_table, const void *));
    GAME_FLAGS |= GAME_FLAG_SAVING;
    MC_REQUEST = MC_REQUEST_SAVE;
    MC_REQUEST_CARD = card;
    record_dirty = 0;
    ee_debug_rz_card[0]++;
}

s32 ee_rz_layout_item(s32 check)
{
    return check >= 0 && check < RZ_CHECKS_MAX ? RECORD.item[check] : RZ_ITEM_NONE;
}

s32 ee_rz_check_collected(s32 check)
{
    return check >= 0 && check < RZ_CHECKS_MAX && (RECORD.collected[check / 8] >> (check % 8)) & 1;
}

void ee_rz_set_collected(s32 check)
{
    if (check >= 0 && check < RZ_CHECKS_MAX) {
        RECORD.collected[check / 8] |= (u8)(1 << (check % 8));
        rechecksum();
    }
}

s32 ee_rz_gold_received(void)
{
    return RECORD.gold;
}

/* The bonus vendor check's planet (1..18), 0 for a layout without the Charge Boots. */
s32 ee_rz_bonus_planet(void)
{
    return (RECORD.flags & RZ_FLAG_BOOTS) ? RECORD.bonus_planet : 0;
}

/* The bonus entry has been listed once (on its planet): as a weapon in the
 * vendor's stock, it is on sale everywhere from then on. */
s32 ee_rz_bonus_seen(void)
{
    return (RECORD.flags & RZ_FLAG_BONUS_SEEN) != 0;
}

void ee_rz_set_bonus_seen(void)
{
    if (!(RECORD.flags & RZ_FLAG_BONUS_SEEN)) {
        RECORD.flags |= RZ_FLAG_BONUS_SEEN;
        rechecksum();
    }
}

void ee_rz_add_gold(void)
{
    if (RECORD.gold < RZ_GOLD_BOLTS) {
        RECORD.gold++;
        rechecksum();
    }
}

s32 ee_rz_received_special(s32 flag)
{
    return flag >= 0 && flag < 8 && (RECORD.specials >> flag) & 1;
}

void ee_rz_set_received_special(s32 flag)
{
    if (flag >= 0 && flag < 8) {
        RECORD.specials |= (u8)(1 << flag);
        rechecksum();
    }
}

void ee_rz_arm(s32 on)
{
    armed = on != 0;
}

void ee_rz_set_seed(u32 seed, s32 given)
{
    seed_given = given != 0;
    given_seed = seed;
}

/* A seed from the clock (sceCdReadClock: {status, second, minute, hour, pad,
 * day, month, year}, BCD) and the menu's tick, within the codes' range. */
static u32 fresh_seed(void)
{
    u8 clock[8];
    u32 hash = (u32)MENU_TICKS * 0x9E3779B9u;   /* the golden-ratio multiplier spreads the tick */
    s32 i;
    for (i = 0; i < 8; i++) {
        clock[i] = 0;
    }
    func_00121A80(clock);
    for (i = 1; i < 8; i++) {
        hash = (hash ^ clock[i]) * 0x01000193u;     /* FNV-1a's prime */
    }
    hash ^= hash >> 15;
    return hash % RZ_SEED_LIMIT;
}

static void write_record(const struct RzLayout *layout)
{
    struct RzRecord record;
    s32 i;
    for (i = 0; i < EE_RZ_RECORD_SIZE; i++) {
        ((u8 *)&record)[i] = 0;
    }
    record.magic = RZ_MAGIC;
    record.size = EE_RZ_RECORD_SIZE;
    record.record_version = RZ_RECORD_VERSION;
    record.flags = layout->with_boots ? RZ_FLAG_BOOTS : 0;
    record.seed = layout->seed;
    record.logic_version = layout->version;
    record.weapon_mode = layout->weapon_mode;
    record.bonus_planet = layout->bonus_planet;
    record.bonus_price = layout->bonus_price;
    record.checks = layout->checks;
    for (i = 0; i < RZ_CHECKS_MAX; i++) {
        record.item[i] = layout->item[i];
    }
    record.checksum = (u16)ee_save_checksum((const u8 *)&record, CHECKSUM_BYTES);
    for (i = 0; i < EE_RZ_RECORD_SIZE; i++) {
        ((volatile u8 *)EE_RZ_PAGE)[i] = ((u8 *)&record)[i];
    }
    record_dirty = 1;
}

void ee_rz_after_reset(s32 L)
{
    static struct RzLayout layout;      /* 100-odd bytes; off the stack */
    u32 seed;
    (void)L;
    if (!armed) {
        return;
    }
    armed = 0;
    seed = seed_given ? given_seed : fresh_seed();
    seed_given = 0;
    if (rz_generate(seed, EE_RANDOMIZER_CHARGE_BOOTS, &layout)) {
        write_record(&layout);
    }
}
