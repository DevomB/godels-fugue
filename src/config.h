#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

enum { CONSONANCE_OFF, CONSONANCE_STRONG, CONSONANCE_ALL };
enum { ORDER_MRV, ORDER_ENTROPY, ORDER_COLLAPSE, ORDER_INDEX };
enum { INSTRUMENT_PLUCK, INSTRUMENT_ORGAN, INSTRUMENT_SINE, INSTRUMENT_COUNT };
enum { GRID_QUARTER, GRID_EIGHTH, GRID_SIXTEENTH, GRID_COUNT };
/* The instruments the score files are written for (parts.c gives each voice
 * one), and whether transposing instruments are written in their own key. */
enum {
    ENSEMBLE_NONE,
    ENSEMBLE_CHAMBER,
    ENSEMBLE_STRINGS,
    ENSEMBLE_WINDS,
    ENSEMBLE_SAXES,
    ENSEMBLE_BRASS,
    ENSEMBLE_BAND,
    ENSEMBLE_ORCHESTRA,
    ENSEMBLE_PIANO,
    ENSEMBLE_HARPSICHORD,
    ENSEMBLE_ORGAN,
    ENSEMBLE_HARP,
    ENSEMBLE_MALLETS,
    ENSEMBLE_COUNT
};
enum { WRITTEN_TRANSPOSED, WRITTEN_CONCERT, WRITTEN_COUNT };
/* How the score page plays a piece (mood key); auto guesses from key and tempo. */
enum {
    MOOD_AUTO,
    MOOD_PLAIN,
    MOOD_LAMENT,
    MOOD_HYMN,
    MOOD_TRIUMPH,
    MOOD_LONGING,
    MOOD_DANCE,
    MOOD_NOCTURNE,
    MOOD_COUNT
};
enum { KEY_SEARCH = -1 };
enum { TRANSPOSE_SAME = -128 };

/* Every field is an int so one table can load, check and print them.
 * See config.c for ranges, defaults and the help text of each key. */
typedef struct PieceConfig {
    /* shape */
    int length;
    int grid;
    int voices;
    int delay;
    int voice_delay[VOICE_MAX]; /* 0 = voice * delay */
    int phase;
    int range_low;
    int range_high;
    /* canon transforms */
    int transpose;
    int voice_transpose[VOICE_MAX]; /* TRANSPOSE_SAME = transpose */
    int diatonic;
    int invert;
    int axis;
    int invert_mod12;
    int retrograde;
    int augment;
    int diminish;
    int cyclic;
    /* keys */
    int key;
    int mode;
    int modulate_at;
    int key_second;
    int mode_second;
    /* hard rules */
    int max_leap;
    int leading_tone;
    int double_leaps;
    int consonance;
    int allow_fourth;
    int allow_unison;
    int parallels;
    int max_spacing;
    int crossing;
    int harmony;
    int progression;
    int cadence;
    int poly_meter;
    int mirror;
    int mirror_axis;
    /* rhythm */
    int rhythm;
    int max_hold;
    int rest_at;
    int max_rests;
    /* given notes */
    int lock;
    int lock_index;
    int lock_pitch;
    int melody[MELODY_MAX]; /* pitch of each note, PITCH_REST, or -1 = free */
    /* soft rules */
    int energy;
    int temperature;
    int seed;
    int anneal_start;
    int anneal_end;
    int anneal_steps;
    int anneal_ratio;
    int w_gravity;
    int w_curve;
    int tension[MELODY_MAX]; /* curve points, TENSION_FREE (?) or TENSION_UNLISTED */
    int w_leap;
    int w_step;
    int w_arc;
    int climax;
    int w_repeat;
    int w_recover;
    int w_dissonance;
    int w_parallel;
    int w_contrary;
    int w_motif;
    int motif_a;
    int motif_b;
    int motif_c;
    int motif_d;
    int w_sequence;
    int w_modulate;
    int w_harmony;
    int w_rest;
    int w_hold;
    int w_syncopation;
    int w_rhythm;
    int w_run;
    int w_figure;
    int w_final;
    int w_corpus;
    int pc_weight[12];
    /* search */
    int var_order;
    int hierarchy;
    int backjump;
    int learn;
    int optimize;
    int max_nodes;
    int time_limit;
    int delay_search;
    int delay_min;
    int delay_max;
    /* output */
    int tempo;
    int instrument;
    int ensemble;
    int written;
    int mood;
} PieceConfig;

void config_defaults(PieceConfig *config);

/* Set one key from its text form. Array keys take a comma- or
 * space-separated list. Returns false and fills err on failure. */
bool config_set(PieceConfig *config, const char *key, const char *value, char *err,
                size_t cap);
/* "key=value" form used by --set. */
bool config_assign(PieceConfig *config, const char *assignment, char *err, size_t cap);
bool config_apply_preset(PieceConfig *config, const char *name, char *err, size_t cap);
/* Text files hold "key value" lines ("#" starts a comment); a file
 * whose name ends in .json holds one object of key/value members.
 * The key "preset" applies a preset in place. */
bool config_load_file(PieceConfig *config, const char *path, char *err, size_t cap);
bool config_validate(const PieceConfig *config, char *err, size_t cap);

/* Every key and its current value, in the text format. */
void config_write(FILE *f, const PieceConfig *config);
int config_key_count(void);
const char *config_key_name(int index);
/* Text form of one key's value; arrays are comma-separated. The longest,
 * a melody of MELODY_MAX rests, fits in CONFIG_VALUE_MAX characters. */
enum { CONFIG_VALUE_MAX = 5 * MELODY_MAX };
void config_key_value(const PieceConfig *config, int index, char *buf, size_t cap);
/* Reference of every key: plain text, or a Markdown table. */
void config_print_reference(FILE *f, bool markdown);
void config_print_presets(FILE *f);
int config_preset_count(void);
const char *config_preset_name(int index);

int config_voice_count(const PieceConfig *config);
/* Steps in one quarter-note beat (1, 2 on the eighth grid, 4 on the
 * sixteenth grid) and in one 4/4 bar. */
int config_beat_steps(const PieceConfig *config);
int config_bar_steps(const PieceConfig *config);

#endif
