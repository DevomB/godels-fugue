#ifndef EXPLAIN_H
#define EXPLAIN_H

#include "solver.h"

#include <stdio.h>

/* Room for any removal reason: a refutation can list every decision. */
enum { EXPLAIN_TEXT_MAX = 4096 };

/* Propagations one pass over the variables may spend minimizing forced
 * values. Each costs at most one per decision in its reason, plus one, and
 * is minimized only if that much is left; the others keep their whole
 * reason, which is still sound. Pieces of 32 notes in four voices need up
 * to about 500, and the largest tried, 64 notes in three or four voices,
 * about 900; the cap keeps a worse one from holding up its own output. One
 * variable (at most VAR_MAX decisions) always fits. */
enum { EXPLAIN_BUDGET = 1000 };

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
    LevelSet reason; /* every decision behind the removals */
    /* For a forced value, a part of reason that forces it by propagation
     * alone and no longer does without any one of its decisions. Equal to
     * reason when all of reason cannot (the search's refutations did part
     * of the work), when the pass's budget is spent, and for any other
     * status. */
    LevelSet minimal;
    bool minimized; /* minimal was checked by propagation */
    int nrejected;
    Rejection rejected[128];
} Explanation;

struct ExplainReplay;

/* Explains the variables of one solved state in turn with a single replay
 * solver: it reaches the root fixpoint once, on the first forced value,
 * and every later replay starts again from there. explain_end frees it. */
typedef struct ExplainContext {
    const SolverState *state;
    struct ExplainReplay *replay; /* NULL until a forced value needs it */
    bool unavailable; /* out of memory, or the rules fail before any decision */
    int budget;       /* propagations left */
} ExplainContext;

void explain_begin(ExplainContext *ctx, const SolverState *s);
/* Propagates once per decision in a forced value's reason to find the
 * minimal set. */
void explain_var_with(ExplainContext *ctx, int var, Explanation *e);
void explain_end(ExplainContext *ctx);
/* One variable on its own: begin, explain, end. */
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
