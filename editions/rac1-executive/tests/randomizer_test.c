/* Host tests of the randomizer's logic (src/randomizer.c): the tables as
 * transcribed, every seed's layout complete and deterministic in both
 * pools, and the seed code's error detection.
 *
 *   bash tools/test_randomizer.sh [SEEDS]      (gcc; WSL's does)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "randomizer.h"

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_tables(void)
{
    int seen[RZ_ITEM_KINDS] = { 0 };
    int bolts = 0, i, item, vendors = 0;
    for (i = 0; i < RZ_CHECKS_BASE; i++) {
        const struct RzCheck *c = rz_check(i);
        CHECK(c != 0, "check %d missing", i);
        CHECK(c->planet >= 1 && c->planet <= 18, "check %d planet %d", i, c->planet);
        CHECK((c->all & rz_item_bit(RZ_INFOBOT_NOVALIS + c->planet - 1)) != 0, "check %d lacks its planet's infobot", i);
        if (c->kind == RZ_KIND_GOLD_BOLT) {
            bolts++;
            CHECK(c->vanilla == RZ_GOLD_BOLT, "check %d: a bolt spot with item %d", i, c->vanilla);
        } else {
            CHECK(c->vanilla != RZ_GOLD_BOLT && c->vanilla != RZ_ITEM_NONE, "check %d: vanilla %d", i, c->vanilla);
            seen[c->vanilla]++;
        }
        if (c->kind == RZ_KIND_VENDOR) {
            vendors++;
        }
    }
    CHECK(bolts == RZ_GOLD_BOLTS, "%d Gold Bolt spots", bolts);
    CHECK(RZ_CHECKS_BASE - bolts == 54, "%d non-bolt checks", RZ_CHECKS_BASE - bolts);
    CHECK(vendors == 12, "%d vendor checks", vendors);
    /* every item retail gives is given once, the start's two excepted */
    for (item = RZ_INFOBOT_NOVALIS; item < RZ_LOGICAL_ITEMS; item++) {
        int expected = (item == RZ_INFOBOT_NOVALIS || item == RZ_BOMB_GLOVE) ? 0 : 1;
        CHECK(seen[item] == expected, "%s is the vanilla reward of %d checks", rz_item_name(item), seen[item]);
    }
    CHECK(rz_check(RZ_CHECKS_BASE) == 0, "a 95th table check");
    CHECK(rz_item_is_progression(RZ_SWINGSHOT) && !rz_item_is_progression(RZ_PYROCITOR), "classification");
    CHECK(rz_item_is_progression(RZ_INFOBOT_VELDIN) && !rz_item_is_progression(RZ_GOLD_BOLT), "classification");
}

static void test_layouts(int seeds)
{
    int with_boots, seed, i, max_attempts = 0, differ = 0;
    struct RzLayout a, b, prev;
    memset(&prev, 0, sizeof(prev));
    for (with_boots = 0; with_boots <= 1; with_boots++) {
        long progression_index = 0, progression_count = 0;
        for (seed = 0; seed < seeds; seed++) {
            int count[RZ_ITEM_KINDS] = { 0 };
            CHECK(rz_generate((u32)seed, with_boots, &a), "seed %d (boots %d) failed", seed, with_boots);
            CHECK(rz_generate((u32)seed, with_boots, &b), "seed %d again", seed);
            CHECK(memcmp(&a, &b, sizeof(a)) == 0, "seed %d is not deterministic", seed);
            CHECK(a.checks == (with_boots ? RZ_CHECKS_MAX : RZ_CHECKS_BASE), "seed %d: %d checks", seed, a.checks);
            CHECK(rz_layout_complete(&a), "seed %d: not every check is reachable", seed);
            for (i = 0; i < a.checks; i++) {
                count[a.item[i]]++;
                if (rz_item_is_progression(a.item[i])) {
                    progression_index += i;
                    progression_count++;
                }
            }
            CHECK(count[RZ_ITEM_NONE] == 0, "seed %d: an empty check", seed);
            CHECK(count[RZ_GOLD_BOLT] == RZ_GOLD_BOLTS, "seed %d: %d Gold Bolts", seed, count[RZ_GOLD_BOLT]);
            CHECK(count[RZ_CHARGE_BOOTS] == with_boots, "seed %d: %d Charge Boots", seed, count[RZ_CHARGE_BOOTS]);
            CHECK(count[RZ_INFOBOT_NOVALIS] == 0 && count[RZ_BOMB_GLOVE] == 0, "seed %d: a start item placed", seed);
            for (i = RZ_INFOBOT_NOVALIS + 1; i < RZ_LOGICAL_ITEMS; i++) {
                if (i != RZ_BOMB_GLOVE) {
                    CHECK(count[i] == 1, "seed %d: %s placed %d times", seed, rz_item_name(i), count[i]);
                }
            }
            if (with_boots) {
                CHECK(a.bonus_planet >= 1 && a.bonus_planet <= 18 && a.bonus_price <= 1, "seed %d: bonus %d/%d",
                      seed, a.bonus_planet, a.bonus_price);
            }
            if (a.attempts > max_attempts) {
                max_attempts = a.attempts;
            }
            if (seed > 0 && memcmp(a.item, prev.item, RZ_CHECKS_BASE) != 0) {
                differ++;
            }
            prev = a;
        }
        printf("  boots %d: %d seeds, every layout complete; progression items sit at check %.1f on average\n",
               with_boots, seeds, (double)progression_index / (double)progression_count);
    }
    CHECK(differ >= 2 * (seeds - 1) - 1, "layouts of consecutive seeds are the same (%d differ)", differ);
    printf("  most fills a seed needed: %d\n", max_attempts);
}

static void test_codes(int seeds)
{
    u8 d[RZ_CODE_LENGTH], e[RZ_CODE_LENGTH];
    u32 seed, got;
    int version, mode, i, j, step;
    for (step = 0, seed = 0; seed < RZ_SEED_LIMIT; seed += 1 + (u32)step, step = (step * 7 + 3) % 1000) {
        rz_code_encode(seed, RZ_LOGIC_VERSION, RZ_WEAPON_VANILLA, d);
        for (i = 0; i < RZ_CODE_LENGTH; i++) {
            CHECK(d[i] < RZ_CODE_BUTTONS, "seed %u digit %d is %d", seed, i, d[i]);
        }
        CHECK(rz_code_decode(d, &got, &version, &mode) && got == seed && version == RZ_LOGIC_VERSION
              && mode == RZ_WEAPON_VANILLA, "seed %u does not round-trip", seed);
        if ((int)(seed % 2311) < seeds / 100 + 1) {
            for (i = 0; i < RZ_CODE_LENGTH; i++) {               /* every single wrong press */
                for (j = 0; j < RZ_CODE_BUTTONS; j++) {
                    if (j == d[i]) {
                        continue;
                    }
                    memcpy(e, d, sizeof(e));
                    e[i] = (u8)j;
                    CHECK(!rz_code_decode(e, &got, &version, &mode) || got != seed,
                          "seed %u: digit %d changed to %d passes as the same seed", seed, i, j);
                    CHECK(!rz_code_decode(e, &got, &version, &mode),
                          "seed %u: digit %d changed to %d is accepted (as seed %u)", seed, i, j, got);
                }
            }
            for (i = 0; i + 1 < RZ_CODE_LENGTH; i++) {           /* every swap of neighbours */
                if (d[i] == d[i + 1]) {
                    continue;
                }
                memcpy(e, d, sizeof(e));
                e[i] = d[i + 1];
                e[i + 1] = d[i];
                CHECK(!rz_code_decode(e, &got, &version, &mode), "seed %u: swapping presses %d and %d is accepted", seed, i, i + 1);
            }
        }
    }
    rz_code_encode(5, RZ_LOGIC_VERSION + 1, RZ_WEAPON_VANILLA, d);
    CHECK(!rz_code_decode(d, &got, &version, &mode) && version == RZ_LOGIC_VERSION + 1, "another version is accepted");
    rz_code_encode(5, RZ_LOGIC_VERSION, 1, d);
    CHECK(!rz_code_decode(d, &got, &version, &mode) && mode == 1, "another weapon mode is accepted");
    rz_code_encode(RZ_SEED_LIMIT - 1, RZ_LOGIC_VERSION, RZ_WEAPON_VANILLA, d);
    CHECK(rz_code_decode(d, &got, &version, &mode) && got == RZ_SEED_LIMIT - 1, "the last seed does not round-trip");
    memset(d, 10, sizeof(d));
    rz_code_decode(d, &got, &version, &mode);
    CHECK(got < 11u * 11 * 11 * 11 * 11 * 11 * 11 * 11 / 64 + 1, "all R2 overflows");
}

static void print_layout(u32 seed, int with_boots)
{
    struct RzLayout l;
    int i;
    if (!rz_generate(seed, with_boots, &l)) {
        printf("seed %u: no layout\n", seed);
        return;
    }
    printf("seed %u (boots %d, %d fills):\n", seed, with_boots, l.attempts);
    for (i = 0; i < l.checks; i++) {
        const struct RzCheck *c = rz_check(i);
        printf("  %2d planet %2d %-22s -> %s\n", i + 1, c ? c->planet : l.bonus_planet,
               c ? rz_item_name(c->vanilla) : "bonus vendor entry", rz_item_name(l.item[i]));
    }
}

int main(int argc, char **argv)
{
    int seeds = argc > 1 ? atoi(argv[1]) : 2000;
    if (argc > 3) {                     /* items SEED BOOTS: the layout's items, comma separated (for a record) */
        struct RzLayout l;
        int i;
        if (!rz_generate((u32)atoi(argv[1]), atoi(argv[2]), &l)) {
            return 1;
        }
        printf("%u %d %d %d %d\n", l.seed, l.checks, l.with_boots, l.bonus_planet, l.bonus_price);
        for (i = 0; i < RZ_CHECKS_MAX; i++) {
            printf("%s%d", i ? "," : "", l.item[i]);
        }
        printf("\n");
        return 0;
    }
    if (argc > 2) {
        print_layout((u32)atoi(argv[1]), atoi(argv[2]));
        return 0;
    }
    test_tables();
    test_layouts(seeds);
    test_codes(seeds);
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
