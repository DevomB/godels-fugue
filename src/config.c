#include "config.h"

#include "canon.h"
#include "json.h"
#include "theory.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef bool (*WordParser)(const char *word, int *out);
typedef void (*WordPrinter)(int value, char *buf, size_t cap);

typedef struct KeyDef {
    const char *name;
    size_t offset;
    int count; /* array length, 1 for a scalar; OPEN(n) allows fewer values */
    int min;
    int max;
    int def;
    WordParser parse; /* extra word values, or NULL */
    WordPrinter print;
    const char *group;
    const char *help;
} KeyDef;

/* An array key that may be given fewer than n values; the rest keep the default. */
#define OPEN(n) (-(n))

static int key_count(const KeyDef *def) {
    return def->count < 0 ? -def->count : def->count;
}

static bool key_open(const KeyDef *def) {
    return def->count < 0;
}

static bool parse_key_word(const char *w, int *out) {
    if (strcmp(w, "search") == 0) {
        *out = KEY_SEARCH;
        return true;
    }
    int pc = parse_tonic(w);
    if (pc < 0) return false;
    *out = pc;
    return true;
}

static void print_key_word(int v, char *buf, size_t cap) {
    snprintf(buf, cap, "%s", v < 0 ? "search" : tonic_name(v, false));
}

static bool parse_second_key_word(const char *w, int *out) {
    if (strcmp(w, "related") == 0) {
        *out = KEY_SEARCH;
        return true;
    }
    int pc = parse_tonic(w);
    if (pc < 0) return false;
    *out = pc;
    return true;
}

static void print_second_key_word(int v, char *buf, size_t cap) {
    snprintf(buf, cap, "%s", v < 0 ? "related" : tonic_name(v, false));
}

static bool parse_mode_word(const char *w, int *out) {
    if (strcmp(w, "search") == 0) {
        *out = KEY_SEARCH;
        return true;
    }
    int m = parse_mode(w);
    if (m < 0) return false;
    *out = m;
    return true;
}

static void print_mode_word(int v, char *buf, size_t cap) {
    snprintf(buf, cap, "%s", v < 0 ? "search" : mode_name(v));
}

static bool parse_second_mode_word(const char *w, int *out) {
    if (strcmp(w, "auto") == 0) {
        *out = KEY_SEARCH;
        return true;
    }
    int m = parse_mode(w);
    if (m < 0) return false;
    *out = m;
    return true;
}

static void print_second_mode_word(int v, char *buf, size_t cap) {
    snprintf(buf, cap, "%s", v < 0 ? "auto" : mode_name(v));
}

static const char *const consonance_words[] = {"off", "strong", "all"};
static const char *const order_words[] = {"mrv", "entropy", "collapse", "index"};

static bool parse_list_word(const char *w, const char *const *words, int n, int *out) {
    for (int i = 0; i < n; i++) {
        if (strcmp(w, words[i]) == 0) {
            *out = i;
            return true;
        }
    }
    return false;
}

static bool parse_consonance_word(const char *w, int *out) {
    return parse_list_word(w, consonance_words, 3, out);
}

static void print_consonance_word(int v, char *buf, size_t cap) {
    snprintf(buf, cap, "%s", v >= 0 && v < 3 ? consonance_words[v] : "?");
}

static bool parse_order_word(const char *w, int *out) {
    return parse_list_word(w, order_words, 4, out);
}

static void print_order_word(int v, char *buf, size_t cap) {
    snprintf(buf, cap, "%s", v >= 0 && v < 4 ? order_words[v] : "?");
}

static bool parse_off_word(const char *w, int *out) {
    if (strcmp(w, "off") != 0) return false;
    *out = -1;
    return true;
}

static void print_off_word(int v, char *buf, size_t cap) {
    if (v < 0) {
        snprintf(buf, cap, "off");
    } else {
        snprintf(buf, cap, "%d", v);
    }
}

static bool parse_same_word(const char *w, int *out) {
    if (strcmp(w, "same") != 0) return false;
    *out = TRANSPOSE_SAME;
    return true;
}

static void print_same_word(int v, char *buf, size_t cap) {
    if (v == TRANSPOSE_SAME) {
        snprintf(buf, cap, "same");
    } else {
        snprintf(buf, cap, "%d", v);
    }
}

static bool parse_note_word(const char *w, int *out) {
    if (strcmp(w, "?") == 0) {
        *out = -1;
        return true;
    }
    if (strcmp(w, "rest") == 0) {
        *out = PITCH_REST;
        return true;
    }
    return false;
}

static void print_note_word(int v, char *buf, size_t cap) {
    if (v < 0) {
        snprintf(buf, cap, "?");
    } else if (v == PITCH_REST) {
        snprintf(buf, cap, "rest");
    } else {
        snprintf(buf, cap, "%d", v);
    }
}

static bool parse_tension_word(const char *w, int *out) {
    if (strcmp(w, "?") == 0) {
        *out = TENSION_FREE;
        return true;
    }
    if (strcmp(w, "arch") == 0) {
        *out = TENSION_UNLISTED;
        return true;
    }
    return false;
}

static void print_tension_word(int v, char *buf, size_t cap) {
    if (v == TENSION_UNLISTED) {
        snprintf(buf, cap, "arch");
    } else if (v < 0) {
        snprintf(buf, cap, "?");
    } else {
        snprintf(buf, cap, "%d", v);
    }
}

static bool parse_motif_word(const char *w, int *out) {
    if (strcmp(w, "off") != 0) return false;
    *out = -128;
    return true;
}

static void print_motif_word(int v, char *buf, size_t cap) {
    if (v == -128) {
        snprintf(buf, cap, "off");
    } else {
        snprintf(buf, cap, "%d", v);
    }
}

#define F(field) offsetof(PieceConfig, field)
#define VOICE_DELAY(v) (offsetof(PieceConfig, voice_delay) + (v) * sizeof(int))
#define VOICE_TRANSPOSE(v) (offsetof(PieceConfig, voice_transpose) + (v) * sizeof(int))

static const KeyDef keys[] = {
    {"length", F(length), 1, 1, MELODY_MAX, 12, NULL, NULL, "shape",
     "Melody length in steps. One step is a quarter note; four steps make a bar."},
    {"voices", F(voices), 1, 2, VOICE_MAX, 2, NULL, NULL, "shape",
     "Number of voices. Voice 1 plays the melody; the others follow it."},
    {"delay", F(delay), 1, 0, SPAN_MAX, 4, NULL, NULL, "shape",
     "Steps between entries: voice v enters after v * delay steps."},
    {"delay_1", VOICE_DELAY(1), 1, 0, SPAN_MAX, 0, NULL, NULL, "shape",
     "Entry step of voice 2 (0 = 1 * delay)."},
    {"delay_2", VOICE_DELAY(2), 1, 0, SPAN_MAX, 0, NULL, NULL, "shape",
     "Entry step of voice 3 (0 = 2 * delay)."},
    {"delay_3", VOICE_DELAY(3), 1, 0, SPAN_MAX, 0, NULL, NULL, "shape",
     "Entry step of voice 4 (0 = 3 * delay)."},
    {"phase", F(phase), 1, 0, 16, 0, NULL, NULL, "shape",
     "Extra steps added to every follower's entry."},
    {"range_low", F(range_low), 1, 1, 127, 60, NULL, NULL, "shape",
     "Lowest MIDI pitch any voice may sound (60 = middle C)."},
    {"range_high", F(range_high), 1, 1, 127, 72, NULL, NULL, "shape",
     "Highest MIDI pitch any voice may sound."},

    {"transpose", F(transpose), 1, -24, 24, 0, NULL, NULL, "canon",
     "Semitones added to every follower (scale steps when diatonic)."},
    {"transpose_1", VOICE_TRANSPOSE(1), 1, TRANSPOSE_SAME, 24, TRANSPOSE_SAME,
     parse_same_word, print_same_word, "canon",
     "Transposition of voice 2 like transpose, or same to use transpose."},
    {"transpose_2", VOICE_TRANSPOSE(2), 1, TRANSPOSE_SAME, 24, TRANSPOSE_SAME,
     parse_same_word, print_same_word, "canon",
     "Transposition of voice 3 like transpose, or same to use transpose."},
    {"transpose_3", VOICE_TRANSPOSE(3), 1, TRANSPOSE_SAME, 24, TRANSPOSE_SAME,
     parse_same_word, print_same_word, "canon",
     "Transposition of voice 4 like transpose, or same to use transpose."},
    {"diatonic", F(diatonic), 1, 0, 1, 0, NULL, NULL, "canon",
     "Transpositions count scale steps of the key instead of semitones (7 = an "
     "octave), so a canon at the third stays in the key. A note outside the "
     "seven-note scale, such as minor's raised 7th, moves with the scale note "
     "below it and keeps its distance. Needs a fixed key and mode and no "
     "modulation."},
    {"invert", F(invert), 1, 0, 1, 0, NULL, NULL, "canon",
     "Followers play the melody upside down around `axis`."},
    {"axis", F(axis), 1, 0, 127, 67, NULL, NULL, "canon",
     "Inversion axis as a MIDI pitch: follower pitch = 2 * axis - pitch."},
    {"invert_mod12", F(invert_mod12), 1, 0, 1, 0, NULL, NULL, "canon",
     "Invert pitch classes and keep each note's octave, instead of mirroring MIDI pitch."},
    {"retrograde", F(retrograde), 1, 0, 1, 0, NULL, NULL, "canon",
     "Followers play the melody backwards."},
    {"augment", F(augment), 1, 0, 4, 0, NULL, NULL, "canon",
     "Followers hold each note this many steps (0 = off)."},
    {"diminish", F(diminish), 1, 0, 4, 0, NULL, NULL, "canon",
     "Followers play every nth melody note (0 = off)."},
    {"cyclic", F(cyclic), 1, 0, 1, 0, NULL, NULL, "canon",
     "Followers wrap around the melody, so the piece is one closed loop."},

    {"key", F(key), 1, -1, 11, 0, parse_key_word, print_key_word, "key",
     "Tonic of the key: C, C#, Db ... B, or search to let the solver choose."},
    {"mode", F(mode), 1, -1, MODE_COUNT - 1, MODE_MAJOR, parse_mode_word,
     print_mode_word, "key",
     "major, minor, dorian, phrygian, lydian, mixolydian, locrian, or search "
     "(major or minor)."},
    {"modulate_at", F(modulate_at), 1, -1, SPAN_MAX, -1, parse_off_word,
     print_off_word, "key",
     "Step (1 or later) where every voice switches to the second key (off = no "
     "modulation)."},
    {"key_second", F(key_second), 1, -1, 11, -1, parse_second_key_word,
     print_second_key_word, "key",
     "Tonic of the second key, or related to search the closely related keys."},
    {"mode_second", F(mode_second), 1, -1, MODE_COUNT - 1, -1,
     parse_second_mode_word, print_second_mode_word, "key",
     "Mode of the second key, or auto: the first key's mode for a named "
     "key_second, major or minor for related."},

    {"max_leap", F(max_leap), 1, 0, 24, 7, NULL, NULL, "rules",
     "Largest melodic interval in semitones."},
    {"leading_tone", F(leading_tone), 1, 0, 1, 0, NULL, NULL, "rules",
     "A melody note a semitone below the tonic of the key in force must be followed by "
     "the tonic a semitone above; a tie carries the duty to the next attack, a rest "
     "does not resolve it, and the last note is free."},
    {"double_leaps", F(double_leaps), 1, 0, 1, 1, NULL, NULL, "rules",
     "Allow two leaps larger than a major third (over 4 semitones) in a row in the same "
     "direction along any voice; 0 forbids them. A note repeated or held between the "
     "leaps, or a silence, separates them."},
    {"consonance", F(consonance), 1, 0, 2, CONSONANCE_STRONG, parse_consonance_word,
     print_consonance_word, "rules",
     "Where sounding voices must be consonant: off, strong beats, or all steps."},
    {"allow_fourth", F(allow_fourth), 1, 0, 1, 0, NULL, NULL, "rules",
     "Count a fourth above the lowest voice as consonant."},
    {"allow_unison", F(allow_unison), 1, 0, 1, 0, NULL, NULL, "rules",
     "Allow two voices on the same pitch where consonance applies."},
    {"parallels", F(parallels), 1, 0, 1, 1, NULL, NULL, "rules",
     "Forbid parallel fifths and octaves between any two voices."},
    {"max_spacing", F(max_spacing), 1, 0, 127, 0, NULL, NULL, "rules",
     "Largest interval in semitones between any two voices sounding together (0 = "
     "no limit)."},
    {"crossing", F(crossing), 1, 0, 1, 1, NULL, NULL, "rules",
     "Let a later voice sound above an earlier one; 0 keeps voice 1 on top, voice 2 "
     "below it, and so on."},
    {"harmony", F(harmony), 1, 0, 1, 0, NULL, NULL, "rules",
     "Give each bar a chord variable; strong-beat notes must be its chord tones."},
    {"progression", F(progression), 1, 0, 1, 1, NULL, NULL, "rules",
     "With harmony, consecutive bar chords follow the usual root progressions."},
    {"cadence", F(cadence), 1, 0, 1, 1, NULL, NULL, "rules",
     "End on the tonic, approached from the dominant triad; followers end on "
     "tonic-triad notes."},
    {"poly_meter", F(poly_meter), 1, 0, 1, 0, NULL, NULL, "rules",
     "Treat every third step as strong as well as every fourth."},
    {"mirror", F(mirror), 1, 0, 1, 0, NULL, NULL, "rules",
     "Make the melody its own retrograde inversion: notes i and length - 1 - i sum to "
     "2 * mirror_axis, a rest pairs only with a rest, and an odd length has the axis "
     "itself as its middle note. Only pitches are mirrored, not ties."},
    {"mirror_axis", F(mirror_axis), 1, 1, 127, 66, NULL, NULL, "rules",
     "MIDI pitch the mirror reflects around; it must lie within range_low..range_high. "
     "The second degree of a major key keeps every scale note (D for C major)."},

    {"rhythm", F(rhythm), 1, 0, 1, 0, NULL, NULL, "rhythm",
     "Let notes be tied into longer values and let rests appear."},
    {"max_hold", F(max_hold), 1, 1, 3, 1, NULL, NULL, "rhythm",
     "Longest tie in steps after the attack (1 = half notes, 3 = whole notes)."},
    {"rest_at", F(rest_at), 1, -1, MELODY_MAX - 1, -1, parse_off_word,
     print_off_word, "rhythm", "Force a rest at this melody index (needs rhythm)."},
    {"max_rests", F(max_rests), 1, 0, MELODY_MAX, 2, NULL, NULL, "rhythm",
     "Most rests the melody may contain."},

    {"lock", F(lock), 1, 0, 1, 0, NULL, NULL, "lock",
     "Fix one melody note before the search and report what it changed (melody "
     "fixes any number)."},
    {"lock_index", F(lock_index), 1, 0, MELODY_MAX - 1, 0, NULL, NULL, "lock",
     "Melody index of the locked note."},
    {"lock_pitch", F(lock_pitch), 1, 0, 127, 60, NULL, NULL, "lock",
     "MIDI pitch of the locked note (0 = rest)."},
    {"melody", F(melody), OPEN(MELODY_MAX), -1, 127, -1, parse_note_word, print_note_word,
     "lock",
     "The melody's notes, given as MIDI pitches, rest (needs rhythm) or ? for a note "
     "the solver chooses; notes past the list are free. Give them all to check a "
     "melody against the rules."},

    {"energy", F(energy), 1, 0, 1, 1, NULL, NULL, "energy",
     "Try cheaper values first (1) or plain ascending order (0)."},
    {"temperature", F(temperature), 1, 0, 1000, 0, NULL, NULL, "energy",
     "Above 0, sample values with weight exp(-cost / temperature)."},
    {"seed", F(seed), 1, INT_MIN, INT_MAX, 1, NULL, NULL, "energy",
     "Random seed for sampling."},
    {"anneal_start", F(anneal_start), 1, 0, 1000, 0, NULL, NULL, "energy",
     "Temperature at the first decision when annealing."},
    {"anneal_end", F(anneal_end), 1, 0, 1000, 0, NULL, NULL, "energy",
     "Temperature after anneal_steps decisions (linear schedule)."},
    {"anneal_steps", F(anneal_steps), 1, 0, 1000, 0, NULL, NULL, "energy",
     "Decisions over which the linear schedule cools (0 = off)."},
    {"anneal_ratio", F(anneal_ratio), 1, 0, 99, 0, NULL, NULL, "energy",
     "Geometric schedule: temperature *= ratio / 100 per decision (0 = off)."},
    {"w_gravity", F(w_gravity), 1, 0, 100, 1, NULL, NULL, "energy",
     "Cost of unstable scale degrees (leading tone high, tonic low)."},
    {"w_curve", F(w_curve), 1, 0, 100, 1, NULL, NULL, "energy",
     "Cost per unit of difference between a melody note's gravity and its target on the "
     "tension curve."},
    {"tension", F(tension), OPEN(MELODY_MAX), TENSION_UNLISTED, 4, TENSION_UNLISTED,
     parse_tension_word, print_tension_word, "energy",
     "The tension curve on the gravity scale (0 tonic or third .. 4 outside the key), as "
     "points spread evenly from the first melody note to the last; notes between points "
     "take the straight line between them, rounded. 0 4 0 peaks in the middle and one "
     "value is flat. A ? point is filled in from the given points on either side, or "
     "from the nearest one at either end, so 0 4 ? rises over the first half and then "
     "holds. arch (or no given point) is the built-in arch."},
    {"w_leap", F(w_leap), 1, 0, 100, 1, NULL, NULL, "energy",
     "Cost per four semitones of melodic interval."},
    {"w_repeat", F(w_repeat), 1, 0, 100, 4, NULL, NULL, "energy",
     "Cost of striking the same pitch twice in a row."},
    {"w_recover", F(w_recover), 1, 0, 100, 2, NULL, NULL, "energy",
     "Cost of not stepping back after a leap larger than a third."},
    {"w_dissonance", F(w_dissonance), 1, 0, 100, 1, NULL, NULL, "energy",
     "Cost per grade of vertical dissonance (fourth 1, second or tritone 2, "
     "semitone 3)."},
    {"w_parallel", F(w_parallel), 1, 0, 100, 2, NULL, NULL, "energy",
     "Cost of similar motion into a fifth or octave."},
    {"w_contrary", F(w_contrary), 1, 0, 100, 0, NULL, NULL, "energy",
     "Cost of each pair of voices that both move up or both move down from one step "
     "to the next, favouring contrary and oblique motion."},
    {"w_motif", F(w_motif), 1, 0, 100, 0, NULL, NULL, "energy",
     "Cost of each melodic step that breaks the motif pattern."},
    {"motif_a", F(motif_a), 1, -128, 24, 0, parse_motif_word, print_motif_word,
     "energy", "First interval of the motif pattern in semitones."},
    {"motif_b", F(motif_b), 1, -128, 24, 0, parse_motif_word, print_motif_word,
     "energy", "Second interval of the motif pattern."},
    {"motif_c", F(motif_c), 1, -128, 24, -128, parse_motif_word, print_motif_word,
     "energy", "Third interval (off = pattern ends)."},
    {"motif_d", F(motif_d), 1, -128, 24, -128, parse_motif_word, print_motif_word,
     "energy", "Fourth interval (off = pattern ends)."},
    {"w_modulate", F(w_modulate), 1, 0, 100, 1, NULL, NULL, "energy",
     "Cost per fifth between the two keys, and per accidental of a searched key."},
    {"w_harmony", F(w_harmony), 1, 0, 100, 1, NULL, NULL, "energy",
     "Cost of weaker chords (ii, vi, and twice for iii, vii), twice this for a "
     "chord repeated from the bar before, and of non-chord tones on weak beats."},
    {"w_rest", F(w_rest), 1, 0, 100, 4, NULL, NULL, "energy", "Cost of each rest."},
    {"w_hold", F(w_hold), 1, 0, 100, 1, NULL, NULL, "energy", "Cost of each tie."},
    {"w_syncopation", F(w_syncopation), 1, 0, 100, 3, NULL, NULL, "energy",
     "Cost of a tie that carries a note over beat 1 or 3."},
    {"w_rhythm", F(w_rhythm), 1, 0, 100, 3, NULL, NULL, "energy",
     "Cost of a bar of four plain quarter notes."},
    {"w_final", F(w_final), 1, 0, 100, 2, NULL, NULL, "energy",
     "Cost of a short final note when rhythm is on."},
    {"w_corpus", F(w_corpus), 1, 0, 100, 4, NULL, NULL, "energy",
     "Largest pitch-class cost that --corpus --apply-weights may add."},
    {"pc_weight", F(pc_weight), 12, 0, 100, 0, NULL, NULL, "energy",
     "Twelve extra costs, one per pitch class C..B."},

    {"var_order", F(var_order), 1, 0, 3, ORDER_MRV, parse_order_word,
     print_order_word, "search",
     "Which variable to decide next: mrv (smallest domain, then most "
     "constraints), entropy (most certain by cost), collapse (largest expected "
     "entropy drop), or index."},
    {"hierarchy", F(hierarchy), 1, 0, 1, 1, NULL, NULL, "search",
     "Decide keys, then chords, then notes."},
    {"backjump", F(backjump), 1, 0, 1, 1, NULL, NULL, "search",
     "Jump back to the latest decision a conflict depends on."},
    {"learn", F(learn), 1, 0, 1, 1, NULL, NULL, "search",
     "Remember failed decision sets and prune them elsewhere."},
    {"optimize", F(optimize), 1, 0, INT_MAX, 10000, NULL, NULL, "search",
     "After the first solution, spend up to this many more nodes improving it: "
     "windows of six notes are re-solved in turn for a lower energy (0 = keep "
     "the first solution)."},
    {"max_nodes", F(max_nodes), 1, 0, INT_MAX, 200000, NULL, NULL, "search",
     "Give up after this many search nodes (0 = no limit)."},
    {"time_limit", F(time_limit), 1, 0, INT_MAX, 10000, NULL, NULL, "search",
     "Give up after this many milliseconds (0 = no limit)."},
    {"delay_search", F(delay_search), 1, 0, 1, 0, NULL, NULL, "search",
     "Try every delay from delay_min to delay_max and keep the lowest energy."},
    {"delay_min", F(delay_min), 1, 1, SPAN_MAX, 1, NULL, NULL, "search",
     "Smallest delay tried by delay_search."},
    {"delay_max", F(delay_max), 1, 1, SPAN_MAX, 8, NULL, NULL, "search",
     "Largest delay tried by delay_search."},

    {"tempo", F(tempo), 1, 20, 300, 120, NULL, NULL, "output",
     "Quarter notes per minute in MIDI and WAV."},
    {"sample", F(sample), 1, 0, 1, 0, NULL, NULL, "output",
     "Synthesize the WAV from a 256-sample wavetable instead of sine waves."},
};

#undef F
#undef VOICE_DELAY

enum { KEY_DEF_COUNT = (int)(sizeof(keys) / sizeof(keys[0])) };

static int *field(PieceConfig *config, const KeyDef *def) {
    return (int *)((char *)config + def->offset);
}

static const int *cfield(const PieceConfig *config, const KeyDef *def) {
    return (const int *)((const char *)config + def->offset);
}

static const KeyDef *find_key(const char *name) {
    for (int i = 0; i < KEY_DEF_COUNT; i++) {
        if (strcmp(keys[i].name, name) == 0) return &keys[i];
    }
    return NULL;
}

void config_defaults(PieceConfig *config) {
    memset(config, 0, sizeof(*config));
    for (int i = 0; i < KEY_DEF_COUNT; i++) {
        int *f = field(config, &keys[i]);
        for (int k = 0; k < key_count(&keys[i]); k++) f[k] = keys[i].def;
    }
}

static bool is_bool_key(const KeyDef *def) {
    return def->min == 0 && def->max == 1 && def->parse == NULL;
}

static bool parse_int(const char *text, long *out) {
    if (*text == '\0') return false;
    char *end = NULL;
    errno = 0;
    long v = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0') return false;
    *out = v;
    return true;
}

static bool set_scalar(const KeyDef *def, int *slot, const char *value, char *err,
                       size_t cap) {
    long v;
    int word = 0;
    if (def->parse != NULL && def->parse(value, &word)) {
        *slot = word;
        return true;
    }
    if (is_bool_key(def)) {
        static const char *const truthy[] = {"on", "true", "yes"};
        static const char *const falsy[] = {"off", "false", "no"};
        for (int i = 0; i < 3; i++) {
            if (strcmp(value, truthy[i]) == 0) {
                *slot = 1;
                return true;
            }
            if (strcmp(value, falsy[i]) == 0) {
                *slot = 0;
                return true;
            }
        }
    }
    if (!parse_int(value, &v)) {
        snprintf(err, cap, "config value for %s is not valid: %s", def->name, value);
        return false;
    }
    /* -128 is only the stored form of a transposition's "same" or a motif
     * interval's "off"; as numbers these take -24 and up */
    int low = def->parse != NULL && def->min == TRANSPOSE_SAME ? -24 : def->min;
    if (v < low || v > def->max) {
        if (low != def->min) {
            char name[16];
            def->print(def->min, name, sizeof(name));
            snprintf(err, cap, "config value for %s must be %d..%d or %s: %s", def->name, low,
                     def->max, name, value);
        } else {
            snprintf(err, cap, "config value for %s must be %d..%d: %s", def->name, def->min,
                     def->max, value);
        }
        return false;
    }
    *slot = (int)v;
    return true;
}

bool config_set(PieceConfig *config, const char *key, const char *value, char *err,
                size_t cap) {
    if (strcmp(key, "preset") == 0) return config_apply_preset(config, value, err, cap);
    const KeyDef *def = find_key(key);
    if (def == NULL) {
        snprintf(err, cap, "unknown config key: %s", key);
        return false;
    }
    int *slot = field(config, def);
    int count = key_count(def);
    if (count == 1) return set_scalar(def, slot, value, err, cap);

    char buf[512];
    if (strlen(value) >= sizeof(buf)) {
        snprintf(err, cap, "config value for %s is too long", def->name);
        return false;
    }
    snprintf(buf, sizeof(buf), "%s", value);
    /* a list replaces the whole array */
    for (int k = 0; k < count; k++) slot[k] = def->def;
    int n = 0;
    for (char *tok = strtok(buf, ", \t"); tok != NULL; tok = strtok(NULL, ", \t")) {
        if (n >= count) {
            snprintf(err, cap, "config value for %s takes %s%d values", def->name,
                     key_open(def) ? "at most " : "", count);
            return false;
        }
        if (!set_scalar(def, &slot[n], tok, err, cap)) return false;
        n++;
    }
    if (n != count && !(key_open(def) && n > 0)) {
        snprintf(err, cap, "config value for %s takes %s%d values", def->name,
                 key_open(def) ? "at most " : "", count);
        return false;
    }
    return true;
}

bool config_assign(PieceConfig *config, const char *assignment, char *err, size_t cap) {
    const char *eq = strchr(assignment, '=');
    if (eq == NULL || eq == assignment) {
        snprintf(err, cap, "expected KEY=VALUE: %s", assignment);
        return false;
    }
    char key[64];
    size_t n = (size_t)(eq - assignment);
    if (n >= sizeof(key)) {
        snprintf(err, cap, "unknown config key: %s", assignment);
        return false;
    }
    memcpy(key, assignment, n);
    key[n] = '\0';
    return config_set(config, key, eq + 1, err, cap);
}

typedef struct Preset {
    const char *name;
    const char *summary;
    const char *settings[24];
} Preset;

static const Preset presets[] = {
    {"renaissance",
     "Modal, stepwise lines in half and quarter notes with strict consonance.",
     {"mode=dorian", "key=D", "range_low=57", "range_high=74", "max_leap=5",
      "consonance=strong", "rhythm=1", "max_hold=1", "w_leap=3", "w_recover=3",
      "w_repeat=6", "w_dissonance=2", "w_rhythm=4", "length=16", NULL}},
    {"baroque",
     "Three voices two bars apart over a functional chord progression, V-I at the end.",
     {"voices=3", "delay=8", "length=24", "range_low=55", "range_high=79",
      "harmony=1", "progression=1", "cadence=1", "rhythm=1", "max_rests=1",
      "w_repeat=6", "var_order=entropy", NULL}},
    {"classical",
     "Two voices in periodic phrases, clear cadence, long final note.",
     {"voices=2", "delay=8", "length=16", "range_low=60", "range_high=79",
      "harmony=1", "progression=1", "cadence=1", "rhythm=1", "w_curve=3",
      "w_final=6", "w_leap=2", NULL}},
    {"minimalist",
     "A three-note arpeggio looping against the 4/4 bar in a three-voice round.",
     {"cyclic=1", "voices=3", "delay=2", "length=12", "range_low=60", "range_high=72",
      "cadence=0", "consonance=off", "allow_unison=1", "w_motif=6", "motif_a=4",
      "motif_b=3", "motif_c=-7", "w_curve=0", "w_repeat=1", "w_dissonance=2",
      "var_order=index", NULL}},
    {"experimental",
     "Lydian mirror canon with off-beat entries on a 3-against-4 grid.",
     {"mode=lydian", "invert=1", "axis=69", "voices=3", "delay=3", "length=16",
      "poly_meter=1", "rhythm=1", "consonance=strong", "allow_fourth=1", "parallels=0",
      "temperature=2", "cadence=0", "range_low=55", "range_high=83", NULL}},
};

enum { PRESET_COUNT = (int)(sizeof(presets) / sizeof(presets[0])) };

int config_preset_count(void) {
    return PRESET_COUNT;
}

const char *config_preset_name(int index) {
    return index >= 0 && index < PRESET_COUNT ? presets[index].name : NULL;
}

bool config_apply_preset(PieceConfig *config, const char *name, char *err, size_t cap) {
    for (int i = 0; i < PRESET_COUNT; i++) {
        if (strcmp(presets[i].name, name) != 0) continue;
        for (int k = 0; presets[i].settings[k] != NULL; k++) {
            if (!config_assign(config, presets[i].settings[k], err, cap)) return false;
        }
        return true;
    }
    snprintf(err, cap, "unknown preset: %s", name);
    return false;
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    size_t start = 0;
    while (s[start] != '\0' && isspace((unsigned char)s[start])) start++;
    if (start > 0) memmove(s, s + start, n - start + 1);
}

static char *read_file(const char *path, size_t *length) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    size_t cap = 4096;
    size_t len = 0;
    char *data = malloc(cap);
    while (data != NULL) {
        size_t got = fread(data + len, 1, cap - len - 1, f);
        len += got;
        if (len + 1 < cap) break;
        cap *= 2;
        char *grown = realloc(data, cap);
        if (grown == NULL) {
            free(data);
            data = NULL;
        } else {
            data = grown;
        }
    }
    /* a directory opens on POSIX and only fails when read */
    if (data != NULL && ferror(f)) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data != NULL) data[len] = '\0';
    *length = len;
    return data;
}

/* A '#' starts a comment at the start of a line or after a space, so a
 * sharp such as "key F#" is not cut short. */
static void strip_comment(char *line) {
    for (char *p = line; *p != '\0'; p++) {
        if (*p == '#' && (p == line || isspace((unsigned char)p[-1]))) {
            *p = '\0';
            return;
        }
    }
}

/* The preset line or member is applied first so the other keys override it. */
static bool load_text(PieceConfig *config, char *text, const char *path, char *err,
                      size_t cap) {
    for (int pass = 0; pass < 2; pass++) {
        char *cursor = text;
        int line_no = 0;
        while (cursor != NULL && *cursor != '\0') {
            char *next = strchr(cursor, '\n');
            size_t len = next != NULL ? (size_t)(next - cursor) : strlen(cursor);
            char line[512];
            line_no++;
            if (len >= sizeof(line)) {
                snprintf(err, cap, "%s:%d: line too long", path, line_no);
                return false;
            }
            memcpy(line, cursor, len);
            line[len] = '\0';
            cursor = next != NULL ? next + 1 : NULL;

            strip_comment(line);
            trim(line);
            if (line[0] == '\0') continue;
            char *space = line;
            while (*space != '\0' && !isspace((unsigned char)*space)) space++;
            char *value = space;
            if (*space != '\0') {
                *space = '\0';
                value = space + 1;
                trim(value);
            }
            bool is_preset = strcmp(line, "preset") == 0;
            if (is_preset != (pass == 0)) continue;
            char why[200];
            if (*value == '\0') {
                snprintf(err, cap, "%s:%d: missing value for %s", path, line_no, line);
                return false;
            }
            if (!config_set(config, line, value, why, sizeof(why))) {
                snprintf(err, cap, "%s:%d: %s", path, line_no, why);
                return false;
            }
        }
    }
    return true;
}

static bool json_scalar_text(const JsonValue *v, char *out, size_t cap) {
    switch (v->type) {
    case JSON_NUMBER:
        if (v->number != floor(v->number) || v->number < INT_MIN ||
            v->number > INT_MAX)
            return false;
        snprintf(out, cap, "%d", (int)v->number);
        return true;
    case JSON_BOOL:
        snprintf(out, cap, "%d", v->boolean ? 1 : 0);
        return true;
    case JSON_STRING:
        snprintf(out, cap, "%s", v->string);
        return true;
    default:
        return false;
    }
}

static bool load_json(PieceConfig *config, const char *text, const char *path,
                      char *err, size_t cap) {
    JsonValue root;
    char why[200];
    if (!json_parse(text, &root, why, sizeof(why))) {
        snprintf(err, cap, "%s: %s", path, why);
        return false;
    }
    if (root.type != JSON_OBJECT) {
        snprintf(err, cap, "%s: expected an object of config keys", path);
        json_free(&root);
        return false;
    }
    bool ok = true;
    for (int pass = 0; pass < 2 && ok; pass++) {
        for (int i = 0; i < root.count && ok; i++) {
            const char *key = root.keys[i];
            const JsonValue *v = &root.items[i];
            if ((strcmp(key, "preset") == 0) != (pass == 0)) continue;
            char value[512];
            if (v->type == JSON_ARRAY) {
                size_t used = 0;
                value[0] = '\0';
                for (int k = 0; k < v->count && ok; k++) {
                    char item[64];
                    if (!json_scalar_text(&v->items[k], item, sizeof(item))) {
                        ok = false;
                        break;
                    }
                    int w = snprintf(value + used, sizeof(value) - used, "%s%s",
                                     k == 0 ? "" : ",", item);
                    if (w < 0 || (size_t)w >= sizeof(value) - used) {
                        ok = false;
                        break;
                    }
                    used += (size_t)w;
                }
            } else if (!json_scalar_text(v, value, sizeof(value))) {
                ok = false;
            }
            if (!ok) {
                snprintf(err, cap, "%s: config value for %s is not valid", path, key);
                break;
            }
            if (!config_set(config, key, value, why, sizeof(why))) {
                snprintf(err, cap, "%s: %s", path, why);
                ok = false;
            }
        }
    }
    json_free(&root);
    return ok;
}

static bool has_json_extension(const char *path) {
    size_t n = strlen(path);
    if (n < 5) return false;
    const char *ext = path + n - 5;
    const char *want = ".json";
    for (int i = 0; i < 5; i++) {
        if (tolower((unsigned char)ext[i]) != want[i]) return false;
    }
    return true;
}

bool config_load_file(PieceConfig *config, const char *path, char *err, size_t cap) {
    size_t length = 0;
    char *text = read_file(path, &length);
    if (text == NULL) {
        snprintf(err, cap, "cannot read config: %s", path);
        return false;
    }
    if (strlen(text) != length) {
        snprintf(err, cap, "%s: contains a NUL byte", path);
        free(text);
        return false;
    }
    const char *body = text;
    if ((unsigned char)body[0] == 0xEF && (unsigned char)body[1] == 0xBB &&
        (unsigned char)body[2] == 0xBF)
        body += 3; /* UTF-8 byte order mark */
    bool ok = has_json_extension(path) ? load_json(config, body, path, err, cap)
                                       : load_text(config, (char *)body, path, err, cap);
    free(text);
    return ok;
}

int config_voice_count(const PieceConfig *config) {
    int v = config->voices;
    if (v < 1) v = 1;
    if (v > VOICE_MAX) v = VOICE_MAX;
    return v;
}

bool config_validate(const PieceConfig *config, char *err, size_t cap) {
    for (int i = 0; i < KEY_DEF_COUNT; i++) {
        const int *f = cfield(config, &keys[i]);
        for (int k = 0; k < key_count(&keys[i]); k++) {
            if (f[k] < keys[i].min || f[k] > keys[i].max) {
                snprintf(err, cap, "invalid %s: must be %d..%d", keys[i].name,
                         keys[i].min, keys[i].max);
                return false;
            }
        }
    }
    if (config->range_low > config->range_high) {
        snprintf(err, cap, "invalid range: range_low is above range_high");
        return false;
    }
    if (config->augment == 1 || config->diminish == 1) {
        snprintf(err, cap, "invalid %s: use 0 for off or 2..4",
                 config->augment == 1 ? "augment" : "diminish");
        return false;
    }
    if (config->augment >= 2 && config->diminish >= 2) {
        snprintf(err, cap, "invalid rhythm transform: augment and diminish together");
        return false;
    }
    if (config->cyclic && (config->augment >= 2 || config->diminish >= 2)) {
        snprintf(err, cap, "invalid cyclic: a looping canon cannot augment or diminish");
        return false;
    }
    for (int v = 1; v < VOICE_MAX; v++) {
        int t = config->voice_transpose[v];
        if (t != TRANSPOSE_SAME && t < -24) {
            snprintf(err, cap, "invalid transpose_%d: use -24..24 or same", v);
            return false;
        }
    }
    if (config->diatonic && (config->key < 0 || config->mode < 0)) {
        snprintf(err, cap, "invalid diatonic: needs a fixed key and mode, not search");
        return false;
    }
    if (config->diatonic && config->modulate_at >= 0) {
        snprintf(err, cap, "invalid diatonic: cannot be used with modulate_at");
        return false;
    }
    for (const int *m = &config->motif_a; m <= &config->motif_d; m++) {
        if (*m != -128 && *m < -24) {
            snprintf(err, cap, "invalid motif interval: use -24..24 or off");
            return false;
        }
    }
    if (config->lock) {
        if (config->lock_index >= config->length) {
            snprintf(err, cap, "invalid lock: lock_index is past the melody");
            return false;
        }
        if (config->lock_pitch == PITCH_REST && !config->rhythm) {
            snprintf(err, cap, "invalid lock: a rest needs rhythm");
            return false;
        }
    }
    for (int i = 0; i < MELODY_MAX; i++) {
        if (config->melody[i] < 0) continue;
        if (i >= config->length) {
            snprintf(err, cap, "invalid melody: note %d is past the end of %d notes", i,
                     config->length);
            return false;
        }
        if (config->melody[i] == PITCH_REST && !config->rhythm) {
            snprintf(err, cap, "invalid melody: a rest at note %d needs rhythm", i);
            return false;
        }
    }
    if (config->rest_at >= 0) {
        if (!config->rhythm) {
            snprintf(err, cap, "rest_at requires rhythm");
            return false;
        }
        if (config->rest_at >= config->length) {
            snprintf(err, cap, "invalid rest_at: past the melody");
            return false;
        }
    }
    if (config->cadence && config->length < 2) {
        snprintf(err, cap, "invalid cadence: needs a melody of two notes or more");
        return false;
    }
    /* two notes mirrored around the axis lie on either side of it (an odd
     * length's middle note on it), so both fit the range only if it does */
    if (config->mirror &&
        (config->mirror_axis < config->range_low || config->mirror_axis > config->range_high)) {
        snprintf(err, cap, "invalid mirror_axis: %d is outside range_low..range_high",
                 config->mirror_axis);
        return false;
    }
    if (config->delay_search && config->delay_min > config->delay_max) {
        snprintf(err, cap, "invalid delay_search: delay_min is above delay_max");
        return false;
    }
    PieceConfig widest = *config;
    if (config->delay_search) widest.delay = config->delay_max;
    int span = canon_span_config(&widest);
    if (span > SPAN_MAX) {
        snprintf(err, cap, "piece too long: canon spans more than %d steps", SPAN_MAX);
        return false;
    }
    /* delay_search skips delays too short to reach modulate_at, so only
     * the longest one tried has to */
    if (config->modulate_at == 0) {
        snprintf(err, cap, "invalid modulate_at: 0 would skip the first key; set key instead");
        return false;
    }
    if (config->modulate_at >= 0 && config->modulate_at >= canon_span_config(&widest)) {
        snprintf(err, cap, "invalid modulate_at: past the end of the piece");
        return false;
    }
    return true;
}

static void format_value(const KeyDef *def, int value, char *buf, size_t cap) {
    if (def->print != NULL) {
        def->print(value, buf, cap);
    } else {
        snprintf(buf, cap, "%d", value);
    }
}

int config_key_count(void) {
    return KEY_DEF_COUNT;
}

const char *config_key_name(int index) {
    return index >= 0 && index < KEY_DEF_COUNT ? keys[index].name : NULL;
}

void config_key_value(const PieceConfig *config, int index, char *buf, size_t cap) {
    buf[0] = '\0';
    if (index < 0 || index >= KEY_DEF_COUNT || cap == 0) return;
    const KeyDef *def = &keys[index];
    const int *v = cfield(config, def);
    int count = key_count(def);
    /* an open array lists its values up to the last one given */
    if (key_open(def)) {
        count = 1;
        for (int k = 0; k < key_count(def); k++) {
            if (v[k] != def->def) count = k + 1;
        }
    }
    size_t used = 0;
    for (int k = 0; k < count; k++) {
        char item[32];
        format_value(&keys[index], v[k], item, sizeof(item));
        int w = snprintf(buf + used, cap - used, "%s%s", k == 0 ? "" : ",", item);
        if (w < 0 || (size_t)w >= cap - used) return;
        used += (size_t)w;
    }
}

void config_write(FILE *f, const PieceConfig *config) {
    for (int i = 0; i < KEY_DEF_COUNT; i++) {
        char buf[256];
        config_key_value(config, i, buf, sizeof(buf));
        fprintf(f, "%s %s\n", keys[i].name, buf);
    }
}

static const char *const group_titles[][2] = {
    {"shape", "Shape"},   {"canon", "Canon transforms"}, {"key", "Keys"},
    {"rules", "Rules"},   {"rhythm", "Rhythm"},           {"lock", "Lock"},
    {"energy", "Energy"}, {"search", "Search"},           {"output", "Output"},
};

void config_print_reference(FILE *f, bool markdown) {
    for (size_t g = 0; g < sizeof(group_titles) / sizeof(group_titles[0]); g++) {
        if (markdown) {
            fprintf(f, "%s### %s\n\n| Key | Default | Range | Meaning |\n"
                       "| --- | --- | --- | --- |\n",
                    g == 0 ? "" : "\n", group_titles[g][1]);
        } else {
            fprintf(f, "%s%s\n", g == 0 ? "" : "\n", group_titles[g][1]);
        }
        for (int i = 0; i < KEY_DEF_COUNT; i++) {
            const KeyDef *def = &keys[i];
            if (strcmp(def->group, group_titles[g][0]) != 0) continue;
            char dflt[32];
            char range[64];
            format_value(def, def->def, dflt, sizeof(dflt));
            if (key_open(def)) {
                snprintf(range, sizeof(range), "up to %d values", key_count(def));
            } else if (def->count > 1) {
                snprintf(range, sizeof(range), "%d x %d..%d", def->count, def->min,
                         def->max);
            } else if (def->min == INT_MIN) {
                snprintf(range, sizeof(range), "any integer");
            } else if (def->max == INT_MAX) {
                snprintf(range, sizeof(range), "%d or more", def->min);
            } else if (def->parse != NULL) {
                snprintf(range, sizeof(range), "see meaning");
            } else {
                snprintf(range, sizeof(range), "%d..%d", def->min, def->max);
            }
            if (markdown) {
                fprintf(f, "| `%s` | %s | %s | %s |\n", def->name, dflt, range,
                        def->help);
            } else {
                fprintf(f, "  %-14s %-8s %-14s %s\n", def->name, dflt, range,
                        def->help);
            }
        }
    }
}

void config_print_presets(FILE *f) {
    for (int i = 0; i < PRESET_COUNT; i++) {
        fprintf(f, "%-13s %s\n", presets[i].name, presets[i].summary);
    }
}
