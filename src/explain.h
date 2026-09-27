#ifndef EXPLAIN_H
#define EXPLAIN_H

#include "solver.h"

#include <stdio.h>

/* Room for any removal reason: a refutation can list every decision. */
enum { EXPLAIN_TEXT_MAX = 4096 };

/* How a variable reached its final value. */
enum {
    WHY_CONFIG,  /* the config left one value from the start */
    WHY_DECIDED, /* the search chose it among several */
    WHY_FORCED,  /* rules removed every other value */
    WHY_LOCKED,  /* the user's lock fixed it */
    WHY_OPEN     /* still undecided (the search failed or stopped) */
};

typedef struct Rejection {
    int value;
    int event; /* the removal event */
} Rejection;

typedef struct Explanation {
    int var;
    int value;
    int status;
    int event;    /* the decision or forced event, or -1 */
    int level;    /* search depth when the value was fixed */
    int decision; /* index into state->decisions when decided, else 0 */
    LevelSet reason;
    int nrejected;
    Rejection rejected[128];
} Explanation;

void explain_var(const SolverState *s, int var, Explanation *e);
/* A value's name, spelled and numbered in the key the solver settled on. */
void explain_label(const SolverState *s, int var, int value, char *buf, size_t cap);
const char *explain_status_name(int status);
/* Short text for why a removal happened, e.g. "consonance: voices 1,2 ...". */
void explain_removal(const SolverState *s, const ProofEvent *ev, char *buf, size_t cap);
void explain_print(FILE *f, const SolverState *s, const Explanation *e);
void explain_json(FILE *f, const SolverState *s, const Explanation *e);
/* Writes s as a JSON string literal, escaping quotes, controls and '<'. */
void json_string(FILE *f, const char *s);

#endif
