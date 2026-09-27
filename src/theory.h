#ifndef THEORY_H
#define THEORY_H

#include <stdbool.h>
#include <stddef.h>

/* Modes share one key id space: key = mode * 12 + tonic pitch class.
 * MODE_MINOR is natural minor plus the raised leading tone, so both
 * B-flat and B are in C minor and V is a major triad. */
enum {
    MODE_MAJOR,
    MODE_MINOR,
    MODE_DORIAN,
    MODE_PHRYGIAN,
    MODE_LYDIAN,
    MODE_MIXOLYDIAN,
    MODE_LOCRIAN,
    MODE_COUNT
};

enum { KEY_COUNT = 12 * MODE_COUNT };

/* Scale degrees, used as chord variable values. */
enum { DEGREE_I, DEGREE_II, DEGREE_III, DEGREE_IV, DEGREE_V, DEGREE_VI,
       DEGREE_VII, DEGREE_COUNT };

int pitch_class(int pitch);
int key_id(int tonic, int mode);
int key_tonic(int key);
int key_mode(int key);
bool key_valid(int key);
bool key_has_pitch(int key, int pitch);
int key_scale_mask(int key);
int key_triad_mask(int key, int degree);
bool key_triad_has(int key, int degree, int pitch);
int key_fifths(int key);
int key_distance(int a, int b);
bool keys_closely_related(int a, int b);
void key_name(int key, char *buf, size_t cap);
void degree_name(int key, int degree, char *buf, size_t cap);
bool progression_allowed(int from, int to);

const char *mode_name(int mode);
int parse_mode(const char *name);
int parse_tonic(const char *name);
const char *tonic_name(int pc, bool flats);
void pitch_name(int pitch, bool flats, char *buf, size_t cap);

int is_strong_time(int t, int poly_meter);
int interval_class(int a, int b);
bool same_direction(int delta_a, int delta_b);
bool is_parallel_fifth(int v0_prev, int v1_prev, int v0_now, int v1_now);
bool is_parallel_octave(int v0_prev, int v1_prev, int v0_now, int v1_now);
bool is_direct_perfect(int v0_prev, int v1_prev, int v0_now, int v1_now);
bool leap_exceeds(int a, int b, int max_leap);
bool sonority_consonant(const int *pitches, int n, bool allow_fourth,
                        bool allow_unison);
int dissonance_grade(int a, int b);

int invert_pitch(int axis, int pitch);
int invert_pitch_mod12(int axis, int pitch);

int pitch_gravity(int pitch, int key);
int tension_target(int index, int length);

#endif
