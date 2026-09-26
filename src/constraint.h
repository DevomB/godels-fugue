#ifndef CONSTRAINT_H
#define CONSTRAINT_H

#include "types.h"

#include <stdbool.h>

enum {
    CID_SCALE = 1,
    CID_RANGE = 2,
    CID_LEAP = 3,
    CID_SECOND = 4,
    CID_PARALLEL_FIFTH = 5,
    CID_PARALLEL_OCTAVE = 6,
    CID_CHORD = 7,
    CID_CADENCE = 8,
    CID_MAX = 9
};

bool constraints_revise(SolverState *s);
bool constraints_revise_var(SolverState *s, int variable);
const char *constraint_name(int cid);

#endif
