#include "config.h"
#include "corpus.h"
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
    CHECK(c.voice_transpose[1] == TRANSPOSE_SAME);
    CHECK(c.w_step == 0);
    CHECK(config_validate(&c, err, sizeof(err)));
    CHECK(config_set(&c, "w_step", "3", err, sizeof(err)));
    CHECK(c.w_step == 3);
    CHECK(!config_set(&c, "w_step", "-1", err, sizeof(err)));
    c.w_step = 0;
    CHECK(config_set(&c, "transpose_1", "0", err, sizeof(err)));
    CHECK(c.voice_transpose[1] == 0);
    CHECK(config_set(&c, "transpose_1", "same", err, sizeof(err)));
    CHECK(c.voice_transpose[1] == TRANSPOSE_SAME);
    CHECK(!config_set(&c, "transpose_2", "25", err, sizeof(err)));

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
    CHECK(c.instrument == INSTRUMENT_PLUCK);
    CHECK(config_set(&c, "instrument", "organ", err, sizeof(err)));
    CHECK(c.instrument == INSTRUMENT_ORGAN);
    CHECK(config_set(&c, "instrument", "sine", err, sizeof(err)));
    CHECK(c.instrument == INSTRUMENT_SINE);
    CHECK(config_set(&c, "instrument", "pluck", err, sizeof(err)));
    CHECK(c.instrument == INSTRUMENT_PLUCK);
    CHECK(!config_set(&c, "instrument", "kazoo", err, sizeof(err)));
    CHECK(strstr(err, "instrument") != NULL);
    CHECK(!config_set(&c, "sample", "1", err, sizeof(err)));
    CHECK(config_set(&c, "modulate_at", "off", err, sizeof(err)));
    CHECK(c.modulate_at == -1);
    CHECK(config_set(&c, "pc_weight", "0,1,2,3,4,5,6,7,8,9,10,11", err, sizeof(err)));
    CHECK(c.pc_weight[11] == 11);
    CHECK(config_set(&c, "delay_2", "7", err, sizeof(err)));
    CHECK(c.voice_delay[2] == 7);
    CHECK(config_assign(&c, "max_leap=5", err, sizeof(err)));
    CHECK(c.max_leap == 5);
    /* melody takes up to length notes; the rest stay free */
    CHECK(c.melody[0] == -1 && c.melody[MELODY_MAX - 1] == -1);
    CHECK(config_set(&c, "melody", "60 ? rest 64", err, sizeof(err)));
    CHECK(c.melody[0] == 60 && c.melody[1] == -1 && c.melody[2] == PITCH_REST);
    CHECK(c.melody[3] == 64 && c.melody[4] == -1);
    CHECK(config_set(&c, "melody", "67", err, sizeof(err))); /* a new list replaces the old */
    CHECK(c.melody[0] == 67 && c.melody[3] == -1);
    CHECK(!config_set(&c, "melody", "60 x", err, sizeof(err)));
    CHECK(!config_set(&c, "melody", "", err, sizeof(err)));
    CHECK(!config_set(&c, "melody", "128", err, sizeof(err)));
    /* tension: unlisted (the arch) until a curve is drawn; a short list
     * leaves the later slots unlisted, a ? is a point of its own, and a
     * new list replaces the old */
    CHECK(c.tension[0] == TENSION_UNLISTED && c.tension[MELODY_MAX - 1] == TENSION_UNLISTED);
    CHECK(config_set(&c, "tension", "0 4 0", err, sizeof(err)));
    CHECK(c.tension[0] == 0 && c.tension[1] == 4 && c.tension[2] == 0);
    CHECK(c.tension[3] == TENSION_UNLISTED);
    CHECK(config_set(&c, "tension", "1,?,3", err, sizeof(err)));
    CHECK(c.tension[0] == 1 && c.tension[1] == TENSION_FREE && c.tension[2] == 3);
    CHECK(config_set(&c, "tension", "0 4 ?", err, sizeof(err)));
    CHECK(c.tension[2] == TENSION_FREE && c.tension[3] == TENSION_UNLISTED);
    CHECK(config_set(&c, "tension", "?", err, sizeof(err)));
    CHECK(c.tension[0] == TENSION_FREE && c.tension[2] == TENSION_UNLISTED);
    CHECK(config_set(&c, "tension", "arch", err, sizeof(err)));
    CHECK(c.tension[0] == TENSION_UNLISTED && c.tension[2] == TENSION_UNLISTED);
    CHECK(!config_set(&c, "tension", "5", err, sizeof(err)));
    CHECK(!config_set(&c, "tension", "0 rest", err, sizeof(err)));
    CHECK(config_set(&c, "mirror", "on", err, sizeof(err)));
    CHECK(c.mirror == 1 && c.mirror_axis == 66);
    CHECK(config_set(&c, "mirror_axis", "62", err, sizeof(err)));
    CHECK(c.mirror_axis == 62);
    CHECK(!config_set(&c, "mirror_axis", "0", err, sizeof(err)));

    CHECK(!config_set(&c, "voices", "9", err, sizeof(err)));
    CHECK(strstr(err, "voices") != NULL);
    CHECK(!config_set(&c, "length", "twelve", err, sizeof(err)));
    CHECK(!config_set(&c, "key_second", "H", err, sizeof(err)));
    CHECK(!config_set(&c, "strong_chord", "1", err, sizeof(err)));
    CHECK(strstr(err, "unknown config key: strong_chord") != NULL);
    CHECK(!config_set(&c, "pc_weight", "1,2", err, sizeof(err)));
    char many[3 * (MELODY_MAX + 1) + 1] = "";
    for (int i = 0; i <= MELODY_MAX; i++) strcat(many, i ? ",60" : "60");
    CHECK(!config_set(&c, "melody", many, err, sizeof(err)));
    CHECK(strstr(err, "at most") != NULL);
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
    c.melody[12] = 60;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "melody") != NULL && strstr(err, "past") != NULL);
    config_defaults(&c);
    c.melody[2] = PITCH_REST;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "rest") != NULL);
    c.rhythm = 1;
    CHECK(config_validate(&c, err, sizeof(err)));

    config_defaults(&c);
    c.length = 32;
    c.voices = 4;
    c.delay = 80;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "too long") != NULL);

    config_defaults(&c);
    c.modulate_at = 0;
    CHECK(!config_validate(&c, err, sizeof(err)));
    c.modulate_at = 16;
    CHECK(!config_validate(&c, err, sizeof(err)));
    c.modulate_at = 15;
    CHECK(config_validate(&c, err, sizeof(err)));

    config_defaults(&c);
    c.motif_c = -60;
    CHECK(!config_validate(&c, err, sizeof(err)));

    config_defaults(&c);
    c.voice_transpose[3] = -60;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "transpose_3") != NULL);

    config_defaults(&c);
    c.diatonic = 1;
    CHECK(config_validate(&c, err, sizeof(err)));
    c.key = KEY_SEARCH;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "diatonic") != NULL);
    c.key = 0;
    c.mode = -1;
    CHECK(!config_validate(&c, err, sizeof(err)));
    c.mode = MODE_MAJOR;
    c.modulate_at = 8;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "modulate_at") != NULL);

    config_defaults(&c);
    c.delay_search = 1;
    c.delay_min = 5;
    c.delay_max = 3;
    CHECK(!config_validate(&c, err, sizeof(err)));

    /* mirrored notes straddle the axis, so it must be in range; given
     * notes and an odd length are fine */
    config_defaults(&c);
    c.mirror = 1;
    c.mirror_axis = 73;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "mirror_axis") != NULL);
    c.mirror_axis = 59;
    CHECK(!config_validate(&c, err, sizeof(err)));
    c.mirror_axis = 72;
    c.length = 13;
    c.melody[0] = 72;
    c.lock = 1;
    c.lock_index = 12;
    c.lock_pitch = 72;
    CHECK(config_validate(&c, err, sizeof(err)));
    c.mirror = 0;
    c.mirror_axis = 100;
    CHECK(config_validate(&c, err, sizeof(err)));
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

    write_file("output/tests/tension.json",
               "{\"tension\": [0, \"?\", 4], \"mirror\": true, \"instrument\": \"organ\"}");
    config_defaults(&c);
    CHECK(config_load_file(&c, "output/tests/tension.json", err, sizeof(err)));
    CHECK(c.tension[0] == 0 && c.tension[1] == TENSION_FREE && c.tension[2] == 4);
    CHECK(c.tension[3] == TENSION_UNLISTED);
    CHECK(c.mirror == 1);
    CHECK(c.instrument == INSTRUMENT_ORGAN);

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

/* grid sets how long a step is: a quarter note, an eighth or a sixteenth. */
static void test_grid(void) {
    PieceConfig c;
    char err[200];
    config_defaults(&c);
    CHECK(c.grid == GRID_QUARTER);
    CHECK(config_beat_steps(&c) == 1 && config_bar_steps(&c) == 4);
    CHECK(c.w_run == 2);
    CHECK(config_set(&c, "w_run", "5", err, sizeof(err)));
    CHECK(c.w_run == 5);
    CHECK(!config_set(&c, "w_run", "101", err, sizeof(err)));
    CHECK(config_set(&c, "grid", "eighth", err, sizeof(err)));
    CHECK(c.grid == GRID_EIGHTH);
    CHECK(config_beat_steps(&c) == 2 && config_bar_steps(&c) == 8);
    CHECK(config_set(&c, "grid", "quarter", err, sizeof(err)));
    CHECK(c.grid == GRID_QUARTER);
    CHECK(config_set(&c, "grid", "1", err, sizeof(err)));
    CHECK(c.grid == GRID_EIGHTH);
    CHECK(config_set(&c, "grid", "sixteenth", err, sizeof(err)));
    CHECK(c.grid == GRID_SIXTEENTH);
    CHECK(config_beat_steps(&c) == 4 && config_bar_steps(&c) == 16);
    CHECK(config_set(&c, "grid", "eighth", err, sizeof(err)));
    CHECK(config_set(&c, "grid", "2", err, sizeof(err)));
    CHECK(c.grid == GRID_SIXTEENTH);
    CHECK(!config_set(&c, "grid", "thirtysecond", err, sizeof(err)));
    CHECK(strstr(err, "config value for grid is not valid: thirtysecond") != NULL);
    CHECK(!config_set(&c, "grid", "3", err, sizeof(err)));
    CHECK(c.grid == GRID_SIXTEENTH);
    CHECK(config_set(&c, "grid", "eighth", err, sizeof(err)));
    CHECK(c.grid == GRID_EIGHTH);
    /* a tie may hold a note to a whole note of eighths */
    CHECK(config_set(&c, "max_hold", "7", err, sizeof(err)));
    CHECK(!config_set(&c, "max_hold", "8", err, sizeof(err)));
    CHECK(config_validate(&c, err, sizeof(err)));
    c.grid = GRID_COUNT;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "invalid grid") != NULL);

    /* written as a word, and read back from text and JSON */
    c.grid = GRID_EIGHTH;
    char value[16] = "";
    for (int i = 0; i < config_key_count(); i++) {
        if (strcmp(config_key_name(i), "grid") == 0) config_key_value(&c, i, value, sizeof(value));
    }
    CHECK(strcmp(value, "eighth") == 0);
    c.grid = GRID_SIXTEENTH;
    for (int i = 0; i < config_key_count(); i++) {
        if (strcmp(config_key_name(i), "grid") == 0) config_key_value(&c, i, value, sizeof(value));
    }
    CHECK(strcmp(value, "sixteenth") == 0);
    CHECK(config_validate(&c, err, sizeof(err)));
    test_output_dir();
    write_file("output/tests/grid.txt", "grid eighth\nlength 32\n");
    config_defaults(&c);
    CHECK(config_load_file(&c, "output/tests/grid.txt", err, sizeof(err)));
    CHECK(c.grid == GRID_EIGHTH && c.length == 32);
    write_file("output/tests/grid.json", "{\"grid\": \"eighth\", \"delay\": 8}");
    config_defaults(&c);
    CHECK(config_load_file(&c, "output/tests/grid.json", err, sizeof(err)));
    CHECK(c.grid == GRID_EIGHTH && c.delay == 8);
    write_file("output/tests/grid16.txt", "grid sixteenth\nlength 128\ndelay 48\n");
    config_defaults(&c);
    CHECK(config_load_file(&c, "output/tests/grid16.txt", err, sizeof(err)));
    CHECK(c.grid == GRID_SIXTEENTH && c.length == 128 && c.delay == 48);
    CHECK(config_validate(&c, err, sizeof(err)));
    write_file("output/tests/grid16.json", "{\"grid\": \"sixteenth\", \"delay\": 16}");
    config_defaults(&c);
    CHECK(config_load_file(&c, "output/tests/grid16.json", err, sizeof(err)));
    CHECK(c.grid == GRID_SIXTEENTH && c.delay == 16);
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
    test_set(&a, "melody=62,?,rest,66");
    test_set(&a, "transpose_1=0");
    test_set(&a, "transpose_2=-5");
    test_set(&a, "tension=0,?,4,?"); /* a trailing ? survives the round trip */
    test_set(&a, "mirror=1");
    test_set(&a, "mirror_axis=62");
    test_set(&a, "instrument=sine");
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

/* A melody takes up to 128 notes, from text or JSON, and writing the
 * config keeps every one of them; a canon spans up to 256 steps. */
static void test_longest_melody(void) {
    PieceConfig a;
    PieceConfig b;
    char err[300];
    test_output_dir();
    CHECK(MELODY_MAX == 128 && SPAN_MAX == 256);
    config_defaults(&b);
    CHECK(config_set(&b, "lock_index", "127", err, sizeof(err)));
    CHECK(!config_set(&b, "lock_index", "128", err, sizeof(err)));
    CHECK(config_set(&b, "rest_at", "127", err, sizeof(err)));
    CHECK(!config_set(&b, "rest_at", "128", err, sizeof(err)));
    CHECK(config_set(&b, "delay", "256", err, sizeof(err)));
    CHECK(!config_set(&b, "delay", "257", err, sizeof(err)));
    CHECK(config_set(&b, "delay_max", "256", err, sizeof(err)));
    CHECK(config_set(&b, "modulate_at", "256", err, sizeof(err)));
    config_defaults(&a);
    CHECK(config_set(&a, "length", "128", err, sizeof(err)));
    CHECK(a.length == 128);
    CHECK(!config_set(&a, "length", "129", err, sizeof(err)));
    CHECK(strstr(err, "must be 1..128") != NULL);
    CHECK(config_set(&a, "max_rests", "128", err, sizeof(err)));
    CHECK(!config_set(&a, "max_rests", "129", err, sizeof(err)));
    /* three voices 64 steps apart fill the longest span exactly */
    config_defaults(&b);
    test_set(&b, "length=128");
    test_set(&b, "voices=3");
    test_set(&b, "delay=64");
    CHECK(config_validate(&b, err, sizeof(err)));
    test_set(&b, "phase=1");
    CHECK(!config_validate(&b, err, sizeof(err)));
    CHECK(strstr(err, "more than 256 steps") != NULL);

    /* rests and three-digit pitches make the longest text a melody has */
    char text[5 * MELODY_MAX] = "";
    char json[16 * MELODY_MAX] = "{\"length\": 128, \"rhythm\": 1, \"max_rests\": 128, \"melody\": [";
    for (int i = 0; i < MELODY_MAX; i++) {
        strcat(text, i ? "," : "");
        strcat(text, i % 2 ? "127" : "rest");
        strcat(json, i ? ", " : "");
        strcat(json, i % 2 ? "127" : "\"rest\"");
    }
    strcat(json, "]}");
    CHECK(strlen(text) > 256);
    CHECK(config_set(&a, "melody", text, err, sizeof(err)));
    for (int i = 0; i < MELODY_MAX; i++) CHECK(a.melody[i] == (i % 2 ? 127 : PITCH_REST));
    test_set(&a, "rhythm=1");
    CHECK(config_validate(&a, err, sizeof(err)));
    a.length = 127; /* the last note is past the end */
    CHECK(!config_validate(&a, err, sizeof(err)));
    CHECK(strstr(err, "note 127 is past the end") != NULL);
    a.length = 128;

    write_file("output/tests/longest.json", json);
    config_defaults(&b);
    CHECK(config_load_file(&b, "output/tests/longest.json", err, sizeof(err)));
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);

    int key = -1;
    for (int i = 0; i < config_key_count(); i++) {
        if (strcmp(config_key_name(i), "melody") == 0) key = i;
    }
    CHECK(key >= 0);
    char value[CONFIG_VALUE_MAX];
    config_key_value(&a, key, value, sizeof(value));
    CHECK(strcmp(value, text) == 0);

    FILE *f = fopen("output/tests/longest.txt", "w");
    CHECK(f != NULL);
    config_write(f, &a);
    CHECK(fclose(f) == 0);
    config_defaults(&b);
    CHECK(config_load_file(&b, "output/tests/longest.txt", err, sizeof(err)));
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
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

/* Regressions: sharps survive the comment rule, and odd files fail loudly. */
static void test_file_edges(void) {
    PieceConfig c;
    char err[300];
    test_output_dir();

    write_file("output/tests/sharp.txt", "key F#   # a sharp, then a comment\nkey_second C#\n");
    config_defaults(&c);
    CHECK(config_load_file(&c, "output/tests/sharp.txt", err, sizeof(err)));
    CHECK(c.key == 6 && c.key_second == 1);

    PieceConfig a;
    config_defaults(&a);
    test_set(&a, "key=A#");
    test_set(&a, "key_second=D#");
    test_set(&a, "modulate_at=8");
    FILE *f = fopen("output/tests/sharp_round.txt", "w");
    CHECK(f != NULL);
    config_write(f, &a);
    CHECK(fclose(f) == 0);
    config_defaults(&c);
    CHECK(config_load_file(&c, "output/tests/sharp_round.txt", err, sizeof(err)));
    CHECK(memcmp(&a, &c, sizeof(a)) == 0);

    char longline[1200];
    memset(longline, ' ', sizeof(longline));
    memcpy(longline, "seed", 4);
    memcpy(longline + 1100, "1234\n", 6);
    write_file("output/tests/long.txt", longline);
    CHECK(!config_load_file(&c, "output/tests/long.txt", err, sizeof(err)));
    CHECK(strstr(err, "too long") != NULL);

    f = fopen("output/tests/nul.txt", "wb");
    CHECK(f != NULL);
    CHECK(fwrite("length 16\n\0voices 3\n", 1, 20, f) == 20);
    CHECK(fclose(f) == 0);
    CHECK(!config_load_file(&c, "output/tests/nul.txt", err, sizeof(err)));
    CHECK(strstr(err, "NUL") != NULL);

    write_file("output/tests/upper.JSON", "\xEF\xBB\xBF{\"voices\": 3}");
    config_defaults(&c);
    CHECK(config_load_file(&c, "output/tests/upper.JSON", err, sizeof(err)));
    CHECK(c.voices == 3);

    config_defaults(&c);
    CHECK(!config_set(&c, "pc_weight", longline, err, sizeof(err)));
}

static void test_validation_edges(void) {
    PieceConfig c;
    char err[200];
    config_defaults(&c);
    c.cyclic = 1;
    c.augment = 2;
    CHECK(!config_validate(&c, err, sizeof(err)));
    CHECK(strstr(err, "cyclic") != NULL);
    c.augment = 0;
    c.diminish = 2;
    CHECK(!config_validate(&c, err, sizeof(err)));

    /* with delay_search, modulate_at must fit the longest delay tried;
     * shorter delays are skipped */
    config_defaults(&c);
    c.delay_search = 1;
    c.delay_min = 1;
    c.delay_max = 12;
    c.modulate_at = 20;
    CHECK(config_validate(&c, err, sizeof(err)));
    c.delay_max = 2;
    CHECK(!config_validate(&c, err, sizeof(err)));
}

static void test_corpus(void) {
    test_output_dir();
    write_file("output/tests/corpus.txt", "60 64 # a comment word\n67 60 x 200 -3 64\n");
    int counts[12] = {0};
    CHECK(corpus_row_counts("output/tests/corpus.txt", counts) == 5);
    CHECK(counts[0] == 2 && counts[4] == 2 && counts[7] == 1);
    int weights[12];
    corpus_weights_from_counts(counts, 4, weights);
    CHECK(weights[0] == 0 && weights[4] == 0);
    CHECK(weights[7] == 2);
    CHECK(weights[1] == 4);
    int none[12] = {0};
    corpus_weights_from_counts(none, 4, weights);
    CHECK(weights[5] == 0);
    CHECK(corpus_dir_counts("output/tests/no-such-dir", counts) == 0);
}

int main(void) {
    test_file_edges();
    test_validation_edges();
    test_corpus();
    test_defaults_and_set();
    test_validation();
    test_presets();
    test_files();
    test_round_trip();
    test_grid();
    test_longest_melody();
    test_reference();
    printf("ok\n");
    return 0;
}
