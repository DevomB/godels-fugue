#include "run.h"
#include "sat.h"
#include "theory.h"
#include "test_util.h"

/* Every hard constraint of the model holds for a full assignment. */
static bool satisfies_model(const Model *m, const int *values) {
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
    for (int v = 0; v < m->nvars; v++) {
        if (!domain_contains(&m->initial[v], values[v])) return false;
    }
    return true;
}

static void test_default(void) {
    PieceConfig c = test_config();
    TestRun *a = test_solve(&c);
    TestRun *b = test_solve(&c);
    CHECK(a->status == SOLVE_SAT);
    CHECK(memcmp(a->values, b->values, sizeof(int) * (size_t)a->model.nvars) == 0);
    CHECK(a->state.proof.event_count == b->state.proof.event_count);
    CHECK(a->state.stats.backtracks == b->state.stats.backtracks);
    CHECK(satisfies_model(&a->model, a->values));
    CHECK(solver_entropy(&a->state) == 0.0);
    /* no melody note repeats the one before it */
    for (int i = 1; i < c.length; i++) CHECK(test_pitch(a, i) != test_pitch(a, i - 1));
    CHECK(pitch_class(test_pitch(a, c.length - 1)) == 0);
    test_close(a);
    test_close(b);
}

static void test_limits(void) {
    PieceConfig c = test_config();
    c.max_nodes = 3;
    TestRun *r = test_solve(&c);
    CHECK(r->status == SOLVE_LIMIT);
    CHECK(r->state.limit_hit);
    test_close(r);
}

static const char *const shapes[][8] = {
    {"voices=2", NULL},
    {"voices=3", "length=16", "range_low=55", "range_high=79", NULL},
    {"rhythm=1", "harmony=1", "length=16", NULL},
    {"retrograde=1", "rhythm=1", NULL},
    {"augment=2", NULL},
    {"key=search", "mode=search", "modulate_at=8", "lock=1", "lock_index=10", "lock_pitch=66", NULL},
    {"invert=1", "axis=68", "range_high=76", NULL},
    {"cyclic=1", "cadence=0", "allow_unison=1", NULL},
};

static PieceConfig shape_config(size_t s) {
    PieceConfig c = test_config();
    for (int k = 0; shapes[s][k] != NULL; k++) test_set(&c, shapes[s][k]);
    return c;
}

static void test_orders(void) {
    for (size_t s = 0; s < sizeof(shapes) / sizeof(shapes[0]); s++) {
        for (int order = ORDER_MRV; order <= ORDER_INDEX; order++) {
            PieceConfig c = shape_config(s);
            c.var_order = order;
            TestRun *r = test_solve(&c);
            if (r->status != SOLVE_SAT) {
                fprintf(stderr, "shape %u order %d: status %d\n", (unsigned)s, order, r->status);
            }
            CHECK(r->status == SOLVE_SAT);
            CHECK(satisfies_model(&r->model, r->values));
            test_close(r);
        }
    }
}

static void test_sampling(void) {
    PieceConfig c = test_config();
    test_set(&c, "temperature=4");
    test_set(&c, "seed=7");
    TestRun *a = test_solve(&c);
    TestRun *b = test_solve(&c);
    CHECK(a->status == SOLVE_SAT);
    CHECK(memcmp(a->values, b->values, sizeof(int) * (size_t)a->model.nvars) == 0);
    test_close(a);
    test_close(b);

    c = test_config();
    test_set(&c, "anneal_start=6");
    test_set(&c, "anneal_ratio=70");
    TestRun *g = test_solve(&c);
    CHECK(g->status == SOLVE_SAT && satisfies_model(&g->model, g->values));
    test_close(g);
    c = test_config();
    test_set(&c, "anneal_start=6");
    test_set(&c, "anneal_end=1");
    test_set(&c, "anneal_steps=8");
    TestRun *l = test_solve(&c);
    CHECK(l->status == SOLVE_SAT && satisfies_model(&l->model, l->values));
    test_close(l);
}

static void test_lock(void) {
    PieceConfig c = test_config();
    test_set(&c, "lock=1");
    test_set(&c, "lock_index=3");
    test_set(&c, "lock_pitch=69");
    TestRun *r = test_solve(&c);
    CHECK(r->status == SOLVE_SAT);
    CHECK(test_pitch(r, 3) == 69);
    test_close(r);

    /* a melody given in part is completed; given in full it is checked */
    c = test_config();
    test_set(&c, "melody=64,?,?,62,?,?,?,?,?,?,67");
    r = test_solve(&c);
    CHECK(r->status == SOLVE_SAT);
    CHECK(test_pitch(r, 0) == 64 && test_pitch(r, 3) == 62 && test_pitch(r, 10) == 67);
    CHECK(test_pitch(r, 11) == 60); /* the cadence still ends on the tonic */
    int whole[MELODY_MAX];
    for (int i = 0; i < c.length; i++) whole[i] = test_pitch(r, i);
    test_close(r);
    c = test_config();
    for (int i = 0; i < c.length; i++) c.melody[i] = whole[i];
    r = test_solve(&c);
    CHECK(r->status == SOLVE_SAT);
    CHECK(r->state.stats.decisions == 0);
    for (int i = 0; i < c.length; i++) CHECK(test_pitch(r, i) == whole[i]);
    test_close(r);
    c.melody[5] = 61; /* C# is not in C major */
    r = test_solve(&c);
    CHECK(r->status == SOLVE_UNSAT);
    test_close(r);
}

/* A free first variable, then four notes that must all sound consonant
 * together (so pairwise distinct) over `holes` pitches. With three
 * holes the notes cannot all fit, arc consistency cannot see it, and
 * the conflict never involves the first variable. */
static void build_pigeons(Model *m, int holes, int backjump, int learn) {
    static const int pitches[4] = {60, 64, 67, 76};
    memset(m, 0, sizeof(*m));
    config_defaults(&m->config);
    m->config.time_limit = 0;
    m->config.var_order = ORDER_INDEX;
    m->config.energy = 0;
    m->config.backjump = backjump;
    m->config.learn = learn;
    m->voices = 1;
    m->span = 1;
    m->nbars = 1;
    m->nsections = 1;
    m->max_rests_con = -1;
    m->nvars = 5;
    for (int v = 0; v < m->nvars; v++) {
        m->vars[v].kind = VAR_PITCH;
        m->vars[v].index = v;
        domain_clear(&m->initial[v]);
        for (int h = 0; h < (v == 0 ? 2 : holes); h++) domain_add(&m->initial[v], pitches[h]);
    }
    m->cons_cap = 6;
    m->cons = calloc((size_t)m->cons_cap, sizeof(Constraint));
    CHECK(m->cons != NULL);
    for (int a = 1; a < 5; a++) {
        for (int b = a + 1; b < 5; b++) {
            Constraint *c = &m->cons[m->ncons++];
            c->rule = CID_CONSONANCE;
            c->type = C_CONSONANCE;
            c->n = 2;
            c->vars[0] = a;
            c->vars[1] = b;
            c->nslots = 2;
            c->slot[0] = 0;
            c->slot[1] = 1;
            c->time = -1;
        }
    }
    CHECK(model_link(m));
}

static void test_backjumping(void) {
    long nodes[2];
    for (int bj = 0; bj <= 1; bj++) {
        Model m;
        build_pigeons(&m, 3, bj, 0);
        SolverState s;
        CHECK(solver_init(&s, &m));
        CHECK(solver_solve(&s) == SOLVE_UNSAT);
        nodes[bj] = s.stats.nodes;
        if (bj) CHECK(s.stats.backjumps >= 1);
        if (!bj) CHECK(s.stats.backjumps == 0);
        solver_free(&s);
        model_free(&m);
    }
    CHECK(nodes[1] < nodes[0]);

    Model m;
    build_pigeons(&m, 3, 1, 1);
    SolverState s;
    CHECK(solver_init(&s, &m));
    CHECK(solver_solve(&s) == SOLVE_UNSAT);
    CHECK(s.stats.learned >= 1);
    solver_free(&s);
    model_free(&m);

    build_pigeons(&m, 4, 1, 1);
    CHECK(solver_init(&s, &m));
    CHECK(solver_solve(&s) == SOLVE_SAT);
    int values[VAR_MAX];
    solver_values(&s, values);
    CHECK(satisfies_model(&m, values));
    solver_free(&s);
    model_free(&m);
}

/* Backjumping skips only subtrees with no solution, so it finds the
 * same first solution. Learning may reorder the search but never
 * changes whether a solution exists. */
static void test_search_equivalence(void) {
    static const char *const hard[][16] = {
        {"var_order=index", "energy=0", "voices=4", "delay=3", "length=20", "consonance=all",
         "range_low=55", "range_high=79", NULL},
        {"var_order=index", "voices=3", "length=24", "delay=2", "consonance=all",
         "range_low=55", "range_high=76", NULL},
        {"voices=3", "delay=2", "max_leap=3", NULL},
        {"harmony=1", "consonance=all", "voices=3", "length=16", "range_low=55",
         "range_high=79", NULL},
        /* nine fixed rests: every one must be a reason when rests run out */
        {"voices=2", "delay=1", "length=19", "range_low=60", "range_high=60", "rhythm=1",
         "max_rests=9", "consonance=all", "cadence=0", "w_rest=0",
         "pc_weight=5,5,5,5,5,5,5,5,5,5,5,5", "var_order=index", NULL},
    };
    for (size_t h = 0; h < sizeof(hard) / sizeof(hard[0]); h++) {
        PieceConfig c = test_config();
        for (int k = 0; hard[h][k] != NULL; k++) test_set(&c, hard[h][k]);
        c.learn = 0;
        c.backjump = 0;
        TestRun *plain = test_solve(&c);
        c.backjump = 1;
        TestRun *jump = test_solve(&c);
        c.learn = 1;
        TestRun *learn = test_solve(&c);
        CHECK(plain->status == jump->status);
        CHECK(plain->status == learn->status);
        CHECK(jump->state.stats.nodes <= plain->state.stats.nodes);
        if (plain->status == SOLVE_SAT) {
            CHECK(memcmp(plain->values, jump->values, sizeof(int) * (size_t)plain->model.nvars) ==
                  0);
            CHECK(satisfies_model(&learn->model, learn->values));
        }
        test_close(plain);
        test_close(jump);
        test_close(learn);
    }
}

static void test_unsat_core(void) {
    PieceConfig c = test_config();
    test_set(&c, "range_low=60");
    test_set(&c, "range_high=60");
    TestRun *r = test_solve(&c);
    CHECK(r->status == SOLVE_UNSAT);
    int core[CID_MAX];
    int n = 0;
    bool approximate = true;
    CHECK(solver_unsat_core(&r->model, core, CID_MAX, &n, &approximate));
    CHECK(!approximate);
    CHECK(n >= 1);
    unsigned char in_core[CID_MAX] = {0};
    for (int i = 0; i < n; i++) in_core[core[i]] = 1;
    /* with every other rule skipped the core alone stays unsat ... */
    SolverState s;
    CHECK(solver_init(&s, &r->model));
    for (int rule = 1; rule < CID_MAX; rule++) s.skip[rule] = !in_core[rule];
    CHECK(solver_solve(&s) == SOLVE_UNSAT);
    solver_free(&s);
    /* ... and dropping any one core rule as well makes it satisfiable */
    for (int i = 0; i < n; i++) {
        CHECK(solver_init(&s, &r->model));
        for (int rule = 1; rule < CID_MAX; rule++) s.skip[rule] = !in_core[rule];
        s.skip[core[i]] = 1;
        CHECK(solver_solve(&s) == SOLVE_SAT);
        solver_free(&s);
    }
    test_close(r);

    c = test_config();
    TestRun *ok = test_open(&c);
    CHECK(!solver_unsat_core(&ok->model, core, CID_MAX, &n, &approximate));
    test_close(ok);
}

static void test_sat_backend(void) {
    for (size_t s = 0; s < sizeof(shapes) / sizeof(shapes[0]); s++) {
        PieceConfig c = shape_config(s);
        TestRun *r = test_solve(&c);
        int values[VAR_MAX];
        int rc = sat_solve(&r->model, values, 0);
        CHECK(rc != SAT_LIMIT);
        if (rc == SAT_TOO_LARGE) {
            test_close(r);
            continue;
        }
        CHECK((rc == SAT_SAT) == (r->status == SOLVE_SAT));
        if (rc == SAT_SAT) CHECK(satisfies_model(&r->model, values));
        test_close(r);
    }
    PieceConfig c = test_config();
    test_set(&c, "range_low=60");
    test_set(&c, "range_high=60");
    TestRun *r = test_open(&c);
    CHECK(sat_solve(&r->model, NULL, 0) == SAT_UNSAT);
    test_close(r);

    c = test_config();
    TestRun *d = test_open(&c);
    CHECK(sat_solve(&d->model, NULL, 1) == SAT_LIMIT);
    test_close(d);
}

/* The optimizer starts from the plain search's piece, never ends worse,
 * keeps every rule, and replays its best piece as guided decisions. */
static void test_optimize(void) {
    static const char *const cases[][6] = {
        {"voices=2", NULL},
        {"harmony=1", "voices=3", "length=16", "range_low=55", "range_high=79", NULL},
        {"rhythm=1", "length=16", NULL},
    };
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        PieceConfig c = test_config();
        for (int j = 0; cases[k][j] != NULL; j++) test_set(&c, cases[k][j]);
        TestRun *plain = test_solve(&c);
        CHECK(plain->status == SOLVE_SAT);
        int first = model_energy(&plain->model, plain->values, NULL);

        c.optimize = 20000;
        TestRun *best = test_solve(&c);
        TestRun *again = test_solve(&c);
        CHECK(best->status == SOLVE_SAT);
        CHECK(satisfies_model(&best->model, best->values));
        CHECK(best->state.stats.first_energy == first);
        CHECK(best->state.stats.solutions >= 1);
        CHECK(best->state.stats.windows >= 1);
        int energy = model_energy(&best->model, best->values, NULL);
        CHECK(energy <= first);
        CHECK(memcmp(best->values, again->values, sizeof(int) * (size_t)best->model.nvars) == 0);
        for (int l = 1; l <= best->state.level; l++) CHECK(best->state.decisions[l].guided);
        test_close(plain);
        test_close(best);
        test_close(again);
    }
    /* chords of bars where only followers play are searched too */
    PieceConfig tail = test_config();
    test_set(&tail, "harmony=1");
    test_set(&tail, "voices=3");
    test_set(&tail, "delay=8");
    test_set(&tail, "length=8");
    test_set(&tail, "range_low=55");
    test_set(&tail, "range_high=79");
    tail.optimize = 20000;
    TestRun *t = test_solve(&tail);
    CHECK(t->status == SOLVE_SAT);
    CHECK(model_energy(&t->model, t->values, NULL) < 22);
    test_close(t);

    /* rhythm example: the optimizer finds a strictly better piece */
    PieceConfig c = test_config();
    test_set(&c, "rhythm=1");
    test_set(&c, "length=16");
    c.optimize = 20000;
    TestRun *r = test_solve(&c);
    CHECK(model_energy(&r->model, r->values, NULL) < r->state.stats.first_energy);
    test_close(r);
}

/* Canons of the longest melody solve and keep every rule; with rhythm and
 * harmony the decisions go past level 64, and every forced value is still
 * explained by a minimal set of them. */
static void test_longest_melody(void) {
    static const char *const cases[][5] = {
        {"length=64", NULL},
        {"length=64", "voices=3", "rhythm=1", "harmony=1", NULL},
    };
    Run *run = malloc(sizeof(Run));
    CHECK(run != NULL);
    char err[200];
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        PieceConfig c = test_config();
        for (int j = 0; cases[k][j] != NULL; j++) test_set(&c, cases[k][j]);
        CHECK(run_piece(run, &c, err, sizeof(err)));
        CHECK(run->status == SOLVE_SAT);
        CHECK(run->model.config.length == MELODY_MAX);
        CHECK(satisfies_model(&run->model, run->values));
        CHECK(run->model.span == MELODY_MAX + (c.voices - 1) * c.delay);
        if (c.rhythm) CHECK(run->state.level > 64);
        CHECK(run_explain(run));
        int forced = 0;
        for (int v = 0; v < run->model.nvars; v++) {
            const Explanation *e = &run->explained[v];
            CHECK(e->status != WHY_OPEN);
            if (e->status != WHY_FORCED) continue;
            forced++;
            CHECK(e->minimized);
            LevelSet both = e->minimal;
            levelset_union(&both, &e->reason);
            CHECK(memcmp(&both, &e->reason, sizeof(both)) == 0);
        }
        if (c.rhythm) CHECK(forced > 0);
        run_free(run);
    }
    free(run);
}

/* An eighth-note canon with rhythm and harmony solves and keeps every
 * rule: a chord to each eight-step bar, and the melody a mix of eighths
 * and longer notes. */
static void test_eighth_grid(void) {
    static const char *const settings[] = {
        "grid=eighth", "voices=3", "delay=8",       "length=32",      "rhythm=1",
        "harmony=1",   "max_hold=3", "key=A",       "mode=minor",     "range_low=55",
        "range_high=79", "optimize=2000"};
    PieceConfig c = test_config();
    for (size_t k = 0; k < sizeof(settings) / sizeof(settings[0]); k++) test_set(&c, settings[k]);
    Run *run = malloc(sizeof(Run));
    CHECK(run != NULL);
    char err[200];
    CHECK(run_piece(run, &c, err, sizeof(err)));
    CHECK(run->status == SOLVE_SAT);
    CHECK(satisfies_model(&run->model, run->values));
    CHECK(run->model.span == 48 && run->model.nbars == 6);
    CHECK(run->score.beat_steps == 2);
    int eighths = 0;
    int longer = 0;
    const ScoreVoice *lead = &run->score.voice[0];
    for (int k = 0; k < lead->count; k++) {
        if (lead->notes[k].pitch == SOUND_REST) continue;
        if (lead->notes[k].length == 1) {
            eighths++;
        } else {
            longer++;
        }
    }
    CHECK(eighths > 0 && longer > 0);
    run_free(run);
    free(run);
}

/* Semitones moved and stepwise moves (two semitones or fewer, a held
 * note aside) along the melody between sounding notes. */
static void melody_motion(const TestRun *r, int *moved, int *steps) {
    *moved = 0;
    *steps = 0;
    for (int i = 0; i + 1 < r->model.config.length; i++) {
        int a = test_pitch(r, i);
        int b = test_pitch(r, i + 1);
        if (a == PITCH_REST || b == PITCH_REST || a == b) continue;
        *moved += abs(a - b);
        *steps += abs(a - b) <= 2;
    }
}

/* w_step favours stepwise lines, on either grid. */
static void test_step_cost(void) {
    for (int grid = GRID_QUARTER; grid <= GRID_EIGHTH; grid++) {
        PieceConfig c = test_config();
        test_set(&c, "voices=3");
        test_set(&c, "rhythm=1");
        test_set(&c, "harmony=1");
        test_set(&c, "length=24");
        test_set(&c, "delay=8");
        test_set(&c, "range_low=55");
        test_set(&c, "range_high=79");
        c.grid = grid;
        TestRun *free_lines = test_solve(&c);
        test_set(&c, "w_step=4");
        TestRun *stepwise = test_solve(&c);
        CHECK(free_lines->status == SOLVE_SAT && stepwise->status == SOLVE_SAT);
        int moved[2];
        int steps[2];
        melody_motion(free_lines, &moved[0], &steps[0]);
        melody_motion(stepwise, &moved[1], &steps[1]);
        CHECK(moved[1] < moved[0]);
        CHECK(steps[1] > steps[0]);
        test_close(free_lines);
        test_close(stepwise);
    }
}

static void test_run_pipeline(void) {
    Run *run = malloc(sizeof(Run));
    CHECK(run != NULL);
    char err[200];
    PieceConfig c = test_config();
    test_set(&c, "delay_search=1");
    test_set(&c, "delay_min=1");
    test_set(&c, "delay_max=6");
    CHECK(run_piece(run, &c, err, sizeof(err)));
    CHECK(run->status == SOLVE_SAT);
    CHECK(run->ndelays == 6);
    int best = -1;
    for (int i = 0; i < run->ndelays; i++) {
        const DelayTrial *t = &run->delays[i];
        if (t->status != SOLVE_SAT) continue;
        if (best < 0 || t->energy < run->delays[best].energy) best = i;
    }
    CHECK(best >= 0);
    CHECK(run->config.delay == run->delays[best].delay);
    CHECK(run->energy == run->delays[best].energy);
    run_free(run);

    c = test_config();
    test_set(&c, "lock=1");
    test_set(&c, "lock_index=0");
    test_set(&c, "lock_pitch=61");
    CHECK(run_piece(run, &c, err, sizeof(err)));
    CHECK(run->status == SOLVE_UNSAT);
    CHECK(run->counterfactual);
    CHECK(run->unlocked_status == SOLVE_SAT);
    bool lock_in_core = false;
    for (int i = 0; i < run->core_n; i++) lock_in_core |= run->core[i] == CID_LOCK;
    CHECK(lock_in_core);
    run_free(run);

    /* given melody notes get the same counterfactual */
    c = test_config();
    test_set(&c, "melody=?,?,?,?,69");
    CHECK(run_piece(run, &c, err, sizeof(err)));
    CHECK(run->status == SOLVE_SAT);
    CHECK(run->counterfactual);
    CHECK(run->unlocked_status == SOLVE_SAT);
    CHECK(run->values[run->model.pitch[4]] == 69);
    run_free(run);
    c = test_config();
    CHECK(run_piece(run, &c, err, sizeof(err)));
    CHECK(!run->counterfactual);
    run_free(run);
    free(run);
}

int main(void) {
    test_default();
    test_limits();
    test_orders();
    test_sampling();
    test_lock();
    test_backjumping();
    test_search_equivalence();
    test_unsat_core();
    test_sat_backend();
    test_optimize();
    test_longest_melody();
    test_eighth_grid();
    test_step_cost();
    test_run_pipeline();
    printf("ok\n");
    return 0;
}
