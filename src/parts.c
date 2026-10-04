#include "parts.h"

#include "config.h"
#include "theory.h"

#include <stdio.h>

/* Names, General MIDI programs, clefs, written transpositions and sounding
 * ranges. General MIDI has no euphonium: it borrows the tuba's program. */
static const Part parts[PART_COUNT] = {
    {NULL, 0, 0, 0, 0, NULL, 0, 127},
    {"Flute", 73, 'G', 0, 0, NULL, 60, 96},
    {"Oboe", 68, 'G', 0, 0, NULL, 58, 91},
    {"Clarinet in Bb", 71, 'G', 2, 1, "bes", 50, 94},
    {"Bassoon", 70, 'F', 0, 0, NULL, 34, 75},
    {"Soprano Sax in Bb", 64, 'G', 2, 1, "bes", 56, 87},
    {"Alto Sax in Eb", 65, 'G', 9, 5, "ees", 49, 80},
    {"Tenor Sax in Bb", 66, 'G', 14, 8, "bes,", 44, 75},
    {"Baritone Sax in Eb", 67, 'G', 21, 12, "ees,", 36, 68},
    {"Trumpet in Bb", 56, 'G', 2, 1, "bes", 52, 84},
    {"Horn in F", 60, 'G', 7, 4, "f", 35, 77},
    {"Trombone", 57, 'F', 0, 0, NULL, 40, 72},
    {"Euphonium", 58, 'F', 0, 0, NULL, 34, 72},
    {"Tuba", 58, 'F', 0, 0, NULL, 28, 65},
    {"Violin", 40, 'G', 0, 0, NULL, 55, 100},
    {"Viola", 41, 'C', 0, 0, NULL, 48, 88},
    {"Cello", 42, 'F', 0, 0, NULL, 36, 76},
    {"Double Bass", 43, 'F', 12, 7, "c", 28, 67},
    {"Piano", 0, 0, 0, 0, NULL, 21, 108},
    {"Harpsichord", 6, 0, 0, 0, NULL, 29, 89},
    {"Organ", 19, 0, 0, 0, NULL, 24, 96},
    {"Harp", 46, 0, 0, 0, NULL, 24, 103},
    {"Vibraphone", 11, 'G', 0, 0, NULL, 53, 89},
    {"Marimba", 12, 0, 0, 0, NULL, 45, 96},
};

/* Each ensemble's instruments from the highest to the lowest, as the score
 * page plays them. */
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

/* Which of an ensemble's four instruments 1, 2, 3 or 4 voices take: the
 * outer ones first, so two voices are the highest and the lowest. */
static const int pick[VOICE_MAX][VOICE_MAX] = {{0}, {0, 3}, {0, 1, 3}, {0, 1, 2, 3}};

const Part *part_get(int id) {
    return id > PART_NONE && id < PART_COUNT ? &parts[id] : NULL;
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

void parts_assign(Score *score, int ensemble, int written) {
    score->concert = written == WRITTEN_CONCERT;
    for (int v = 0; v < VOICE_MAX; v++) score->part[v] = PART_NONE;
    int n = score->voices < VOICE_MAX ? score->voices : VOICE_MAX;
    if (ensemble <= ENSEMBLE_NONE || ensemble >= ENSEMBLE_COUNT || n < 1) return;
    /* voices from the highest to the lowest; equal ones keep their order */
    int order[VOICE_MAX];
    double mean[VOICE_MAX];
    for (int v = 0; v < n; v++) {
        mean[v] = mean_pitch(score, v);
        order[v] = v;
    }
    for (int i = 1; i < n; i++) {
        int v = order[i];
        int j = i;
        while (j > 0 && mean[order[j - 1]] < mean[v]) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = v;
    }
    for (int rank = 0; rank < n; rank++)
        score->part[order[rank]] = ensembles[ensemble][pick[n - 1][rank]];
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

char score_clef(const Score *score, int v) {
    const Part *p = score_part(score, v);
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
