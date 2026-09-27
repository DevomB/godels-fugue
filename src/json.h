#ifndef JSON_H
#define JSON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue {
    JsonType type;
    bool boolean;
    double number;
    char *string;
    int count; /* array items or object members */
    struct JsonValue *items;
    char **keys; /* object member names, parallel to items */
} JsonValue;

/* Parses one JSON document. On failure writes "line L col C: reason". */
bool json_parse(const char *text, JsonValue *out, char *err, size_t cap);
void json_free(JsonValue *value);
const JsonValue *json_get(const JsonValue *object, const char *key);

#endif
