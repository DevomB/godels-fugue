#ifndef SOLVER_H
#define SOLVER_H

#include "types.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct SolverSnapshot {
    MidiDomain domains[MELODY_MAX];
    unsigned char rhythm_mask[MELODY_MAX];
    int duration[MELODY_MAX];
    ProofMark proof_mark;
    bool failed;
    int failed_variable;
    uint32_t rng;
    int anneal_step;
} SolverSnapshot;

void solver_init(SolverState *s, const PieceConfig *config);
void solver_free(SolverState *s);
bool propagate_to_fixpoint(SolverState *s);
bool solve(SolverState *s, int *melody, int *backtracks);
void solver_lock(SolverState *s, int index, int pitch);
/* Ceiling: greedy deletion until each remaining rule is necessary.
 * Small constraint set; not a named SAT MUS library. */
bool solver_unsat_core(const PieceConfig *config, int *cids, int max_cids,
                       int *n);
void solver_save(const SolverState *s, SolverSnapshot *snap);
void solver_restore(SolverState *s, const SolverSnapshot *snap);

#endif
