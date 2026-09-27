#include "json.h"
#include "test_util.h"

#include <math.h>

static void test_values(void) {
    JsonValue v;
    char err[128];
    CHECK(json_parse("{\"a\": 1, \"b\": [true, false, null], \"c\": \"x\\ty\", \"d\": -2.5e1}",
                     &v, err, sizeof(err)));
    CHECK(v.type == JSON_OBJECT);
    CHECK(v.count == 4);
    const JsonValue *a = json_get(&v, "a");
    CHECK(a != NULL && a->type == JSON_NUMBER && a->number == 1.0);
    const JsonValue *b = json_get(&v, "b");
    CHECK(b != NULL && b->type == JSON_ARRAY && b->count == 3);
    CHECK(b->items[0].type == JSON_BOOL && b->items[0].boolean);
    CHECK(b->items[1].type == JSON_BOOL && !b->items[1].boolean);
    CHECK(b->items[2].type == JSON_NULL);
    const JsonValue *c = json_get(&v, "c");
    CHECK(c != NULL && strcmp(c->string, "x\ty") == 0);
    const JsonValue *d = json_get(&v, "d");
    CHECK(d != NULL && fabs(d->number + 25.0) < 1e-9);
    CHECK(json_get(&v, "missing") == NULL);
    json_free(&v);

    CHECK(json_parse("  [ ]  ", &v, err, sizeof(err)));
    CHECK(v.type == JSON_ARRAY && v.count == 0);
    json_free(&v);

    CHECK(json_parse("\"\\u00e9\\ud83c\\udfb5\"", &v, err, sizeof(err)));
    CHECK(strcmp(v.string, "\xc3\xa9\xf0\x9f\x8e\xb5") == 0);
    json_free(&v);

    CHECK(json_parse("\"\"", &v, err, sizeof(err)));
    CHECK(v.type == JSON_STRING && v.string[0] == '\0');
    json_free(&v);
}

static void test_errors(void) {
    JsonValue v;
    char err[128];
    CHECK(!json_parse("{\"a\": 1,}", &v, err, sizeof(err)));
    CHECK(strstr(err, "line 1") != NULL);
    CHECK(!json_parse("{\n  \"a\": tru\n}", &v, err, sizeof(err)));
    CHECK(strstr(err, "line 2") != NULL);
    CHECK(!json_parse("[1, 2", &v, err, sizeof(err)));
    CHECK(!json_parse("\"open", &v, err, sizeof(err)));
    CHECK(!json_parse("01x", &v, err, sizeof(err)));
    CHECK(!json_parse("{} {}", &v, err, sizeof(err)));
    CHECK(strstr(err, "trailing") != NULL);
    CHECK(!json_parse("\"\\q\"", &v, err, sizeof(err)));
    CHECK(!json_parse("\"\\ud800\\u0041\"", &v, err, sizeof(err)));
    CHECK(!json_parse("{\"length\\u0000junk\": 16}", &v, err, sizeof(err)));
    CHECK(!json_parse("", &v, err, sizeof(err)));
    char deep[200];
    memset(deep, '[', 100);
    deep[100] = '\0';
    CHECK(!json_parse(deep, &v, err, sizeof(err)));
    CHECK(strstr(err, "deeply") != NULL);
}

int main(void) {
    test_values();
    test_errors();
    printf("ok\n");
    return 0;
}
