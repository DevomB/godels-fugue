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
/* How a pitch is written in a key: letter 0..6 (C..B), alteration in
 * semitones, and the written octave (B#3 sounds as C4). Scale notes take
 * the key's own letters (C# in D minor, E# in F# major); other notes use
 * sharps in sharp keys and flats in flat keys. key -1 means no key. */
void key_spell(int key, int pitch, int *letter, int *alter, int *octave);
void key_pitch_name(int key, int pitch, char *buf, size_t cap);

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
/* How many scale notes of key stay in the key after inversion around
 * axis and transposition by shift; and the axis nearest to `near` that
 * keeps all of them, or -1. */
int inversion_kept(int key, int axis, int shift);
int inversion_nearest_axis(int key, int near, int shift);
int key_scale_size(int key);
/* Moves a pitch `steps` notes along the key's seven-note scale (natural
 * minor for minor). A note outside it moves with the scale note below
 * it and keeps its distance from that note. */
int diatonic_shift(int key, int pitch, int steps);

int pitch_gravity(int pitch, int key);
int tension_target(int index, int length);
/* A tension point given as ?, and a slot past the end of the curve. */
enum { TENSION_FREE = -1, TENSION_UNLISTED = -2 };
/* Target tension of note index from a drawn curve. The points up to the
 * last listed one spread evenly from the first note to the last, and a
 * note between two given points takes the straight line between them,
 * rounded. A ? point is a position like any other, filled in from the
 * given points on either side, or the nearest one at either end. No
 * point given: the arch. */
int tension_curve(const int *points, int count, int index, int length);

#endif
