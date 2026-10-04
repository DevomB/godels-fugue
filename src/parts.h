#ifndef PARTS_H
#define PARTS_H

#include "config.h"

#include <stddef.h>

/* An instrument a voice can be written for. A transposing instrument's part
 * is written `up` semitones (`steps` scale steps) above the sound: an alto
 * sax in E-flat reads a B where it sounds a D. */
typedef struct Part {
    const char *id;   /* the config word, e.g. "alto_sax" */
    const char *name; /* the part's heading, e.g. "Alto Sax in Eb" */
    int program;      /* General MIDI program, counted from 0 */
    char clef;        /* 'G' treble, 'C' alto, 'F' bass; 0 follows the voice's register */
    int up;
    int steps;
    const char *lily; /* the pitch LilyPond's \transposition names, NULL at concert pitch */
    int low;          /* the lowest and highest sounding pitch */
    int high;
    const char *sample; /* the General MIDI soundfont the score page plays */
    int gain;           /* percent: the soundfont's level relative to the others */
    int struck;         /* 1 for a struck or plucked instrument that rings on */
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
/* The part a config word names ("alto_sax"), or -1. */
int part_find(const char *word);

/* Each voice's instrument under the config: the one its `parts` entry
 * names, else the ensemble's, given out by register (the voice transposed
 * highest takes the ensemble's highest instrument; equal transpositions keep
 * voice order, so the assignment never depends on the notes). PART_NONE
 * without either. */
void parts_for_config(const PieceConfig *config, int part[VOICE_MAX]);
/* The sounding range voice v may use: range_low..range_high narrowed by its
 * instrument and by its part_low and part_high. low > high when nothing is
 * left. */
void part_range(const PieceConfig *config, int v, int *low, int *high);
/* Checks that every voice can play some melody note: false with a sentence
 * naming the voices when their ranges cannot meet (a canon without
 * inversion; the solver judges the rest). */
bool parts_check(const PieceConfig *config, char *err, size_t cap);

struct Score;
/* Voice v's instrument, or NULL. */
const Part *score_part(const struct Score *score, int v);
/* Semitones voice v is written above its sound: 0 without a transposing
 * instrument, or when the score is written at concert pitch. */
int score_written_up(const struct Score *score, int v);
/* The key voice v is written in where the music sounds in `key`. */
int score_written_key(const struct Score *score, int v, int key);
/* 'G', 'C' or 'F': the instrument's clef, else bass for a low voice. */
char score_clef(const struct Score *score, int v);
/* The instrument's name, or "Voice n". */
void score_part_name(const struct Score *score, int v, char *buf, size_t cap);
/* Voice v's notes outside its instrument's range; 0 without one. */
int score_out_of_range(const struct Score *score, int v);

#endif
