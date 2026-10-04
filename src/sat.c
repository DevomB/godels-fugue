#include "sat.h"

#include "solver.h"

#include <stdlib.h>
#include <string.h>

enum {
    SAT_VAR_LIMIT = 8192,
    SAT_LIT_LIMIT = 4000000,
    SAT_TUPLE_LIMIT = 2000000
};

typedef struct Cnf {
    int nvars;
    int nclauses;
    int *clause_start; /* nclauses + 1 */
    int *lits;         /* +v true, -v false; v is 1-based */
    int nlits;
    int clause_cap;
    int lit_cap;
    bool too_large;
} Cnf;

static bool cnf_add(Cnf *c, const int *lits, int n) {
    if (c->nlits + n > SAT_LIT_LIMIT) {
        c->too_large = true;
        return false;
    }
    if (c->nclauses + 2 > c->clause_cap) {
        int cap = c->clause_cap == 0 ? 1024 : c->clause_cap * 2;
        int *grown = realloc(c->clause_start, (size_t)cap * sizeof(int));
        if (grown == NULL) {
            c->too_large = true;
            return false;
        }
        c->clause_start = grown;
        c->clause_cap = cap;
    }
    if (c->nlits + n > c->lit_cap) {
        int cap = c->lit_cap == 0 ? 4096 : c->lit_cap;
        while (cap < c->nlits + n) cap *= 2;
        int *grown = realloc(c->lits, (size_t)cap * sizeof(int));
        if (grown == NULL) {
            c->too_large = true;
            return false;
        }
        c->lits = grown;
        c->lit_cap = cap;
    }
    c->clause_start[c->nclauses] = c->nlits;
    memcpy(c->lits + c->nlits, lits, (size_t)n * sizeof(int));
    c->nlits += n;
    c->nclauses++;
    c->clause_start[c->nclauses] = c->nlits;
    return true;
}

typedef struct Encoding {
    const Model *m;
    MidiDomain dom[VAR_MAX];
    int first[VAR_MAX]; /* boolean id of each variable's lowest value */
    int count[VAR_MAX];
    int values[VAR_MAX][128];
} Encoding;

static int lit_of(const Encoding *e, int var, int value) {
    for (int k = 0; k < e->count[var]; k++) {
        if (e->values[var][k] == value) return e->first[var] + k;
    }
    return 0;
}

/* Node consistency, independent of the solver's propagator: a
 * constraint with one open variable filters that variable directly. */
static bool filter_domains(Encoding *e) {
    const Model *m = e->m;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int ci = 0; ci < m->ncons; ci++) {
            const Constraint *c = &m->cons[ci];
            if (c->type == C_MAX_RESTS) continue;
            int open = -1;
            int nopen = 0;
            int vals[SCOPE_MAX];
            for (int i = 0; i < c->n; i++) {
                if (domain_count(&e->dom[c->vars[i]]) == 1) {
                    vals[i] = domain_value(&e->dom[c->vars[i]]);
                } else {
                    open = i;
                    nopen++;
                }
            }
            if (nopen > 1) continue;
            if (nopen == 0) {
                if (!constraint_holds(m, c, vals)) return false;
                continue;
            }
            int values[128];
            int n = domain_collect(&e->dom[c->vars[open]], values);
            for (int k = 0; k < n; k++) {
                vals[open] = values[k];
                if (constraint_holds(m, c, vals)) continue;
                domain_remove(&e->dom[c->vars[open]], values[k]);
                changed = true;
            }
            if (domain_count(&e->dom[c->vars[open]]) == 0) return false;
        }
    }
    return true;
}

static void encode_constraint(const Encoding *e, const Constraint *c, Cnf *cnf) {
    long tuples = 1;
    for (int i = 0; i < c->n; i++) {
        tuples *= e->count[c->vars[i]];
        if (tuples > SAT_TUPLE_LIMIT) {
            cnf->too_large = true;
            return;
        }
    }
    int idx[SCOPE_MAX] = {0};
    int vals[SCOPE_MAX];
    int lits[SCOPE_MAX];
    for (;;) {
        for (int i = 0; i < c->n; i++) vals[i] = e->values[c->vars[i]][idx[i]];
        if (!constraint_holds(e->m, c, vals)) {
            for (int i = 0; i < c->n; i++) lits[i] = -(e->first[c->vars[i]] + idx[i]);
            if (!cnf_add(cnf, lits, c->n)) return;
        }
        int i = 0;
        for (; i < c->n; i++) {
            if (++idx[i] < e->count[c->vars[i]]) break;
            idx[i] = 0;
        }
        if (i == c->n) return;
    }
}

/* At most `limit` rests: forbid every set of limit + 1 rest literals. */
static void encode_max_rests(const Encoding *e, int limit, Cnf *cnf) {
    const Model *m = e->m;
    int rests[MELODY_MAX];
    int n = 0;
    for (int i = 0; i < m->config.length; i++) {
        int lit = lit_of(e, m->pitch[i], PITCH_REST);
        if (lit != 0) rests[n++] = lit;
    }
    int k = limit + 1;
    if (k > n) return;
    int pick[MELODY_MAX + 1];
    for (int i = 0; i < k; i++) pick[i] = i;
    long clauses = 0;
    for (;;) {
        int lits[MELODY_MAX + 1];
        for (int i = 0; i < k; i++) lits[i] = -rests[pick[i]];
        if (!cnf_add(cnf, lits, k) || ++clauses > SAT_TUPLE_LIMIT) {
            cnf->too_large = true;
            return;
        }
        int i = k - 1;
        while (i >= 0 && pick[i] == n - k + i) i--;
        if (i < 0) return;
        pick[i]++;
        for (int j = i + 1; j < k; j++) pick[j] = pick[j - 1] + 1;
    }
}

typedef struct Dpll {
    const Cnf *cnf;
    int *occ_start; /* per literal slot: 2 * v + (negative ? 1 : 0) */
    int *occ;
    signed char *val;
    int *trail;
    int trail_n;
    int qhead;
} Dpll;

static int slot_of(int lit) {
    return lit > 0 ? 2 * lit : 2 * -lit + 1;
}

static signed char lit_value(const Dpll *d, int lit) {
    signed char v = d->val[lit > 0 ? lit : -lit];
    return lit > 0 ? v : (signed char)-v;
}

static void assign(Dpll *d, int lit) {
    d->val[lit > 0 ? lit : -lit] = lit > 0 ? 1 : -1;
    d->trail[d->trail_n++] = lit;
}

static bool propagate(Dpll *d) {
    const Cnf *c = d->cnf;
    while (d->qhead < d->trail_n) {
        int lit = d->trail[d->qhead++];
        int falsified = slot_of(-lit);
        for (int o = d->occ_start[falsified]; o < d->occ_start[falsified + 1]; o++) {
            int cl = d->occ[o];
            int open = 0;
            int unit = 0;
            bool sat = false;
            for (int k = c->clause_start[cl]; k < c->clause_start[cl + 1]; k++) {
                signed char v = lit_value(d, c->lits[k]);
                if (v > 0) {
                    sat = true;
                    break;
                }
                if (v == 0) {
                    open++;
                    unit = c->lits[k];
                }
            }
            if (sat) continue;
            if (open == 0) return false;
            if (open == 1) assign(d, unit);
        }
    }
    return true;
}

static void undo(Dpll *d, int mark) {
    while (d->trail_n > mark) {
        int lit = d->trail[--d->trail_n];
        d->val[lit > 0 ? lit : -lit] = 0;
    }
    d->qhead = mark;
}

static int run_dpll(Dpll *d, long max_decisions) {
    const Cnf *c = d->cnf;
    typedef struct Choice {
        int var;
        int mark;
        bool flipped;
    } Choice;
    Choice *stack = malloc((size_t)(c->nvars + 1) * sizeof(Choice));
    if (stack == NULL) return SAT_TOO_LARGE;
    int depth = 0;
    long decisions = 0;
    int next = 1;
    /* unit clauses first */
    for (int cl = 0; cl < c->nclauses; cl++) {
        if (c->clause_start[cl + 1] - c->clause_start[cl] != 1) continue;
        int lit = c->lits[c->clause_start[cl]];
        signed char v = lit_value(d, lit);
        if (v < 0) {
            free(stack);
            return SAT_UNSAT;
        }
        if (v == 0) assign(d, lit);
    }
    int result = SAT_UNSAT;
    for (;;) {
        if (!propagate(d)) {
            bool resumed = false;
            while (depth > 0) {
                Choice *top = &stack[--depth];
                undo(d, top->mark);
                if (!top->flipped) {
                    top->flipped = true;
                    depth++;
                    assign(d, -top->var);
                    resumed = true;
                    break;
                }
            }
            if (!resumed) break;
            next = 1;
            continue;
        }
        while (next <= c->nvars && d->val[next] != 0) next++;
        if (next > c->nvars) {
            result = SAT_SAT;
            break;
        }
        if ((max_decisions > 0 && ++decisions > max_decisions) ||
            ((decisions & 255) == 0 && solver_past_deadline())) {
            result = SAT_LIMIT;
            break;
        }
        stack[depth].var = next;
        stack[depth].mark = d->trail_n;
        stack[depth].flipped = false;
        depth++;
        assign(d, next);
    }
    free(stack);
    return result;
}

int sat_solve(const Model *m, int *values, long max_decisions) {
    Encoding *e = malloc(sizeof(Encoding));
    if (e == NULL) return SAT_TOO_LARGE;
    e->m = m;
    for (int v = 0; v < m->nvars; v++) e->dom[v] = m->initial[v];
    if (!filter_domains(e)) {
        free(e);
        return SAT_UNSAT;
    }
    int nb = 0;
    for (int v = 0; v < m->nvars; v++) {
        e->count[v] = domain_collect(&e->dom[v], e->values[v]);
        e->first[v] = nb + 1;
        nb += e->count[v];
    }
    if (nb > SAT_VAR_LIMIT) {
        free(e);
        return SAT_TOO_LARGE;
    }

    Cnf cnf;
    memset(&cnf, 0, sizeof(cnf));
    cnf.nvars = nb;
    for (int v = 0; v < m->nvars && !cnf.too_large; v++) {
        int lits[128];
        for (int k = 0; k < e->count[v]; k++) lits[k] = e->first[v] + k;
        cnf_add(&cnf, lits, e->count[v]);
        for (int a = 0; a < e->count[v]; a++) {
            for (int b = a + 1; b < e->count[v]; b++) {
                int pair[2] = {-(e->first[v] + a), -(e->first[v] + b)};
                cnf_add(&cnf, pair, 2);
            }
        }
    }
    for (int ci = 0; ci < m->ncons && !cnf.too_large; ci++) {
        const Constraint *c = &m->cons[ci];
        if (c->type == C_MAX_RESTS) {
            encode_max_rests(e, c->param, &cnf);
        } else {
            encode_constraint(e, c, &cnf);
        }
    }

    int result = SAT_TOO_LARGE;
    Dpll d;
    memset(&d, 0, sizeof(d));
    if (!cnf.too_large) {
        d.cnf = &cnf;
        int slots = 2 * nb + 2;
        d.occ_start = calloc((size_t)slots + 1, sizeof(int));
        d.occ = malloc((size_t)(cnf.nlits > 0 ? cnf.nlits : 1) * sizeof(int));
        d.val = calloc((size_t)nb + 1, 1);
        d.trail = malloc((size_t)(nb + 1) * sizeof(int));
        if (d.occ_start != NULL && d.occ != NULL && d.val != NULL && d.trail != NULL) {
            for (int k = 0; k < cnf.nlits; k++) d.occ_start[slot_of(cnf.lits[k]) + 1]++;
            for (int s = 0; s < slots; s++) d.occ_start[s + 1] += d.occ_start[s];
            int *fill = malloc((size_t)slots * sizeof(int));
            if (fill != NULL) {
                memcpy(fill, d.occ_start, (size_t)slots * sizeof(int));
                for (int cl = 0; cl < cnf.nclauses; cl++) {
                    for (int k = cnf.clause_start[cl]; k < cnf.clause_start[cl + 1]; k++)
                        d.occ[fill[slot_of(cnf.lits[k])]++] = cl;
                }
                free(fill);
                result = run_dpll(&d, max_decisions);
            }
        }
    }
    if (result == SAT_SAT && values != NULL) {
        for (int v = 0; v < m->nvars; v++) {
            values[v] = -1;
            for (int k = 0; k < e->count[v]; k++) {
                if (d.val[e->first[v] + k] > 0) values[v] = e->values[v][k];
            }
        }
    }
    free(d.occ_start);
    free(d.occ);
    free(d.val);
    free(d.trail);
    free(cnf.clause_start);
    free(cnf.lits);
    free(e);
    return result;
}
