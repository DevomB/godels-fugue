#include "parts.h"

#include "canon.h"
#include "score.h"
#include "theory.h"

#include <stdio.h>
#include <string.h>

/* Names, General MIDI programs, clefs, written transpositions, sounding
 * ranges, and the soundfont and level the score page plays each with (the
 * MusyngKite soundfonts are recorded quietly and unevenly; the levels were
 * matched by ear). General MIDI has no euphonium: it borrows the tuba's
 * program and samples. */
static const Part parts[PART_COUNT] = {
    {"auto", NULL, 0, 0, 0, 0, NULL, 0, 127, NULL, 0, 0},
    {"flute", "Flute", 73, 'G', 0, 0, NULL, 60, 96, "flute", 500, 0},
    {"oboe", "Oboe", 68, 'G', 0, 0, NULL, 58, 91, "oboe", 500, 0},
    {"clarinet", "Clarinet in Bb", 71, 'G', 2, 1, "bes", 50, 94, "clarinet", 500, 0},
    {"bassoon", "Bassoon", 70, 'F', 0, 0, NULL, 34, 75, "bassoon", 500, 0},
    {"soprano_sax", "Soprano Sax in Bb", 64, 'G', 2, 1, "bes", 56, 87, "soprano_sax", 500, 0},
    {"alto_sax", "Alto Sax in Eb", 65, 'G', 9, 5, "ees", 49, 80, "alto_sax", 500, 0},
    {"tenor_sax", "Tenor Sax in Bb", 66, 'G', 14, 8, "bes,", 44, 75, "tenor_sax", 500, 0},
    {"baritone_sax", "Baritone Sax in Eb", 67, 'G', 21, 12, "ees,", 36, 68, "baritone_sax", 500,
     0},
    {"trumpet", "Trumpet in Bb", 56, 'G', 2, 1, "bes", 52, 84, "trumpet", 500, 0},
    {"horn", "Horn in F", 60, 'G', 7, 4, "f", 35, 77, "french_horn", 500, 0},
    {"trombone", "Trombone", 57, 'F', 0, 0, NULL, 40, 72, "trombone", 500, 0},
    {"euphonium", "Euphonium", 58, 'F', 0, 0, NULL, 34, 72, "tuba", 500, 0},
    {"tuba", "Tuba", 58, 'F', 0, 0, NULL, 28, 65, "tuba", 500, 0},
    {"violin", "Violin", 40, 'G', 0, 0, NULL, 55, 100, "violin", 500, 0},
    {"viola", "Viola", 41, 'C', 0, 0, NULL, 48, 88, "viola", 500, 0},
    {"cello", "Cello", 42, 'F', 0, 0, NULL, 36, 76, "cello", 500, 0},
    {"double_bass", "Double Bass", 43, 'F', 12, 7, "c", 28, 67, "contrabass", 500, 0},
    {"piano", "Piano", 0, 0, 0, 0, NULL, 21, 108, "piano", 240, 1},
    {"harpsichord", "Harpsichord", 6, 0, 0, 0, NULL, 29, 89, "harpsichord", 500, 1},
    {"organ", "Organ", 19, 0, 0, 0, NULL, 24, 96, "church_organ", 240, 0},
    {"harp", "Harp", 46, 0, 0, 0, NULL, 24, 103, "orchestral_harp", 360, 1},
    {"vibraphone", "Vibraphone", 11, 'G', 0, 0, NULL, 53, 89, "vibraphone", 650, 1},
    {"marimba", "Marimba", 12, 0, 0, 0, NULL, 45, 96, "marimba", 650, 1},
};

/* Each ensemble's instruments from the highest to the lowest. */
static const unsigned char ensembles[ENSEMBLE_COUNT][4] = {
    {PART_NONE, PART_NONE, PART_NONE, PART_NONE},
    {PART_FLUTE, PART_VIOLIN, PART_CLARINET, PART_CELLO},
    {PART_VIOLIN, PART_VIOLIN, PART_VIOLA, PART_CELLO},
    {PART_FLUTE, PART_OBOE, PART_CLARINET, PART_BASSOON},
    {PART_SOPRANO_SAX, PART_ALTO_SAX, PART_TENOR_SAX, PART_BARITONE_SAX},
    {PART_TRUMPET, PART_HORN, PART_TROMBONE, PART_EUPHONIUM},
    {PART_FLUTE, PART_ALTO_SAX, PART_EUPHONIUM, PART_BARITONE_SAX},
    {PART_FLUTE, PART_HORN, PART_CLARINET, PART_CELLO},
    {PART_PIANO, PART_PIANO, PART_PIANO, PART_PIANO},
    {PART_HARPSICHORD, PART_HARPSICHORD, PART_HARPSICHORD, PART_HARPSICHORD},
    {PART_ORGAN, PART_ORGAN, PART_ORGAN, PART_ORGAN},
    {PART_HARP, PART_HARP, PART_HARP, PART_HARP},
    {PART_VIBRAPHONE, PART_MARIMBA, PART_VIBRAPHONE, PART_MARIMBA},
};

/* Which of an ensemble's four instruments 1, 2, 3 or 4 voices take: two
 * voices the outer ones, three a trio from the top, the third and the bass
 * (flute, clarinet, cello; violin, viola, cello). */
static const int pick[VOICE_MAX][VOICE_MAX] = {{0}, {0, 3}, {0, 2, 3}, {0, 1, 2, 3}};

const Part *part_get(int id) {
    return id > PART_NONE && id < PART_COUNT ? &parts[id] : NULL;
}

int part_find(const char *word) {
    for (int id = 0; id < PART_COUNT; id++) {
        if (strcmp(parts[id].id, word) == 0) return id;
    }
    return -1;
}

/* A voice's place in the register, in semitones: its transposition (a
 * diatonic one counted at about a whole step and a half per two steps). */
static int voice_register(const PieceConfig *c, int v) {
    int t = canon_transpose(c, v);
    return c->diatonic ? t * 12 / 7 : t;
}

void parts_for_config(const PieceConfig *c, int part[VOICE_MAX]) {
    int n = config_voice_count(c);
    for (int v = 0; v < VOICE_MAX; v++) part[v] = PART_NONE;
    int ensemble = c->ensemble > ENSEMBLE_NONE && c->ensemble < ENSEMBLE_COUNT ? c->ensemble : 0;
    if (ensemble != ENSEMBLE_NONE) {
        int order[VOICE_MAX];
        for (int v = 0; v < n; v++) {
            int j = v;
            while (j > 0 && voice_register(c, order[j - 1]) < voice_register(c, v)) {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = v;
        }
        for (int rank = 0; rank < n; rank++) part[order[rank]] = ensembles[ensemble][pick[n - 1][rank]];
    }
    for (int v = 0; v < n; v++) {
        if (c->part[v] > PART_NONE && c->part[v] < PART_COUNT) part[v] = c->part[v];
    }
}

void part_range(const PieceConfig *c, int v, int *low, int *high) {
    int part[VOICE_MAX];
    parts_for_config(c, part);
    *low = c->range_low;
    *high = c->range_high;
    const Part *p = v >= 0 && v < VOICE_MAX ? part_get(part[v]) : NULL;
    if (p != NULL && p->low > *low) *low = p->low;
    if (p != NULL && p->high < *high) *high = p->high;
    if (v >= 0 && v < VOICE_MAX && c->part_low[v] > *low) *low = c->part_low[v];
    if (v >= 0 && v < VOICE_MAX && c->part_high[v] > 0 && c->part_high[v] < *high)
        *high = c->part_high[v];
}

static void voice_name(const PieceConfig *c, int v, char *buf, size_t cap) {
    int part[VOICE_MAX];
    parts_for_config(c, part);
    const Part *p = part_get(part[v]);
    if (p != NULL) {
        snprintf(buf, cap, "voice %d (%s)", v + 1, p->name);
    } else {
        snprintf(buf, cap, "voice %d", v + 1);
    }
}

bool parts_check(const PieceConfig *c, char *err, size_t cap) {
    int n = config_voice_count(c);
    int low[VOICE_MAX];
    int high[VOICE_MAX];
    for (int v = 0; v < n; v++) {
        part_range(c, v, &low[v], &high[v]);
        if (low[v] <= high[v]) continue;
        char name[48];
        voice_name(c, v, name, sizeof(name));
        snprintf(err, cap,
                 "%s has no pitch it may play: range_low..range_high is %d..%d and its own "
                 "range narrows that to nothing; widen the range or pick another instrument",
                 name, c->range_low, c->range_high);
        return false;
    }
    /* each voice plays the melody shifted by its transposition, so the
     * melody's pitches must fit every voice's range shifted back; an
     * inversion or a diatonic transposition maps pitches unevenly, and the
     * solver's range rule judges those */
    if (c->invert || c->diatonic) return true;
    for (int a = 0; a < n; a++) {
        for (int b = a + 1; b < n; b++) {
            int ta = canon_transpose(c, a);
            int tb = canon_transpose(c, b);
            int lo = low[a] - ta > low[b] - tb ? low[a] - ta : low[b] - tb;
            int hi = high[a] - ta < high[b] - tb ? high[a] - ta : high[b] - tb;
            if (lo <= hi) continue;
            char na[48];
            char nb[48];
            voice_name(c, a, na, sizeof(na));
            voice_name(c, b, nb, sizeof(nb));
            snprintf(err, cap,
                     "%s (%d..%d) and %s (%d..%d, playing %d semitones %s it) cannot play the "
                     "same melody: no note fits both; change a transposition, an instrument or "
                     "a range",
                     na, low[a], high[a], nb, low[b], high[b], tb - ta > 0 ? tb - ta : ta - tb,
                     tb >= ta ? "above" : "below");
            return false;
        }
    }
    return true;
}

const Part *score_part(const Score *score, int v) {
    return v >= 0 && v < VOICE_MAX ? part_get(score->part[v]) : NULL;
}

int score_written_up(const Score *score, int v) {
    const Part *p = score_part(score, v);
    return p != NULL && !score->concert ? p->up : 0;
}

int score_written_key(const Score *score, int v, int key) {
    int up = score_written_up(score, v);
    if (up % 12 == 0 || !key_valid(key)) return key;
    return key_id(key_tonic(key) + up, key_mode(key));
}

static double mean_pitch(const Score *score, int v) {
    long sum = 0;
    int n = 0;
    for (int t = 0; t < score->span; t++) {
        if (score->line[v][t] == SOUND_REST) continue;
        sum += score->line[v][t];
        n++;
    }
    return n > 0 ? (double)sum / n : 0.0;
}

char score_clef(const Score *score, int v) {
    const Part *p = score_part(score, v);
    /* a part written an octave or more above the sound in treble clef (the
     * tenor and baritone saxes) reads bass clef at concert pitch */
    if (p != NULL && p->clef == 'G' && p->up >= 12 && score->concert) return 'F';
    if (p != NULL && p->clef != 0) return p->clef;
    double mean = mean_pitch(score, v);
    return mean > 0.0 && (long)mean < 57 ? 'F' : 'G';
}

void score_part_name(const Score *score, int v, char *buf, size_t cap) {
    const Part *p = score_part(score, v);
    if (p != NULL) {
        snprintf(buf, cap, "%s", p->name);
    } else {
        snprintf(buf, cap, "Voice %d", v + 1);
    }
}

int score_out_of_range(const Score *score, int v) {
    const Part *p = score_part(score, v);
    if (p == NULL || v >= score->voices) return 0;
    int out = 0;
    const ScoreVoice *voice = &score->voice[v];
    for (int k = 0; k < voice->count; k++) {
        int pitch = voice->notes[k].pitch;
        if (pitch != SOUND_REST && (pitch < p->low || pitch > p->high)) out++;
    }
    return out;
}
