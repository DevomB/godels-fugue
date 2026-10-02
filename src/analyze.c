#include "analyze.h"

#include <string.h>

bool analyze_count(const Model *m, long max, Count *out) {
    memset(out, 0, sizeof(*out));
    out->max = max;
    SolverState s;
    if (!solver_init(&s, m)) return false;
    out->pieces = solver_count(&s, max, &out->exact);
    out->limit_hit = s.limit_hit;
    out->nodes = s.stats.nodes;
    out->seconds = s.stats.seconds;
    solver_free(&s);
    return true;
}

void analyze_print_count(FILE *f, const Count *c) {
    if (c->exact) {
        fprintf(f, "count: %ld (exact)\n", c->pieces);
    } else if (c->limit_hit) {
        fprintf(f, "count: at least %ld (stopped at the search limit)\n", c->pieces);
    } else {
        fprintf(f, "count: at least %ld (stopped at %ld)\n", c->pieces, c->max);
    }
    fprintf(f, "nodes: %ld\n", c->nodes);
}

/* Is there a piece with var fixed to value? */
static SolveStatus solve_fixed(const Model *m, int var, int value) {
    SolverState s;
    if (!solver_init(&s, m)) return SOLVE_LIMIT;
    s.optimize = 0; /* only satisfiability matters here */
    domain_clear(&s.domains[var]);
    domain_add(&s.domains[var], value);
    solver_touch(&s, var);
    SolveStatus st = solver_solve(&s);
    solver_free(&s);
    return st;
}

bool analyze_sensitivity(const Model *m, NoteSensitivity *notes) {
    SolverState root;
    if (!solver_init(&root, m)) return false;
    bool consistent = !root.failed && solver_propagate(&root);
    for (int i = 0; i < m->config.length; i++) {
        NoteSensitivity *n = &notes[i];
        memset(n, 0, sizeof(*n));
        if (!consistent) continue;
        int var = m->pitch[i];
        n->nvalues = domain_collect(&root.domains[var], n->values);
        for (int k = 0; k < n->nvalues; k++) {
            n->status[k] = solve_fixed(m, var, n->values[k]);
            n->viable += n->status[k] == SOLVE_SAT;
            n->unknown += n->status[k] == SOLVE_LIMIT;
        }
    }
    solver_free(&root);
    return true;
}

const char *sensitivity_verdict(const NoteSensitivity *n) {
    if (n->viable >= 2) return "bifurcation point";
    if (n->unknown > 0) return "unknown";
    return n->viable == 1 ? "frozen" : "none";
}

static void print_values(FILE *f, const Model *m, int var, const NoteSensitivity *n,
                         SolveStatus status) {
    bool any = false;
    for (int k = 0; k < n->nvalues; k++) {
        if (n->status[k] != status) continue;
        char name[16];
        value_label(m, var, n->values[k], name, sizeof(name));
        fprintf(f, "%s%s", any ? " " : "", name);
        any = true;
    }
    if (!any) fprintf(f, "-");
}

void analyze_print_sensitivity(FILE *f, const Model *m, const NoteSensitivity *notes) {
    fprintf(f, "note  verdict            viable values\n");
    for (int i = 0; i < m->config.length; i++) {
        const NoteSensitivity *n = &notes[i];
        fprintf(f, "x%-4d %-18s %d/%d: ", i, sensitivity_verdict(n), n->viable, n->nvalues);
        print_values(f, m, m->pitch[i], n, SOLVE_SAT);
        if (n->unknown > 0) {
            fprintf(f, " (unknown: ");
            print_values(f, m, m->pitch[i], n, SOLVE_LIMIT);
            fprintf(f, ")");
        }
        fprintf(f, "\n");
    }
}
