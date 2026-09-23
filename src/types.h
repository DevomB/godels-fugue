#ifndef TYPES_H
#define TYPES_H

#include "domain.h"
#include "proof.h"

#include <stdbool.h>

enum { MELODY_MAX = 32 };

typedef struct PieceConfig {
    int length;
    int voices;
    int delay;
    int range_low;
    int range_high;
    int max_leap;
    int invert; /* 0 = identity canon, 1 = chromatic inversion */
    int axis;
    int retrograde; /* 0 = forward follower, 1 = reversed follower */
} PieceConfig;

typedef struct SolverState {
    PieceConfig config;
    MidiDomain domains[MELODY_MAX];
    ProofLog proof;
    bool failed;
    int failed_variable;
    int backtracks;
} SolverState;

#endif
