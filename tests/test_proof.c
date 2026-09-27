#include "explain.h"
#include "proof.h"
#include "theory.h"
#include "test_util.h"

static void test_levelsets(void) {
    LevelSet a;
    LevelSet b;
    levelset_clear(&a);
    levelset_clear(&b);
    CHECK(levelset_max(&a) == 0);
    CHECK(levelset_count(&a) == 0);
    levelset_add(&a, 3);
    levelset_add(&a, 70);
    levelset_add(&a, 0);    /* level 0 is the root and never stored */
    levelset_add(&a, 1000); /* out of range */
    CHECK(levelset_has(&a, 3));
    CHECK(levelset_has(&a, 70));
    CHECK(!levelset_has(&a, 0));
    CHECK(levelset_count(&a) == 2);
    CHECK(levelset_max(&a) == 70);
    levelset_add(&b, 5);
    levelset_union(&a, &b);
    CHECK(levelset_count(&a) == 3);
    levelset_remove(&a, 70);
    CHECK(levelset_max(&a) == 5);
}

static void test_log(void) {
    MidiDomain domains[12];
    for (int i = 0; i < 12; i++) domain_fill_range(&domains[i], 60, 67);
    CHECK(entropy_bits(domains, 12) == 36.0);
    for (int i = 0; i < 12; i++) {
        domain_clear(&domains[i]);
        domain_add(&domains[i], 60);
    }
    CHECK(entropy_bits(domains, 12) == 0.0);

    ProofLog log;
    proof_init(&log);
    ProofEvent *e = proof_append(&log, PROOF_REMOVE, 0, 61);
    CHECK(e != NULL && e->constraint == -1);
    snprintf(e->message, sizeof(e->message), "keep");
    CHECK(proof_append_entropy(&log, 36.0));
    ProofMark mark = proof_mark(&log);
    for (int i = 0; i < 1000; i++) CHECK(proof_append(&log, PROOF_REMOVE, 1, i % 128) != NULL);
    CHECK(proof_append_entropy(&log, 30.0));
    CHECK(log.event_count == 1001);
    proof_truncate(&log, mark);
    CHECK(log.event_count == 1);
    CHECK(log.sample_count == 1);
    CHECK(strcmp(log.events[0].message, "keep") == 0);
    CHECK(log.samples[0].after_event == 1);

    ProofEvent *p = proof_append(&log, PROOF_REMOVE, 2, 60);
    for (int i = 0; i < PROOF_PARENT_MAX + 3; i++) proof_add_parent(p, i, i, 60);
    CHECK(p->parent_count == PROOF_PARENT_MAX);
    proof_free(&log);
    CHECK(log.events == NULL && log.event_count == 0);
}

/* A snapshot restores domains, reasons and the proof log exactly. */
static void test_restore(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    CHECK(solver_propagate(&r->state));
    Snapshot *snap = malloc(sizeof(Snapshot));
    CHECK(snap != NULL);
    solver_save(&r->state, snap);
    int events = r->state.proof.event_count;
    MidiDomain before = r->state.domains[r->model.pitch[1]];
    test_only(r, r->model.pitch[0], 60);
    test_only(r, r->model.pitch[1], 72);
    CHECK(!solver_propagate(&r->state));
    CHECK(r->state.failed);
    solver_restore(&r->state, snap);
    CHECK(!r->state.failed);
    CHECK(domain_equal(&r->state.domains[r->model.pitch[1]], &before));
    CHECK(r->state.proof.event_count == events);
    free(snap);
    test_close(r);
}

static void test_explanations(void) {
    PieceConfig c = test_config();
    test_set(&c, "lock=1");
    test_set(&c, "lock_index=2");
    test_set(&c, "lock_pitch=64");
    TestRun *r = test_solve(&c);
    CHECK(r->status == SOLVE_SAT);
    const Model *m = &r->model;

    Explanation e;
    explain_var(&r->state, m->key[0], &e);
    CHECK(e.status == WHY_CONFIG);
    CHECK(e.nrejected == 0);

    explain_var(&r->state, m->pitch[2], &e);
    CHECK(e.status == WHY_LOCKED);
    CHECK(e.value == 64);

    /* the last note is forced to the tonic by the cadence, and C#4 by scale */
    explain_var(&r->state, m->pitch[11], &e);
    CHECK(e.value == 60 || e.value == 72);
    bool saw_scale = false;
    bool saw_cadence = false;
    for (int k = 0; k < e.nrejected; k++) {
        const ProofEvent *ev = &r->state.proof.events[e.rejected[k].event];
        CHECK(ev->type == PROOF_REMOVE);
        CHECK(ev->variable_id == m->pitch[11]);
        CHECK(ev->value == e.rejected[k].value);
        if (ev->value == 61) saw_scale = ev->rule == CID_SCALE;
        if (ev->value == 64) saw_cadence = ev->rule == CID_CADENCE;
    }
    CHECK(saw_scale);
    CHECK(saw_cadence);

    /* every other variable was chosen or forced, and a choice lists its candidates */
    int decided = 0;
    for (int v = 0; v < m->nvars; v++) {
        explain_var(&r->state, v, &e);
        CHECK(e.status != WHY_OPEN);
        if (e.status == WHY_DECIDED) {
            decided++;
            const Decision *d = &r->state.decisions[e.decision];
            CHECK(d->var == v);
            CHECK(d->value == e.value);
            CHECK(d->ncand >= 2);
            bool listed = false;
            for (int k = 0; k < d->ncand; k++) listed |= d->cand_values[k] == e.value;
            CHECK(listed);
            /* costs are listed cheapest first */
            for (int k = 1; k < d->ncand; k++) CHECK(d->cand_costs[k] >= d->cand_costs[k - 1]);
        }
        if (e.status == WHY_FORCED) {
            CHECK(e.nrejected >= 1);
            /* a forced value depends only on decisions made before it */
            CHECK(levelset_max(&e.reason) <= e.level);
        }
    }
    CHECK(decided >= 1);

    test_output_dir();
    FILE *f = fopen("output/tests/explain.txt", "w");
    CHECK(f != NULL);
    for (int v = 0; v < m->nvars; v++) {
        explain_var(&r->state, v, &e);
        explain_print(f, &r->state, &e);
    }
    CHECK(fclose(f) == 0);
    char *text = test_slurp("output/tests/explain.txt", NULL);
    CHECK(strstr(text, "x2 = E4") != NULL);
    CHECK(strstr(text, "locked by the user") != NULL);
    CHECK(strstr(text, "chosen by the search") != NULL);
    CHECK(strstr(text, "candidates by cost") != NULL);
    CHECK(strstr(text, "scale: notes stay in the key") != NULL);
    free(text);

    f = fopen("output/tests/explain.json", "w");
    CHECK(f != NULL);
    explain_var(&r->state, m->pitch[11], &e);
    explain_json(f, &r->state, &e);
    CHECK(fclose(f) == 0);
    text = test_slurp("output/tests/explain.json", NULL);
    CHECK(strstr(text, "\"var\":\"x11\"") != NULL);
    CHECK(strstr(text, "\"rejected\":[") != NULL);
    CHECK(strstr(text, "\"rule\":\"cadence\"") != NULL);
    free(text);
    test_close(r);
}

/* Every listed candidate's cost is the sum of its parts, even when the
 * value was chosen only after others were refuted. */
static void check_breakdowns(const TestRun *r) {
    for (int l = 1; l <= r->state.level; l++) {
        const Decision *d = &r->state.decisions[l];
        for (int k = 0; k < d->ncand && k < CAND_BREAKDOWN; k++) {
            int sum = 0;
            for (int t = 0; t < TERM_COUNT; t++) sum += d->cand_breakdown[k][t];
            CHECK(sum == d->cand_costs[k]);
        }
    }
}

static void test_breakdowns(void) {
    PieceConfig c = test_config();
    TestRun *r = test_solve(&c);
    check_breakdowns(r);
    test_close(r);
    test_set(&c, "length=32");
    test_set(&c, "voices=4");
    test_set(&c, "delay=5");
    test_set(&c, "range_low=48");
    test_set(&c, "range_high=72");
    test_set(&c, "consonance=all");
    test_set(&c, "harmony=1");
    test_set(&c, "rhythm=1");
    test_set(&c, "backjump=0");
    c.max_nodes = 20000;
    r = test_solve(&c);
    if (r->status == SOLVE_SAT) check_breakdowns(r);
    test_close(r);
}

/* After an unsat run no open variable points at an earlier event. */
static void test_open_variables(void) {
    PieceConfig c = test_config();
    test_set(&c, "range_low=60");
    test_set(&c, "range_high=60");
    TestRun *r = test_solve(&c);
    CHECK(r->status == SOLVE_UNSAT);
    CHECK(r->state.result == SOLVE_UNSAT);
    for (int v = 0; v < r->model.nvars; v++) {
        Explanation e;
        explain_var(&r->state, v, &e);
        if (e.status == WHY_OPEN) CHECK(e.event == -1);
    }
    test_close(r);
}

/* A refuted value names the decisions that doomed it. */
static void test_refutation(void) {
    PieceConfig c = test_config();
    test_set(&c, "var_order=index");
    test_set(&c, "voices=3");
    test_set(&c, "length=24");
    test_set(&c, "delay=2");
    test_set(&c, "consonance=all");
    test_set(&c, "range_low=55");
    test_set(&c, "range_high=76");
    TestRun *r = test_solve(&c);
    CHECK(r->status == SOLVE_SAT);
    CHECK(r->state.stats.backtracks > 0);
    int refuted = 0;
    for (int i = 0; i < r->state.proof.event_count; i++) {
        const ProofEvent *ev = &r->state.proof.events[i];
        if (ev->rule != CID_REFUTED) continue;
        refuted++;
        char why[320];
        explain_removal(&r->state, ev, why, sizeof(why));
        CHECK(strstr(why, "every completion failed") != NULL);
    }
    CHECK(refuted > 0);
    test_close(r);
}

int main(void) {
    test_levelsets();
    test_log();
    test_restore();
    test_explanations();
    test_refutation();
    test_breakdowns();
    test_open_variables();
    printf("ok\n");
    return 0;
}
