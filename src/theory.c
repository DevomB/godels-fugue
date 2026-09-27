#include "theory.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int pitch_class(int pitch) {
    int pc = pitch % 12;
    return pc < 0 ? pc + 12 : pc;
}

static int pc_mask(const int *pcs, int n) {
    int mask = 0;
    for (int i = 0; i < n; i++) mask |= 1 << pcs[i];
    return mask;
}

/* Seven degrees per mode, tonic first. Minor lists natural minor here;
 * the raised leading tone is added to its scale mask and triads. */
static const int mode_steps[MODE_COUNT][7] = {
    {0, 2, 4, 5, 7, 9, 11}, /* major */
    {0, 2, 3, 5, 7, 8, 10}, /* minor */
    {0, 2, 3, 5, 7, 9, 10}, /* dorian */
    {0, 1, 3, 5, 7, 8, 10}, /* phrygian */
    {0, 2, 4, 6, 7, 9, 11}, /* lydian */
    {0, 2, 4, 5, 7, 9, 10}, /* mixolydian */
    {0, 1, 3, 5, 6, 8, 10}, /* locrian */
};

/* Semitones from the parent major tonic up to the mode's tonic. */
static const int mode_offset[MODE_COUNT] = {0, 9, 2, 4, 5, 7, 11};

static const char *const mode_names[MODE_COUNT] = {
    "major", "minor", "dorian", "phrygian", "lydian", "mixolydian", "locrian"};

int key_id(int tonic, int mode) {
    if (mode < 0 || mode >= MODE_COUNT) return -1;
    return mode * 12 + pitch_class(tonic);
}

int key_tonic(int key) {
    return key % 12;
}

int key_mode(int key) {
    return key / 12;
}

bool key_valid(int key) {
    return key >= 0 && key < KEY_COUNT;
}

int key_scale_mask(int key) {
    int mode = key_mode(key);
    int tonic = key_tonic(key);
    int mask = 0;
    for (int d = 0; d < 7; d++) mask |= 1 << pitch_class(tonic + mode_steps[mode][d]);
    if (mode == MODE_MINOR) mask |= 1 << pitch_class(tonic + 11);
    return mask;
}

bool key_has_pitch(int key, int pitch) {
    return (key_scale_mask(key) >> pitch_class(pitch)) & 1;
}

/* Root, third and fifth of a diatonic triad. Minor takes V and vii from
 * harmonic minor, so V is major and vii is diminished. */
static void triad_pcs(int key, int degree, int pcs[3]) {
    int mode = key_mode(key);
    int tonic = key_tonic(key);
    if (mode == MODE_MINOR && (degree == DEGREE_V || degree == DEGREE_VII)) {
        static const int raised[2][3] = {{7, 11, 2}, {11, 2, 5}};
        const int *t = raised[degree == DEGREE_V ? 0 : 1];
        for (int i = 0; i < 3; i++) pcs[i] = pitch_class(tonic + t[i]);
        return;
    }
    for (int i = 0; i < 3; i++)
        pcs[i] = pitch_class(tonic + mode_steps[mode][(degree + 2 * i) % 7]);
}

int key_triad_mask(int key, int degree) {
    if (degree < 0 || degree >= DEGREE_COUNT) return 0;
    int pcs[3];
    triad_pcs(key, degree, pcs);
    return pc_mask(pcs, 3);
}

bool key_triad_has(int key, int degree, int pitch) {
    return (key_triad_mask(key, degree) >> pitch_class(pitch)) & 1;
}

int key_fifths(int key) {
    static const int major_fifths[12] = {0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5};
    int parent = pitch_class(key_tonic(key) - mode_offset[key_mode(key)]);
    return major_fifths[parent];
}

int key_distance(int a, int b) {
    int d = key_fifths(a) - key_fifths(b);
    if (d < 0) d = -d;
    if (d > 6) d = 12 - d;
    return d;
}

/* Keys whose signatures differ by at most one accidental. */
bool keys_closely_related(int a, int b) {
    return a != b && key_distance(a, b) <= 1;
}

const char *mode_name(int mode) {
    if (mode < 0 || mode >= MODE_COUNT) return "?";
    return mode_names[mode];
}

int parse_mode(const char *name) {
    for (int m = 0; m < MODE_COUNT; m++) {
        if (strcmp(name, mode_names[m]) == 0) return m;
    }
    if (strcmp(name, "ionian") == 0) return MODE_MAJOR;
    if (strcmp(name, "aeolian") == 0) return MODE_MINOR;
    return -1;
}

int parse_tonic(const char *name) {
    static const int letters[7] = {9, 11, 0, 2, 4, 5, 7}; /* A..G */
    int c = toupper((unsigned char)name[0]);
    if (c < 'A' || c > 'G') return -1;
    int pc = letters[c - 'A'];
    const char *p = name + 1;
    for (; *p != '\0'; p++) {
        if (*p == '#' || *p == 's') {
            pc++;
        } else if (*p == 'b') {
            pc--;
        } else {
            return -1;
        }
    }
    return pitch_class(pc);
}

const char *tonic_name(int pc, bool flats) {
    static const char *const sharp_names[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                "F#", "G",  "G#", "A",  "A#", "B"};
    static const char *const flat_names[12] = {"C",  "Db", "D",  "Eb", "E",  "F",
                                               "Gb", "G",  "Ab", "A",  "Bb", "B"};
    return flats ? flat_names[pitch_class(pc)] : sharp_names[pitch_class(pc)];
}

void key_name(int key, char *buf, size_t cap) {
    if (!key_valid(key)) {
        snprintf(buf, cap, "?");
        return;
    }
    snprintf(buf, cap, "%s %s", tonic_name(key_tonic(key), key_fifths(key) < 0),
             mode_name(key_mode(key)));
}

void pitch_name(int pitch, bool flats, char *buf, size_t cap) {
    snprintf(buf, cap, "%s%d", tonic_name(pitch, flats), pitch / 12 - 1);
}

/* Roman numeral from the triad's quality: upper case for major,
 * lower case for minor, a trailing o for diminished, + for augmented. */
void degree_name(int key, int degree, char *buf, size_t cap) {
    static const char *const upper[DEGREE_COUNT] = {"I", "II", "III", "IV",
                                                    "V", "VI", "VII"};
    static const char *const lower[DEGREE_COUNT] = {"i", "ii", "iii", "iv",
                                                    "v", "vi", "vii"};
    if (!key_valid(key) || degree < 0 || degree >= DEGREE_COUNT) {
        snprintf(buf, cap, "?");
        return;
    }
    int pcs[3];
    triad_pcs(key, degree, pcs);
    int third = pitch_class(pcs[1] - pcs[0]);
    int fifth = pitch_class(pcs[2] - pcs[0]);
    snprintf(buf, cap, "%s%s", third == 4 ? upper[degree] : lower[degree],
             fifth == 6 ? "o" : (fifth == 8 ? "+" : ""));
}

/* Usual root progressions between consecutive bars. Repeating a chord
 * is always allowed; I may go anywhere. */
bool progression_allowed(int from, int to) {
    static const unsigned char next[DEGREE_COUNT] = {
        0x7F,                                                   /* I -> any */
        (1 << DEGREE_V) | (1 << DEGREE_VII),                    /* ii */
        (1 << DEGREE_VI) | (1 << DEGREE_IV),                    /* iii */
        (1 << DEGREE_V) | (1 << DEGREE_VII) | (1 << DEGREE_I) |
            (1 << DEGREE_II),                                   /* IV */
        (1 << DEGREE_I) | (1 << DEGREE_VI),                     /* V */
        (1 << DEGREE_II) | (1 << DEGREE_IV) | (1 << DEGREE_V) |
            (1 << DEGREE_III),                                  /* vi */
        (1 << DEGREE_I),                                        /* vii */
    };
    if (from < 0 || from >= DEGREE_COUNT || to < 0 || to >= DEGREE_COUNT)
        return false;
    return from == to || ((next[from] >> to) & 1);
}

int is_strong_time(int t, int poly_meter) {
    if (t % 4 == 0) return 1;
    if (poly_meter && t % 3 == 0) return 1;
    return 0;
}

int interval_class(int a, int b) {
    int delta = a - b;
    if (delta < 0) delta = -delta;
    return delta % 12;
}

bool same_direction(int delta_a, int delta_b) {
    if (delta_a == 0 || delta_b == 0) return false;
    return (delta_a > 0 && delta_b > 0) || (delta_a < 0 && delta_b < 0);
}

static bool parallel_interval(int v0_prev, int v1_prev, int v0_now, int v1_now,
                              int interval) {
    if (interval_class(v0_prev, v1_prev) != interval) return false;
    if (interval_class(v0_now, v1_now) != interval) return false;
    return same_direction(v0_now - v0_prev, v1_now - v1_prev);
}

bool is_parallel_fifth(int v0_prev, int v1_prev, int v0_now, int v1_now) {
    return parallel_interval(v0_prev, v1_prev, v0_now, v1_now, 7);
}

bool is_parallel_octave(int v0_prev, int v1_prev, int v0_now, int v1_now) {
    return parallel_interval(v0_prev, v1_prev, v0_now, v1_now, 0);
}

/* Similar motion into a perfect fifth or octave ("hidden" perfects). */
bool is_direct_perfect(int v0_prev, int v1_prev, int v0_now, int v1_now) {
    int ic = interval_class(v0_now, v1_now);
    if (ic != 0 && ic != 7) return false;
    return same_direction(v0_now - v0_prev, v1_now - v1_prev);
}

bool leap_exceeds(int a, int b, int max_leap) {
    int delta = a - b;
    if (delta < 0) delta = -delta;
    return delta > max_leap;
}

/* Every pair must be an octave, third, fifth or sixth. A fourth is
 * consonant between two upper voices but not against the lowest note,
 * unless allow_fourth is set. A unison needs allow_unison. */
bool sonority_consonant(const int *pitches, int n, bool allow_fourth,
                        bool allow_unison) {
    int low = -1;
    for (int i = 0; i < n; i++) {
        if (low < 0 || pitches[i] < low) low = pitches[i];
    }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int a = pitches[i];
            int b = pitches[j];
            if (a == b) {
                if (!allow_unison) return false;
                continue;
            }
            switch (interval_class(a, b)) {
            case 0:
            case 3:
            case 4:
            case 7:
            case 8:
            case 9:
                break;
            case 5:
                if (!allow_fourth && (a == low || b == low)) return false;
                break;
            default:
                return false;
            }
        }
    }
    return true;
}

/* 0 consonant, 1 fourth, 2 whole tone, minor seventh or tritone,
 * 3 semitone or major seventh. */
int dissonance_grade(int a, int b) {
    switch (interval_class(a, b)) {
    case 5:
        return 1;
    case 2:
    case 6:
    case 10:
        return 2;
    case 1:
    case 11:
        return 3;
    default:
        return 0;
    }
}

int invert_pitch(int axis, int pitch) {
    return 2 * axis - pitch;
}

int invert_pitch_mod12(int axis, int pitch) {
    int pc = pitch_class(2 * pitch_class(axis) - pitch_class(pitch));
    int result = (pitch / 12) * 12 + pc;
    while (result < 0) result += 12;
    while (result > 127) result -= 12;
    return result;
}

int key_scale_size(int key) {
    int n = 0;
    for (int mask = key_scale_mask(key); mask != 0; mask &= mask - 1) n++;
    return n;
}

int inversion_kept(int key, int axis, int shift) {
    int mask = key_scale_mask(key);
    int kept = 0;
    for (int pc = 0; pc < 12; pc++) {
        if (!((mask >> pc) & 1)) continue;
        if ((mask >> pitch_class(2 * axis - pc + shift)) & 1) kept++;
    }
    return kept;
}

int inversion_nearest_axis(int key, int near, int shift) {
    int size = key_scale_size(key);
    for (int d = 0; d < 128; d++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            int axis = near + sign * d;
            if (axis < 0 || axis > 127) continue;
            if (inversion_kept(key, axis, shift) == size) return axis;
            if (d == 0) break;
        }
    }
    return -1;
}

/* Tonal potential of a pitch in a key: tonic and mediant are stable,
 * the dominant nearly so, the leading tone and anything outside the
 * scale pull hardest. */
int pitch_gravity(int pitch, int key) {
    static const int by_step[12] = {0, 3, 2, 0, 0, 2, 3, 1, 2, 2, 2, 3};
    if (pitch < 0 || pitch > 127) return 4;
    if (!key_has_pitch(key, pitch)) return 4;
    return by_step[pitch_class(pitch - key_tonic(key))];
}

int tension_target(int index, int length) {
    static const int arch[12] = {0, 0, 1, 2, 3, 3, 2, 1, 1, 0, 0, 0};
    if (length <= 1) return 0;
    if (index < 0) index = 0;
    if (index > length - 1) index = length - 1;
    return arch[index * 11 / (length - 1)];
}
