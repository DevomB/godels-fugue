#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { JSON_DEPTH_MAX = 32 };

typedef struct Parser {
    const char *text;
    size_t pos;
    char *err;
    size_t cap;
    bool failed;
} Parser;

static void fail(Parser *p, const char *reason) {
    if (p->failed) return;
    p->failed = true;
    int line = 1;
    int col = 1;
    for (size_t i = 0; i < p->pos && p->text[i] != '\0'; i++) {
        if (p->text[i] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    if (p->err != NULL && p->cap > 0)
        snprintf(p->err, p->cap, "line %d col %d: %s", line, col, reason);
}

static void skip_space(Parser *p) {
    for (;;) {
        char c = p->text[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            return;
        }
    }
}

static bool literal(Parser *p, const char *word) {
    size_t n = strlen(word);
    if (strncmp(p->text + p->pos, word, n) != 0) return false;
    p->pos += n;
    return true;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_hex4(Parser *p, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        int d = hex_digit(p->text[p->pos + (size_t)i]);
        if (d < 0) {
            fail(p, "bad \\u escape");
            return false;
        }
        v = v * 16 + (unsigned)d;
    }
    p->pos += 4;
    *out = v;
    return true;
}

typedef struct Buffer {
    char *data;
    size_t len;
    size_t cap;
} Buffer;

static bool buf_push(Buffer *b, char c) {
    if (b->len + 1 >= b->cap) {
        size_t cap = b->cap == 0 ? 32 : b->cap * 2;
        char *grown = realloc(b->data, cap);
        if (grown == NULL) return false;
        b->data = grown;
        b->cap = cap;
    }
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
    return true;
}

static bool buf_utf8(Buffer *b, unsigned cp) {
    if (cp < 0x80) return buf_push(b, (char)cp);
    if (cp < 0x800)
        return buf_push(b, (char)(0xC0 | (cp >> 6))) &&
               buf_push(b, (char)(0x80 | (cp & 0x3F)));
    if (cp < 0x10000)
        return buf_push(b, (char)(0xE0 | (cp >> 12))) &&
               buf_push(b, (char)(0x80 | ((cp >> 6) & 0x3F))) &&
               buf_push(b, (char)(0x80 | (cp & 0x3F)));
    return buf_push(b, (char)(0xF0 | (cp >> 18))) &&
           buf_push(b, (char)(0x80 | ((cp >> 12) & 0x3F))) &&
           buf_push(b, (char)(0x80 | ((cp >> 6) & 0x3F))) &&
           buf_push(b, (char)(0x80 | (cp & 0x3F)));
}

static char *parse_string(Parser *p) {
    Buffer b = {0};
    p->pos++; /* opening quote */
    if (!buf_push(&b, '\0')) {
        fail(p, "out of memory");
        return NULL;
    }
    b.len = 0;
    for (;;) {
        unsigned char c = (unsigned char)p->text[p->pos];
        if (c == '\0') {
            fail(p, "unterminated string");
            break;
        }
        if (c < 0x20) {
            fail(p, "control character in string");
            break;
        }
        p->pos++;
        if (c == '"') return b.data;
        if (c != '\\') {
            if (!buf_push(&b, (char)c)) break;
            continue;
        }
        char e = p->text[p->pos++];
        bool ok = true;
        switch (e) {
        case '"':
        case '\\':
        case '/':
            ok = buf_push(&b, e);
            break;
        case 'b':
            ok = buf_push(&b, '\b');
            break;
        case 'f':
            ok = buf_push(&b, '\f');
            break;
        case 'n':
            ok = buf_push(&b, '\n');
            break;
        case 'r':
            ok = buf_push(&b, '\r');
            break;
        case 't':
            ok = buf_push(&b, '\t');
            break;
        case 'u': {
            unsigned cp;
            if (!read_hex4(p, &cp)) break;
            if (cp >= 0xD800 && cp <= 0xDBFF && p->text[p->pos] == '\\' &&
                p->text[p->pos + 1] == 'u') {
                unsigned lo;
                p->pos += 2;
                if (!read_hex4(p, &lo)) break;
                if (lo < 0xDC00 || lo > 0xDFFF) {
                    fail(p, "bad surrogate pair");
                    break;
                }
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            if (cp == 0) {
                fail(p, "\\u0000 in a string");
                break;
            }
            ok = buf_utf8(&b, cp);
            break;
        }
        default:
            p->pos--;
            fail(p, "bad escape");
            break;
        }
        if (p->failed) break;
        if (!ok) {
            fail(p, "out of memory");
            break;
        }
    }
    free(b.data);
    return NULL;
}

static bool parse_value(Parser *p, JsonValue *out, int depth);

static bool append_item(Parser *p, JsonValue *container, char *key) {
    int n = container->count + 1;
    JsonValue *items = realloc(container->items, (size_t)n * sizeof(*items));
    if (items == NULL) {
        fail(p, "out of memory");
        return false;
    }
    container->items = items;
    memset(&items[n - 1], 0, sizeof(items[n - 1]));
    if (container->type == JSON_OBJECT) {
        char **keys = realloc(container->keys, (size_t)n * sizeof(*keys));
        if (keys == NULL) {
            fail(p, "out of memory");
            return false;
        }
        container->keys = keys;
        keys[n - 1] = key;
    }
    container->count = n;
    return true;
}

static bool parse_array(Parser *p, JsonValue *out, int depth) {
    out->type = JSON_ARRAY;
    p->pos++;
    skip_space(p);
    if (p->text[p->pos] == ']') {
        p->pos++;
        return true;
    }
    for (;;) {
        if (!append_item(p, out, NULL)) return false;
        if (!parse_value(p, &out->items[out->count - 1], depth + 1)) return false;
        skip_space(p);
        char c = p->text[p->pos];
        if (c == ',') {
            p->pos++;
            continue;
        }
        if (c == ']') {
            p->pos++;
            return true;
        }
        fail(p, "expected , or ]");
        return false;
    }
}

static bool parse_object(Parser *p, JsonValue *out, int depth) {
    out->type = JSON_OBJECT;
    p->pos++;
    skip_space(p);
    if (p->text[p->pos] == '}') {
        p->pos++;
        return true;
    }
    for (;;) {
        skip_space(p);
        if (p->text[p->pos] != '"') {
            fail(p, "expected a member name");
            return false;
        }
        char *key = parse_string(p);
        if (key == NULL) return false;
        if (!append_item(p, out, key)) {
            free(key);
            return false;
        }
        skip_space(p);
        if (p->text[p->pos] != ':') {
            fail(p, "expected :");
            return false;
        }
        p->pos++;
        if (!parse_value(p, &out->items[out->count - 1], depth + 1)) return false;
        skip_space(p);
        char c = p->text[p->pos];
        if (c == ',') {
            p->pos++;
            continue;
        }
        if (c == '}') {
            p->pos++;
            return true;
        }
        fail(p, "expected , or }");
        return false;
    }
}

static bool parse_number(Parser *p, JsonValue *out) {
    size_t start = p->pos;
    const char *s = p->text;
    size_t i = start;
    if (s[i] == '-') i++;
    if (s[i] < '0' || s[i] > '9') {
        fail(p, "unexpected character");
        return false;
    }
    while (s[i] >= '0' && s[i] <= '9') i++;
    if (s[i] == '.') {
        i++;
        if (s[i] < '0' || s[i] > '9') {
            p->pos = i;
            fail(p, "bad number");
            return false;
        }
        while (s[i] >= '0' && s[i] <= '9') i++;
    }
    if (s[i] == 'e' || s[i] == 'E') {
        i++;
        if (s[i] == '+' || s[i] == '-') i++;
        if (s[i] < '0' || s[i] > '9') {
            p->pos = i;
            fail(p, "bad number");
            return false;
        }
        while (s[i] >= '0' && s[i] <= '9') i++;
    }
    out->type = JSON_NUMBER;
    out->number = strtod(s + start, NULL);
    p->pos = i;
    return true;
}

static bool parse_value(Parser *p, JsonValue *out, int depth) {
    if (depth > JSON_DEPTH_MAX) {
        fail(p, "nested too deeply");
        return false;
    }
    skip_space(p);
    char c = p->text[p->pos];
    switch (c) {
    case '{':
        return parse_object(p, out, depth);
    case '[':
        return parse_array(p, out, depth);
    case '"':
        out->type = JSON_STRING;
        out->string = parse_string(p);
        return out->string != NULL;
    case 't':
    case 'f':
        if (literal(p, "true") || literal(p, "false")) {
            out->type = JSON_BOOL;
            out->boolean = c == 't';
            return true;
        }
        break;
    case 'n':
        if (literal(p, "null")) {
            out->type = JSON_NULL;
            return true;
        }
        break;
    case '\0':
        fail(p, "unexpected end of input");
        return false;
    default:
        return parse_number(p, out);
    }
    fail(p, "unexpected character");
    return false;
}

bool json_parse(const char *text, JsonValue *out, char *err, size_t cap) {
    Parser p = {text, 0, err, cap, false};
    memset(out, 0, sizeof(*out));
    if (text == NULL) {
        fail(&p, "no input");
        return false;
    }
    if (!parse_value(&p, out, 0)) {
        json_free(out);
        return false;
    }
    skip_space(&p);
    if (p.text[p.pos] != '\0') {
        fail(&p, "trailing characters");
        json_free(out);
        return false;
    }
    return true;
}

void json_free(JsonValue *value) {
    if (value == NULL) return;
    for (int i = 0; i < value->count; i++) {
        json_free(&value->items[i]);
        if (value->keys != NULL) free(value->keys[i]);
    }
    free(value->items);
    free(value->keys);
    free(value->string);
    memset(value, 0, sizeof(*value));
}

const JsonValue *json_get(const JsonValue *object, const char *key) {
    if (object == NULL || object->type != JSON_OBJECT) return NULL;
    for (int i = 0; i < object->count; i++) {
        if (strcmp(object->keys[i], key) == 0) return &object->items[i];
    }
    return NULL;
}
