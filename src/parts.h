#ifndef PARTS_H
#define PARTS_H

#include "score.h"

#include <stddef.h>

/* An instrument a voice can be written for. A transposing instrument's part
 * is written `up` semitones (`steps` scale steps) above the sound: an alto
 * sax in E-flat reads a B where it sounds a D. */
typedef struct Part {
    const char *name; /* the part's heading, e.g. "Alto Sax in Eb" */
    int program;      /* General MIDI program, counted from 0 */
    char clef;        /* 'G' treble, 'C' alto, 'F' bass; 0 follows the voice's register */
    int up;
    int steps;
    const char *lily; /* the pitch LilyPond's \transposition names, NULL at concert pitch */
    int low;          /* the lowest and highest sounding pitch */
    int high;
} Part;

enum {
    PART_NONE,
    PART_FLUTE,
    PART_OBOE,
    PART_CLARINET,
    PART_BASSOON,
    PART_SOPRANO_SAX,
    PART_ALTO_SAX,
    PART_TENOR_SAX,
    PART_BARITONE_SAX,
    PART_TRUMPET,
    PART_HORN,
    PART_TROMBONE,
    PART_EUPHONIUM,
    PART_TUBA,
    PART_VIOLIN,
    PART_VIOLA,
    PART_CELLO,
    PART_DOUBLE_BASS,
    PART_PIANO,
    PART_HARPSICHORD,
    PART_ORGAN,
    PART_HARP,
    PART_VIBRAPHONE,
    PART_MARIMBA,
    PART_COUNT
};

/* NULL for PART_NONE or an unknown id. */
const Part *part_get(int id);

/* Gives each voice an instrument of the ensemble (ENSEMBLE_*), the voice
 * with the highest average sounding pitch the ensemble's highest, and
 * records whether transposing parts are written at concert pitch
 * (WRITTEN_*). ENSEMBLE_NONE leaves every voice without one. */
void parts_assign(Score *score, int ensemble, int written);

/* Voice v's instrument, or NULL. */
const Part *score_part(const Score *score, int v);
/* Semitones voice v is written above its sound: 0 without a transposing
 * instrument, or when the score is written at concert pitch. */
int score_written_up(const Score *score, int v);
/* The key voice v is written in where the music sounds in `key`. */
int score_written_key(const Score *score, int v, int key);
/* 'G', 'C' or 'F': the instrument's clef, else bass for a low voice. */
char score_clef(const Score *score, int v);
/* The instrument's name, or "Voice n". */
void score_part_name(const Score *score, int v, char *buf, size_t cap);
/* Voice v's notes outside its instrument's range; 0 without one. */
int score_out_of_range(const Score *score, int v);

#endif
