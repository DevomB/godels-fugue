#ifndef RUN_H
#define RUN_H

#include "check.h"
#include "explain.h"
#include "perform.h"
#include "score.h"
#include "solver.h"

typedef struct DelayTrial {
    int delay;
    SolveStatus status;
    int energy;
    long nodes;
} DelayTrial;

/* Everything one invocation computes. Large; allocate on the heap. */
typedef struct Run {
    PieceConfig config;
    Model model;
    SolverState state;
    SolveStatus status;
    int values[VAR_MAX]; /* final value per variable, -1 if open */
    int energy;
    int breakdown[TERM_COUNT];
    Score score;
    Performance perf; /* how the solved piece is played (perform.h) */
    int core[CID_MAX];
    int core_n;
    bool core_approximate;
    Violation violations[CHECK_MAX]; /* rules the given notes break */
    int nviolations;
    DelayTrial delays[SPAN_MAX];
    int ndelays;
    bool counterfactual; /* the config gives notes, by lock or melody */
    SolveStatus unlocked_status;
    int unlocked_pitch[MELODY_MAX];
    int unlocked_key[SECTION_MAX]; /* the key per section it solved in */
    Explanation *explained; /* one per variable once run_explain has run, else NULL */
} Run;

bool run_piece(Run *run, const PieceConfig *config, char *err, size_t cap);
/* Explains every variable once, so the outputs share one pass. False when
 * out of memory; the writers then explain as they go. */
bool run_explain(Run *run);
/* The shared explanation of var, or a fresh one through ctx (NULL: on its own). */
void run_explanation(const Run *run, ExplainContext *ctx, int var, Explanation *e);
void run_free(Run *run);

#endif
