#ifndef SOLVER_H
#define SOLVER_H

#include "model.h"
#include "proof.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum { SOLVE_SAT, SOLVE_UNSAT, SOLVE_LIMIT } SolveStatus;

enum { CAND_MAX = 48, CAND_BREAKDOWN = 4, NOGOOD_LITS = 12, NOGOOD_CAP = 4096 };

/* A removal by learned conflict k stores constraint NOGOOD_CONSTRAINT(k). */
#define NOGOOD_CONSTRAINT(k) (-2 - (k))
#define NOGOOD_INDEX(con) (-2 - (con))

/* A search decision and the ranked candidates it was chosen from, with
 * each soft rule's share of the cost for the first few. */
typedef struct Decision {
    int var;
    int value;
    int event;
    int temperature;
    bool guided; /* replayed from the best piece the optimizer found */
    int ncand;
    int cand_values[CAND_MAX];
    int cand_costs[CAND_MAX];
    int cand_breakdown[CAND_BREAKDOWN][TERM_COUNT];
} Decision;

/* Learned conflict: these assignments cannot all hold. */
typedef struct Nogood {
    int n;
    int vars[NOGOOD_LITS];
    int values[NOGOOD_LITS];
} Nogood;

typedef struct SolverStats {
    long nodes;
    long decisions;
    long backtracks;
    long backjumps;
    long learned;
    long propagations;
    long removals;
    long removals_by_rule[CID_MAX];
    long solutions;   /* pieces found: the first, then each improvement */
    int first_energy; /* energy of the first piece found */
    long windows;     /* neighbourhoods the optimizer searched */
    bool converged;   /* a full pass of windows found nothing cheaper */
    double seconds;
} SolverStats;

typedef struct Snapshot {
    MidiDomain domains[VAR_MAX];
    LevelSet reason[VAR_MAX];
    int last_event[VAR_MAX];
    ProofMark mark;
    int level;
    bool failed;
    int failed_variable;
    uint32_t rng;
    int anneal_step;
} Snapshot;

typedef struct Frame {
    Snapshot snap;
    int n;
    int values[128];
    int costs[128];
} Frame;

typedef struct SolverState {
    const Model *model;
    MidiDomain domains[VAR_MAX];
    LevelSet reason[VAR_MAX];
    int last_event[VAR_MAX]; /* latest proof event on each variable, or -1 */
    ProofLog proof;
    Decision decisions[VAR_MAX + 1]; /* index = level */
    int level;
    Frame *frames; /* one per level, plus a scratch frame for look-ahead */
    int *queue;
    unsigned char *queued;
    int qcap;
    int qhead;
    int qlen;
    /* Last support found for each (constraint, position, value): a whole
     * tuple, still valid while its values remain in their domains. */
    int *residues;
    int *residue_base; /* ncons * SCOPE_MAX offsets into residues */
    Nogood *nogoods;
    int nnogoods;
    int *watch[VAR_MAX];
    int watch_n[VAR_MAX];
    int watch_cap[VAR_MAX];
    SolverStats stats;
    long optimize; /* node budget for improving on the first piece */
    bool optimizing;
    int bound;           /* energy of the best piece so far */
    int best[VAR_MAX];   /* the best piece so far */
    long first_found_at; /* node count when the first piece appeared */
    long window_end;     /* node count at which the current window stops */
    bool guided;         /* try best[var] first: replaying the best piece */
    bool failed;
    int failed_variable;
    bool limit_hit;
    unsigned char skip[CID_MAX];
    uint32_t rng;
    int anneal_step;
    clock_t start;
} SolverState;

/* The model must outlive the state. Every constraint starts queued, so
 * the first propagation reaches the root fixpoint. */
bool solver_init(SolverState *s, const Model *m);
void solver_free(SolverState *s);
bool solver_propagate(SolverState *s);
SolveStatus solver_solve(SolverState *s);

/* Queue the constraints on a variable after changing its domain by hand. */
void solver_touch(SolverState *s, int var);
int solver_value(const SolverState *s, int var); /* -1 unless collapsed */
void solver_values(const SolverState *s, int *values);
double solver_entropy(const SolverState *s);
/* Cost of each candidate value given the variables collapsed so far. */
void solver_choice_costs(const SolverState *s, int var, const int *values, int n,
                         int *costs, int *breakdown_per_value);

void solver_save(const SolverState *s, Snapshot *snap);
void solver_restore(SolverState *s, const Snapshot *snap);

/* Deletion-based unsat core over rule ids. Returns false when the rules
 * are satisfiable or the first solve hits the search limit. A trial that
 * hits the limit keeps its rule and sets *approximate. */
bool solver_unsat_core(const Model *m, int *rules, int max_rules, int *n,
                       bool *approximate);

#endif
