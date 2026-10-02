#include "analyze.h"
#include "test_util.h"

/* Every hard constraint holds for a full assignment. */
static bool satisfies(const Model *m, const int *values) {
    for (int ci = 0; ci < m->ncons; ci++) {
        const Constraint *c = &m->cons[ci];
        if (c->type == C_MAX_RESTS) {
            int rests = 0;
            for (int i = 0; i < m->config.length; i++) rests += values[m->pitch[i]] == PITCH_REST;
            if (rests > c->param) return false;
            continue;
        }
        int vals[SCOPE_MAX];
        for (int i = 0; i < c->n; i++) vals[i] = values[c->vars[i]];
        if (!constraint_holds(m, c, vals)) return false;
    }
    return true;
}

/* Enumerates every assignment of the initial domains: the number of
 * pieces, and which pitches each melody note takes in some piece. */
static long brute_force(const Model *m, MidiDomain *seen) {
    int doms[VAR_MAX][128];
    int sizes[VAR_MAX];
    int at[VAR_MAX];
    int values[VAR_MAX];
    for (int v = 0; v < m->nvars; v++) {
        sizes[v] = domain_collect(&m->initial[v], doms[v]);
        if (sizes[v] == 0) return 0;
        at[v] = 0;
    }
    for (int i = 0; i < m->config.length; i++) domain_clear(&seen[i]);
    long pieces = 0;
    for (;;) {
        for (int v = 0; v < m->nvars; v++) values[v] = doms[v][at[v]];
        if (satisfies(m, values)) {
            pieces++;
            for (int i = 0; i < m->config.length; i++) domain_add(&seen[i], values[m->pitch[i]]);
        }
        int v = 0;
        while (v < m->nvars && ++at[v] == sizes[v]) at[v++] = 0;
        if (v == m->nvars) return pieces;
    }
}

static PieceConfig tiny(void) {
    PieceConfig c = test_config();
    c.max_nodes = 0;
    test_set(&c, "length=4");
    test_set(&c, "delay=1");
    test_set(&c, "range_low=60");
    test_set(&c, "range_high=67");
    return c;
}

static void check_against_brute_force(const PieceConfig *c) {
    TestRun *r = test_open(c);
    MidiDomain seen[MELODY_MAX];
    long expect = brute_force(&r->model, seen);
    Count count;
    CHECK(analyze_count(&r->model, 1000000, &count));
    if (count.pieces != expect) {
        fprintf(stderr, "count %ld, brute force %ld\n", count.pieces, expect);
    }
    CHECK(count.exact && !count.limit_hit);
    CHECK(count.pieces == expect);

    NoteSensitivity notes[MELODY_MAX];
    CHECK(analyze_sensitivity(&r->model, notes));
    for (int i = 0; i < c->length; i++) {
        const NoteSensitivity *n = &notes[i];
        CHECK(n->unknown == 0);
        CHECK(n->viable == domain_count(&seen[i]));
        for (int k = 0; k < n->nvalues; k++) {
            CHECK((n->status[k] == SOLVE_SAT) == domain_contains(&seen[i], n->values[k]));
        }
        const char *verdict = sensitivity_verdict(n);
        CHECK(strcmp(verdict, n->viable == 0   ? "none"
                              : n->viable == 1 ? "frozen"
                                               : "bifurcation point") == 0);
    }
    test_close(r);
}

static void test_brute_force(void) {
    PieceConfig a = tiny();
    check_against_brute_force(&a);

    PieceConfig b = tiny();
    test_set(&b, "cadence=0");
    test_set(&b, "consonance=all");
    check_against_brute_force(&b);

    PieceConfig rhythm = tiny();
    test_set(&rhythm, "rhythm=1");
    test_set(&rhythm, "max_rests=1");
    check_against_brute_force(&rhythm);

    /* without learning or backjumping the plain search must agree */
    PieceConfig plain = tiny();
    plain.learn = 0;
    plain.backjump = 0;
    check_against_brute_force(&plain);
}

static void test_unsat(void) {
    PieceConfig c = tiny();
    test_set(&c, "range_high=60");
    TestRun *r = test_open(&c);
    Count count;
    CHECK(analyze_count(&r->model, 100, &count));
    CHECK(count.pieces == 0 && count.exact);
    NoteSensitivity notes[MELODY_MAX];
    CHECK(analyze_sensitivity(&r->model, notes));
    for (int i = 0; i < c.length; i++) {
        CHECK(notes[i].viable == 0);
        CHECK(strcmp(sensitivity_verdict(&notes[i]), "none") == 0);
    }
    test_close(r);
}

static void test_limits(void) {
    PieceConfig c = tiny();
    TestRun *r = test_open(&c);
    Count all;
    CHECK(analyze_count(&r->model, 1000000, &all));
    CHECK(all.pieces > 3);

    /* stopping at max gives a lower bound */
    Count some;
    CHECK(analyze_count(&r->model, 3, &some));
    CHECK(some.pieces == 3 && !some.exact && !some.limit_hit);
    test_close(r);

    /* so does the node limit */
    c.max_nodes = 5;
    r = test_open(&c);
    Count cut;
    CHECK(analyze_count(&r->model, 1000000, &cut));
    CHECK(!cut.exact && cut.limit_hit);
    CHECK(cut.pieces < all.pieces);
    test_close(r);
}

int main(void) {
    test_brute_force();
    test_unsat();
    test_limits();
    printf("test_analyze: ok\n");
    return 0;
}
