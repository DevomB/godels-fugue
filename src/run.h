#ifndef RUN_H
#define RUN_H

#include "check.h"
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
} Run;

bool run_piece(Run *run, const PieceConfig *config, char *err, size_t cap);
void run_free(Run *run);

#endif
