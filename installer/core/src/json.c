#include "json.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Deeper nesting than any of the installer's files or commands use: refused, so no input can exhaust the stack. */
#define MAX_DEPTH 64

typedef struct {
    const char *text;
    size_t size;
    size_t position;
    const char *error;
} Parser;

static void skip_space(Parser *parser)
{
    while (parser->position < parser->size) {
        char c = parser->text[parser->position];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return;
        }
        parser->position++;
    }
}

static bool fail(Parser *parser, const char *error)
{
    if (parser->error == NULL) {
        parser->error = error;
    }
    return false;
}

static bool take_literal(Parser *parser, const char *literal)
{
    size_t length = strlen(literal);
    if (parser->size - parser->position < length || memcmp(parser->text + parser->position, literal, length) != 0) {
        return fail(parser, "unknown word");
    }
    parser->position += length;
    return true;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool take_hex4(Parser *parser, unsigned *code)
{
    if (parser->size - parser->position < 4) {
        return fail(parser, "short \\u escape");
    }
    *code = 0;
    for (int i = 0; i < 4; i++) {
        int digit = hex_digit(parser->text[parser->position + (size_t)i]);
        if (digit < 0) {
            return fail(parser, "bad \\u escape");
        }
        *code = *code << 4 | (unsigned)digit;
    }
    parser->position += 4;
    return true;
}

static void add_utf8(Text *out, unsigned code)
{
    char bytes[4];
    size_t length;
    if (code < 0x80) {
        bytes[0] = (char)code;
        length = 1;
    } else if (code < 0x800) {
        bytes[0] = (char)(0xC0 | code >> 6);
        bytes[1] = (char)(0x80 | (code & 0x3F));
        length = 2;
    } else if (code < 0x10000) {
        bytes[0] = (char)(0xE0 | code >> 12);
        bytes[1] = (char)(0x80 | (code >> 6 & 0x3F));
        bytes[2] = (char)(0x80 | (code & 0x3F));
        length = 3;
    } else {
        bytes[0] = (char)(0xF0 | code >> 18);
        bytes[1] = (char)(0x80 | (code >> 12 & 0x3F));
        bytes[2] = (char)(0x80 | (code >> 6 & 0x3F));
        bytes[3] = (char)(0x80 | (code & 0x3F));
        length = 4;
    }
    text_append(out, bytes, length);
}

/* A string's contents after its opening quote, unescaped; NULL on an error. */
static char *take_string(Parser *parser)
{
    Text out = {0};
    while (parser->position < parser->size) {
        char c = parser->text[parser->position++];
        if (c == '"') {
            if (out.text == NULL) {
                text_append(&out, "", 0);
            }
            if (out.failed) {
                text_free(&out);
                fail(parser, "out of memory");
                return NULL;
            }
            return out.text;
        }
        if ((unsigned char)c < 0x20) {
            break;
        }
        if (c != '\\') {
            text_append(&out, &c, 1);
            continue;
        }
        if (parser->position >= parser->size) {
            break;
        }
        char escape = parser->text[parser->position++];
        static const char ESCAPES[] = "\"\\/bfnrt";
        static const char REPLACEMENTS[] = "\"\\/\b\f\n\r\t";
        const char *plain = escape != 0 ? strchr(ESCAPES, escape) : NULL;
        if (plain != NULL) {
            text_append(&out, &REPLACEMENTS[plain - ESCAPES], 1);
            continue;
        }
        if (escape != 'u') {
            break;
        }
        unsigned code;
        if (!take_hex4(parser, &code)) {
            text_free(&out);
            return NULL;
        }
        if (code >= 0xD800 && code < 0xDC00) {
            /* a high surrogate: its low surrogate must follow as another \u escape */
            unsigned low;
            if (parser->size - parser->position < 2 || parser->text[parser->position] != '\\' ||
                parser->text[parser->position + 1] != 'u') {
                break;
            }
            parser->position += 2;
            if (!take_hex4(parser, &low) || low < 0xDC00 || low >= 0xE000) {
                break;
            }
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
        } else if (code >= 0xDC00 && code < 0xE000) {
            break;
        }
        if (code == 0) {
            break;
        }
        add_utf8(&out, code);
    }
    text_free(&out);
    fail(parser, "bad string");
    return NULL;
}

static bool parse_value(Parser *parser, Json *value, int depth);

static bool parse_number(Parser *parser, Json *value)
{
    size_t start = parser->position;
    if (parser->position < parser->size && parser->text[parser->position] == '-') {
        parser->position++;
    }
    size_t digits_start = parser->position;
    while (parser->position < parser->size && parser->text[parser->position] >= '0' &&
           parser->text[parser->position] <= '9') {
        parser->position++;
    }
    if (parser->position == digits_start) {
        return fail(parser, "bad number");
    }
    bool whole = true;
    if (parser->position < parser->size && parser->text[parser->position] == '.') {
        whole = false;
        parser->position++;
        while (parser->position < parser->size && parser->text[parser->position] >= '0' &&
               parser->text[parser->position] <= '9') {
            parser->position++;
        }
    }
    if (parser->position < parser->size && (parser->text[parser->position] == 'e' || parser->text[parser->position] == 'E')) {
        whole = false;
        parser->position++;
        if (parser->position < parser->size && (parser->text[parser->position] == '+' || parser->text[parser->position] == '-')) {
            parser->position++;
        }
        while (parser->position < parser->size && parser->text[parser->position] >= '0' &&
               parser->text[parser->position] <= '9') {
            parser->position++;
        }
    }
    char buffer[64];
    size_t length = parser->position - start;
    if (length >= sizeof buffer) {
        return fail(parser, "number too long");
    }
    memcpy(buffer, parser->text + start, length);
    buffer[length] = 0;
    value->type = JSON_NUMBER;
    value->number = strtod(buffer, NULL);
    if (whole && length <= 18) {
        value->integer = strtoll(buffer, NULL, 10);
        value->is_integer = true;
    } else if (value->number == floor(value->number) && fabs(value->number) < 9007199254740992.0) {
        value->integer = (int64_t)value->number;
        value->is_integer = true;
    }
    return true;
}

static bool grow_items(Json *value, size_t *capacity, bool with_keys)
{
    if (value->count < *capacity) {
        return true;
    }
    size_t grown = *capacity ? *capacity * 2 : 8;
    Json *items = realloc(value->items, grown * sizeof *items);
    if (items == NULL) {
        return false;
    }
    value->items = items;
    if (with_keys) {
        char **keys = realloc(value->keys, grown * sizeof *keys);
        if (keys == NULL) {
            return false;
        }
        value->keys = keys;
    }
    *capacity = grown;
    return true;
}

static bool parse_array(Parser *parser, Json *value, int depth)
{
    value->type = JSON_ARRAY;
    size_t capacity = 0;
    skip_space(parser);
    if (parser->position < parser->size && parser->text[parser->position] == ']') {
        parser->position++;
        return true;
    }
    for (;;) {
        if (!grow_items(value, &capacity, false)) {
            return fail(parser, "out of memory");
        }
        Json *item = &value->items[value->count];
        memset(item, 0, sizeof *item);
        value->count++;
        if (!parse_value(parser, item, depth + 1)) {
            return false;
        }
        skip_space(parser);
        if (parser->position >= parser->size) {
            return fail(parser, "unfinished array");
        }
        char c = parser->text[parser->position++];
        if (c == ']') {
            return true;
        }
        if (c != ',') {
            return fail(parser, "expected , or ] in an array");
        }
    }
}

static bool parse_object(Parser *parser, Json *value, int depth)
{
    value->type = JSON_OBJECT;
    size_t capacity = 0;
    skip_space(parser);
    if (parser->position < parser->size && parser->text[parser->position] == '}') {
        parser->position++;
        return true;
    }
    for (;;) {
        skip_space(parser);
        if (parser->position >= parser->size || parser->text[parser->position] != '"') {
            return fail(parser, "expected a key");
        }
        parser->position++;
        if (!grow_items(value, &capacity, true)) {
            return fail(parser, "out of memory");
        }
        Json *item = &value->items[value->count];
        memset(item, 0, sizeof *item);
        value->keys[value->count] = take_string(parser);
        value->count++;
        if (value->keys[value->count - 1] == NULL) {
            return false;
        }
        skip_space(parser);
        if (parser->position >= parser->size || parser->text[parser->position] != ':') {
            return fail(parser, "expected :");
        }
        parser->position++;
        if (!parse_value(parser, item, depth + 1)) {
            return false;
        }
        skip_space(parser);
        if (parser->position >= parser->size) {
            return fail(parser, "unfinished object");
        }
        char c = parser->text[parser->position++];
        if (c == '}') {
            return true;
        }
        if (c != ',') {
            return fail(parser, "expected , or } in an object");
        }
    }
}

static bool parse_value(Parser *parser, Json *value, int depth)
{
    if (depth > MAX_DEPTH) {
        return fail(parser, "nested too deeply");
    }
    skip_space(parser);
    if (parser->position >= parser->size) {
        return fail(parser, "expected a value");
    }
    char c = parser->text[parser->position];
    switch (c) {
    case '{':
        parser->position++;
        return parse_object(parser, value, depth);
    case '[':
        parser->position++;
        return parse_array(parser, value, depth);
    case '"':
        parser->position++;
        value->type = JSON_STRING;
        value->string = take_string(parser);
        return value->string != NULL;
    case 't':
        value->type = JSON_TRUE;
        return take_literal(parser, "true");
    case 'f':
        value->type = JSON_FALSE;
        return take_literal(parser, "false");
    case 'n':
        value->type = JSON_NULL;
        return take_literal(parser, "null");
    default:
        return parse_number(parser, value);
    }
}

static void free_contents(Json *value)
{
    for (size_t i = 0; i < value->count; i++) {
        free_contents(&value->items[i]);
        if (value->keys != NULL) {
            free(value->keys[i]);
        }
    }
    free(value->items);
    free(value->keys);
    free(value->string);
}

Json *json_parse(const char *text, size_t size, char *error, size_t error_capacity)
{
    Parser parser = {.text = text, .size = size};
    Json *value = calloc(1, sizeof *value);
    if (value == NULL) {
        return NULL;
    }
    bool parsed = parse_value(&parser, value, 0);
    if (parsed) {
        skip_space(&parser);
        if (parser.position != parser.size) {
            parsed = fail(&parser, "text after the value");
        }
    }
    if (!parsed) {
        if (error != NULL && error_capacity > 0) {
            snprintf(error, error_capacity, "%s at byte %zu", parser.error ? parser.error : "invalid JSON", parser.position);
        }
        json_free(value);
        return NULL;
    }
    return value;
}

void json_free(Json *value)
{
    if (value == NULL) {
        return;
    }
    free_contents(value);
    free(value);
}

const Json *json_get(const Json *value, const char *key)
{
    if (value == NULL || value->type != JSON_OBJECT) {
        return NULL;
    }
    for (size_t i = 0; i < value->count; i++) {
        if (strcmp(value->keys[i], key) == 0) {
            return &value->items[i];
        }
    }
    return NULL;
}

const char *json_get_string(const Json *value, const char *key)
{
    const Json *item = json_get(value, key);
    return item != NULL && item->type == JSON_STRING ? item->string : NULL;
}

int64_t json_get_integer(const Json *value, const char *key, int64_t fallback)
{
    const Json *item = json_get(value, key);
    return item != NULL && item->type == JSON_NUMBER && item->is_integer ? item->integer : fallback;
}

void text_append(Text *text, const char *bytes, size_t length)
{
    if (text->failed) {
        return;
    }
    if (text->length + length + 1 > text->capacity) {
        size_t grown = text->capacity ? text->capacity : 64;
        while (grown < text->length + length + 1) {
            grown *= 2;
        }
        char *bigger = realloc(text->text, grown);
        if (bigger == NULL) {
            text->failed = true;
            return;
        }
        text->text = bigger;
        text->capacity = grown;
    }
    memcpy(text->text + text->length, bytes, length);
    text->length += length;
    text->text[text->length] = 0;
}

void text_add(Text *text, const char *string)
{
    text_append(text, string, strlen(string));
}

void text_format(Text *text, const char *format, ...)
{
    char buffer[1024];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(buffer, sizeof buffer, format, arguments);
    va_end(arguments);
    if (length < 0) {
        return;
    }
    if ((size_t)length < sizeof buffer) {
        text_append(text, buffer, (size_t)length);
        return;
    }
    char *long_buffer = malloc((size_t)length + 1);
    if (long_buffer == NULL) {
        text->failed = true;
        return;
    }
    va_start(arguments, format);
    vsnprintf(long_buffer, (size_t)length + 1, format, arguments);
    va_end(arguments);
    text_append(text, long_buffer, (size_t)length);
    free(long_buffer);
}

/* The code point UTF-8 starts at BYTES, and its LENGTH in bytes; U+FFFD for a malformed sequence (one byte). */
static unsigned decode_utf8(const unsigned char *bytes, size_t *length)
{
    unsigned code;
    size_t count;
    if (bytes[0] >= 0xF0 && bytes[0] < 0xF5) {
        code = bytes[0] & 0x07u;
        count = 4;
    } else if (bytes[0] >= 0xE0) {
        code = bytes[0] & 0x0Fu;
        count = 3;
    } else if (bytes[0] >= 0xC2 && bytes[0] < 0xE0) {
        code = bytes[0] & 0x1Fu;
        count = 2;
    } else {
        *length = 1;
        return 0xFFFD;
    }
    for (size_t i = 1; i < count; i++) {
        if ((bytes[i] & 0xC0) != 0x80) {
            *length = 1;
            return 0xFFFD;
        }
        code = code << 6 | (bytes[i] & 0x3Fu);
    }
    *length = count;
    return code;
}

void text_add_json_string(Text *text, const char *string)
{
    if (string == NULL) {
        text_add(text, "null");
        return;
    }
    text_add(text, "\"");
    const unsigned char *c = (const unsigned char *)string;
    while (*c != 0) {
        switch (*c) {
        case '"':
            text_add(text, "\\\"");
            break;
        case '\\':
            text_add(text, "\\\\");
            break;
        case '\n':
            text_add(text, "\\n");
            break;
        case '\r':
            text_add(text, "\\r");
            break;
        case '\t':
            text_add(text, "\\t");
            break;
        case '\b':
            text_add(text, "\\b");
            break;
        case '\f':
            text_add(text, "\\f");
            break;
        default:
            if (*c < 0x20) {
                text_format(text, "\\u%04x", *c);
            } else if (*c < 0x80) {
                text_append(text, (const char *)c, 1);
            } else {
                /* beyond ASCII as \u escapes, a surrogate pair above U+FFFF, as Python's json.dumps writes them */
                size_t length;
                unsigned code = decode_utf8(c, &length);
                if (code >= 0x10000) {
                    code -= 0x10000;
                    text_format(text, "\\u%04x\\u%04x", 0xD800 + (code >> 10), 0xDC00 + (code & 0x3FF));
                } else {
                    text_format(text, "\\u%04x", code);
                }
                c += length;
                continue;
            }
        }
        c++;
    }
    text_add(text, "\"");
}

void text_add_json(Text *text, const Json *value)
{
    switch (value->type) {
    case JSON_NULL:
        text_add(text, "null");
        break;
    case JSON_FALSE:
        text_add(text, "false");
        break;
    case JSON_TRUE:
        text_add(text, "true");
        break;
    case JSON_NUMBER:
        if (value->is_integer) {
            text_format(text, "%lld", (long long)value->integer);
        } else {
            text_format(text, "%.17g", value->number);
        }
        break;
    case JSON_STRING:
        text_add_json_string(text, value->string);
        break;
    case JSON_ARRAY:
        text_add(text, "[");
        for (size_t i = 0; i < value->count; i++) {
            if (i > 0) {
                text_add(text, ", ");
            }
            text_add_json(text, &value->items[i]);
        }
        text_add(text, "]");
        break;
    case JSON_OBJECT:
        text_add(text, "{");
        for (size_t i = 0; i < value->count; i++) {
            if (i > 0) {
                text_add(text, ", ");
            }
            text_add_json_string(text, value->keys[i]);
            text_add(text, ": ");
            text_add_json(text, &value->items[i]);
        }
        text_add(text, "}");
        break;
    }
}

void text_free(Text *text)
{
    free(text->text);
    text->text = NULL;
    text->length = 0;
    text->capacity = 0;
    text->failed = false;
}
