#include "config.h"
#include "theory.h"
#include "test_util.h"

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    CHECK(fputs(text, f) >= 0);
    CHECK(fclose(f) == 0);
}

static void test_defaults_and_set(void) {
    PieceConfig c;
    char err[200];
    config_defaults(&c);
    CHECK(c.length == 12);
    CHECK(c.voices == 2);
    CHECK(c.energy == 1);
    CHECK(c.cadence == 1);
    CHECK(c.consonance == CONSONANCE_STRONG);
    CHECK(c.modulate_at == -1);
    CHECK(c.motif_c == -128);
    CHECK(config_validate(&c, err, sizeof(err)));

    CHECK(config_set(&c, "voices", "3", err, sizeof(err)));
    CHECK(c.voices == 3);
    CHECK(config_set(&c, "key", "Eb", err, sizeof(err)));
    CHECK(c.key == 3);
    CHECK(config_set(&c, "key", "search", err, sizeof(err)));
    CHECK(c.key == KEY_SEARCH);
    CHECK(config_set(&c, "mode", "dorian", err, sizeof(err)));
    CHECK(c.mode == MODE_DORIAN);
    CHECK(config_set(&c, "consonance", "all", err, sizeof(err)));
    CHECK(c.consonance == CONSONANCE_ALL);
    CHECK(config_set(&c, "rhythm", "on", err, sizeof(err)));
    CHECK(c.rhythm == 1);
    CHECK(config_set(&c, "rhythm", "false", err, sizeof(err)));
    CHECK(c.rhythm == 0);
    CHECK(config_set(&c, "var_order", "entropy", err, sizeof(err)));
    CHECK(c.var_order == ORDER_ENTROPY);
    CHECK(config_set(&c, "modulate_at", "off", err, sizeof(err)));
    CHECK(c.modulate_at == -1);
    CHECK(config_set(&c, "pc_weight", "0,1,2,3,4,5,6,7,8,9,10,11", err, sizeof(err)));
    CHECK(c.pc_weight[11] == 11);
    CHECK(config_set(&c, "delay_2", "7", err, sizeof(err)));
    CHECK(c.voice_delay[2] == 7);
    CHECK(config_assign(&c, "max_leap=5", err, sizeof(err)));
    CHECK(c.max_leap == 5);

    CHECK(!config_set(&c, "voices", "9", err, sizeof(err)));
    CHECK(strstr(err, "voices") != NULL);
    CHECK(!config_set(&c, "length", "twelve", err, sizeof(err)));
    CHECK(!config_set(&c, "key_second", "H", err, sizeof(err)));
    CHECK(!config_set(&c, "strong_chord", "1", err, sizeof(err)));
    CHECK(strstr(err, "unknown config key: strong_chord") != NULL);
    CHECK(!config_set(&c, "pc_weight", "1,2", err, sizeof(err)));
    CHECK(!config_assign(&c, "=3", err, sizeof(err)));
    CHECK(!config_assign(&c, "voices", err, sizeof(err)));
}

static void test_validation(void) {
    PieceConfig c;
    char err[200];

    config_defaults(&c);
    c.range_low = 70;
    c.range_high = 60;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "range") != NULL);

    config_defaults(&c);
    c.augment = 2;
    c.diminish = 2;
    CHECK(!config_validate(&c, err, sizeof(err)));

    config_defaults(&c);
    c.augment = 1;
    CHECK(!config_validate(&c, err, sizeof(err)));

    config_defaults(&c);
    c.rest_at = 3;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "rest_at requires rhythm") != NULL);
    c.rhythm = 1;
    CHECK(config_validate(&c, err, sizeof(err)));
    c.rest_at = 12;
    CHECK(!config_validate(&c, err, sizeof(err)));

    config_defaults(&c);
    c.lock = 1;
    c.lock_index = 12;
    CHECK(!config_validate(&c, err, sizeof(err)));

    config_defaults(&c);
    c.length = 32;
    c.voices = 4;
    c.delay = 40;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "too long") != NULL);

    config_defaults(&c);
    c.modulate_at = 16;
    CHECK(!config_validate(&c, err, sizeof(err)));
    c.modulate_at = 15;
    CHECK(config_validate(&c, err, sizeof(err)));

    config_defaults(&c);
    c.motif_c = -60;
    CHECK(!config_validate(&c, err, sizeof(err)));

    config_defaults(&c);
    c.delay_search = 1;
    c.delay_min = 5;
    c.delay_max = 3;
    CHECK(!config_validate(&c, err, sizeof(err)));
}

static void test_presets(void) {
    CHECK(config_preset_count() >= 5);
    for (int i = 0; i < config_preset_count(); i++) {
        PieceConfig c;
        char err[200];
        config_defaults(&c);
        CHECK(config_apply_preset(&c, config_preset_name(i), err, sizeof(err)));
        CHECK(config_validate(&c, err, sizeof(err)));
    }
    PieceConfig c;
    char err[200];
    config_defaults(&c);
    CHECK(!config_apply_preset(&c, "romantic", err, sizeof(err)));
    CHECK(strstr(err, "unknown preset") != NULL);
    CHECK(config_apply_preset(&c, "baroque", err, sizeof(err)));
    CHECK(c.voices == 3 && c.harmony == 1);
}

static void test_files(void) {
    test_output_dir();
    PieceConfig c;
    char err[300];

    /* the preset line applies first, whatever its position */
    write_file("output/tests/config.txt",
               "# comment line\n"
               "voices 2   # trailing comment\n"
               "key D\n"
               "mode minor\n"
               "preset baroque\n"
               "\n"
               "pc_weight 0 0 0 0 0 0 0 0 0 0 0 5\n");
    config_defaults(&c);
    CHECK(config_load_file(&c, "output/tests/config.txt", err, sizeof(err)));
    CHECK(c.voices == 2);
    CHECK(c.harmony == 1);
    CHECK(c.key == 2 && c.mode == MODE_MINOR);
    CHECK(c.pc_weight[11] == 5);

    write_file("output/tests/bad.txt", "length 12\nvoices 2\nshape round\n");
    config_defaults(&c);
    CHECK(!config_load_file(&c, "output/tests/bad.txt", err, sizeof(err)));
    CHECK(strstr(err, ":3:") != NULL);
    CHECK(strstr(err, "unknown config key: shape") != NULL);

    write_file("output/tests/missing.txt", "length\n");
    CHECK(!config_load_file(&c, "output/tests/missing.txt", err, sizeof(err)));
    CHECK(strstr(err, "missing value") != NULL);

    write_file("output/tests/config.json",
               "{\"voices\": 3, \"invert\": true, \"key\": \"G\", \"mode\": \"search\",\n"
               " \"pc_weight\": [1,1,1,1,1,1,1,1,1,1,1,2], \"preset\": \"renaissance\"}\n");
    config_defaults(&c);
    CHECK(config_load_file(&c, "output/tests/config.json", err, sizeof(err)));
    CHECK(c.voices == 3);
    CHECK(c.invert == 1);
    CHECK(c.key == 7 && c.mode == KEY_SEARCH);
    CHECK(c.pc_weight[11] == 2);
    CHECK(c.max_leap == 5); /* from the preset */

    write_file("output/tests/bad.json", "{\"voices\": 2.5}");
    CHECK(!config_load_file(&c, "output/tests/bad.json", err, sizeof(err)));
    write_file("output/tests/broken.json", "{\"voices\": }");
    CHECK(!config_load_file(&c, "output/tests/broken.json", err, sizeof(err)));
    CHECK(strstr(err, "line 1") != NULL);
    write_file("output/tests/array.json", "[1, 2]");
    CHECK(!config_load_file(&c, "output/tests/array.json", err, sizeof(err)));
    CHECK(!config_load_file(&c, "output/tests/nope.txt", err, sizeof(err)));
    CHECK(strstr(err, "cannot read config") != NULL);
}

/* Writing every key and loading it back gives the same config. */
static void test_round_trip(void) {
    PieceConfig a;
    PieceConfig b;
    char err[200];
    config_defaults(&a);
    test_set(&a, "preset=baroque");
    test_set(&a, "key=search");
    test_set(&a, "modulate_at=8");
    test_set(&a, "key_second=related");
    test_set(&a, "pc_weight=3,0,0,0,0,0,0,0,0,0,0,1");
    FILE *f = fopen("output/tests/round.txt", "w");
    CHECK(f != NULL);
    config_write(f, &a);
    CHECK(fclose(f) == 0);
    config_defaults(&b);
    CHECK(config_load_file(&b, "output/tests/round.txt", err, sizeof(err)));
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);

    char value[64];
    for (int i = 0; i < config_key_count(); i++) {
        CHECK(config_key_name(i) != NULL);
        config_key_value(&a, i, value, sizeof(value));
        CHECK(value[0] != '\0');
    }
    CHECK(config_key_name(config_key_count()) == NULL);
}

static void test_reference(void) {
    FILE *f = fopen("output/tests/reference.md", "w");
    CHECK(f != NULL);
    config_print_reference(f, true);
    CHECK(fclose(f) == 0);
    char *text = test_slurp("output/tests/reference.md", NULL);
    for (int i = 0; i < config_key_count(); i++) {
        char needle[64];
        snprintf(needle, sizeof(needle), "| `%s` |", config_key_name(i));
        CHECK(strstr(text, needle) != NULL);
    }
    free(text);
}

int main(void) {
    test_defaults_and_set();
    test_validation();
    test_presets();
    test_files();
    test_round_trip();
    test_reference();
    printf("ok\n");
    return 0;
}
