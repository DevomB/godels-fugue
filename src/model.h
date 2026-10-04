#ifndef MODEL_H
#define MODEL_H

#include "config.h"
#include "domain.h"

#include <stdbool.h>
#include <stddef.h>

/* Variable kinds. Every kind stores its values in a MidiDomain:
 * pitch 0..127 (0 = rest), tie TIE_NOTE/TIE_HOLD, chord DEGREE_*, key id. */
enum { VAR_PITCH, VAR_TIE, VAR_CHORD, VAR_KEY, VAR_KIND_COUNT };

/* Rule ids name what removed a value in the proof, the unsat core and
 * the rule-impact report. */
enum {
    CID_NONE,
    CID_SCALE,
    CID_RANGE,
    CID_LEAP,
    CID_LEADING_TONE,
    CID_DOUBLE_LEAP,
    CID_CONSONANCE,
    CID_PARALLEL_FIFTH,
    CID_PARALLEL_OCTAVE,
    CID_SPACING,
    CID_CROSSING,
    CID_CHORD,
    CID_PROGRESSION,
    CID_CADENCE,
    CID_TIE,
    CID_HOLD,
    CID_REST,
    CID_LOCK,
    CID_MODULATION,
    CID_MIRROR,
    CID_REFUTED, /* the search tried the value and every completion failed */
    CID_LEARNED, /* a learned conflict excluded the value */
    CID_MAX
};

/* Soft cost categories; each soft term belongs to one. */
enum {
    TERM_GRAVITY,
    TERM_CURVE,
    TERM_CORPUS,
    TERM_REST,
    TERM_LEAP,
    TERM_REPEAT,
    TERM_RECOVER,
    TERM_MOTIF,
    TERM_DISSONANCE,
    TERM_DIRECT,
    TERM_CONTRARY,
    TERM_HOLD,
    TERM_SYNCOPATION,
    TERM_RHYTHM,
    TERM_FINAL,
    TERM_CHORD,
    TERM_CHORD_MOTION,
    TERM_NONCHORD,
    TERM_KEY,
    TERM_KEY_DISTANCE,
    TERM_RUN,
    TERM_STEP,
    TERM_FIGURE,
    TERM_ARC,
    TERM_SEQUENCE,
    TERM_COUNT
};

/* Predicate shapes of hard constraints. */
enum {
    C_RANGE,            /* (pitch) every voice's sounding pitch in range */
    C_SCALE,            /* (pitch, key) sounding pitch in the key */
    C_LEAP,             /* (pitch, pitch) consecutive notes of one voice */
    C_CONSONANCE,       /* (pitches sounding at one step) */
    C_PARALLEL,         /* (a now, a next, b now, b next); param 1 = fifth */
    C_SPACING,          /* (pitches sounding at one step) param = widest interval */
    C_CROSSING,         /* (pitches sounding at one step, by voice) none above an earlier one */
    C_CHORD_TONE,       /* (pitch, chord, key) */
    C_PROGRESSION,      /* (chord, next chord) */
    C_CHORD_IS,         /* (chord) param = mask of allowed degrees */
    C_CADENCE_FINAL,    /* (pitch, key) param 1 = tonic, 0 = tonic triad */
    C_CADENCE_APPROACH, /* (pitch, key, ties...) see model.c */
    C_TIE,              /* (tie, previous pitch, pitch) */
    C_MAX_HOLD,         /* (ties...) not all held */
    C_REST_AT,          /* (pitch) is a rest */
    C_MAX_RESTS,        /* every pitch; propagated by counting */
    C_LOCK,             /* (var) param = value; the lock key or a given melody note */
    C_KEY_RELATION,     /* (key, key) closely related */
    C_SAME_MODE,        /* (key, key) same mode */
    C_LEADING_TONE,     /* (pitch, next pitch, key[, next tie]) the melody */
    C_DOUBLE_LEAP,      /* (pitch, pitch, pitch) consecutive notes of one voice */
    C_MIRROR            /* (pitch, pitch) mirrored around param; one slot: the middle note */
};

/* The widest rule is the cadence approach to a final note held max_hold
 * (up to 7) extra steps: its pitch, the key and eight ties; the widest term
 * the rhythm cost of a bar of the sixteenth grid, fifteen ties. */
enum { SCOPE_MAX = 16, SLOT_MAX = 16 };

typedef struct ModelVar {
    int kind;
    int index; /* melody index, bar, or key section */
} ModelVar;

/* One hard constraint or soft term over a few variables. A slot is one
 * argument of the rule; two slots may name the same variable (a note
 * heard in two voices at once), so slots point into the scope. */
typedef struct Constraint {
    int rule; /* CID_* for a constraint, TERM_* for a term */
    int type; /* C_* for a constraint */
    int n;
    int vars[SCOPE_MAX];
    int nslots;
    int slot[SLOT_MAX];  /* scope position of each slot */
    int voice[SLOT_MAX]; /* voice a pitch slot sounds in */
    int time;            /* step the rule looks at, or -1 */
    int param;
    int weight; /* terms only */
} Constraint;

typedef struct Model {
    PieceConfig config;
    int span;
    int voices;
    int nbars;
    int nsections;
    int nvars;
    ModelVar vars[VAR_MAX];
    MidiDomain initial[VAR_MAX];
    int pitch[MELODY_MAX];
    int tie[MELODY_MAX]; /* -1 without rhythm, and always for index 0 */
    int chord[BAR_MAX];  /* -1 without harmony */
    int key[SECTION_MAX];
    int source[VOICE_MAX][SPAN_MAX]; /* melody index or -1 */
    int max_rests_con; /* index of the rest-count constraint, or -1 */

    Constraint *cons;
    int ncons;
    int cons_cap;
    Constraint *terms;
    int nterms;
    int terms_cap;
    int *cons_adj_start; /* nvars + 1 entries */
    int *cons_adj;
    int *terms_adj_start;
    int *terms_adj;
    int degree[VAR_MAX];
    unsigned char rank[VAR_MAX][128]; /* position of a value in its initial domain */
} Model;

bool model_build(Model *m, const PieceConfig *config, char *err, size_t cap);
/* Builds the variable-to-constraint index; model_build calls it, and a
 * model assembled by hand (as in the tests) must call it too. */
bool model_link(Model *m);
void model_free(Model *m);

int model_section_at(const Model *m, int time);
bool model_rule_used(const Model *m, int rule);

/* vals holds one value per scope position. */
bool constraint_holds(const Model *m, const Constraint *c, const int *vals);
int term_cost(const Model *m, const Constraint *t, const int *vals);

/* Energy of a full assignment (one value per variable). breakdown, if
 * not NULL, receives TERM_COUNT per-category totals. */
int model_energy(const Model *m, const int *values, int *breakdown);

const char *rule_name(int rule);
const char *term_name(int term);
const char *var_kind_name(int kind);
void var_label(const Model *m, int var, char *buf, size_t cap);
void value_label(const Model *m, int var, int value, char *buf, size_t cap);
/* One-line description of where a constraint applies, e.g.
 * "voices 1-2 at steps 7-8". */
void constraint_describe(const Model *m, const Constraint *c, char *buf, size_t cap);

#endif
