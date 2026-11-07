/*
 * JSON (RFC 8259): a parser to a tree, for the payload's files, the hash cache and the builder's commands, and a
 * writer to a growing string, for the protocol's events. Strings are UTF-8; numbers keep their exact integer value
 * when they have one, since the payload's offsets and sizes are integers.
 */
#ifndef RAC_JSON_H
#define RAC_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    JSON_NULL,
    JSON_FALSE,
    JSON_TRUE,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonType;

typedef struct Json Json;

struct Json {
    JsonType type;
    double number;
    int64_t integer;            /* the number, when it is a whole number in range */
    bool is_integer;
    char *string;               /* a string's UTF-8 bytes, zero-terminated (strings with \u0000 are refused) */
    Json *items;                /* an array's items, or an object's values */
    char **keys;                /* an object's keys, beside its values */
    size_t count;
};

/* TEXT parsed, or NULL when it is not valid JSON; ERROR, when given, says where. Freed with json_free. */
Json *json_parse(const char *text, size_t size, char *error, size_t error_capacity);
void json_free(Json *value);

/* An object's value for KEY, or NULL (also when VALUE is no object). */
const Json *json_get(const Json *value, const char *key);
/* KEY's string, or NULL when it is missing or no string. */
const char *json_get_string(const Json *value, const char *key);
/* KEY's integer, or FALLBACK when it is missing or not an integer. */
int64_t json_get_integer(const Json *value, const char *key, int64_t fallback);

/* A growing UTF-8 string, for writing JSON (and anything else). */
typedef struct {
    char *text;
    size_t length;
    size_t capacity;
    bool failed;                /* an allocation failed: the text is incomplete */
} Text;

void text_append(Text *text, const char *bytes, size_t length);
void text_add(Text *text, const char *string);
void text_format(Text *text, const char *format, ...);
/* STRING as a JSON string, quoted and escaped; NULL as null. */
void text_add_json_string(Text *text, const char *string);
/* VALUE written back as JSON, spaced as Python's json.dumps spaces it (", " and ": "). */
void text_add_json(Text *text, const Json *value);
void text_free(Text *text);

#endif
