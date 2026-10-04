#include "notation.h"
#include "theory.h"
#include "test_util.h"

#include <ctype.h>

/* A score from hand-written notes: {start, length, pitch} per voice. */
static void build(Score *s, int voices, int span, const int (*notes)[8][3],
                  const int *counts) {
    memset(s, 0, sizeof(*s));
    s->voices = voices;
    s->span = span;
    s->tempo = 90;
    s->nsections = 1;
    for (int v = 0; v < voices; v++) {
        s->voice[v].count = counts[v];
        for (int k = 0; k < counts[v]; k++) {
            ScoreNote n = {notes[v][k][0], notes[v][k][1], notes[v][k][2], k};
            if (n.pitch == SOUND_REST) n.source = -1;
            s->voice[v].notes[k] = n;
            for (int t = n.start; t < n.start + n.length; t++) s->line[v][t] = n.pitch;
        }
    }
}

/* Quarters in each bar of every voice; returns the bars counted, or -1 if
 * one does not hold exactly four. Rhythm tokens: LilyPond r/a-g + 4 2 2. 1,
 * ABC z/A-G/a-g + digits. */
static int ly_bars(const char *text) {
    const char *p = strstr(text, "\\new Staff");
    int bars = 0;
    int sum = 0;
    while (p != NULL && *p != '\0') {
        while (*p == ' ' || *p == '\n') p++;
        if (*p == '\0') break;
        const char *tok = p;
        while (*p != '\0' && *p != ' ' && *p != '\n') p++;
        size_t len = (size_t)(p - tok);
        if (tok[0] == '\\') {
            if (strncmp(tok, "\\key", 4) == 0 || strncmp(tok, "\\clef", 5) == 0 ||
                strncmp(tok, "\\time", 5) == 0 || strncmp(tok, "\\bar", 4) == 0) {
                while (*p == ' ') p++;
                while (*p != '\0' && *p != ' ' && *p != '\n') p++;
            } else if (strncmp(tok, "\\tempo", 6) == 0) {
                for (int skip = 0; skip < 3; skip++) {
                    while (*p == ' ') p++;
                    while (*p != '\0' && *p != ' ' && *p != '\n') p++;
                }
            } else if (strncmp(tok, "\\with", 5) == 0) {
                p = strchr(p, '}') + 1;
            }
            if (strncmp(tok, "\\bar", 4) == 0) {
                if (sum != 4) return -1;
                bars++;
                sum = 0;
            }
            continue;
        }
        if (len == 1 && tok[0] == '|') {
            if (sum != 4) return -1;
            bars++;
            sum = 0;
            continue;
        }
        if (strchr("abcdefgr", tok[0]) == NULL) continue;
        const char *d = tok;
        while (d < p && !isdigit((unsigned char)*d)) d++;
        int q = *d == '4' ? 1 : *d == '2' ? 2 : *d == '1' ? 4 : 0;
        if (q == 2 && d + 1 < p && d[1] == '.') q = 3;
        CHECK(q > 0);
        sum += q;
    }
    return bars;
}

static int abc_bars(const char *text) {
    const char *p = strstr(text, "\nV:1\n");
    CHECK(p != NULL);
    int bars = 0;
    int sum = 0;
    for (; *p != '\0'; p++) {
        if (*p == '\n' && strncmp(p + 1, "V:", 2) == 0) {
            p = strchr(p + 1, '\n');
            CHECK(sum == 0);
            continue;
        }
        if (*p == '[') {
            p = strchr(p, ']');
            continue;
        }
        if (*p == '|') {
            if (sum != 4) return -1;
            bars++;
            sum = 0;
            if (p[1] == ']') p++;
            continue;
        }
        if (strchr("ABCDEFGabcdefgz", *p) == NULL) continue;
        while (p[1] == '\'' || p[1] == ',') p++;
        int q = isdigit((unsigned char)p[1]) ? p[1] - '0' : 1;
        sum += q;
    }
    return bars;
}

static void test_modulating(void) {
    /* F major to C major in the middle of bar 2; voice 2 is low. */
    static const int notes[2][8][3] = {
        {{0, 3, 65}, {3, 2, 70}, {5, 1, 71}, {6, 1, 70}, {7, 3, 60}},
        {{0, 4, SOUND_REST}, {4, 4, 41}, {8, 2, 43}},
    };
    static const int counts[2] = {5, 3};
    Score s;
    build(&s, 2, 10, notes, counts);
    s.key[0] = key_id(5, MODE_MAJOR);
    s.key[1] = key_id(0, MODE_MAJOR);
    s.nsections = 2;
    s.modulate_at = 6;
    test_output_dir();

    CHECK(export_lilypond("output/tests/score.ly", &s));
    char *ly = test_slurp("output/tests/score.ly", NULL);
    CHECK(strncmp(ly, "\\version \"2.24.0\"", 17) == 0);
    CHECK(strstr(ly, "\\new StaffGroup <<") != NULL);
    CHECK(strstr(ly, "\\clef treble \\key f \\major \\time 4/4 \\tempo 4 = 90") != NULL);
    CHECK(strstr(ly, "f'2. bes'4~ |") != NULL);
    CHECK(strstr(ly, "bes'4 b'4 \\key c \\major ais'4 c'4~ |") != NULL);
    CHECK(strstr(ly, "c'2 r2 \\bar \"|.\"") != NULL);
    CHECK(strstr(ly, "\\clef bass") != NULL);
    CHECK(strstr(ly, "r1 |") != NULL);
    CHECK(strstr(ly, "f,2~ \\key c \\major f,2 |") != NULL);
    CHECK(ly_bars(ly) == 6);
    free(ly);

    CHECK(export_abc("output/tests/score.abc", &s));
    char *abc = test_slurp("output/tests/score.abc", NULL);
    CHECK(strncmp(abc, "X:1\n", 4) == 0);
    CHECK(strstr(abc, "M:4/4\nL:1/4\nQ:1/4=90\n") != NULL);
    CHECK(strstr(abc, "V:1 clef=treble") != NULL);
    CHECK(strstr(abc, "V:2 clef=bass") != NULL);
    CHECK(strstr(abc, "\nK:F\n") != NULL);
    /* B-flat from the signature, then the natural, then the new key */
    CHECK(strstr(abc, "F3 B- |\nB =B [K:C] ^A C- |\nC2 z2 |]\n") != NULL);
    CHECK(strstr(abc, "z4 |\nF,,2- [K:C] F,,2 |\nG,,2 z2 |]\n") != NULL);
    CHECK(abc_bars(abc) == 6);
    free(abc);
}

static void test_keys(void) {
    /* A minor's raised seventh, then G natural in the same bar */
    static const int minor[1][8][3] = {{{0, 1, 69}, {1, 1, 68}, {2, 1, 67}, {3, 1, 80},
                                        {4, 1, 68}, {5, 3, 81}}};
    static const int one[1] = {6};
    Score s;
    build(&s, 1, 8, minor, one);
    s.key[0] = key_id(9, MODE_MINOR);
    CHECK(export_abc("output/tests/minor.abc", &s));
    char *abc = test_slurp("output/tests/minor.abc", NULL);
    CHECK(strstr(abc, "\nK:Am\n") != NULL);
    /* every G in the bar after the first sharp spells itself out */
    CHECK(strstr(abc, "A ^G =G ^g |\n^G a3 |]\n") != NULL);
    CHECK(abc_bars(abc) == 2);
    free(abc);
    CHECK(export_lilypond("output/tests/minor.ly", &s));
    char *ly = test_slurp("output/tests/minor.ly", NULL);
    CHECK(strstr(ly, "\\key a \\minor") != NULL);
    CHECK(strstr(ly, "a'4 gis'4 g'4 gis''4 |") != NULL);
    CHECK(strstr(ly, "gis'4 a''2. \\bar") != NULL);
    CHECK(ly_bars(ly) == 2);
    free(ly);

    /* flat keys and modes name their tonic as the signature spells it */
    static const struct {
        int tonic;
        int mode;
        const char *ly;
        const char *abc;
    } keys[] = {
        {10, MODE_MAJOR, "\\key bes \\major", "K:Bb\n"},
        {0, MODE_MINOR, "\\key c \\minor", "K:Cm\n"},
        {2, MODE_DORIAN, "\\key d \\dorian", "K:Ddor\n"},
        {4, MODE_PHRYGIAN, "\\key e \\phrygian", "K:Ephr\n"},
        {5, MODE_LYDIAN, "\\key f \\lydian", "K:Flyd\n"},
        {7, MODE_MIXOLYDIAN, "\\key g \\mixolydian", "K:Gmix\n"},
        {11, MODE_LOCRIAN, "\\key b \\locrian", "K:Bloc\n"},
        {6, MODE_MAJOR, "\\key fis \\major", "K:F#\n"},
        {5, MODE_LOCRIAN, "\\key f \\locrian", "K:Floc\n"},
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        s.key[0] = key_id(keys[i].tonic, keys[i].mode);
        CHECK(export_lilypond("output/tests/key.ly", &s));
        CHECK(export_abc("output/tests/key.abc", &s));
        char *kly = test_slurp("output/tests/key.ly", NULL);
        char *kabc = test_slurp("output/tests/key.abc", NULL);
        CHECK(strstr(kly, keys[i].ly) != NULL);
        CHECK(strstr(kabc, keys[i].abc) != NULL);
        free(kly);
        free(kabc);
    }

    /* in B-flat major an E natural needs its sign, an E-flat does not */
    static const int flat[1][8][3] = {{{0, 1, 63}, {1, 1, 64}, {2, 2, 70}}};
    static const int three[1] = {3};
    build(&s, 1, 4, flat, three);
    s.key[0] = key_id(10, MODE_MAJOR);
    CHECK(export_abc("output/tests/flat.abc", &s));
    char *fabc = test_slurp("output/tests/flat.abc", NULL);
    CHECK(strstr(fabc, "E =E B2 |]\n") != NULL);
    free(fabc);
    CHECK(export_lilypond("output/tests/flat.ly", &s));
    char *fly = test_slurp("output/tests/flat.ly", NULL);
    CHECK(strstr(fly, "ees'4 e'4 bes'2 \\bar") != NULL);
    free(fly);

    /* a final rest runs on to the end of the last bar */
    static const int ending[1][8][3] = {{{0, 2, 60}, {2, 3, SOUND_REST}}};
    static const int two[1] = {2};
    build(&s, 1, 5, ending, two);
    s.key[0] = key_id(0, MODE_MAJOR);
    CHECK(export_lilypond("output/tests/ending.ly", &s));
    char *ely = test_slurp("output/tests/ending.ly", NULL);
    CHECK(strstr(ely, "c'2 r2 |\n      r1 \\bar") != NULL);
    CHECK(ly_bars(ely) == 2);
    free(ely);

    CHECK(!export_lilypond(NULL, &s));
    CHECK(!export_abc("output/tests/none.abc", NULL));
}

/* On the eighth grid a step is an eighth and a bar eight steps: LilyPond
 * writes 8, 4., 2. and ties what no single value writes; ABC counts in
 * eighths (L:1/8). */
static void test_eighth_grid(void) {
    static const int notes[2][8][3] = {
        /* eighth, dotted quarter off the beat, five eighths over the barline, half */
        {{0, 1, 60}, {1, 3, 62}, {4, 5, 64}, {9, 4, 65}},
        /* seven eighths, an eighth, five eighths */
        {{0, 7, 55}, {7, 1, 57}, {8, 5, 59}},
    };
    static const int counts[2] = {4, 3};
    Score s;
    build(&s, 2, 13, notes, counts);
    s.beat_steps = 2;
    s.key[0] = key_id(0, MODE_MAJOR);
    test_output_dir();

    CHECK(export_lilypond("output/tests/eighths.ly", &s));
    char *ly = test_slurp("output/tests/eighths.ly", NULL);
    CHECK(strstr(ly, "\\time 4/4 \\tempo 4 = 90") != NULL);
    CHECK(strstr(ly, "\n      c'8 d'4. e'2~ |\n      e'8 f'2 r4. \\bar \"|.\"\n") != NULL);
    CHECK(strstr(ly, "\\clef bass") != NULL);
    CHECK(strstr(ly, "\n      g2.~ g8 a8 |\n      b2~ b8 r4. \\bar \"|.\"\n") != NULL);
    free(ly);

    CHECK(export_abc("output/tests/eighths.abc", &s));
    char *abc = test_slurp("output/tests/eighths.abc", NULL);
    CHECK(strstr(abc, "M:4/4\nL:1/8\nQ:1/4=90\n") != NULL);
    CHECK(strstr(abc, "\nV:1\nC D3 E4- |\nE F4 z3 |]\n") != NULL);
    CHECK(strstr(abc, "\nV:2\nG,6- G, A, |\nB,4- B, z3 |]\n") != NULL);
    free(abc);

    /* a whole bar's rest and a whole note */
    static const int whole[1][8][3] = {{{0, 8, SOUND_REST}, {8, 8, 72}}};
    static const int one[1] = {2};
    build(&s, 1, 16, whole, one);
    s.beat_steps = 2;
    s.key[0] = key_id(0, MODE_MAJOR);
    CHECK(export_lilypond("output/tests/whole.ly", &s));
    ly = test_slurp("output/tests/whole.ly", NULL);
    CHECK(strstr(ly, " r1 |\n      c''1 \\bar") != NULL);
    free(ly);
    CHECK(export_abc("output/tests/whole.abc", &s));
    abc = test_slurp("output/tests/whole.abc", NULL);
    CHECK(strstr(abc, "\nV:1\nz8 |\nc8 |]\n") != NULL);
    free(abc);
}

int main(void) {
    test_modulating();
    test_keys();
    test_eighth_grid();
    printf("ok\n");
    return 0;
}
