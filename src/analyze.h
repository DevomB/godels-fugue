#ifndef ANALYZE_H
#define ANALYZE_H

#include "solver.h"

#include <stdio.h>

/* How many pieces the rules allow, counted by the solver. */
typedef struct Count {
    long pieces;
    long max;
    bool exact;     /* the search ran to the end */
    bool limit_hit; /* max_nodes or time_limit stopped it, not max */
    long nodes;
    double seconds;
} Count;

/* Pitch values of one melody note left after root propagation, and
 * whether a piece exists with the note fixed to each. */
typedef struct NoteSensitivity {
    int nvalues;
    int values[128];
    SolveStatus status[128];
    int viable;  /* values with a piece */
    int unknown; /* values whose solve hit the search limit */
} NoteSensitivity;

bool analyze_count(const Model *m, long max, Count *out);
void analyze_print_count(FILE *f, const Count *c);

/* notes receives m->config.length entries. */
bool analyze_sensitivity(const Model *m, NoteSensitivity *notes);
const char *sensitivity_verdict(const NoteSensitivity *n);
void analyze_print_sensitivity(FILE *f, const Model *m, const NoteSensitivity *notes);

#endif
