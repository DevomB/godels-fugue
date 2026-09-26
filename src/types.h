#ifndef TYPES_H
#define TYPES_H

#include "domain.h"
#include "proof.h"

#include <stdbool.h>
#include <stdint.h>

enum { MELODY_MAX = 32, VOICE_MAX = 4, TRAIL_MAX = 512 };

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
    int anneal_start;
    int anneal_end;
    int anneal_steps; /* 0 = off */
    int w_dissonance; /* vertical second/tritone; order only */
    int w_parallel;   /* soft parallel fifth/octave; order only */
} PieceConfig;

typedef struct SolverState {
    PieceConfig config;
    MidiDomain domains[MELODY_MAX];
    ProofLog proof;
    bool failed;
    int failed_variable;
    int backtracks;
    uint32_t rng;
    int anneal_step;
    int ac3_q[MELODY_MAX];
    int ac3_n;
    unsigned char ac3_in[MELODY_MAX];
    int trail_var[TRAIL_MAX];
    MidiDomain trail_dom[TRAIL_MAX];
    int trail_n;
} SolverState;

static inline void solver_enqueue(SolverState *s, int var) {
    if (var < 0 || var >= s->config.length || s->ac3_in[var]) {
        return;
    }
    s->ac3_in[var] = 1;
    s->ac3_q[s->ac3_n++] = var;
}

static inline void solver_trail_push(SolverState *s, int var) {
    if (s->trail_n >= TRAIL_MAX || var < 0 || var >= s->config.length) {
        return;
    }
    s->trail_var[s->trail_n] = var;
    s->trail_dom[s->trail_n] = s->domains[var];
    s->trail_n++;
}

#endif
