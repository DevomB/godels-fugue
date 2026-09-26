#ifndef TYPES_H
#define TYPES_H

#include "domain.h"
#include "proof.h"

#include <stdbool.h>
#include <stdint.h>

enum { MELODY_MAX = 32, VOICE_MAX = 4 };

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
    int energy;      /* 0 = ascending MIDI, 1 = cost order */
    int temperature; /* 0 = cheapest first; >0 = weighted sample */
    int seed;
    int w_gravity;
    int w_leap;
    int w_curve;
    int transpose; /* add to follower sounding pitch; 0 = off */
    int augment;   /* 0 = off; k>=2 holds each source for k steps */
    int diminish;  /* 0 = off; k>=2 reads every kth source step */
    int phase;     /* extra follower delay steps; 0 = off */
    int voice_delay[VOICE_MAX]; /* 0 = use voice * delay */
    int lock; /* 0 = off; re-solve with lock_index = lock_pitch */
    int lock_index;
    int lock_pitch;
} PieceConfig;

typedef struct SolverState {
    PieceConfig config;
    MidiDomain domains[MELODY_MAX];
    ProofLog proof;
    bool failed;
    int failed_variable;
    int backtracks;
    uint32_t rng;
} SolverState;

#endif
