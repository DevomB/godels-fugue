#ifndef SOLVER_H
#define SOLVER_H

#include "types.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct SolverSnapshot {
    MidiDomain domains[MELODY_MAX];
    ProofMark proof_mark;
    bool failed;
    int failed_variable;
    uint32_t rng;
} SolverSnapshot;

void solver_init(SolverState *s, const PieceConfig *config);
void solver_free(SolverState *s);
bool propagate_to_fixpoint(SolverState *s);
bool solve(SolverState *s, int *melody, int *backtracks);
void solver_save(const SolverState *s, SolverSnapshot *snap);
void solver_restore(SolverState *s, const SolverSnapshot *snap);

#endif
