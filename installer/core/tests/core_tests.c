/*
 * The core's own tests, for what can be checked without a disc: SHA-256 against FIPS 180-4's examples, the JSON
 * parser and writer, the byte-order helpers and the job pool. What needs a disc is checked
 * against tools/install.py's, byte for byte, with probe.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytes.h"
#include "json.h"
#include "pool.h"
#include "sha256.h"

static int failures = 0;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static void check_sha256(const char *input, size_t repeat, const char *expected)
{
    Sha256 hash;
    sha256_start(&hash);
    for (size_t i = 0; i < repeat; i++) {
        sha256_add(&hash, input, strlen(input));
    }
    uint8_t digest[32];
    char hex[65];
    sha256_finish(&hash, digest);
    sha256_hex(digest, hex);
    if (strcmp(hex, expected) != 0) {
        printf("FAIL sha256 of %zu x \"%.20s\": %s\n", repeat, input, hex);
        failures++;
    }
}

static void test_sha256(void)
{
    check_sha256("", 1, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check_sha256("abc", 1, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1,
                 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    /* one million a's, fed 1 byte at a time: every partial-block path */
    check_sha256("a", 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void test_json(void)
{
    const char *text = "{\"edition\": \"rac1-executive\", \"modseg_base\": 647168, \"discs\": {\"rac1\": \"0f18\"},"
                       " \"list\": [1, -2.5, true, false, null], \"escaped\": \"a\\\"b\\\\c\\n\\u00e9\\ud83d\\ude00\"}";
    char error[128];
    Json *value = json_parse(text, strlen(text), error, sizeof error);
    check(value != NULL, "json parses an object");
    if (value == NULL) {
        printf("  %s\n", error);
        return;
    }
    check(strcmp(json_get_string(value, "edition"), "rac1-executive") == 0, "json string value");
    check(json_get_integer(value, "modseg_base", -1) == 647168, "json integer value");
    check(strcmp(json_get_string(json_get(value, "discs"), "rac1"), "0f18") == 0, "json nested object");
    const Json *list = json_get(value, "list");
    check(list != NULL && list->count == 5 && list->items[1].number == -2.5 && !list->items[1].is_integer,
          "json array with a fraction");
    check(strcmp(json_get_string(value, "escaped"), "a\"b\\c\n\xc3\xa9\xf0\x9f\x98\x80") == 0,
          "json escapes and a surrogate pair to UTF-8");

    Text written = {0};
    text_add_json(&written, value);
    check(strcmp(written.text, "{\"edition\": \"rac1-executive\", \"modseg_base\": 647168, \"discs\": {\"rac1\": \"0f18\"}, "
                               "\"list\": [1, -2.5, true, false, null], "
                               "\"escaped\": \"a\\\"b\\\\c\\n\\u00e9\\ud83d\\ude00\"}") == 0,
          "json written back as Python's json.dumps writes it");
    text_free(&written);
    json_free(value);

    const char *bad[] = {"", "{", "[1,]", "{\"a\" 1}", "\"\\u0000\"", "\"\\ud800\"", "01x", "[1] 2", "\"\x01\""};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        Json *refused = json_parse(bad[i], strlen(bad[i]), NULL, 0);
        if (refused != NULL) {
            printf("FAIL json accepted %s\n", bad[i]);
            failures++;
            json_free(refused);
        }
    }

    char deep[200];
    memset(deep, '[', sizeof deep);
    check(json_parse(deep, sizeof deep, NULL, 0) == NULL, "json refuses deep nesting");
}

static void test_bytes(void)
{
    uint8_t buffer[8];
    write_u32_both(buffer, 0x12345678);
    static const uint8_t expected[8] = {0x78, 0x56, 0x34, 0x12, 0x12, 0x34, 0x56, 0x78};
    check(memcmp(buffer, expected, 8) == 0, "ISO 9660 both-endian field");
    check(read_u32_le(buffer) == 0x12345678, "little-endian read");
    write_u16_le(buffer, 0xBEEF);
    check(read_u16_le(buffer) == 0xBEEF, "16-bit round trip");
}

static void count_job(void *arg, size_t k)
{
    unsigned char *seen = arg;
    seen[k]++;
}

static void test_pool(void)
{
    static unsigned char seen[10000];
    unsigned thread_counts[] = {1, 3, pool_cpus(), 200};
    for (size_t i = 0; i < sizeof thread_counts / sizeof thread_counts[0]; i++) {
        memset(seen, 0, sizeof seen);
        pool_run(&thread_counts[i], count_job, seen, sizeof seen);
        size_t once = 0;
        for (size_t k = 0; k < sizeof seen; k++) {
            once += seen[k] == 1;
        }
        if (once != sizeof seen) {
            printf("FAIL pool on %u threads ran %zu of %zu jobs once\n", thread_counts[i], once, sizeof seen);
            failures++;
        }
    }
    pool_run(&thread_counts[1], count_job, seen, 0);
}

int main(void)
{
    test_sha256();
    test_json();
    test_bytes();
    test_pool();
    if (failures == 0) {
        printf("core tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
