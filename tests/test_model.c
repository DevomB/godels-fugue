#include "canon.h"
#include "theory.h"
#include "test_util.h"

static int count_rule(const Model *m, int rule) {
    int n = 0;
    for (int i = 0; i < m->ncons; i++) {
        if (m->cons[i].rule == rule) n++;
    }
    return n;
}

static int count_term(const Model *m, int term) {
    int n = 0;
    for (int i = 0; i < m->nterms; i++) {
        if (m->terms[i].rule == term) n++;
    }
    return n;
}

static void test_variables(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    const Model *m = &r->model;
    CHECK(m->nvars == 13); /* key + 12 pitches */
    CHECK(m->span == 16);
    CHECK(m->nsections == 1);
    CHECK(domain_count(&m->initial[m->key[0]]) == 1);
    CHECK(domain_count(&m->initial[m->pitch[0]]) == 13);
    CHECK(!domain_contains(&m->initial[m->pitch[0]], PITCH_REST));
    CHECK(m->tie[1] == -1);
    CHECK(count_rule(m, CID_CONSONANCE) == 2); /* steps 4 and 8 */
    CHECK(count_rule(m, CID_LOCK) == 0);
    CHECK(count_rule(m, CID_SPACING) == 0 && count_rule(m, CID_CROSSING) == 0);
    CHECK(model_rule_used(m, CID_LEAP));
    CHECK(!model_rule_used(m, CID_CHORD));
    test_close(r);

    /* given notes lock, and the spacing and crossing rules cover every
     * step where two voices sound (steps 4 to 11) */
    test_set(&c, "melody=60,?,?,64");
    test_set(&c, "max_spacing=12");
    test_set(&c, "crossing=0");
    r = test_open(&c);
    m = &r->model;
    CHECK(count_rule(m, CID_LOCK) == 2);
    CHECK(count_rule(m, CID_SPACING) == 8);
    CHECK(count_rule(m, CID_CROSSING) == 8);
    CHECK(count_rule(m, CID_CONSONANCE) == 2);
    test_close(r);
    c = test_config();

    test_set(&c, "rhythm=1");
    test_set(&c, "harmony=1");
    test_set(&c, "key=search");
    test_set(&c, "mode=search");
    test_set(&c, "modulate_at=8");
    r = test_open(&c);
    m = &r->model;
    CHECK(m->nsections == 2);
    CHECK(m->nbars == 4);
    CHECK(m->nvars == 2 + 4 + 12 + 11);
    CHECK(domain_count(&m->initial[m->key[0]]) == 24);
    CHECK(domain_contains(&m->initial[m->pitch[0]], PITCH_REST));
    CHECK(m->tie[0] == -1 && m->tie[1] >= 0);
    CHECK(count_rule(m, CID_TIE) == 11);
    CHECK(count_rule(m, CID_MODULATION) == 1);
    CHECK(count_rule(m, CID_PROGRESSION) == 2); /* bars 1-2 and 3-4; bar 3 changes key */
    CHECK(count_term(m, TERM_KEY) == 1);
    CHECK(count_term(m, TERM_KEY_DISTANCE) == 1);
    CHECK(m->max_rests_con >= 0);
    char buf[32];
    var_label(m, m->key[1], buf, sizeof(buf));
    CHECK(strcmp(buf, "key2") == 0);
    var_label(m, m->tie[3], buf, sizeof(buf));
    CHECK(strcmp(buf, "tie3") == 0);
    var_label(m, m->chord[2], buf, sizeof(buf));
    CHECK(strcmp(buf, "chord2") == 0);
    value_label(m, m->pitch[0], PITCH_REST, buf, sizeof(buf));
    CHECK(strcmp(buf, "rest") == 0);
    value_label(m, m->tie[3], TIE_HOLD, buf, sizeof(buf));
    CHECK(strcmp(buf, "hold") == 0);
    test_close(r);

    /* one leading-tone rule per melody step; double leaps once per
     * triple, shared by a plain or retrograde follower but not by a
     * pitch-class inversion; contrary motion wherever both voices move */
    c = test_config();
    CHECK(c.double_leaps == 1 && c.leading_tone == 0);
    test_set(&c, "leading_tone=1");
    test_set(&c, "double_leaps=0");
    test_set(&c, "w_contrary=3");
    r = test_open(&c);
    m = &r->model;
    CHECK(count_rule(m, CID_LEADING_TONE) == 11);
    CHECK(count_rule(m, CID_DOUBLE_LEAP) == 10);
    CHECK(count_term(m, TERM_CONTRARY) == 7); /* steps 4-5 to 10-11 */
    test_close(r);
    test_set(&c, "retrograde=1");
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == 10);
    test_close(r);
    c.retrograde = 0;
    test_set(&c, "invert=1");
    test_set(&c, "invert_mod12=1");
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == 20);
    test_close(r);
    c = test_config();
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_LEADING_TONE) == 0);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == 0);
    CHECK(count_term(&r->model, TERM_CONTRARY) == 0);
    test_close(r);
}

static const Constraint *find(const Model *m, int type, int time) {
    for (int i = 0; i < m->ncons; i++) {
        if (m->cons[i].type == type && (time < 0 || m->cons[i].time == time)) return &m->cons[i];
    }
    return NULL;
}

/* The first term of a rule at a step with nslots slots, or NULL. */
static const Constraint *find_term(const Model *m, int rule, int time, int nslots) {
    for (int i = 0; i < m->nterms; i++) {
        const Constraint *t = &m->terms[i];
        if (t->rule == rule && t->time == time && t->nslots == nslots) return t;
    }
    return NULL;
}

/* Cost of a term over ties alone, given in slot order as N (note) and H (hold). */
static int tie_cost(const Model *m, const Constraint *t, const char *ties) {
    int v[SCOPE_MAX];
    CHECK(t != NULL && (int)strlen(ties) == t->nslots);
    for (int k = 0; k < t->nslots; k++) v[t->slot[k]] = ties[k] == 'H' ? TIE_HOLD : TIE_NOTE;
    return term_cost(m, t, v);
}

/* Cost of a run term for two pitches and its ties, N or H in slot order. */
static int run_cost(const Model *m, const Constraint *t, int a, int b, const char *ties) {
    int v[SCOPE_MAX];
    CHECK(t != NULL && (int)strlen(ties) == t->nslots - 2);
    v[t->slot[0]] = a;
    v[t->slot[1]] = b;
    for (int k = 2; k < t->nslots; k++) v[t->slot[k]] = ties[k - 2] == 'H' ? TIE_HOLD : TIE_NOTE;
    return term_cost(m, t, v);
}

/* On the eighth grid a bar is eight steps: one chord, one strong step on
 * its first beat, and rhythm terms that count a beat as two steps. */
static void test_eighth_grid(void) {
    PieceConfig c = test_config();
    test_set(&c, "grid=eighth");
    test_set(&c, "length=24");
    test_set(&c, "delay=8");
    test_set(&c, "rhythm=1");
    test_set(&c, "harmony=1");
    TestRun *r = test_open(&c);
    const Model *m = &r->model;
    CHECK(m->span == 32 && m->nbars == 4);
    /* both voices sound on the first beats of bars 2 and 3, steps 8 and 16 */
    CHECK(count_rule(m, CID_CONSONANCE) == 2);
    CHECK(count_rule(m, CID_CHORD) == 6); /* each voice on three downbeats */
    CHECK(count_rule(m, CID_PROGRESSION) == 3);
    for (int i = 0; i < m->ncons; i++) {
        const Constraint *k = &m->cons[i];
        if (k->type == C_CONSONANCE || k->type == C_CHORD_TONE) CHECK(k->time % 8 == 0);
        if (k->type == C_CHORD_TONE) CHECK(k->vars[k->slot[1]] == m->chord[k->time / 8]);
    }
    CHECK(count_term(m, TERM_NONCHORD) == 2 * (24 - 3)); /* every other sounding step */
    CHECK(count_term(m, TERM_CHORD) == 4);
    CHECK(count_term(m, TERM_RHYTHM) == 3);
    CHECK(count_term(m, TERM_SYNCOPATION) == 5 + 11);
    char where[128];
    constraint_describe(m, find(m, C_CONSONANCE, 16), where, sizeof(where));
    CHECK(strstr(where, "at step 16 (bar 3)") != NULL);

    /* a note attacked on beat 2 (step 2) and held over beat 3 (step 4) */
    const Constraint *over = find_term(m, TERM_SYNCOPATION, 4, 3);
    CHECK(over != NULL && over->vars[over->slot[0]] == m->tie[2]);
    CHECK(tie_cost(m, over, "NHH") == c.w_syncopation);
    CHECK(tie_cost(m, over, "NHN") == 0 && tie_cost(m, over, "HHH") == 0);
    /* an off-beat eighth (step 1) held across beat 2 (step 2) */
    const Constraint *off = find_term(m, TERM_SYNCOPATION, 2, 2);
    CHECK(off != NULL && off->vars[off->slot[0]] == m->tie[1]);
    CHECK(tie_cost(m, off, "NH") == c.w_syncopation);
    CHECK(tie_cost(m, off, "NN") == 0 && tie_cost(m, off, "HH") == 0);
    CHECK(find_term(m, TERM_SYNCOPATION, 3, 2) == NULL); /* step 3 starts no beat */
    /* a bar of eight eighths or of four quarters is plain; any mix is not */
    const Constraint *bar = find_term(m, TERM_RHYTHM, 8, 7);
    CHECK(bar != NULL && bar->vars[bar->slot[0]] == m->tie[9]);
    CHECK(tie_cost(m, bar, "NNNNNNN") == c.w_rhythm);
    CHECK(tie_cost(m, bar, "HNHNHNH") == c.w_rhythm);
    CHECK(tie_cost(m, bar, "HNHNHNN") == 0);
    CHECK(tie_cost(m, bar, "NNHNHNH") == 0);
    CHECK(tie_cost(m, bar, "HHHNHHH") == 0); /* two half notes */

    /* two eighths in a row move by step: notes 5 and 6 are eighths when
     * the ties at 5, 6 and 7 are all attacks */
    CHECK(count_term(m, TERM_RUN) == 23);
    const Constraint *run = find_term(m, TERM_RUN, 5, 5);
    CHECK(run != NULL && run->vars[run->slot[2]] == m->tie[5]);
    CHECK(run_cost(m, run, 60, 64, "NNN") == c.w_run);
    CHECK(run_cost(m, run, 72, 60, "NNN") == c.w_run);
    CHECK(run_cost(m, run, 60, 62, "NNN") == 0);         /* a step */
    CHECK(run_cost(m, run, 60, 64, "HNN") == 0);         /* the first note is longer */
    CHECK(run_cost(m, run, 60, 64, "NNH") == 0);         /* so is the second */
    CHECK(run_cost(m, run, PITCH_REST, 64, "NNN") == 0); /* a rest is no note */
    /* the first note has no tie, and the last has no next step */
    CHECK(run_cost(m, find_term(m, TERM_RUN, 0, 4), 60, 67, "NN") == c.w_run);
    CHECK(run_cost(m, find_term(m, TERM_RUN, 22, 4), 67, 60, "NN") == c.w_run);
    CHECK(run_cost(m, find_term(m, TERM_RUN, 22, 4), 67, 60, "HN") == 0);
    test_close(r);

    /* the same piece on the quarter grid keeps its terms */
    c.grid = GRID_QUARTER;
    r = test_open(&c);
    m = &r->model;
    CHECK(m->span == 32 && m->nbars == 8);
    CHECK(count_rule(m, CID_CONSONANCE) == 4); /* steps 8, 12, 16, 20 */
    CHECK(count_term(m, TERM_SYNCOPATION) == 11);
    CHECK(count_term(m, TERM_RHYTHM) == 6);
    CHECK(count_term(m, TERM_RUN) == 0);
    over = find_term(m, TERM_SYNCOPATION, 4, 2);
    CHECK(tie_cost(m, over, "NH") == c.w_syncopation && tie_cost(m, over, "HH") == 0);
    bar = find_term(m, TERM_RHYTHM, 4, 3);
    CHECK(tie_cost(m, bar, "NNN") == c.w_rhythm && tie_cost(m, bar, "HNH") == 0);
    test_close(r);
}

/* The curve term of each note carries its target: the arch until a
 * curve is drawn, then the drawn curve stretched over the melody. */
static void check_targets(const PieceConfig *c, const int *want) {
    TestRun *r = test_open(c);
    int seen = 0;
    for (int k = 0; k < r->model.nterms; k++) {
        const Constraint *t = &r->model.terms[k];
        if (t->rule != TERM_CURVE) continue;
        CHECK(t->param == want[t->time]);
        seen++;
    }
    CHECK(seen == c->length);
    test_close(r);
}

static void test_tension(void) {
    PieceConfig c = test_config();
    int arch[12];
    for (int i = 0; i < 12; i++) arch[i] = tension_target(i, 12);
    check_targets(&c, arch);
    test_set(&c, "tension=0 4 0");
    const int peak[12] = {0, 1, 1, 2, 3, 4, 4, 3, 2, 1, 1, 0};
    check_targets(&c, peak);
    test_set(&c, "tension=3");
    const int flat[12] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    check_targets(&c, flat);
    test_set(&c, "tension=0,1,2,3,4,4,3,2,1,0,1,2");
    const int full[12] = {0, 1, 2, 3, 4, 4, 3, 2, 1, 0, 1, 2};
    check_targets(&c, full);

    /* the cost is the distance from the target: E (gravity 0) under a
     * target of 3 costs three times w_curve */
    test_set(&c, "w_curve=2");
    TestRun *r = test_open(&c);
    const Constraint *t = NULL;
    for (int k = 0; k < r->model.nterms; k++) {
        if (r->model.terms[k].rule == TERM_CURVE && r->model.terms[k].time == 3)
            t = &r->model.terms[k];
    }
    CHECK(t != NULL);
    int v[SCOPE_MAX];
    v[t->slot[0]] = 64;
    v[t->slot[1]] = key_id(0, MODE_MAJOR);
    CHECK(term_cost(&r->model, t, v) == 6);
    v[t->slot[0]] = 71; /* B, gravity 3 */
    CHECK(term_cost(&r->model, t, v) == 0);
    test_close(r);
}

/* One mirror constraint per pair of notes, and the middle note of an
 * odd length alone. */
static void test_mirror(void) {
    PieceConfig c = test_config();
    TestRun *r = test_open(&c);
    CHECK(count_rule(&r->model, CID_MIRROR) == 0);
    test_close(r);
    CHECK(strcmp(rule_name(CID_MIRROR), "mirror") == 0);
    CHECK(strcmp(rule_name(CID_MODULATION), "modulation") == 0);
    CHECK(strcmp(rule_name(CID_REFUTED), "search") == 0);
    CHECK(strcmp(rule_name(CID_LEARNED), "learned conflict") == 0);

    test_set(&c, "mirror=1");
    r = test_open(&c);
    const Model *m = &r->model;
    CHECK(count_rule(m, CID_MIRROR) == 6);
    CHECK(model_rule_used(m, CID_MIRROR));
    const Constraint *pair = find(m, C_MIRROR, 2);
    CHECK(pair != NULL && pair->n == 2 && pair->nslots == 2 && pair->param == 66);
    CHECK(pair->vars[0] == m->pitch[2] && pair->vars[1] == m->pitch[9]);
    int v[SCOPE_MAX];
    v[0] = 60;
    v[1] = 72;
    CHECK(constraint_holds(m, pair, v));
    v[1] = 71;
    CHECK(!constraint_holds(m, pair, v));
    v[0] = PITCH_REST;
    CHECK(!constraint_holds(m, pair, v)); /* a rest pairs only with a rest */
    v[1] = PITCH_REST;
    CHECK(constraint_holds(m, pair, v));
    v[0] = 66;
    v[1] = 66;
    CHECK(constraint_holds(m, pair, v));
    char why[128];
    constraint_describe(m, pair, why, sizeof(why));
    CHECK(strstr(why, "notes 2 and 9 mirror each other around F#4") != NULL);
    test_close(r);

    test_set(&c, "length=13");
    test_set(&c, "rhythm=1");
    r = test_open(&c);
    m = &r->model;
    CHECK(count_rule(m, CID_MIRROR) == 7);
    const Constraint *middle = find(m, C_MIRROR, 6);
    CHECK(middle != NULL && middle->n == 1 && middle->nslots == 1);
    CHECK(middle->vars[0] == m->pitch[6]);
    v[0] = 66;
    CHECK(constraint_holds(m, middle, v));
    v[0] = 67;
    CHECK(!constraint_holds(m, middle, v));
    v[0] = PITCH_REST;
    CHECK(!constraint_holds(m, middle, v)); /* the middle note is the axis itself */
    constraint_describe(m, middle, why, sizeof(why));
    CHECK(strstr(why, "middle note 6") != NULL);
    test_close(r);
}

static void test_predicates(void) {
    PieceConfig c = test_config();
    test_set(&c, "rhythm=1");
    TestRun *r = test_open(&c);
    const Model *m = &r->model;

    const Constraint *tie = find(m, C_TIE, 5);
    CHECK(tie != NULL && tie->n == 3);
    int v[SCOPE_MAX];
    v[0] = TIE_HOLD;
    v[1] = 64;
    v[2] = 64;
    CHECK(constraint_holds(m, tie, v));
    v[2] = 65;
    CHECK(!constraint_holds(m, tie, v));
    v[0] = TIE_NOTE;
    CHECK(constraint_holds(m, tie, v));
    v[0] = TIE_HOLD;
    v[1] = PITCH_REST;
    v[2] = PITCH_REST;
    CHECK(!constraint_holds(m, tie, v)); /* a rest cannot be tied */

    const Constraint *cons = find(m, C_CONSONANCE, 4);
    CHECK(cons != NULL);
    int a = cons->slot[0];
    int b = cons->slot[1];
    v[a] = 60;
    v[b] = 64;
    CHECK(constraint_holds(m, cons, v));
    v[b] = 65;
    CHECK(!constraint_holds(m, cons, v));
    v[b] = 60;
    CHECK(!constraint_holds(m, cons, v));
    v[b] = PITCH_REST;
    CHECK(constraint_holds(m, cons, v)); /* a silent voice cannot clash */

    /* approach k=0 applies only when the last note is attacked */
    const Constraint *approach = NULL;
    for (int i = 0; i < m->ncons; i++) {
        if (m->cons[i].type == C_CADENCE_APPROACH && m->cons[i].param == 0) approach = &m->cons[i];
    }
    CHECK(approach != NULL && approach->nslots == 3);
    int key = key_id(0, MODE_MAJOR);
    v[approach->slot[0]] = 60;
    v[approach->slot[1]] = key;
    v[approach->slot[2]] = TIE_NOTE;
    CHECK(!constraint_holds(m, approach, v));
    v[approach->slot[0]] = 71;
    CHECK(constraint_holds(m, approach, v));
    v[approach->slot[0]] = 60;
    v[approach->slot[2]] = TIE_HOLD;
    CHECK(constraint_holds(m, approach, v));
    test_close(r);
}

/* Changing one variable changes the total energy by exactly the change
 * in its choice cost when every other variable is fixed. */
static void check_energy_consistency(const PieceConfig *c, unsigned seed) {
    TestRun *r = test_open(c);
    const Model *m = &r->model;
    int values[VAR_MAX];
    unsigned x = seed * 2654435761u + 1u;
    for (int v = 0; v < m->nvars; v++) {
        int dom[128];
        int n = domain_collect(&m->initial[v], dom);
        x = x * 1103515245u + 12345u;
        values[v] = dom[(x >> 8) % (unsigned)n];
    }
    for (int v = 0; v < m->nvars; v++) {
        domain_clear(&r->state.domains[v]);
        domain_add(&r->state.domains[v], values[v]);
    }
    for (int var = 0; var < m->nvars; var++) {
        int dom[128];
        int n = domain_collect(&m->initial[var], dom);
        int costs[128];
        MidiDomain saved = r->state.domains[var];
        domain_fill_range(&r->state.domains[var], 0, 127); /* let var look open */
        solver_choice_costs(&r->state, var, dom, n, costs, NULL);
        r->state.domains[var] = saved;
        int keep = values[var];
        values[var] = dom[0];
        int base = model_energy(m, values, NULL);
        for (int k = 1; k < n; k++) {
            values[var] = dom[k];
            CHECK(model_energy(m, values, NULL) - base == costs[k] - costs[0]);
        }
        values[var] = keep;
    }
    int breakdown[TERM_COUNT];
    int total = model_energy(m, values, breakdown);
    int sum = 0;
    for (int t = 0; t < TERM_COUNT; t++) sum += breakdown[t];
    CHECK(sum == total);
    test_close(r);
}

static void test_energy(void) {
    static const char *const shapes[][6] = {
        {"voices=2", NULL},
        {"voices=4", "delay=3", NULL},
        {"voices=3", "delay_1=2", "delay_2=7", "phase=1", NULL},
        {"invert=1", "axis=68", "range_high=76", NULL},
        {"invert=1", "invert_mod12=1", "axis=62", NULL},
        {"retrograde=1", "transpose=7", NULL},
        {"augment=2", NULL},
        {"diminish=2", "delay=3", NULL},
        {"cyclic=1", "voices=3", NULL},
        {"rhythm=1", "harmony=1", "w_motif=3", "motif_a=2", "motif_b=-1", NULL},
        {"key=search", "mode=search", "modulate_at=6", "pc_weight=0,1,2,3,4,5,6,7,8,9,10,11", NULL},
        {"poly_meter=1", "rhythm=1", "voices=3", NULL},
        {"tension=0,?,4,1", "w_curve=3", "mirror=1", "rhythm=1", NULL},
        {"grid=eighth", "rhythm=1", "harmony=1", "length=20", "delay=6", NULL},
        {"grid=eighth", "voices=3", "poly_meter=1", "rhythm=1", "max_hold=5", NULL},
    };
    for (size_t s = 0; s < sizeof(shapes) / sizeof(shapes[0]); s++) {
        PieceConfig c = test_config();
        test_set(&c, "w_dissonance=3");
        test_set(&c, "w_parallel=2");
        test_set(&c, "w_contrary=1");
        for (int k = 0; shapes[s][k] != NULL; k++) test_set(&c, shapes[s][k]);
        for (unsigned seed = 1; seed <= 4; seed++) check_energy_consistency(&c, seed + 10u * (unsigned)s);
    }
}

/* Leaps and double leaps are checked once per line shape, and range
 * once per transposition: diatonic transposition changes interval
 * sizes, chromatic transposition does not. */
static void test_voice_transpose(void) {
    PieceConfig c = test_config();
    test_set(&c, "voices=3");
    test_set(&c, "double_leaps=0");
    TestRun *r = test_open(&c);
    int leaps = count_rule(&r->model, CID_LEAP);
    int doubles = count_rule(&r->model, CID_DOUBLE_LEAP);
    test_close(r);
    CHECK(leaps == 11);
    CHECK(doubles == 10);

    test_set(&c, "transpose_1=7");
    test_set(&c, "transpose_2=12");
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_LEAP) == leaps);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == doubles);
    const Constraint *range = NULL;
    for (int i = 0; i < r->model.ncons && range == NULL; i++) {
        if (r->model.cons[i].rule == CID_RANGE) range = &r->model.cons[i];
    }
    CHECK(range != NULL && range->nslots == 3);
    test_close(r);

    test_set(&c, "diatonic=1");
    test_set(&c, "transpose_1=2");
    test_set(&c, "transpose_2=4");
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_LEAP) == 3 * leaps);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == 3 * doubles);
    test_close(r);

    test_set(&c, "transpose_2=9"); /* an octave above voice 2: same shape */
    r = test_open(&c);
    CHECK(count_rule(&r->model, CID_LEAP) == 2 * leaps);
    CHECK(count_rule(&r->model, CID_DOUBLE_LEAP) == 2 * doubles);
    test_close(r);
}

int main(void) {
    test_variables();
    test_voice_transpose();
    test_predicates();
    test_tension();
    test_mirror();
    test_eighth_grid();
    test_energy();
    printf("ok\n");
    return 0;
}
